// src/core/ProjectManager.cpp
#include "ProjectManager.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <algorithm>

namespace {
struct FastIgnoreMatcher {
    QSet<QString> exactDirs;
    QVector<QRegularExpression> wildcardDirs;
    QSet<QString> allowedExtensions;
    QSet<QString> exactAllowedNames;
    QVector<QRegularExpression> wildcardAllowedNames;

    static FastIgnoreMatcher fromConfig() {
        FastIgnoreMatcher m;
        const auto& cfg = ConfigManager::instance();
        const auto beh = cfg.aiBehavior();

        for (const QString& pat : beh.summaryIgnoredDirs) {
            QString p = pat.trimmed();
            if (p.isEmpty()) continue;
            if (p.contains('*') || p.contains('?')) {
                m.wildcardDirs.append(QRegularExpression(
                    QRegularExpression::wildcardToRegularExpression(p),
                    QRegularExpression::CaseInsensitiveOption));
            } else {
                m.exactDirs.insert(p.toLower());
            }
        }
        // Гарантируем пропуск стандартных тяжёлых служебных каталогов
        m.exactDirs.insert(".git");
        m.exactDirs.insert(".hg");
        m.exactDirs.insert(".svn");
        m.exactDirs.insert("node_modules");
        m.exactDirs.insert("__pycache__");

        for (const QString& ext : beh.summaryAllowedExtensions) {
            QString clean = ext.trimmed().toLower();
            if (clean.startsWith("*.")) clean = clean.mid(2);
            else if (clean.startsWith('.')) clean = clean.mid(1);
            if (!clean.isEmpty()) m.allowedExtensions.insert(clean);
        }
        if (beh.summaryUseSyntaxExtensions) {
            for (const QString& ext : cfg.extensionsFromLoadedSyntaxes()) {
                QString clean = ext.trimmed().toLower();
                if (!clean.isEmpty()) m.allowedExtensions.insert(clean);
            }
        }
        for (const QString& pat : beh.summaryAllowedFilenames) {
            QString p = pat.trimmed();
            if (p.isEmpty()) continue;
            if (p.contains('*') || p.contains('?')) {
                m.wildcardAllowedNames.append(QRegularExpression(
                    QRegularExpression::wildcardToRegularExpression(p),
                    QRegularExpression::CaseInsensitiveOption));
            } else {
                m.exactAllowedNames.insert(p.toLower());
            }
        }
        return m;
    }

    bool isDirIgnored(const QString& dirName) const {
        if (dirName.isEmpty()) return true;
        const QString lower = dirName.toLower();
        if (exactDirs.contains(lower)) return true;
        for (const auto& rx : wildcardDirs) {
            if (rx.match(dirName).hasMatch()) return true;
        }
        return false;
    }

    bool isFileAllowed(const QString& fileName, const QString& suffix) const {
        const QString lower = fileName.toLower();
        if (exactAllowedNames.contains(lower)) return true;
        for (const auto& rx : wildcardAllowedNames) {
            if (rx.match(fileName).hasMatch()) return true;
        }
        if (!suffix.isEmpty() && allowedExtensions.contains(suffix.toLower())) {
            return true;
        }
        return false;
    }
};
} // namespace

ProjectManager::ProjectManager(QObject* parent)
    : QObject(parent),
    m_fsWatcher(new QFileSystemWatcher(this)) {
    m_watchDebounceTimer.setSingleShot(true);
    m_watchDebounceTimer.setInterval(650);
    connect(&m_watchDebounceTimer, &QTimer::timeout, this, [this]() {
        if (hasOpenProject() && !m_reconciling && m_batchDepth == 0) {
            reconcileProjectStructure(nullptr, false);
        }
    });
    connect(m_fsWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) {
        if (hasOpenProject() && !m_reconciling && m_batchDepth == 0) {
            m_watchDebounceTimer.start();
        }
    });
}

QString ProjectManager::computeSha256(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

void ProjectManager::beginBatchUpdate() {
    ++m_batchDepth;
}

void ProjectManager::endBatchUpdate() {
    if (m_batchDepth <= 0) return;
    --m_batchDepth;
    if (m_batchDepth == 0) {
        if (!m_lastBatchChangedFileRel.isEmpty()) {
            QString rel = m_lastBatchChangedFileRel;
            m_lastBatchChangedFileRel.clear();
            emit fileContextChanged(rel);
        }
        if (m_batchProjectDirty) {
            m_batchProjectDirty = false;
            emit projectContextChanged();
        }
    }
}

QString ProjectManager::defaultAideFilePathForDir(const QString& absDirPath) {
    QDir dir(absDirPath);
    QString folderName = dir.dirName().trimmed();
    if (folderName.isEmpty()) folderName = QStringLiteral("project");
    return dir.filePath(folderName + QStringLiteral(".aide"));
}

QString ProjectManager::findExistingAideFileInDir(const QString& absDirPath) {
    QDir dir(absDirPath);
    if (!dir.exists()) return {};
    const QStringList aideFiles = dir.entryList({QStringLiteral("*.aide")}, QDir::Files, QDir::Name);
    if (aideFiles.isEmpty()) return {};
    const QString preferred = dir.dirName() + QStringLiteral(".aide");
    for (const QString& f : aideFiles) {
        if (f.compare(preferred, Qt::CaseInsensitive) == 0) {
            return dir.filePath(f);
        }
    }
    return dir.filePath(aideFiles.first());
}

QString ProjectManager::projectFilePath() const {
    if (m_rootPath.isEmpty()) return {};
    if (m_nodes.contains(QString())) {
        return m_nodes.value(QString()).aideFilePath;
    }
    return defaultAideFilePathForDir(m_rootPath);
}

QString ProjectManager::toRelativePath(const QString& filePathOrRel) const {
    QString input = filePathOrRel.trimmed();
    if (input.isEmpty() || m_rootPath.isEmpty()) return {};

    input.replace('\\', '/');
    while (input.startsWith("./")) input.remove(0, 2);
    if (input == ".") return {};

    QFileInfo fi(input);
    QString rel;
    if (fi.isAbsolute()) {
        QString cleanAbs = QDir::cleanPath(input);
        cleanAbs.replace('\\', '/');
        QString cleanRoot = QDir::cleanPath(m_rootPath);
        cleanRoot.replace('\\', '/');
        if (cleanAbs.compare(cleanRoot, Qt::CaseInsensitive) == 0) {
            return {};
        }
        if (cleanAbs.startsWith(cleanRoot + '/', Qt::CaseInsensitive)) {
            rel = cleanAbs.mid(cleanRoot.length() + 1);
        } else {
            rel = QDir(m_rootPath).relativeFilePath(cleanAbs);
            rel.replace('\\', '/');
        }
    } else {
        rel = QDir::cleanPath(input);
        rel.replace('\\', '/');
    }

    while (rel.startsWith("./")) rel.remove(0, 2);
    if (rel == ".") rel.clear();
    return rel;
}

QString ProjectManager::toAbsolutePath(const QString& relPath) const {
    if (m_rootPath.isEmpty()) return relPath;
    QString clean = relPath.trimmed();
    if (clean.isEmpty()) return m_rootPath;
    if (QFileInfo(clean).isAbsolute()) return QDir::cleanPath(clean);
    return QDir::cleanPath(QDir(m_rootPath).filePath(clean));
}

QString ProjectManager::normalizeScopeRel(const QString& scopeOrPath) const {
    QString trimmed = scopeOrPath.trimmed();
    if (trimmed.isEmpty() || trimmed == ".") return {};
    QString rel = toRelativePath(trimmed);
    if (rel == "." || rel.startsWith("..")) return {};
    if (m_nodes.contains(rel)) return rel;

    // Если передан путь к файлу .aide или вложенной папке, ищем соответствующий узел
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        if (it.key().compare(rel, Qt::CaseInsensitive) == 0) {
            return it.key();
        }
    }
    return rel;
}

