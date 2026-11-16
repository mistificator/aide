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

QString wikiFileNameForSourceFile(const QString& sourceFileName) {
    if (sourceFileName.compare(QStringLiteral("README.md"), Qt::CaseInsensitive) == 0) {
        return sourceFileName + QStringLiteral(".summary.md");
    }
    return sourceFileName + QStringLiteral(".md");
}
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
            emit subprojectsChanged();
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

void ProjectManager::ensureVirtualSubprojectChain(const QString& dirRel) {
    if (!hasOpenProject()) return;
    QString clean = dirRel.trimmed();
    clean.replace('\\', '/');
    while (clean.startsWith("./")) clean.remove(0, 2);
    while (clean.startsWith('/')) clean.remove(0, 1);
    while (clean.endsWith('/')) clean.chop(1);
    if (clean.isEmpty() || clean == "." || clean.startsWith("..")) return;

    const QStringList parts = clean.split('/', Qt::SkipEmptyParts);
    QString accum;
    for (const QString& part : parts) {
        accum = accum.isEmpty() ? part : (accum + '/' + part);
        if (!m_nodes.contains(accum)) {
            ProjectNodeData vnode;
            vnode.subprojectRel = accum;
            vnode.aideFilePath.clear();
            vnode.projectName = part;
            vnode.updatedAt = QDateTime::currentDateTimeUtc();
            m_nodes.insert(accum, vnode);
        }
    }
}

void ProjectManager::ensureSubprojectNode(const QString& dirPathOrRel) {
    if (!hasOpenProject()) return;
    const QString rel = toRelativePath(dirPathOrRel);
    if (rel.isEmpty() || rel.startsWith("..")) return;
    if (m_nodes.contains(rel)) return;
    ensureVirtualSubprojectChain(rel);
    emit subprojectsChanged();
}

bool ProjectManager::hasStandaloneAideFile(const QString& dirPathOrRel) const {
    if (!hasOpenProject()) return false;
    const QString rel = normalizeScopeRel(dirPathOrRel);
    if (rel.isEmpty() || !m_nodes.contains(rel)) return false;
    return !m_nodes.value(rel).aideFilePath.isEmpty();
}

bool ProjectManager::isSubprojectDir(const QString& dirPathOrRel) const {
    if (!hasOpenProject()) return false;
    const QString rel = normalizeScopeRel(dirPathOrRel);
    if (rel.isEmpty() || rel.startsWith("..")) return false;
    if (!m_nodes.contains(rel)) return false;
    const ProjectNodeData& node = m_nodes.value(rel);
    if (!node.aideFilePath.isEmpty()) return true;
    if (!node.summary.trimmed().isEmpty() || !node.systemPrompt.trimmed().isEmpty()) {
        return true;
    }
    return indexedFilesCount(rel, true) > 0;
}

QStringList ProjectManager::subprojectRelPaths() const {
    QStringList out;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        const QString& rel = it.key();
        if (rel.isEmpty()) continue;
        // Включаем только те подпроекты, которые реально проиндексированы, имеют саммари/промпт или собственный .aide файл
        if (isSubprojectDir(rel)) {
            out.append(rel);
        }
    }
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QString ProjectManager::parentSubprojectRel(const QString& subRel) const {
    if (subRel.isEmpty()) return {};
    const int slashIdx = subRel.lastIndexOf('/');
    if (slashIdx > 0) {
        const QString directParent = subRel.left(slashIdx);
        if (m_nodes.contains(directParent)) {
            return directParent;
        }
    }
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

QString ProjectManager::owningStorageNodeForRelPath(const QString& relPath) const {
    if (relPath.isEmpty()) return {};
    QString bestStorageRel;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        const QString& nodeRel = it.key();
        if (nodeRel.isEmpty() || it.value().aideFilePath.isEmpty()) continue;
        if (relPath.compare(nodeRel, Qt::CaseInsensitive) == 0 ||
            relPath.startsWith(nodeRel + '/', Qt::CaseInsensitive)) {
            if (nodeRel.length() > bestStorageRel.length()) {
                bestStorageRel = nodeRel;
            }
        }
    }
    return bestStorageRel;
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

    if (m_nodes.contains(rel)) return rel;

    const int slashIdx = rel.lastIndexOf('/');
    if (slashIdx > 0) {
        const QString fileDirRel = rel.left(slashIdx);
        if (m_nodes.contains(fileDirRel)) {
            return fileDirRel;
        }
        QString bestSub;
        for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
            const QString& sub = it.key();
            if (sub.isEmpty()) continue;
            if (fileDirRel.compare(sub, Qt::CaseInsensitive) == 0 ||
                fileDirRel.startsWith(sub + '/', Qt::CaseInsensitive)) {
                if (sub.length() > bestSub.length()) {
                    bestSub = sub;
                }
            }
        }
        return bestSub.isEmpty() ? fileDirRel : bestSub;
    }
    return {};
}