QString ProjectManager::projectDirPathForRel(const QString& subprojectRel) const {
    if (m_rootPath.isEmpty()) return {};
    QString trimmed = subprojectRel.trimmed();
    if (trimmed.isEmpty() || trimmed == ".") return m_rootPath;
    if (QFileInfo(trimmed).isAbsolute()) {
        return QDir::cleanPath(trimmed);
    }
    return QDir::cleanPath(QDir(m_rootPath).filePath(trimmed));
}

bool ProjectManager::isSubprojectDir(const QString& dirPathOrRel) const {
    if (!hasOpenProject()) return false;
    QString rel = toRelativePath(dirPathOrRel);
    if (rel.isEmpty() || rel.startsWith("..")) return false;
    if (m_nodes.contains(rel)) return true;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        if (!it.key().isEmpty() && it.key().compare(rel, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QStringList ProjectManager::subprojectRelPaths() const {
    QStringList out;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        if (!it.key().isEmpty()) {
            out.append(it.key());
        }
    }
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QString ProjectManager::parentSubprojectRel(const QString& subRel) const {
    if (subRel.isEmpty()) return {};
    QString bestParent;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        const QString& candidate = it.key();
        if (candidate.isEmpty() || candidate.length() >= subRel.length()) continue;
        if (subRel.startsWith(candidate + '/', Qt::CaseInsensitive)) {
            if (candidate.length() > bestParent.length()) {
                bestParent = candidate;
            }
        }
    }
    return bestParent;
}

QStringList ProjectManager::directChildSubprojects(const QString& parentSubRel) const {
    const QString normParent = normalizeScopeRel(parentSubRel);
    QStringList out;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        const QString& sub = it.key();
        if (sub.isEmpty()) continue;
        if (parentSubprojectRel(sub).compare(normParent, Qt::CaseInsensitive) == 0) {
            out.append(sub);
        }
    }
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QStringList ProjectManager::dirtyProjectRelPathsBottomUp() const {
    QStringList dirtyList;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        const ProjectNodeData& node = it.value();
        const bool hasIndexedContent = indexedFilesCount(node.subprojectRel, true) > 0;
        if (!hasIndexedContent) continue;
        if (node.summaryDirty || node.summary.trimmed().isEmpty()) {
            dirtyList.append(node.subprojectRel);
        }
    }
    // Сортируем от самых глубоких вложенных подпроектов к корневому проекту ("")
    std::sort(dirtyList.begin(), dirtyList.end(), [](const QString& a, const QString& b) {
        int depthA = a.isEmpty() ? 0 : (a.count('/') + 1);
        int depthB = b.isEmpty() ? 0 : (b.count('/') + 1);
        if (depthA != depthB) return depthA > depthB;
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return dirtyList;
}

QString ProjectManager::owningProjectRelPathForFile(const QString& filePathOrRel) const {
    if (!hasOpenProject()) return {};
    const QString rel = toRelativePath(filePathOrRel);
    if (rel.isEmpty() || rel.startsWith("..")) return {};

    QString bestSub;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        const QString& sub = it.key();
        if (sub.isEmpty()) continue;
        if (rel.compare(sub, Qt::CaseInsensitive) == 0 ||
            rel.startsWith(sub + '/', Qt::CaseInsensitive)) {
            if (sub.length() > bestSub.length()) {
                bestSub = sub;
            }
        }
    }
    return bestSub;
}

void ProjectManager::reassignAllFilesToNearestOwners() {
    for (auto it = m_files.begin(); it != m_files.end(); ++it) {
        it.value().subprojectRelPath = owningProjectRelPathForFile(it.key());
    }
}

bool ProjectManager::loadNodeFromDisk(const QString& subRel,
                                      const QString& aidePath,
                                      QMap<QString, FileContextData>& outLoadedFiles) {
    ProjectNodeData node;
    node.subprojectRel = subRel;
    node.aideFilePath = aidePath;
    QDir nodeDir(projectDirPathForRel(subRel));
    node.projectName = nodeDir.dirName().isEmpty() ? QStringLiteral("project") : nodeDir.dirName();
    node.updatedAt = QDateTime::currentDateTimeUtc();

    QFile f(aidePath);
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        node.projectName = root["project_name"].toString(node.projectName);
        node.systemPrompt = root["project_system_prompt"].toString();
        node.summary = root["project_summary"].toString();
        node.summaryLlm = root["project_summary_llm"].toString();
        node.summaryDirty = root["project_summary_dirty"].toBool(false);
        node.updatedAt = QDateTime::fromString(root["updated_at"].toString(), Qt::ISODate);
        if (!node.updatedAt.isValid()) node.updatedAt = QDateTime::currentDateTimeUtc();

        const QJsonObject filesObj = root["files"].toObject();
        for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
            QString rawKey = it.key().trimmed();
            rawKey.replace('\\', '/');
            while (rawKey.startsWith("./")) rawKey.remove(0, 2);
            if (rawKey.isEmpty()) continue;

            // Ключ внутри подпроекта может быть записан относительно папки подпроекта или от корня
            QString rootRel;
            if (!subRel.isEmpty() &&
                !rawKey.startsWith(subRel + '/', Qt::CaseInsensitive) &&
                QFileInfo::exists(nodeDir.filePath(rawKey))) {
                rootRel = subRel + '/' + rawKey;
            } else if (!subRel.isEmpty() && !rawKey.startsWith(subRel + '/', Qt::CaseInsensitive)) {
                rootRel = subRel + '/' + rawKey;
            } else {
                rootRel = rawKey;
            }
            rootRel = QDir::cleanPath(rootRel);
            rootRel.replace('\\', '/');

            const QJsonObject fo = it.value().toObject();
            FileContextData fc;
            fc.relativePath = rootRel;
            fc.subprojectRelPath = subRel;
            fc.sha256 = fo["sha256"].toString();
            fc.llmModel = fo["llm_model"].toString();
            fc.userPrompt = fo["user_prompt"].toString();
            fc.aiSummary = fo["ai_summary"].toString();
            fc.summaryCreated = fo["summary_created"].toBool(!fc.aiSummary.trimmed().isEmpty());
            fc.updatedAt = QDateTime::fromString(fo["updated_at"].toString(), Qt::ISODate);
            if (!fc.updatedAt.isValid()) fc.updatedAt = node.updatedAt;

            if (outLoadedFiles.contains(rootRel)) {
                const FileContextData& prev = outLoadedFiles.value(rootRel);
                const bool prevHasSum = !prev.aiSummary.trimmed().isEmpty();
                const bool newHasSum = !fc.aiSummary.trimmed().isEmpty();
                if ((newHasSum && !prevHasSum) || (newHasSum == prevHasSum && fc.updatedAt >= prev.updatedAt)) {
                    outLoadedFiles.insert(rootRel, fc);
                }
            } else {
                outLoadedFiles.insert(rootRel, fc);
            }
        }
    }

    m_nodes.insert(subRel, node);
    return true;
}