QString ProjectManager::nearestIndexedSubprojectForFile(const QString& filePathOrRel) const {
    if (!hasOpenProject()) return {};
    QString cur = owningProjectRelPathForFile(filePathOrRel);
    while (!cur.isEmpty()) {
        if (isSubprojectDir(cur)) {
            return cur;
        }
        cur = parentSubprojectRel(cur);
    }
    return {};
}

void ProjectManager::reassignAllFilesToNearestOwners() {
    for (auto it = m_files.begin(); it != m_files.end(); ++it) {
        const QString rel = it.key();
        const int slashIdx = rel.lastIndexOf('/');
        if (slashIdx > 0) {
            ensureVirtualSubprojectChain(rel.left(slashIdx));
        }
        it.value().subprojectRelPath = owningProjectRelPathForFile(rel);
    }
}

bool ProjectManager::loadNodeFromDisk(const QString& subRel,
                                      const QString& aidePath,
                                      QMap<QString, FileContextData>& outLoadedFiles,
                                      QMap<QString, ProjectNodeData>& outLoadedVirtualNodes) {
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

        const QJsonObject vsubsObj = root["virtual_subprojects"].toObject();
        for (auto vit = vsubsObj.begin(); vit != vsubsObj.end(); ++vit) {
            QString rawSubKey = vit.key().trimmed();
            rawSubKey.replace('\\', '/');
            while (rawSubKey.startsWith("./")) rawSubKey.remove(0, 2);
            while (rawSubKey.startsWith('/')) rawSubKey.remove(0, 1);
            if (rawSubKey.isEmpty()) continue;

            QString fullSubRel = (!subRel.isEmpty() && !rawSubKey.startsWith(subRel + '/', Qt::CaseInsensitive))
                                     ? (subRel + '/' + rawSubKey)
                                     : rawSubKey;
            fullSubRel = QDir::cleanPath(fullSubRel);
            fullSubRel.replace('\\', '/');
            if (fullSubRel.isEmpty() || fullSubRel == "." || fullSubRel.startsWith("..")) continue;

            const QJsonObject vo = vit.value().toObject();
            ProjectNodeData vnode;
            vnode.subprojectRel = fullSubRel;
            vnode.aideFilePath.clear();
            vnode.projectName = vo["project_name"].toString(QFileInfo(fullSubRel).fileName());
            vnode.systemPrompt = vo["project_system_prompt"].toString();
            vnode.summary = vo["project_summary"].toString();
            vnode.summaryLlm = vo["project_summary_llm"].toString();
            vnode.summaryDirty = vo["project_summary_dirty"].toBool(false);
            vnode.updatedAt = QDateTime::fromString(vo["updated_at"].toString(), Qt::ISODate);
            if (!vnode.updatedAt.isValid()) vnode.updatedAt = node.updatedAt;

            outLoadedVirtualNodes.insert(fullSubRel, vnode);
        }

        const QJsonObject filesObj = root["files"].toObject();
        for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
            QString rawKey = it.key().trimmed();
            rawKey.replace('\\', '/');
            while (rawKey.startsWith("./")) rawKey.remove(0, 2);
            if (rawKey.isEmpty()) continue;

            QString rootRel;
            if (!subRel.isEmpty() && !rawKey.startsWith(subRel + '/', Qt::CaseInsensitive)) {
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

bool ProjectManager::saveNodeToDisk(const ProjectNodeData& storageNode) const {
    if (storageNode.aideFilePath.isEmpty()) return true;
    QFileInfo fi(storageNode.aideFilePath);
    if (!fi.dir().exists()) return false;

    QJsonObject root;
    root["project_name"] = storageNode.projectName;
    root["project_system_prompt"] = storageNode.systemPrompt;
    root["project_summary"] = storageNode.summary;
    root["project_summary_llm"] = storageNode.summaryLlm;
    root["project_summary_dirty"] = storageNode.summaryDirty;
    root["updated_at"] = storageNode.updatedAt.toUTC().toString(Qt::ISODate);

    const QString prefix = storageNode.subprojectRel.isEmpty() ? QString() : (storageNode.subprojectRel + '/');

    QJsonObject vsubsObj;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        const ProjectNodeData& vnode = it.value();
        if (!vnode.isVirtual()) continue;
        if (owningStorageNodeForRelPath(vnode.subprojectRel).compare(storageNode.subprojectRel, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (vnode.summary.trimmed().isEmpty() && vnode.systemPrompt.trimmed().isEmpty() && !vnode.summaryDirty) {
            continue;
        }
        QString localSubKey = vnode.subprojectRel;
        if (!prefix.isEmpty() && localSubKey.startsWith(prefix, Qt::CaseInsensitive)) {
            localSubKey = localSubKey.mid(prefix.length());
        }
        QJsonObject vo;
        vo["project_name"] = vnode.projectName;
        vo["project_system_prompt"] = vnode.systemPrompt;
        vo["project_summary"] = vnode.summary;
        vo["project_summary_llm"] = vnode.summaryLlm;
        vo["project_summary_dirty"] = vnode.summaryDirty;
        vo["updated_at"] = vnode.updatedAt.toUTC().toString(Qt::ISODate);
        vsubsObj[localSubKey] = vo;
    }
    if (!vsubsObj.isEmpty()) {
        root["virtual_subprojects"] = vsubsObj;
    }

    QJsonObject filesObj;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        const FileContextData& fc = it.value();
        if (owningStorageNodeForRelPath(fc.relativePath).compare(storageNode.subprojectRel, Qt::CaseInsensitive) != 0) {
            continue;
        }
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

    QSaveFile sf(storageNode.aideFilePath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    sf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return sf.commit();
}

bool ProjectManager::saveProjectFile() {
    if (!hasOpenProject()) return false;
    m_reconciling = true;
    bool allOk = true;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        if (!it.value().aideFilePath.isEmpty()) {
            if (!saveNodeToDisk(it.value())) {
                allOk = false;
            }
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

    QStringList dirsToWatch;
    dirsToWatch.append(m_rootPath);
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        if (dirsToWatch.size() >= 60) break;
        if (it.value().aideFilePath.isEmpty()) continue;
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
    const auto prevAllFiles = m_allProjectFiles;

    m_reconciling = true;
    m_rootPath = cleanRoot;
    m_nodes.clear();
    m_files.clear();
    m_allProjectFiles.clear();

    const FastIgnoreMatcher matcher = FastIgnoreMatcher::fromConfig();
    QMap<QString, QString> discoveredAideByRel;
    QStringList discoveredFiles;

    QString rootAide = findExistingAideFileInDir(cleanRoot);
    if (rootAide.isEmpty()) {
        rootAide = defaultAideFilePathForDir(cleanRoot);
    }
    discoveredAideByRel.insert(QString(), rootAide);

    int processedDirs = 0;
    int foundFilesCount = 0;
    bool cancelled = false;

    auto scanSubprojects = [&](auto& self, const QString& absDirPath, const QString& relPrefix) -> bool {
        if (cancelled) return false;
        ++processedDirs;

        if (progressCb && (processedDirs == 1 || (processedDirs % 16) == 0)) {
            if (!progressCb(processedDirs, foundFilesCount, relPrefix.isEmpty() ? cleanRoot : relPrefix)) {
                cancelled = true;
                return false;
            }
        }

        QDir d(absDirPath);
        const QFileInfoList entries = d.entryInfoList(
            QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
            QDir::NoSort);

        QString foundAideInDir;
        bool hasAnyRelevantFile = false;
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
                    hasAnyRelevantFile = true;
                    const QString fileRel = relPrefix.isEmpty()
                                                ? fi.fileName()
                                                : (relPrefix + '/' + fi.fileName());
                    discoveredFiles.append(fileRel);
                }
            }
        }

        if (!relPrefix.isEmpty() && !foundAideInDir.isEmpty()) {
            discoveredAideByRel.insert(relPrefix, foundAideInDir);
        }

        bool anyChildHasContent = false;
        for (const QFileInfo& subFi : subDirs) {
            if (cancelled) return false;
            const QString childRel = relPrefix.isEmpty()
                                         ? subFi.fileName()
                                         : (relPrefix + '/' + subFi.fileName());
            if (self(self, subFi.absoluteFilePath(), childRel)) {
                anyChildHasContent = true;
            }
        }

        return hasAnyRelevantFile || anyChildHasContent || !foundAideInDir.isEmpty();
    };

    scanSubprojects(scanSubprojects, cleanRoot, QString());

    if (cancelled) {
        m_rootPath = prevRoot;
        m_nodes = prevNodes;
        m_files = prevFiles;
        m_allProjectFiles = prevAllFiles;
        m_reconciling = false;
        return false;
    }

    std::sort(discoveredFiles.begin(), discoveredFiles.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    m_allProjectFiles = std::move(discoveredFiles);

    QMap<QString, FileContextData> loadedFiles;
    QMap<QString, ProjectNodeData> loadedVirtualNodes;
    for (auto it = discoveredAideByRel.constBegin(); it != discoveredAideByRel.constEnd(); ++it) {
        loadNodeFromDisk(it.key(), it.value(), loadedFiles, loadedVirtualNodes);
    }

    for (auto it = loadedVirtualNodes.constBegin(); it != loadedVirtualNodes.constEnd(); ++it) {
        const QString& vrel = it.key();
        if (m_nodes.contains(vrel)) continue;
        if (QDir(projectDirPathForRel(vrel)).exists()) {
            ensureVirtualSubprojectChain(vrel);
            m_nodes.insert(vrel, it.value());
        }
    }

    for (auto it = loadedFiles.begin(); it != loadedFiles.end(); ++it) {
        const QString absFile = toAbsolutePath(it.key());
        if (QFileInfo::exists(absFile)) {
            m_files.insert(it.key(), it.value());
        } else {
            const QString owner = owningProjectRelPathForFile(it.key());
            markProjectSummaryDirty(owner, true);
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

    bool subsModified = false;
    for (auto it = m_nodes.begin(); it != m_nodes.end();) {
        const QString& subRel = it.key();
        if (subRel.isEmpty()) {
            ++it;
            continue;
        }
        const QString absDir = projectDirPathForRel(subRel);
        if (!QDir(absDir).exists()) {
            it = m_nodes.erase(it);
            subsModified = true;
            continue;
        }
        if (!it.value().aideFilePath.isEmpty() && !QFileInfo::exists(it.value().aideFilePath)) {
            it.value().aideFilePath.clear();
            subsModified = true;
        }
        ++it;
    }

    if (fullDiskScan) {
        int processedDirs = 0;
        int foundFiles = 0;
        QMap<QString, ProjectNodeData> extraVirtual;
        QStringList scannedFiles;
        auto scanDirs = [&](auto& self, const QString& absDir, const QString& relPrefix) -> void {
            ++processedDirs;
            if (progressCb && (processedDirs % 20) == 0) {
                if (!progressCb(processedDirs, foundFiles, relPrefix)) return;
            }
            if (!relPrefix.isEmpty()) {
                const QString aide = findExistingAideFileInDir(absDir);
                if (!aide.isEmpty() && (!m_nodes.contains(relPrefix) || m_nodes.value(relPrefix).aideFilePath.isEmpty())) {
                    loadNodeFromDisk(relPrefix, aide, m_files, extraVirtual);
                    subsModified = true;
                }
            }
            QDir d(absDir);
            const QFileInfoList entries = d.entryInfoList(
                QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                QDir::NoSort);
            for (const QFileInfo& efi : entries) {
                if (efi.isDir()) {
                    if (efi.isSymLink() || matcher.isDirIgnored(efi.fileName())) continue;
                    const QString childRel = relPrefix.isEmpty() ? efi.fileName() : (relPrefix + '/' + efi.fileName());
                    self(self, efi.absoluteFilePath(), childRel);
                } else if (efi.suffix().compare(QStringLiteral("aide"), Qt::CaseInsensitive) != 0) {
                    const QString fileRel = relPrefix.isEmpty() ? efi.fileName() : (relPrefix + '/' + efi.fileName());
                    scannedFiles.append(fileRel);
                }
            }
        };
        scanDirs(scanDirs, m_rootPath, QString());
        std::sort(scannedFiles.begin(), scannedFiles.end(), [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        m_allProjectFiles = std::move(scannedFiles);
    }

    QStringList removedRels;
    for (auto it = m_files.begin(); it != m_files.end();) {
        const QString absPath = toAbsolutePath(it.key());
        if (!QFileInfo::exists(absPath)) {
            removedRels.append(it.key());
            const QString owner = it.value().subprojectRelPath;
            markProjectSummaryDirty(owner, true);
            it = m_files.erase(it);
            subsModified = true;
        } else {
            ++it;
        }
    }

    if (!removedRels.isEmpty()) {
        for (const QString& rem : removedRels) {
            m_allProjectFiles.removeAll(rem);
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
    m_allProjectFiles.clear();
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

    ensureVirtualSubprojectChain(subRel);
    ProjectNodeData& node = m_nodes[subRel];
    if (!node.aideFilePath.isEmpty()) return true;

    QString aidePath = findExistingAideFileInDir(absDir);
    if (aidePath.isEmpty()) {
        aidePath = defaultAideFilePathForDir(absDir);
    }
    node.aideFilePath = aidePath;
    if (node.projectName.isEmpty()) {
        node.projectName = QDir(absDir).dirName();
    }
    node.updatedAt = QDateTime::currentDateTimeUtc();

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

    ProjectNodeData& node = m_nodes[subRel];
    const QString aidePath = node.aideFilePath;
    if (aidePath.isEmpty()) return false;

    node.aideFilePath.clear();
    node.updatedAt = QDateTime::currentDateTimeUtc();

    if (QFile::exists(aidePath)) {
        QFile::remove(aidePath);
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
    if (!norm.isEmpty()) {
        ensureVirtualSubprojectChain(norm);
    }
    if (!m_nodes.contains(norm)) return;
    if (m_nodes[norm].systemPrompt == prompt) return;
    m_nodes[norm].systemPrompt = prompt;
    m_nodes[norm].updatedAt = QDateTime::currentDateTimeUtc();
    emit subprojectsChanged();
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
    if (!norm.isEmpty()) {
        ensureVirtualSubprojectChain(norm);
    }
    if (!m_nodes.contains(norm)) return;

    ProjectNodeData& node = m_nodes[norm];
    node.summary = summary;
    if (!llmModel.isEmpty()) {
        node.summaryLlm = llmModel;
    }
    node.summaryDirty = false;
    node.updatedAt = QDateTime::currentDateTimeUtc();

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

    const QString storageRel = owningStorageNodeForRelPath(norm);
    if (m_nodes.contains(storageRel)) {
        saveNodeToDisk(m_nodes[storageRel]);
    }
    emit subprojectsChanged();
    emit projectContextChanged();
}

bool ProjectManager::isProjectSummaryDirty(const QString& subprojectRel) const {
    const QString norm = normalizeScopeRel(subprojectRel);
    return m_nodes.value(norm).summaryDirty;
}

void ProjectManager::markProjectSummaryDirty(const QString& subprojectRel, bool cascadeToParents) {
    if (!hasOpenProject()) return;
    QString cur = normalizeScopeRel(subprojectRel);
    if (!cur.isEmpty()) {
        ensureVirtualSubprojectChain(cur);
    }
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
        if (includeNestedSubprojects) {
            const QFileInfoList subDirs = d.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                QDir::Name | QDir::IgnoreCase);

            for (const QFileInfo& sfi : subDirs) {
                if (sfi.isSymLink() || matcher.isDirIgnored(sfi.fileName())) continue;
                const QString childRel = relDir.isEmpty() ? sfi.fileName() : (relDir + '/' + sfi.fileName());
                self(self, sfi.absoluteFilePath(), childRel);
            }
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

    const int slashIdx = rel.lastIndexOf('/');
    if (slashIdx > 0) ensureVirtualSubprojectChain(rel.left(slashIdx));

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

    const int slashIdx = rel.lastIndexOf('/');
    if (slashIdx > 0) ensureVirtualSubprojectChain(rel.left(slashIdx));

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

    const int slashIdx = rel.lastIndexOf('/');
    if (slashIdx > 0) ensureVirtualSubprojectChain(rel.left(slashIdx));

    if (!m_allProjectFiles.contains(rel, Qt::CaseInsensitive)) {
        m_allProjectFiles.append(rel);
    }

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

    const QString storageRel = owningStorageNodeForRelPath(rel);
    if (m_nodes.contains(storageRel)) {
        saveNodeToDisk(m_nodes[storageRel]);
    }

    if (m_batchDepth > 0) {
        m_lastBatchChangedFileRel = rel;
        m_batchProjectDirty = true;
    } else {
        emit fileContextChanged(rel);
        emit subprojectsChanged();
        emit projectContextChanged();
    }
}

void ProjectManager::clearFileSummary(const QString& filePathOrRel, const QString& sha256) {
    if (!hasOpenProject()) return;
    const QString rel = toRelativePath(filePathOrRel);
    if (rel.isEmpty() || rel.startsWith("..")) return;

    const int slashIdx = rel.lastIndexOf('/');
    if (slashIdx > 0) ensureVirtualSubprojectChain(rel.left(slashIdx));

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
    const QString storageRel = owningStorageNodeForRelPath(rel);
    if (m_nodes.contains(storageRel)) saveNodeToDisk(m_nodes[storageRel]);

    emit fileContextChanged(rel);
    emit subprojectsChanged();
    emit projectContextChanged();
}

void ProjectManager::removeFileSummary(const QString& filePathOrRel) {
    if (!hasOpenProject()) return;
    const QString rel = toRelativePath(filePathOrRel);
    if (rel.isEmpty() || !m_files.contains(rel)) return;

    const QString ownerSub = owningProjectRelPathForFile(rel);
    const QString storageRel = owningStorageNodeForRelPath(rel);
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
    if (m_nodes.contains(storageRel)) saveNodeToDisk(m_nodes[storageRel]);

    emit fileContextChanged(rel);
    emit subprojectsChanged();
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
    const int newSlash = newRel.lastIndexOf('/');
    if (newSlash > 0) ensureVirtualSubprojectChain(newRel.left(newSlash));

    const QString oldOwner = owningProjectRelPathForFile(oldRel);
    const QString newOwner = owningProjectRelPathForFile(newRel);

    m_allProjectFiles.removeAll(oldRel);
    if (!newRel.isEmpty() && !newRel.startsWith("..") && !m_allProjectFiles.contains(newRel)) {
        m_allProjectFiles.append(newRel);
    }

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
    emit subprojectsChanged();
    emit projectContextChanged();
    return true;
}

bool ProjectManager::deleteProjectFile(const QString& absPath) {
    if (!hasOpenProject()) return false;
    const QString rel = toRelativePath(absPath);
    const QString owner = owningProjectRelPathForFile(rel);
    const QString storageRel = owningStorageNodeForRelPath(rel);

    m_reconciling = true;
    if (!QFile::remove(absPath)) {
        m_reconciling = false;
        return false;
    }

    m_files.remove(rel);
    m_allProjectFiles.removeAll(rel);
    markProjectSummaryDirty(owner, true);
    if (m_nodes.contains(storageRel)) saveNodeToDisk(m_nodes[storageRel]);
    m_reconciling = false;

    emit fileRemovedFromProject(rel, absPath);
    emit subprojectsChanged();
    emit projectContextChanged();
    return true;
}

int ProjectManager::exportProjectWiki(const QString& targetRootDir, QString* outError) const {
    if (!hasOpenProject()) {
        if (outError) *outError = QStringLiteral("No project is currently open.");
        return -1;
    }

    const QString cleanDestRoot = QDir::cleanPath(targetRootDir.trimmed());
    if (cleanDestRoot.isEmpty()) {
        if (outError) *outError = QStringLiteral("Destination folder path is empty.");
        return -1;
    }

    if (!QDir().mkpath(cleanDestRoot)) {
        if (outError) *outError = QStringLiteral("Failed to create destination folder: ") + cleanDestRoot;
        return -1;
    }

    const QDir destRootDir(cleanDestRoot);
    int writtenFilesCount = 0;

    auto writeMarkdownFile = [&](const QString& relOutputFilePath, const QString& content) -> bool {
        const QString absOutPath = destRootDir.filePath(relOutputFilePath);
        const QFileInfo outFi(absOutPath);
        if (!QDir().mkpath(outFi.absolutePath())) {
            if (outError) *outError = QStringLiteral("Failed to create subfolder: ") + outFi.absolutePath();
            return false;
        }
        QSaveFile sf(absOutPath);
        if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (outError) *outError = QStringLiteral("Failed to write file: ") + absOutPath;
            return false;
        }
        sf.write(content.toUtf8());
        if (!sf.commit()) {
            if (outError) *outError = QStringLiteral("Failed to commit file: ") + absOutPath;
            return false;
        }
        ++writtenFilesCount;
        return true;
    };

    // 1. Записываем страницы саммари всех проиндексированных файлов в соответствующую иерархию папок
    QStringList sortedFileRels = m_files.keys();
    std::sort(sortedFileRels.begin(), sortedFileRels.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    for (const QString& fileRel : sortedFileRels) {
        const FileContextData& fc = m_files.value(fileRel);
        if (!fc.summaryCreated && fc.aiSummary.trimmed().isEmpty()) {
            continue;
        }

        const QFileInfo srcFi(fileRel);
        const QString dirRel = (srcFi.path() == "." || srcFi.path().isEmpty()) ? QString() : srcFi.path();
        const QString wikiFileName = wikiFileNameForSourceFile(srcFi.fileName());
        const QString wikiFileRel = dirRel.isEmpty() ? wikiFileName : (dirRel + '/' + wikiFileName);

        const QString wikiFileDirAbs = destRootDir.filePath(dirRel);
        const QString relLinkToRootReadme = QDir(wikiFileDirAbs).relativeFilePath(destRootDir.filePath("README.md")).replace('\\', '/');

        QString md;
        md += QString("# `%1`\n\n").arg(srcFi.fileName());

        if (dirRel.isEmpty()) {
            md += QString("[← Project Wiki Home](./README.md)\n\n");
        } else {
            md += QString("[← Project Wiki Home](%1) | [↑ Subproject `%2`](./README.md)\n\n")
                      .arg(relLinkToRootReadme, dirRel);
        }

        md += "---\n\n";
        md += QString("- **Source Path:** `%1`\n").arg(fc.relativePath);
        md += QString("- **Subproject Scope:** `%1`\n").arg(dirRel.isEmpty() ? QStringLiteral("Root") : dirRel);
        md += QString("- **LLM Model:** `%1`\n").arg(fc.llmModel.isEmpty() ? QStringLiteral("-") : fc.llmModel);
        if (!fc.sha256.isEmpty()) {
            md += QString("- **SHA-256:** `%1`\n").arg(fc.sha256);
        }
        if (fc.updatedAt.isValid()) {
            md += QString("- **Updated:** %1 UTC\n").arg(fc.updatedAt.toUTC().toString("yyyy-MM-dd HH:mm:ss"));
        }
        md += "\n";

        if (!fc.userPrompt.trimmed().isEmpty()) {
            md += "## Document Instructions\n\n";
            md += fc.userPrompt.trimmed() + "\n\n";
        }

        md += "## AI Summary\n\n";
        if (!fc.aiSummary.trimmed().isEmpty()) {
            md += fc.aiSummary.trimmed() + "\n";
        } else {
            md += "*(Summary marked as intentionally empty / cleared)*\n";
        }

        if (!writeMarkdownFile(wikiFileRel, md)) {
            return -1;
        }
    }

    // 2. Собираем список всех проиндексированных уровней (корень "" + все активные подпроекты)
    QStringList activeScopes;
    activeScopes.append(QString());
    activeScopes.append(subprojectRelPaths());

    for (const QString& scopeRel : activeScopes) {
        const QString readmeRel = scopeRel.isEmpty()
        ? QStringLiteral("README.md")
        : (scopeRel + QStringLiteral("/README.md"));
        const QString scopeDirAbs = destRootDir.filePath(scopeRel);

        QString md;
        if (scopeRel.isEmpty()) {
            QString projTitle = QDir(m_rootPath).dirName();
            if (projTitle.isEmpty()) projTitle = m_rootPath;
            md += QString("# Project Wiki: %1\n\n").arg(projTitle);
            md += QString("- **Project Root:** `%1`\n").arg(QDir::toNativeSeparators(m_rootPath));
            md += QString("- **Total Indexed Files:** %1\n\n").arg(indexedFilesCount(QString(), true));
        } else {
            const QString relToRoot = QDir(scopeDirAbs).relativeFilePath(destRootDir.filePath("README.md")).replace('\\', '/');
            const QString parentScope = parentSubprojectRel(scopeRel);
            const QString parentReadmeAbs = destRootDir.filePath(
                parentScope.isEmpty() ? QStringLiteral("README.md") : (parentScope + QStringLiteral("/README.md")));
            const QString relToParent = QDir(scopeDirAbs).relativeFilePath(parentReadmeAbs).replace('\\', '/');

            md += QString("# Subproject: `%1`\n\n").arg(scopeRel);
            if (parentScope.isEmpty()) {
                md += QString("[← Project Wiki Home](%1)\n\n").arg(relToRoot);
            } else {
                md += QString("[← Project Wiki Home](%1) | [↑ Parent Subproject `%2`](%3)\n\n")
                          .arg(relToRoot, parentScope, relToParent);
            }
            const QString modeStr = hasStandaloneAideFile(scopeRel)
                                        ? QStringLiteral("Standalone `.aide`")
                                        : QStringLiteral("Virtual Subproject");
            const QString llmStr = projectSummaryLlm(scopeRel);
            md += QString("- **Mode:** %1\n").arg(modeStr);
            if (!llmStr.isEmpty()) {
                md += QString("- **Summary LLM:** `%1`\n").arg(llmStr);
            }
            md += QString("- **Indexed Files (including nested):** %1\n\n").arg(indexedFilesCount(scopeRel, true));
        }

        md += "---\n\n";

        const QString sysPrompt = projectSystemPrompt(scopeRel).trimmed();
        if (!sysPrompt.isEmpty()) {
            md += "## System Prompt / Instructions\n\n";
            md += sysPrompt + "\n\n";
        }

        const QString projSum = projectSummary(scopeRel).trimmed();
        md += "## Architecture Summary\n\n";
        if (!projSum.isEmpty()) {
            md += projSum + "\n\n";
        } else {
            md += "*(No synthesized architecture summary for this scope yet — see child subprojects and file summaries below.)*\n\n";
        }

        // Дочерние проиндексированные подпроекты
        const QStringList directChildren = directChildSubprojects(scopeRel);
        QStringList indexedChildren;
        for (const QString& ch : directChildren) {
            if (isSubprojectDir(ch)) {
                indexedChildren.append(ch);
            }
        }
        if (!indexedChildren.isEmpty()) {
            md += "## Subprojects\n\n";
            for (const QString& childRel : indexedChildren) {
                const QString childReadmeAbs = destRootDir.filePath(childRel + QStringLiteral("/README.md"));
                const QString relLink = QDir(scopeDirAbs).relativeFilePath(childReadmeAbs).replace('\\', '/');
                const int childFiles = indexedFilesCount(childRel, true);
                const bool childHasSum = !projectSummary(childRel).trimmed().isEmpty();
                md += QString("- **[`%1`](%2)** — %3 indexed file(s)%4\n")
                          .arg(childRel, relLink)
                          .arg(childFiles)
                          .arg(childHasSum ? QStringLiteral(" *(Architecture Summary Ready)*") : QString());
            }
            md += "\n";
        }

        // Проиндексированные файлы, лежащие непосредственно в этой папке
        QStringList directFiles;
        for (const QString& fileRel : sortedFileRels) {
            const FileContextData& fc = m_files.value(fileRel);
            if (!fc.summaryCreated && fc.aiSummary.trimmed().isEmpty()) continue;
            if (owningProjectRelPathForFile(fileRel).compare(scopeRel, Qt::CaseInsensitive) == 0) {
                directFiles.append(fileRel);
            }
        }

        if (!directFiles.isEmpty()) {
            md += "## Indexed Files in this Scope\n\n";
            for (const QString& fileRel : directFiles) {
                const QFileInfo fi(fileRel);
                const QString wikiFileName = wikiFileNameForSourceFile(fi.fileName());
                md += QString("- **[`%1`](./%2)**\n").arg(fi.fileName(), wikiFileName);
            }
            md += "\n";
        }

        // Для корневого README добавляем полное оглавление всех проиндексированных файлов проекта
        if (scopeRel.isEmpty() && !sortedFileRels.isEmpty()) {
            md += "## Complete Index of Summarized Project Files\n\n";
            for (const QString& fileRel : sortedFileRels) {
                const FileContextData& fc = m_files.value(fileRel);
                if (!fc.summaryCreated && fc.aiSummary.trimmed().isEmpty()) continue;
                const QFileInfo fi(fileRel);
                const QString dirPart = (fi.path() == "." || fi.path().isEmpty()) ? QString() : fi.path();
                const QString wikiFileName = wikiFileNameForSourceFile(fi.fileName());
                const QString wikiRel = dirPart.isEmpty() ? wikiFileName : (dirPart + '/' + wikiFileName);
                md += QString("- [`%1`](./%2)\n").arg(fileRel, wikiRel);
            }
            md += "\n";
        }

        if (!writeMarkdownFile(readmeRel, md)) {
            return -1;
        }
    }

    return writtenFilesCount;
}

QString ProjectManager::buildAggregatedFilesDigest(const QString& subprojectRel) const {
    if (!hasOpenProject()) return {};
    const QString normScope = normalizeScopeRel(subprojectRel);

    QStringList sections;

    const QStringList childSubs = directChildSubprojects(normScope);
    QStringList childSubBlocks;
    for (const QString& childRel : childSubs) {
        if (!isSubprojectDir(childRel)) continue;
        const QString childSum = projectSummary(childRel).trimmed();
        const QString modeTag = hasStandaloneAideFile(childRel) ? QStringLiteral(".aide") : QStringLiteral("virtual");
        if (!childSum.isEmpty()) {
            childSubBlocks << QString("### Subproject: %1 (%2)\n%3").arg(childRel, modeTag, childSum);
        } else if (indexedFilesCount(childRel, true) > 0) {
            const QString fallbackChildDigest = buildAggregatedFilesDigest(childRel).trimmed();
            if (!fallbackChildDigest.isEmpty()) {
                childSubBlocks << QString("### Subproject: %1 (%2 — File Summaries)\n%3")
                                      .arg(childRel, modeTag, fallbackChildDigest);
            }
        }
    }
    if (!childSubBlocks.isEmpty()) {
        sections << "## Child Subprojects Summaries\n\n" + childSubBlocks.join("\n\n");
    }

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
        ? QStringLiteral("## Root Directory Indexed Files (%1)").arg(fileBlocks.size())
        : QStringLiteral("## Subproject '%1' Direct Indexed Files (%2)").arg(normScope).arg(fileBlocks.size());
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
    QStringList ancestorChain;
    if (!ownerSub.isEmpty()) {
        QString cur = ownerSub;
        while (!cur.isEmpty()) {
            ancestorChain.prepend(cur);
            cur = parentSubprojectRel(cur);
        }
    }

    for (const QString& anc : ancestorChain) {
        const QString subSys = projectSystemPrompt(anc).trimmed();
        if (!subSys.isEmpty() && subSys != rootSys) {
            sections << QString("=== SUBPROJECT (%1) INSTRUCTIONS ===\n%2").arg(anc, subSys);
        }
    }

    if (beh.includeProjectSummaryInPrompt) {
        const QString rootSum = projectSummary().trimmed();
        if (!rootSum.isEmpty()) {
            sections << "=== PROJECT ARCHITECTURE SUMMARY ===\n" + rootSum;
        }
        for (const QString& anc : ancestorChain) {
            const QString subSum = projectSummary(anc).trimmed();
            if (!subSum.isEmpty()) {
                sections << QString("=== SUBPROJECT (%1) ARCHITECTURE SUMMARY ===\n%2").arg(anc, subSum);
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