bool ProjectManager::saveNodeToDisk(const ProjectNodeData& node) const {
    if (node.aideFilePath.isEmpty()) return false;
    QFileInfo fi(node.aideFilePath);
    if (!fi.dir().exists()) return false;

    QJsonObject root;
    root["project_name"] = node.projectName;
    root["project_system_prompt"] = node.systemPrompt;
    root["project_summary"] = node.summary;
    root["project_summary_llm"] = node.summaryLlm;
    root["project_summary_dirty"] = node.summaryDirty;
    root["updated_at"] = node.updatedAt.toUTC().toString(Qt::ISODate);

    QJsonObject filesObj;
    const QString prefix = node.subprojectRel.isEmpty() ? QString() : (node.subprojectRel + '/');
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        const FileContextData& fc = it.value();
        if (fc.subprojectRelPath.compare(node.subprojectRel, Qt::CaseInsensitive) != 0) {
            continue;
        }
        // Не сохраняем совершенно пустые записи без саммари и без пользовательского промпта
        if (!fc.summaryCreated && fc.aiSummary.trimmed().isEmpty() && fc.userPrompt.trimmed().isEmpty()) {
            continue;
        }
        QString localKey = fc.relativePath;
        if (!prefix.isEmpty() && localKey.startsWith(prefix, Qt::CaseInsensitive)) {
            localKey = localKey.mid(prefix.length());
        }
        QJsonObject fo;
        fo["sha256"] = fc.sha256;
        fo["llm_model"] = fc.llmModel;
        fo["user_prompt"] = fc.userPrompt;
        fo["ai_summary"] = fc.aiSummary;
        fo["summary_created"] = fc.summaryCreated;
        fo["updated_at"] = fc.updatedAt.toUTC().toString(Qt::ISODate);
        filesObj[localKey] = fo;
    }
    root["files"] = filesObj;

    QSaveFile sf(node.aideFilePath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    sf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return sf.commit();
}

bool ProjectManager::saveProjectFile() {
    if (!hasOpenProject()) return false;
    m_reconciling = true;
    bool allOk = true;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        if (!saveNodeToDisk(it.value())) {
            allOk = false;
        }
    }
    m_reconciling = false;
    return allOk;
}

void ProjectManager::rebuildWatcherPaths() {
    const QStringList oldDirs = m_fsWatcher->directories();
    if (!oldDirs.isEmpty()) {
        m_fsWatcher->removePaths(oldDirs);
    }
    if (!hasOpenProject()) return;

    // Отслеживаем только корень проекта и папки подпроектов (.aide) (не более 60 путей),
    // чтобы не создавать сотни системных потоков ReadDirectoryChangesW на проектах с 20 000 файлов
    QStringList dirsToWatch;
    dirsToWatch.append(m_rootPath);
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        if (dirsToWatch.size() >= 60) break;
        QString absDir = projectDirPathForRel(it.key());
        if (!absDir.isEmpty() && QDir(absDir).exists() && !dirsToWatch.contains(absDir)) {
            dirsToWatch.append(absDir);
        }
    }
    if (!dirsToWatch.isEmpty()) {
        m_fsWatcher->addPaths(dirsToWatch);
    }
}

bool ProjectManager::openProjectFolder(const QString& folderPath, const ProgressCallback& progressCb) {
    QDir dir(folderPath);
    if (!dir.exists()) return false;

    const QString cleanRoot = QDir::cleanPath(dir.absolutePath());
    const QString prevRoot = m_rootPath;
    const auto prevNodes = m_nodes;
    const auto prevFiles = m_files;

    m_reconciling = true;
    m_rootPath = cleanRoot;
    m_nodes.clear();
    m_files.clear();

    const FastIgnoreMatcher matcher = FastIgnoreMatcher::fromConfig();
    QMap<QString, QString> discoveredAideByRel;

    QString rootAide = findExistingAideFileInDir(cleanRoot);
    if (rootAide.isEmpty()) {
        rootAide = defaultAideFilePathForDir(cleanRoot);
    }
    discoveredAideByRel.insert(QString(), rootAide);

    int processedDirs = 0;
    int foundFilesCount = 0;
    bool cancelled = false;

    // Быстрый обход дерева каталогов с пропуском игнорируемых папок и поддержкой отмены
    auto scanSubprojects = [&](auto& self, const QString& absDirPath, const QString& relPrefix) -> void {
        if (cancelled) return;
        ++processedDirs;

        if (progressCb && (processedDirs == 1 || (processedDirs % 16) == 0)) {
            if (!progressCb(processedDirs, foundFilesCount, relPrefix.isEmpty() ? cleanRoot : relPrefix)) {
                cancelled = true;
                return;
            }
        }

        QDir d(absDirPath);
        const QFileInfoList entries = d.entryInfoList(
            QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
            QDir::NoSort);

        QString foundAideInDir;
        QFileInfoList subDirs;
        subDirs.reserve(entries.size());

        for (const QFileInfo& fi : entries) {
            if (fi.isDir()) {
                if (fi.isSymLink()) continue;
                if (!matcher.isDirIgnored(fi.fileName())) {
                    subDirs.append(fi);
                }
            } else {
                const QString suffix = fi.suffix();
                if (suffix.compare(QStringLiteral("aide"), Qt::CaseInsensitive) == 0) {
                    if (foundAideInDir.isEmpty()) {
                        foundAideInDir = fi.absoluteFilePath();
                    }
                } else {
                    ++foundFilesCount;
                }
            }
        }

        if (!relPrefix.isEmpty() && !foundAideInDir.isEmpty()) {
            discoveredAideByRel.insert(relPrefix, foundAideInDir);
        }

        for (const QFileInfo& subFi : subDirs) {
            if (cancelled) return;
            const QString childRel = relPrefix.isEmpty()
                                         ? subFi.fileName()
                                         : (relPrefix + '/' + subFi.fileName());
            self(self, subFi.absoluteFilePath(), childRel);
        }
    };

    scanSubprojects(scanSubprojects, cleanRoot, QString());

    if (cancelled) {
        m_rootPath = prevRoot;
        m_nodes = prevNodes;
        m_files = prevFiles;
        m_reconciling = false;
        return false;
    }

    QMap<QString, FileContextData> loadedFiles;
    for (auto it = discoveredAideByRel.constBegin(); it != discoveredAideByRel.constEnd(); ++it) {
        loadNodeFromDisk(it.key(), it.value(), loadedFiles);
    }

    // Проверяем существование только тех файлов, которые реально записаны в .aide
    for (auto it = loadedFiles.begin(); it != loadedFiles.end(); ++it) {
        const QString absFile = toAbsolutePath(it.key());
        if (QFileInfo::exists(absFile)) {
            m_files.insert(it.key(), it.value());
        } else {
            // Если проиндексированный файл был удалён вне редактора, помечаем родительский узел dirty
            const QString owner = owningProjectRelPathForFile(it.key());
            if (m_nodes.contains(owner)) {
                m_nodes[owner].summaryDirty = true;
            }
        }
    }

    reassignAllFilesToNearestOwners();
    saveProjectFile();
    rebuildWatcherPaths();
    m_reconciling = false;

    emit projectLoaded(m_rootPath);
    emit subprojectsChanged();
    emit projectContextChanged();
    return true;
}

void ProjectManager::reconcileProjectStructure(const ProgressCallback& progressCb, bool fullDiskScan) {
    if (!hasOpenProject() || m_reconciling) return;
    m_reconciling = true;

    const FastIgnoreMatcher matcher = FastIgnoreMatcher::fromConfig();

    // 1. Проверяем актуальность уже известных подпроектов (.aide)
    bool subsModified = false;
    for (auto it = m_nodes.begin(); it != m_nodes.end();) {
        if (!it.key().isEmpty() && !QFileInfo::exists(it.value().aideFilePath)) {
            it = m_nodes.erase(it);
            subsModified = true;
        } else {
            ++it;
        }
    }

    if (fullDiskScan) {
        int processedDirs = 0;
        int foundFiles = 0;
        auto scanDirs = [&](auto& self, const QString& absDir, const QString& relPrefix) -> void {
            ++processedDirs;
            if (progressCb && (processedDirs % 20) == 0) {
                if (!progressCb(processedDirs, foundFiles, relPrefix)) return;
            }
            if (!relPrefix.isEmpty()) {
                const QString aide = findExistingAideFileInDir(absDir);
                if (!aide.isEmpty() && !m_nodes.contains(relPrefix)) {
                    loadNodeFromDisk(relPrefix, aide, m_files);
                    subsModified = true;
                }
            }
            QDir d(absDir);
            const QFileInfoList subDirs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
            for (const QFileInfo& sfi : subDirs) {
                if (sfi.isSymLink() || matcher.isDirIgnored(sfi.fileName())) continue;
                const QString childRel = relPrefix.isEmpty() ? sfi.fileName() : (relPrefix + '/' + sfi.fileName());
                self(self, sfi.absoluteFilePath(), childRel);
            }
        };
        scanDirs(scanDirs, m_rootPath, QString());
    }

    // 2. Проверяем существование проиндексированных файлов из m_files (быстро, O(число записей в .aide))
    QStringList removedRels;
    for (auto it = m_files.begin(); it != m_files.end();) {
        const QString absPath = toAbsolutePath(it.key());
        if (!QFileInfo::exists(absPath)) {
            removedRels.append(it.key());
            const QString owner = it.value().subprojectRelPath;
            if (m_nodes.contains(owner)) {
                m_nodes[owner].summaryDirty = true;
            }
            it = m_files.erase(it);
        } else {
            ++it;
        }
    }

    if (subsModified) {
        reassignAllFilesToNearestOwners();
        rebuildWatcherPaths();
    }

    if (subsModified || !removedRels.isEmpty()) {
        saveProjectFile();
    }

    m_reconciling = false;

    for (const QString& rel : removedRels) {
        emit fileRemovedFromProject(rel, toAbsolutePath(rel));
    }
    if (subsModified) {
        emit subprojectsChanged();
    }
    if (subsModified || !removedRels.isEmpty()) {
        emit projectContextChanged();
    }
}

void ProjectManager::closeProject() {
    if (!hasOpenProject()) return;
    saveProjectFile();
    const QStringList watched = m_fsWatcher->directories();
    if (!watched.isEmpty()) m_fsWatcher->removePaths(watched);
    m_rootPath.clear();
    m_nodes.clear();
    m_files.clear();
    emit projectClosed();
    emit subprojectsChanged();
    emit projectContextChanged();
}

bool ProjectManager::createSubproject(const QString& dirPath) {
    if (!hasOpenProject()) return false;
    const QString absDir = projectDirPathForRel(dirPath);
    if (absDir.isEmpty() || !QDir(absDir).exists()) return false;
    const QString subRel = toRelativePath(absDir);
    if (subRel.isEmpty() || subRel.startsWith("..")) return false;
    if (m_nodes.contains(subRel)) return true;

    ProjectNodeData node;
    node.subprojectRel = subRel;
    node.aideFilePath = findExistingAideFileInDir(absDir);
    if (node.aideFilePath.isEmpty()) {
        node.aideFilePath = defaultAideFilePathForDir(absDir);
    }
    node.projectName = QDir(absDir).dirName();
    node.summaryDirty = true;
    node.updatedAt = QDateTime::currentDateTimeUtc();
    m_nodes.insert(subRel, node);

    const QString parentRel = parentSubprojectRel(subRel);
    if (m_nodes.contains(parentRel)) {
        m_nodes[parentRel].summaryDirty = true;
    }

    reassignAllFilesToNearestOwners();
    saveProjectFile();
    rebuildWatcherPaths();

    emit subprojectsChanged();
    emit projectContextChanged();
    return true;
}

bool ProjectManager::removeSubproject(const QString& dirPath) {
    if (!hasOpenProject()) return false;
    const QString subRel = normalizeScopeRel(dirPath);
    if (subRel.isEmpty() || !m_nodes.contains(subRel)) return false;

    const QString aidePath = m_nodes.value(subRel).aideFilePath;
    const QString parentRel = parentSubprojectRel(subRel);

    m_nodes.remove(subRel);
    if (!aidePath.isEmpty() && QFile::exists(aidePath)) {
        QFile::remove(aidePath);
    }

    if (m_nodes.contains(parentRel)) {
        m_nodes[parentRel].summaryDirty = true;
    }

    reassignAllFilesToNearestOwners();
    saveProjectFile();
    rebuildWatcherPaths();

    emit subprojectsChanged();
    emit projectContextChanged();
    return true;
}

QString ProjectManager::projectSystemPrompt(const QString& subprojectRel) const {
    const QString norm = normalizeScopeRel(subprojectRel);
    return m_nodes.value(norm).systemPrompt;
}

void ProjectManager::setProjectSystemPrompt(const QString& prompt, const QString& subprojectRel) {
    if (!hasOpenProject()) return;
    const QString norm = normalizeScopeRel(subprojectRel);
    if (!m_nodes.contains(norm)) return;
    if (m_nodes[norm].systemPrompt == prompt) return;
    m_nodes[norm].systemPrompt = prompt;
    m_nodes[norm].updatedAt = QDateTime::currentDateTimeUtc();
    emit projectContextChanged();
}

QString ProjectManager::projectSummary(const QString& subprojectRel) const {
    const QString norm = normalizeScopeRel(subprojectRel);
    return m_nodes.value(norm).summary;
}

QString ProjectManager::projectSummaryLlm(const QString& subprojectRel) const {
    const QString norm = normalizeScopeRel(subprojectRel);
    return m_nodes.value(norm).summaryLlm;
}

void ProjectManager::setProjectSummary(const QString& summary, const QString& llmModel, const QString& subprojectRel) {
    if (!hasOpenProject()) return;
    const QString norm = normalizeScopeRel(subprojectRel);
    if (!m_nodes.contains(norm)) return;

    ProjectNodeData& node = m_nodes[norm];
    node.summary = summary;
    if (!llmModel.isEmpty()) {
        node.summaryLlm = llmModel;
    }
    node.summaryDirty = false;
    node.updatedAt = QDateTime::currentDateTimeUtc();

    // Обновление саммари вложенного подпроекта делает родительские уровни устаревшими (dirty)
    if (!norm.isEmpty()) {
        QString curParent = parentSubprojectRel(norm);
        while (true) {
            if (m_nodes.contains(curParent)) {
                m_nodes[curParent].summaryDirty = true;
            }
            if (curParent.isEmpty()) break;
            curParent = parentSubprojectRel(curParent);
        }
    }

    saveProjectFile();
    emit projectContextChanged();
}

bool ProjectManager::isProjectSummaryDirty(const QString& subprojectRel) const {
    const QString norm = normalizeScopeRel(subprojectRel);
    return m_nodes.value(norm).summaryDirty;
}

void ProjectManager::markProjectSummaryDirty(const QString& subprojectRel, bool cascadeToParents) {
    if (!hasOpenProject()) return;
    QString cur = normalizeScopeRel(subprojectRel);
    while (true) {
        if (m_nodes.contains(cur)) {
            m_nodes[cur].summaryDirty = true;
        }
        if (!cascadeToParents || cur.isEmpty()) break;
        cur = parentSubprojectRel(cur);
    }
}

int ProjectManager::indexedFilesCount(const QString& subprojectRel, bool includeNestedSubprojects) const {
    if (!hasOpenProject()) return 0;
    const QString norm = normalizeScopeRel(subprojectRel);
    const QString prefix = norm.isEmpty() ? QString() : (norm + '/');
    int count = 0;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        const FileContextData& fc = it.value();
        if (!fc.summaryCreated && fc.aiSummary.trimmed().isEmpty()) continue;
        if (includeNestedSubprojects) {
            if (norm.isEmpty() ||
                fc.subprojectRelPath.compare(norm, Qt::CaseInsensitive) == 0 ||
                fc.relativePath.startsWith(prefix, Qt::CaseInsensitive)) {
                ++count;
            }
        } else {
            if (fc.subprojectRelPath.compare(norm, Qt::CaseInsensitive) == 0) {
                ++count;
            }
        }
    }
    return count;
}

QStringList ProjectManager::unindexedAllowedFilePaths(const QString& subprojectRel, bool includeNestedSubprojects) const {
    QStringList missingAbsPaths;
    if (!hasOpenProject()) return missingAbsPaths;

    const QString norm = normalizeScopeRel(subprojectRel);
    const QString startAbsDir = projectDirPathForRel(norm);
    if (startAbsDir.isEmpty() || !QDir(startAbsDir).exists()) return missingAbsPaths;

    const FastIgnoreMatcher matcher = FastIgnoreMatcher::fromConfig();

    auto scanDir = [&](auto& self, const QString& absDir, const QString& relDir) -> void {
        QDir d(absDir);
        const QFileInfoList subDirs = d.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
            QDir::Name | QDir::IgnoreCase);

        for (const QFileInfo& sfi : subDirs) {
            if (sfi.isSymLink() || matcher.isDirIgnored(sfi.fileName())) continue;
            const QString childRel = relDir.isEmpty() ? sfi.fileName() : (relDir + '/' + sfi.fileName());
            if (!includeNestedSubprojects && m_nodes.contains(childRel)) {
                continue; // Не заходим в дочерний подпроект, если нужен только текущий уровень
            }
            self(self, sfi.absoluteFilePath(), childRel);
        }

        const QFileInfoList files = d.entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot,
            QDir::Name | QDir::IgnoreCase);

        for (const QFileInfo& ffi : files) {
            const QString suffix = ffi.suffix();
            if (suffix.compare(QStringLiteral("aide"), Qt::CaseInsensitive) == 0) continue;
            const QString fileName = ffi.fileName();
            if (!matcher.isFileAllowed(fileName, suffix)) continue;

            const QString fileRel = relDir.isEmpty() ? fileName : (relDir + '/' + fileName);
            const FileContextData fc = m_files.value(fileRel);
            const bool hasSummary = fc.summaryCreated || !fc.aiSummary.trimmed().isEmpty();
            if (!hasSummary && !fc.isSummaryPending) {
                missingAbsPaths.append(ffi.absoluteFilePath());
            }
        }
    };

    scanDir(scanDir, startAbsDir, norm);
    return missingAbsPaths;
}

FileContextData ProjectManager::fileContext(const QString& filePathOrRel) const {
    if (!hasOpenProject()) return {};
    const QString rel = toRelativePath(filePathOrRel);
    if (rel.isEmpty()) return {};
    if (m_files.contains(rel)) {
        return m_files.value(rel);
    }
    FileContextData empty;
    empty.relativePath = rel;
    empty.subprojectRelPath = owningProjectRelPathForFile(rel);
    return empty;
}

bool ProjectManager::isFileSummaryOutdated(const QString& filePathOrRel,
                                           const QByteArray& content,
                                           const QString& currentLlm) const {
    if (!hasOpenProject()) return false;
    const QString rel = toRelativePath(filePathOrRel);
    if (rel.isEmpty()) return false;
    const FileContextData fc = fileContext(rel);
    const QString sha = computeSha256(content);
    if (fc.summaryCreated && fc.aiSummary.trimmed().isEmpty() && fc.sha256 == sha) {
        return false;
    }
    if (fc.aiSummary.trimmed().isEmpty() || fc.sha256 != sha) {
        return true;
    }
    if (!currentLlm.isEmpty() && !fc.llmModel.isEmpty() && fc.llmModel != currentLlm) {
        return true;
    }
    return false;
}

void ProjectManager::updateFileUserPrompt(const QString& filePathOrRel, const QString& prompt) {
    if (!hasOpenProject()) return;
    const QString rel = toRelativePath(filePathOrRel);
    if (rel.isEmpty() || rel.startsWith("..")) return;

    FileContextData& fc = m_files[rel];
    fc.relativePath = rel;
    fc.subprojectRelPath = owningProjectRelPathForFile(rel);
    if (fc.userPrompt == prompt) return;
    fc.userPrompt = prompt;
    fc.updatedAt = QDateTime::currentDateTimeUtc();
}

void ProjectManager::setFileSummaryPending(const QString& filePathOrRel,
                                           bool pending,
                                           const QString& pendingSha256,
                                           const QString& pendingLlm) {
    if (!hasOpenProject()) return;
    const QString rel = toRelativePath(filePathOrRel);
    if (rel.isEmpty() || rel.startsWith("..")) return;

    FileContextData& fc = m_files[rel];
    fc.relativePath = rel;
    if (fc.subprojectRelPath.isEmpty()) {
        fc.subprojectRelPath = owningProjectRelPathForFile(rel);
    }
    fc.isSummaryPending = pending;
    fc.pendingSha256 = pending ? pendingSha256 : QString();
    fc.pendingLlmModel = pending ? pendingLlm : QString();

    if (m_batchDepth > 0) {
        m_lastBatchChangedFileRel = rel;
    } else {
        emit fileContextChanged(rel);
    }
}

void ProjectManager::updateFileSummary(const QString& filePathOrRel,
                                       const QString& sha256,
                                       const QString& llmModel,
                                       const QString& summary) {
    if (!hasOpenProject()) return;
    const QString rel = toRelativePath(filePathOrRel);
    if (rel.isEmpty() || rel.startsWith("..")) return;

    const QString ownerSub = owningProjectRelPathForFile(rel);
    FileContextData& fc = m_files[rel];
    fc.relativePath = rel;
    fc.subprojectRelPath = ownerSub;
    fc.sha256 = sha256;
    fc.llmModel = llmModel;
    fc.aiSummary = summary;
    fc.summaryCreated = true;
    fc.isSummaryPending = false;
    fc.pendingSha256.clear();
    fc.pendingLlmModel.clear();
    fc.updatedAt = QDateTime::currentDateTimeUtc();

    markProjectSummaryDirty(ownerSub, true);

    if (m_nodes.contains(ownerSub)) {
        saveNodeToDisk(m_nodes[ownerSub]);
    }

    if (m_batchDepth > 0) {
        m_lastBatchChangedFileRel = rel;
        m_batchProjectDirty = true;
    } else {
        emit fileContextChanged(rel);
        emit projectContextChanged();
    }
}

void ProjectManager::clearFileSummary(const QString& filePathOrRel, const QString& sha256) {
    if (!hasOpenProject()) return;
    const QString rel = toRelativePath(filePathOrRel);
    if (rel.isEmpty() || rel.startsWith("..")) return;

    const QString ownerSub = owningProjectRelPathForFile(rel);
    FileContextData& fc = m_files[rel];
    fc.relativePath = rel;
    fc.subprojectRelPath = ownerSub;
    if (!sha256.isEmpty()) fc.sha256 = sha256;
    fc.llmModel.clear();
    fc.aiSummary.clear();
    fc.summaryCreated = true;
    fc.isSummaryPending = false;
    fc.pendingSha256.clear();
    fc.pendingLlmModel.clear();
    fc.updatedAt = QDateTime::currentDateTimeUtc();

    markProjectSummaryDirty(ownerSub, true);
    if (m_nodes.contains(ownerSub)) saveNodeToDisk(m_nodes[ownerSub]);

    emit fileContextChanged(rel);
    emit projectContextChanged();
}

void ProjectManager::removeFileSummary(const QString& filePathOrRel) {
    if (!hasOpenProject()) return;
    const QString rel = toRelativePath(filePathOrRel);
    if (rel.isEmpty() || !m_files.contains(rel)) return;

    const QString ownerSub = owningProjectRelPathForFile(rel);
    FileContextData& fc = m_files[rel];
    fc.sha256.clear();
    fc.llmModel.clear();
    fc.aiSummary.clear();
    fc.summaryCreated = false;
    fc.isSummaryPending = false;
    fc.pendingSha256.clear();
    fc.pendingLlmModel.clear();

    if (fc.userPrompt.trimmed().isEmpty()) {
        m_files.remove(rel);
    }

    markProjectSummaryDirty(ownerSub, true);
    if (m_nodes.contains(ownerSub)) saveNodeToDisk(m_nodes[ownerSub]);

    emit fileContextChanged(rel);
    emit projectContextChanged();
}

bool ProjectManager::renameProjectFile(const QString& oldAbsPath, const QString& newAbsPath) {
    if (!hasOpenProject()) return false;
    m_reconciling = true;
    if (!QFile::rename(oldAbsPath, newAbsPath)) {
        m_reconciling = false;
        return false;
    }

    const QString oldRel = toRelativePath(oldAbsPath);
    const QString newRel = toRelativePath(newAbsPath);
    const QString oldOwner = owningProjectRelPathForFile(oldRel);
    const QString newOwner = owningProjectRelPathForFile(newRel);

    if (m_files.contains(oldRel)) {
        FileContextData fc = m_files.take(oldRel);
        if (!newRel.isEmpty() && !newRel.startsWith("..")) {
            fc.relativePath = newRel;
            fc.subprojectRelPath = newOwner;
            fc.updatedAt = QDateTime::currentDateTimeUtc();
            m_files.insert(newRel, fc);
        }
    }

    markProjectSummaryDirty(oldOwner, true);
    if (newOwner != oldOwner) {
        markProjectSummaryDirty(newOwner, true);
    }
    saveProjectFile();
    m_reconciling = false;

    emit fileRenamedInProject(oldRel, newRel, newAbsPath);
    emit fileContextChanged(newRel);
    emit projectContextChanged();
    return true;
}

bool ProjectManager::deleteProjectFile(const QString& absPath) {
    if (!hasOpenProject()) return false;
    const QString rel = toRelativePath(absPath);
    const QString owner = owningProjectRelPathForFile(rel);

    m_reconciling = true;
    if (!QFile::remove(absPath)) {
        m_reconciling = false;
        return false;
    }

    m_files.remove(rel);
    markProjectSummaryDirty(owner, true);
    if (m_nodes.contains(owner)) saveNodeToDisk(m_nodes[owner]);
    m_reconciling = false;

    emit fileRemovedFromProject(rel, absPath);
    emit projectContextChanged();
    return true;
}

QString ProjectManager::buildAggregatedFilesDigest(const QString& subprojectRel) const {
    if (!hasOpenProject()) return {};
    const QString normScope = normalizeScopeRel(subprojectRel);

    QStringList sections;

    // 1. Включаем саммари прямых дочерних подпроектов (.aide).
    // Если у дочернего подпроекта ещё не синтезировано собственное саммари,
    // автоматически включаем саммари проиндексированных файлов внутри него, чтобы ничего не терялось.
    const QStringList childSubs = directChildSubprojects(normScope);
    QStringList childSubBlocks;
    for (const QString& childRel : childSubs) {
        const QString childSum = projectSummary(childRel).trimmed();
        if (!childSum.isEmpty()) {
            childSubBlocks << QString("### Subproject: %1\n%2").arg(childRel, childSum);
        } else {
            const QString fallbackChildDigest = buildAggregatedFilesDigest(childRel).trimmed();
            if (!fallbackChildDigest.isEmpty()) {
                childSubBlocks << QString("### Subproject: %1 (File Summaries)\n%2")
                .arg(childRel, fallbackChildDigest);
            }
        }
    }
    if (!childSubBlocks.isEmpty()) {
        sections << "## Nested Subprojects (.aide) Summaries\n\n" + childSubBlocks.join("\n\n");
    }

    // 2. Включаем саммари всех проиндексированных файлов, принадлежащих непосредственно этому узлу
    // (включая файлы во всех вложенных подпапках, не являющихся отдельными .aide подпроектами)
    QStringList fileKeys;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        const FileContextData& fc = it.value();
        if (fc.aiSummary.trimmed().isEmpty()) continue;
        const QString owner = owningProjectRelPathForFile(fc.relativePath);
        if (owner.compare(normScope, Qt::CaseInsensitive) == 0) {
            fileKeys.append(it.key());
        }
    }
    std::sort(fileKeys.begin(), fileKeys.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    QStringList fileBlocks;
    fileBlocks.reserve(fileKeys.size());
    for (const QString& key : fileKeys) {
        const FileContextData& fc = m_files.value(key);
        fileBlocks << QString("### File: %1\n%2").arg(fc.relativePath, fc.aiSummary.trimmed());
    }
    if (!fileBlocks.isEmpty()) {
        const QString hdr = normScope.isEmpty()
        ? QStringLiteral("## Root Project Indexed Files (%1)").arg(fileBlocks.size())
        : QStringLiteral("## Subproject '%1' Indexed Files (%2)").arg(normScope).arg(fileBlocks.size());
        sections << hdr + "\n\n" + fileBlocks.join("\n\n");
    }

    return sections.join("\n\n---\n\n");
}

QString ProjectManager::buildEffectiveSystemContext(const QString& filePath) const {
    if (!hasOpenProject()) return {};
    const auto beh = ConfigManager::instance().aiBehavior();
    QStringList sections;

    const QString rootSys = projectSystemPrompt().trimmed();
    if (!rootSys.isEmpty()) {
        sections << "=== PROJECT SYSTEM INSTRUCTIONS ===\n" + rootSys;
    }

    const QString ownerSub = filePath.isEmpty() ? QString() : owningProjectRelPathForFile(filePath);
    if (!ownerSub.isEmpty()) {
        const QString subSys = projectSystemPrompt(ownerSub).trimmed();
        if (!subSys.isEmpty() && subSys != rootSys) {
            sections << QString("=== SUBPROJECT (%1) INSTRUCTIONS ===\n%2").arg(ownerSub, subSys);
        }
    }

    if (beh.includeProjectSummaryInPrompt) {
        const QString rootSum = projectSummary().trimmed();
        if (!rootSum.isEmpty()) {
            sections << "=== PROJECT ARCHITECTURE SUMMARY ===\n" + rootSum;
        }
        if (!ownerSub.isEmpty()) {
            const QString subSum = projectSummary(ownerSub).trimmed();
            if (!subSum.isEmpty()) {
                sections << QString("=== SUBPROJECT (%1) ARCHITECTURE SUMMARY ===\n%2").arg(ownerSub, subSum);
            }
        }
    }

    if (!filePath.isEmpty()) {
        const FileContextData fc = fileContext(filePath);
        if (!fc.userPrompt.trimmed().isEmpty()) {
            sections << QString("=== DOCUMENT INSTRUCTIONS (%1) ===\n%2")
            .arg(fc.relativePath, fc.userPrompt.trimmed());
        }
        if (beh.includeDocumentSummaryInPrompt && !fc.aiSummary.trimmed().isEmpty()) {
            sections << QString("=== DOCUMENT SUMMARY (%1) ===\n%2")
            .arg(fc.relativePath, fc.aiSummary.trimmed());
        }
    }

    return sections.join("\n\n");
}

QString ProjectManager::buildProjectCustomPromptContext(ProjectCustomPromptContextMode mode,
                                                        const QStringList& selectedFiles,
                                                        const QMap<QString, QString>& openBuffers,
                                                        int* outIncludedFiles) const {
    if (outIncludedFiles) *outIncludedFiles = 0;
    if (!hasOpenProject()) return {};

    QStringList sections;
    const bool includeSummary = (mode != ProjectCustomPromptContextMode::IndexedSourcesMarkdownOnly);
    const bool includeFiles   = (mode != ProjectCustomPromptContextMode::ProjectSummaryOnly);

    if (includeSummary) {
        const QString rootSum = projectSummary().trimmed();
        if (!rootSum.isEmpty()) {
            sections << "## Global Project Architecture Summary\n\n" + rootSum;
        }
        for (const QString& subRel : subprojectRelPaths()) {
            const QString subSum = projectSummary(subRel).trimmed();
            if (!subSum.isEmpty()) {
                sections << QString("## Subproject Architecture Summary: %1\n\n%2").arg(subRel, subSum);
            }
        }
    }

    if (includeFiles && !selectedFiles.isEmpty()) {
        QStringList fileBlocks;
        int included = 0;
        for (const QString& path : selectedFiles) {
            const QString absPath = QFileInfo(path).isAbsolute()
            ? QDir::cleanPath(path)
            : toAbsolutePath(path);
            const QString relPath = toRelativePath(absPath);
            QString content;
            if (openBuffers.contains(absPath)) {
                content = openBuffers.value(absPath);
            } else {
                QFile f(absPath);
                if (f.open(QIODevice::ReadOnly)) {
                    content = QString::fromUtf8(f.readAll());
                }
            }
            const FileContextData fc = fileContext(relPath);
            QString block = QString("### File: `%1`\n").arg(relPath);
            if (!fc.aiSummary.trimmed().isEmpty()) {
                block += QString("**AI Summary:**\n%1\n\n").arg(fc.aiSummary.trimmed());
            }
            if (!content.isEmpty()) {
                const QString lang = ConfigManager::instance().languageIdForFile(absPath);
                block += QString("```%1\n%2\n```").arg(lang, content);
            }
            fileBlocks.append(block);
            ++included;
        }
        if (outIncludedFiles) *outIncludedFiles = included;
        if (!fileBlocks.isEmpty()) {
            sections << "## Selected Project Files Context\n\n" + fileBlocks.join("\n\n");
        }
    }

    return sections.join("\n\n---\n\n");
}
