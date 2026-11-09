// src/core/ProjectManager.cpp
#include "ProjectManager.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QSet>
#include <algorithm>

ProjectManager::ProjectManager(QObject* parent)
    : QObject(parent) {
    m_reconcileTimer.setSingleShot(true);
    m_reconcileTimer.setInterval(250);

    connect(&m_reconcileTimer, &QTimer::timeout, this, &ProjectManager::reconcileProjectStructure);
    connect(&m_fsWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) {
        if (!m_reconciling && hasOpenProject()) {
            m_reconcileTimer.start();
        }
    });
    connect(&m_fsWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (!m_reconciling && hasOpenProject() && !QFileInfo::exists(path)) {
            m_reconcileTimer.start();
        }
    });
}

QString ProjectManager::computeSha256(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString ProjectManager::defaultAideFilePathForDir(const QString& dirPath) {
    QDir dir(dirPath);
    const QStringList aideFiles = dir.entryList({"*.aide"}, QDir::Files, QDir::Name);
    if (!aideFiles.isEmpty()) {
        return dir.filePath(aideFiles.first());
    }
    const QString dirName = dir.dirName().trimmed();
    return dir.filePath(dirName.isEmpty() ? QStringLiteral("project.aide") : (dirName + QStringLiteral(".aide")));
}

QString ProjectManager::toRelativePath(const QString& path) const {
    if (path.isEmpty()) return {};
    if (m_rootNode.rootPath.isEmpty()) {
        QString cleaned = QDir::cleanPath(path);
        cleaned.replace('\\', '/');
        return cleaned;
    }
    QFileInfo fi(path);
    QString rel;
    if (fi.isRelative()) {
        rel = QDir::cleanPath(path);
    } else {
        rel = QDir(m_rootNode.rootPath).relativeFilePath(QDir::cleanPath(fi.absoluteFilePath()));
    }
    rel.replace('\\', '/');
    while (rel.startsWith("./")) rel.remove(0, 2);
    if (rel == ".") rel.clear();
    return rel;
}

QString ProjectManager::toAbsolutePath(const QString& relOrAbsPath) const {
    if (relOrAbsPath.isEmpty()) return {};
    QFileInfo fi(relOrAbsPath);
    if (fi.isAbsolute() || m_rootNode.rootPath.isEmpty()) {
        return QDir::cleanPath(fi.absoluteFilePath());
    }
    return QDir::cleanPath(QDir(m_rootNode.rootPath).filePath(relOrAbsPath));
}

QString ProjectManager::normalizeNodeKey(const QString& projectDirOrRel) const {
    if (projectDirOrRel.trimmed().isEmpty() || !hasOpenProject()) {
        return {};
    }
    QString cleanInput = QDir::cleanPath(projectDirOrRel.trimmed());
    cleanInput.replace('\\', '/');
    if (cleanInput == "." || cleanInput.compare(m_rootNode.rootPath, Qt::CaseInsensitive) == 0) {
        return {};
    }
    QFileInfo fi(projectDirOrRel);
    QString rel = fi.isAbsolute()
                      ? QDir(m_rootNode.rootPath).relativeFilePath(QDir::cleanPath(fi.absoluteFilePath()))
                      : cleanInput;
    rel.replace('\\', '/');
    while (rel.startsWith("./")) rel.remove(0, 2);
    while (rel.endsWith('/')) rel.chop(1);
    if (rel == "." || rel.startsWith("..")) return {};
    return rel;
}

ProjectManager::ProjectNode* ProjectManager::nodeForKey(const QString& nodeKey) {
    if (nodeKey.isEmpty()) return &m_rootNode;
    auto it = m_subprojects.find(nodeKey);
    return (it != m_subprojects.end()) ? &it.value() : nullptr;
}

const ProjectManager::ProjectNode* ProjectManager::nodeForKey(const QString& nodeKey) const {
    if (nodeKey.isEmpty()) return &m_rootNode;
    auto it = m_subprojects.constFind(nodeKey);
    return (it != m_subprojects.constEnd()) ? &it.value() : nullptr;
}

QString ProjectManager::findOwningNodeKeyForAbsPath(const QString& absPath) const {
    if (!hasOpenProject() || absPath.isEmpty()) return {};
    QString relToRoot = toRelativePath(absPath);
    if (relToRoot.isEmpty() || relToRoot.startsWith("..")) return {};

    QString bestKey;
    for (auto it = m_subprojects.constBegin(); it != m_subprojects.constEnd(); ++it) {
        const QString& subKey = it.key();
        if (relToRoot.compare(subKey, Qt::CaseInsensitive) == 0 ||
            relToRoot.startsWith(subKey + "/", Qt::CaseInsensitive)) {
            if (subKey.length() > bestKey.length()) {
                bestKey = subKey;
            }
        }
    }
    return bestKey;
}

QString ProjectManager::findParentNodeKey(const QString& childNodeKey) const {
    if (childNodeKey.isEmpty()) return {};
    QString bestParent;
    for (auto it = m_subprojects.constBegin(); it != m_subprojects.constEnd(); ++it) {
        const QString& candidate = it.key();
        if (candidate.length() < childNodeKey.length() &&
            childNodeKey.startsWith(candidate + "/", Qt::CaseInsensitive)) {
            if (candidate.length() > bestParent.length()) {
                bestParent = candidate;
            }
        }
    }
    return bestParent;
}

void ProjectManager::markNodeAndAncestorsDirty(const QString& nodeKey) {
    QString cur = nodeKey;
    while (true) {
        if (auto* n = nodeForKey(cur)) {
            n->projectSummaryDirty = true;
        }
        if (cur.isEmpty()) break;
        cur = findParentNodeKey(cur);
    }
}

bool ProjectManager::isSkippedSystemDir(const QString& dirName) const {
    if (dirName.isEmpty() || dirName == "." || dirName == "..") return true;
    if (dirName.compare(".git", Qt::CaseInsensitive) == 0 ||
        dirName.compare(".aide", Qt::CaseInsensitive) == 0) {
        return true;
    }
    return ConfigManager::instance().isDirectoryIgnoredForSummary(dirName);
}

bool ProjectManager::loadNodeFromDisk(const QString& aideFilePath, ProjectNode& outNode) {
    QFile f(aideFilePath);
    if (!f.open(QIODevice::ReadOnly)) return false;

    QFileInfo fi(aideFilePath);
    outNode.aideFilePath = QDir::cleanPath(fi.absoluteFilePath());
    outNode.rootPath = QDir::cleanPath(fi.absolutePath());

    const auto root = QJsonDocument::fromJson(f.readAll()).object();
    outNode.projectSystemPrompt = root["project_system_prompt"].toString();
    outNode.projectSummary = root["project_summary"].toString();
    outNode.projectSummaryLlm = root["project_summary_llm"].toString();
    outNode.projectSummaryDirty = root["project_summary_dirty"].toBool(false);

    if (root.contains("ignored_dirs") && root["ignored_dirs"].isArray()) {
        outNode.ignoredDirs.clear();
        for (const auto& v : root["ignored_dirs"].toArray()) {
            const QString d = v.toString().trimmed();
            if (!d.isEmpty()) outNode.ignoredDirs.append(d);
        }
    }

    outNode.files.clear();
    const auto filesObj = root["files"].toObject();
    for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
        QString localKey = QDir::cleanPath(it.key());
        localKey.replace('\\', '/');
        while (localKey.startsWith("./")) localKey.remove(0, 2);
        if (localKey.isEmpty() || localKey == ".") continue;

        const auto fo = it.value().toObject();
        FileContextData fc;
        fc.localRelativePath = localKey;
        fc.subprojectRelPath = outNode.relPathFromRoot;
        fc.relativePath = outNode.relPathFromRoot.isEmpty()
                              ? localKey
                              : (outNode.relPathFromRoot + "/" + localKey);
        fc.sha256 = fo["sha256"].toString();
        fc.llmModel = fo["llm_model"].toString();
        fc.userPrompt = fo["user_prompt"].toString();
        fc.aiSummary = fo["ai_summary"].toString();
        fc.summaryCreated = fo.contains("summary_created")
                                ? fo["summary_created"].toBool()
                                : !fc.aiSummary.trimmed().isEmpty();
        fc.updatedAt = QDateTime::fromString(fo["updated_at"].toString(), Qt::ISODate);
        fc.isSummaryPending = false;

        outNode.files.insert(localKey, fc);
    }
    return true;
}

bool ProjectManager::saveNodeToDisk(const ProjectNode& node) {
    if (node.aideFilePath.isEmpty() || node.rootPath.isEmpty()) return false;

    QJsonObject filesObj;
    for (auto it = node.files.constBegin(); it != node.files.constEnd(); ++it) {
        const QString& localPath = it.key();
        const FileContextData& data = it.value();
        if (!data.summaryCreated &&
            data.aiSummary.trimmed().isEmpty() &&
            data.userPrompt.trimmed().isEmpty()) {
            continue;
        }
        filesObj[localPath] = QJsonObject{
            {"sha256", data.sha256},
            {"llm_model", data.llmModel},
            {"user_prompt", data.userPrompt},
            {"ai_summary", data.aiSummary},
            {"summary_created", data.summaryCreated},
            {"updated_at", data.updatedAt.isValid()
                               ? data.updatedAt.toString(Qt::ISODate)
                               : QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}
        };
    }

    QJsonArray ignoredArr;
    for (const QString& d : node.ignoredDirs) ignoredArr.append(d);

    QJsonObject root{
        {"project_name", QDir(node.rootPath).dirName()},
        {"ignored_dirs", ignoredArr},
        {"project_system_prompt", node.projectSystemPrompt},
        {"project_summary", node.projectSummary},
        {"project_summary_llm", node.projectSummaryLlm},
        {"project_summary_dirty", node.projectSummaryDirty},
        {"updated_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"files", filesObj}
    };

    QSaveFile f(node.aideFilePath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool ProjectManager::openProjectFolder(const QString& folderPath) {
    QDir dir(folderPath);
    if (!dir.exists()) return false;

    m_reconciling = true;
    m_reconcileTimer.stop();

    m_subprojects.clear();
    m_rootNode = ProjectNode{};
    m_rootNode.rootPath = QDir::cleanPath(dir.absolutePath());
    m_rootNode.aideFilePath = defaultAideFilePathForDir(m_rootNode.rootPath);
    m_rootNode.relPathFromRoot.clear();

    if (QFile::exists(m_rootNode.aideFilePath)) {
        loadNodeFromDisk(m_rootNode.aideFilePath, m_rootNode);
    } else {
        saveNodeToDisk(m_rootNode);
    }

    m_reconciling = false;
    reconcileProjectStructure();

    emit projectLoaded(m_rootNode.rootPath);
    emit subprojectsChanged();
    emit projectContextChanged();
    return true;
}

bool ProjectManager::loadProjectFile(const QString& aideFilePath) {
    QFileInfo fi(aideFilePath);
    if (!fi.exists() || !fi.isFile()) return false;
    return openProjectFolder(fi.absolutePath());
}

bool ProjectManager::saveProjectFile() {
    if (!hasOpenProject()) return false;
    m_reconciling = true;
    bool ok = saveNodeToDisk(m_rootNode);
    for (auto it = m_subprojects.constBegin(); it != m_subprojects.constEnd(); ++it) {
        if (!saveNodeToDisk(it.value())) {
            ok = false;
        }
    }
    m_reconciling = false;
    return ok;
}

void ProjectManager::closeProject() {
    m_reconcileTimer.stop();
    if (hasOpenProject()) {
        saveProjectFile();
    }
    if (!m_fsWatcher.directories().isEmpty()) {
        m_fsWatcher.removePaths(m_fsWatcher.directories());
    }
    if (!m_fsWatcher.files().isEmpty()) {
        m_fsWatcher.removePaths(m_fsWatcher.files());
    }
    m_rootNode = ProjectNode{};
    m_subprojects.clear();
    emit projectClosed();
    emit subprojectsChanged();
    emit projectContextChanged();
}

void ProjectManager::rebuildFileWatchers() {
    if (!m_fsWatcher.directories().isEmpty()) {
        m_fsWatcher.removePaths(m_fsWatcher.directories());
    }
    if (!m_fsWatcher.files().isEmpty()) {
        m_fsWatcher.removePaths(m_fsWatcher.files());
    }
    if (!hasOpenProject()) return;

    QStringList dirsToWatch;
    auto collectDirs = [&](auto& self, const QString& currentDir) -> void {
        dirsToWatch.append(currentDir);
        QDir dir(currentDir);
        const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                                                        QDir::NoSort);
        for (const QFileInfo& subFi : subDirs) {
            if (isSkippedSystemDir(subFi.fileName())) continue;
            self(self, subFi.absoluteFilePath());
        }
    };
    collectDirs(collectDirs, m_rootNode.rootPath);

    if (!dirsToWatch.isEmpty()) {
        m_fsWatcher.addPaths(dirsToWatch);
    }
}

void ProjectManager::reconcileProjectStructure() {
    if (!hasOpenProject() || m_reconciling) return;
    m_reconciling = true;

    QSet<QString> dirtyNodesToSave;
    bool subprojectsListChanged = false;

    // 1. Рекурсивно обходим структуру папок: находим все вложенные .aide файлы и существующие на диске файлы
    QMap<QString, QString> discoveredSubprojectAideFiles; // key: relPathFromRoot -> aideFilePath
    QSet<QString> existingDiskFilesAbs;                   // канонические/абсолютные пути существующих файлов

    auto scanTree = [&](auto& self, const QString& currentDirPath) -> void {
        QDir dir(currentDirPath);
        if (!dir.exists()) return;

        const QString cleanDir = QDir::cleanPath(dir.absolutePath());
        if (cleanDir.compare(m_rootNode.rootPath, Qt::CaseInsensitive) != 0) {
            const QStringList aideList = dir.entryList({"*.aide"}, QDir::Files, QDir::Name);
            if (!aideList.isEmpty()) {
                const QString subRel = toRelativePath(cleanDir);
                if (!subRel.isEmpty() && !subRel.startsWith("..")) {
                    discoveredSubprojectAideFiles.insert(subRel, QDir::cleanPath(dir.filePath(aideList.first())));
                }
            }
        }

        const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
        for (const QFileInfo& fFi : files) {
            if (fFi.suffix().compare("aide", Qt::CaseInsensitive) == 0) continue;
            existingDiskFilesAbs.insert(QDir::cleanPath(fFi.absoluteFilePath()));
        }

        const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
        for (const QFileInfo& subFi : subDirs) {
            if (isSkippedSystemDir(subFi.fileName())) continue;
            self(self, subFi.absoluteFilePath());
        }
    };
    scanTree(scanTree, m_rootNode.rootPath);

    // 2. Обрабатываем удалённые подпроекты (если .aide файл подпроекта был удалён с диска,
    //    переносим его саммари на уровень выше перед удалением узла из памяти)
    const QStringList previousSubKeys = m_subprojects.keys();
    for (const QString& oldSubKey : previousSubKeys) {
        if (!discoveredSubprojectAideFiles.contains(oldSubKey)) {
            ProjectNode removedNode = m_subprojects.take(oldSubKey);
            subprojectsListChanged = true;

            const QString parentKey = findParentNodeKey(oldSubKey);
            ProjectNode* parentNode = nodeForKey(parentKey);
            if (parentNode) {
                for (auto it = removedNode.files.constBegin(); it != removedNode.files.constEnd(); ++it) {
                    const QString absFile = QDir::cleanPath(QDir(removedNode.rootPath).filePath(it.key()));
                    QString newLocalRel = QDir(parentNode->rootPath).relativeFilePath(absFile);
                    newLocalRel.replace('\\', '/');
                    if (newLocalRel.isEmpty() || newLocalRel.startsWith("..")) continue;

                    FileContextData movedFc = it.value();
                    movedFc.localRelativePath = newLocalRel;
                    movedFc.subprojectRelPath = parentNode->relPathFromRoot;
                    movedFc.relativePath = toRelativePath(absFile);

                    if (!parentNode->files.contains(newLocalRel) ||
                        (parentNode->files[newLocalRel].aiSummary.trimmed().isEmpty() && !movedFc.aiSummary.trimmed().isEmpty())) {
                        parentNode->files.insert(newLocalRel, movedFc);
                    }
                }
                parentNode->projectSummaryDirty = true;
                dirtyNodesToSave.insert(parentKey);
            }
        }
    }

    // 3. Подгружаем новые вложенные подпроекты (.aide)
    for (auto it = discoveredSubprojectAideFiles.constBegin(); it != discoveredSubprojectAideFiles.constEnd(); ++it) {
        const QString& subRel = it.key();
        const QString& aidePath = it.value();
        if (!m_subprojects.contains(subRel)) {
            ProjectNode subNode;
            subNode.relPathFromRoot = subRel;
            subNode.rootPath = QFileInfo(aidePath).absolutePath();
            subNode.aideFilePath = aidePath;
            if (loadNodeFromDisk(aidePath, subNode)) {
                m_subprojects.insert(subRel, subNode);
                subprojectsListChanged = true;
            }
        }
    }

    // 4. Миграция саммари между уровнями подпроектов:
    //    Каждый файл должен храниться в самом глубоком родительском .aide (ближайшем подпроекте)
    struct CollectedFileEntry {
        QString sourceNodeKey;
        QString sourceLocalRel;
        QString absPath;
        FileContextData data;
    };
    QVector<CollectedFileEntry> allEntries;

    auto collectFromNode = [&](const QString& nodeKey, const ProjectNode& node) {
        for (auto it = node.files.constBegin(); it != node.files.constEnd(); ++it) {
            const QString absP = QDir::cleanPath(QDir(node.rootPath).filePath(it.key()));
            allEntries.append({nodeKey, it.key(), absP, it.value()});
        }
    };
    collectFromNode(QString(), m_rootNode);
    for (auto it = m_subprojects.constBegin(); it != m_subprojects.constEnd(); ++it) {
        collectFromNode(it.key(), it.value());
    }

    auto shouldReplaceEntry = [](const FileContextData& existing, const FileContextData& incoming, const QString& absPath) -> bool {
        const bool existingHasSum = existing.summaryCreated || !existing.aiSummary.trimmed().isEmpty();
        const bool incomingHasSum = incoming.summaryCreated || !incoming.aiSummary.trimmed().isEmpty();
        if (!existingHasSum && incomingHasSum) return true;
        if (existingHasSum && !incomingHasSum) return false;

        QFile f(absPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QString diskSha = computeSha256(f.readAll());
            const bool existingMatchesDisk = (!existing.sha256.isEmpty() && existing.sha256 == diskSha);
            const bool incomingMatchesDisk = (!incoming.sha256.isEmpty() && incoming.sha256 == diskSha);
            if (!existingMatchesDisk && incomingMatchesDisk) return true;
            if (existingMatchesDisk && !incomingMatchesDisk) return false;
        }
        return incoming.updatedAt > existing.updatedAt;
    };

    for (const CollectedFileEntry& item : allEntries) {
        const QString targetNodeKey = findOwningNodeKeyForAbsPath(item.absPath);
        if (targetNodeKey == item.sourceNodeKey) continue;

        ProjectNode* srcNode = nodeForKey(item.sourceNodeKey);
        ProjectNode* dstNode = nodeForKey(targetNodeKey);
        if (!srcNode || !dstNode) continue;

        QString dstLocalRel = QDir(dstNode->rootPath).relativeFilePath(item.absPath);
        dstLocalRel.replace('\\', '/');
        while (dstLocalRel.startsWith("./")) dstLocalRel.remove(0, 2);

        if (dstLocalRel.isEmpty() || dstLocalRel.startsWith("..")) continue;

        FileContextData migrated = item.data;
        migrated.localRelativePath = dstLocalRel;
        migrated.subprojectRelPath = targetNodeKey;
        migrated.relativePath = toRelativePath(item.absPath);

        if (!dstNode->files.contains(dstLocalRel) ||
            shouldReplaceEntry(dstNode->files.value(dstLocalRel), migrated, item.absPath)) {
            dstNode->files.insert(dstLocalRel, migrated);
        }
        srcNode->files.remove(item.sourceLocalRel);

        srcNode->projectSummaryDirty = true;
        dstNode->projectSummaryDirty = true;
        dirtyNodesToSave.insert(item.sourceNodeKey);
        dirtyNodesToSave.insert(targetNodeKey);
    }

    // 5. Проверка удалённых и переименованных/перемещённых файлов
    struct OrphanFileEntry {
        QString sourceNodeKey;
        QString absPath;
        QString rootRelPath;
        QString fileName;
        FileContextData data;
    };
    QVector<OrphanFileEntry> orphans;

    auto extractOrphansFromNode = [&](const QString& nodeKey, ProjectNode& node) {
        const QStringList localKeys = node.files.keys();
        for (const QString& localKey : localKeys) {
            const QString absP = QDir::cleanPath(QDir(node.rootPath).filePath(localKey));
            if (!existingDiskFilesAbs.contains(absP) && !QFileInfo::exists(absP)) {
                FileContextData taken = node.files.take(localKey);
                orphans.append({
                    nodeKey,
                    absP,
                    toRelativePath(absP),
                    QFileInfo(absP).fileName(),
                    taken
                });
                node.projectSummaryDirty = true;
                dirtyNodesToSave.insert(nodeKey);
            } else {
                // Нормализуем пути в существующей записи
                FileContextData& fc = node.files[localKey];
                fc.localRelativePath = localKey;
                fc.subprojectRelPath = nodeKey;
                fc.relativePath = toRelativePath(absP);
            }
        }
    };

    extractOrphansFromNode(QString(), m_rootNode);
    for (auto it = m_subprojects.begin(); it != m_subprojects.end(); ++it) {
        extractOrphansFromNode(it.key(), it.value());
    }

    QVector<QPair<QString, QString>> removedFilesToNotify; // (rootRelPath, absPath)
    QVector<std::tuple<QString, QString, QString>> renamedFilesToNotify; // (oldRootRel, newRootRel, newAbs)

    if (!orphans.isEmpty()) {
        // Ищем неиндексированные файлы на диске, чтобы определить переименование/перемещение по SHA-256
        QMap<QString, QStringList> unindexedDiskFilesBySha;
        for (const QString& diskAbs : existingDiskFilesAbs) {
            const QString ownerKey = findOwningNodeKeyForAbsPath(diskAbs);
            const ProjectNode* ownerNode = nodeForKey(ownerKey);
            if (!ownerNode) continue;

            QString localRel = QDir(ownerNode->rootPath).relativeFilePath(diskAbs);
            localRel.replace('\\', '/');
            if (ownerNode->files.contains(localRel)) {
                const auto& existingFc = ownerNode->files[localRel];
                if (existingFc.summaryCreated || !existingFc.aiSummary.trimmed().isEmpty()) {
                    continue;
                }
            }

            QFileInfo fi(diskAbs);
            if (fi.size() <= 0 || fi.size() > 10 * 1024 * 1024) continue;

            QFile f(diskAbs);
            if (f.open(QIODevice::ReadOnly)) {
                const QString sha = computeSha256(f.readAll());
                if (!sha.isEmpty()) {
                    unindexedDiskFilesBySha[sha].append(diskAbs);
                }
            }
        }

        for (const OrphanFileEntry& orphan : orphans) {
            QString matchedNewAbs;
            if (!orphan.data.sha256.isEmpty() && unindexedDiskFilesBySha.contains(orphan.data.sha256)) {
                QStringList& candidates = unindexedDiskFilesBySha[orphan.data.sha256];
                if (!candidates.isEmpty()) {
                    // При совпадении хэшей отдаём приоритет файлу с тем же именем (перемещение в другую папку)
                    int bestIdx = 0;
                    for (int i = 0; i < candidates.size(); ++i) {
                        if (QFileInfo(candidates[i]).fileName().compare(orphan.fileName, Qt::CaseInsensitive) == 0) {
                            bestIdx = i;
                            break;
                        }
                    }
                    matchedNewAbs = candidates.takeAt(bestIdx);
                }
            }

            if (!matchedNewAbs.isEmpty()) {
                // Файл был переименован или перемещён — переносим его саммари и промпт на новое место
                const QString newOwnerKey = findOwningNodeKeyForAbsPath(matchedNewAbs);
                if (ProjectNode* newOwnerNode = nodeForKey(newOwnerKey)) {
                    QString newLocalRel = QDir(newOwnerNode->rootPath).relativeFilePath(matchedNewAbs);
                    newLocalRel.replace('\\', '/');

                    FileContextData transferred = orphan.data;
                    transferred.localRelativePath = newLocalRel;
                    transferred.subprojectRelPath = newOwnerKey;
                    transferred.relativePath = toRelativePath(matchedNewAbs);
                    transferred.isSummaryPending = false;

                    newOwnerNode->files.insert(newLocalRel, transferred);
                    markNodeAndAncestorsDirty(newOwnerKey);
                    dirtyNodesToSave.insert(newOwnerKey);

                    renamedFilesToNotify.append({orphan.rootRelPath, transferred.relativePath, matchedNewAbs});
                }
            } else {
                // Файл был удалён — его саммари удалено из проекта
                markNodeAndAncestorsDirty(orphan.sourceNodeKey);
                removedFilesToNotify.append({orphan.rootRelPath, orphan.absPath});
            }
        }
    }

    // Протягиваем флаг projectSummaryDirty вверх от грязных подпроектов к родительским проектам
    for (auto it = m_subprojects.constBegin(); it != m_subprojects.constEnd(); ++it) {
        if (it.value().projectSummaryDirty) {
            markNodeAndAncestorsDirty(it.key());
        }
    }

    // Сохраняем все изменённые .aide файлы
    for (const QString& key : dirtyNodesToSave) {
        if (const ProjectNode* n = nodeForKey(key)) {
            saveNodeToDisk(*n);
        }
    }
    if (dirtyNodesToSave.contains(QString()) || m_rootNode.projectSummaryDirty) {
        saveNodeToDisk(m_rootNode);
    }

    rebuildFileWatchers();
    m_reconciling = false;

    for (const auto& [oldRel, newRel, newAbs] : renamedFilesToNotify) {
        emit fileRenamedInProject(oldRel, newRel, newAbs);
        emit fileContextChanged(newRel);
    }
    for (const auto& [remRel, remAbs] : removedFilesToNotify) {
        emit fileRemovedFromProject(remRel, remAbs);
        emit fileContextChanged(remRel);
    }
    if (subprojectsListChanged) {
        emit subprojectsChanged();
    }
    if (!dirtyNodesToSave.isEmpty() || subprojectsListChanged) {
        emit projectContextChanged();
    }
}

bool ProjectManager::isSubprojectDir(const QString& dirPath) const {
    if (!hasOpenProject() || dirPath.isEmpty()) return false;
    const QString key = normalizeNodeKey(dirPath);
    if (key.isEmpty()) return false;
    return m_subprojects.contains(key);
}

QStringList ProjectManager::subprojectRelPaths() const {
    QStringList keys = m_subprojects.keys();
    std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return keys;
}

QStringList ProjectManager::directChildSubprojects(const QString& parentProjectDirOrRel) const {
    const QString parentKey = normalizeNodeKey(parentProjectDirOrRel);
    QStringList directChildren;
    for (auto it = m_subprojects.constBegin(); it != m_subprojects.constEnd(); ++it) {
        if (findParentNodeKey(it.key()) == parentKey) {
            directChildren.append(it.key());
        }
    }
    std::sort(directChildren.begin(), directChildren.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return directChildren;
}

QStringList ProjectManager::dirtyProjectRelPathsBottomUp() const {
    if (!hasOpenProject()) return {};
    QStringList dirtyList;
    for (auto it = m_subprojects.constBegin(); it != m_subprojects.constEnd(); ++it) {
        if (it.value().projectSummaryDirty) {
            dirtyList.append(it.key());
        }
    }
    // Сортируем от самых глубоких вложенных подпроектов к верхним уровням
    std::sort(dirtyList.begin(), dirtyList.end(), [](const QString& a, const QString& b) {
        const int depthA = a.count('/');
        const int depthB = b.count('/');
        if (depthA != depthB) return depthA > depthB;
        return a.length() > b.length();
    });
    if (m_rootNode.projectSummaryDirty) {
        dirtyList.append(QStringLiteral(""));
    }
    return dirtyList;
}

QString ProjectManager::owningProjectRelPathForFile(const QString& filePath) const {
    if (!hasOpenProject() || filePath.isEmpty()) return {};
    return findOwningNodeKeyForAbsPath(toAbsolutePath(filePath));
}

QString ProjectManager::owningProjectDirPathForFile(const QString& filePath) const {
    if (!hasOpenProject()) return {};
    const QString key = owningProjectRelPathForFile(filePath);
    const ProjectNode* n = nodeForKey(key);
    return n ? n->rootPath : m_rootNode.rootPath;
}

QString ProjectManager::projectDirPathForRel(const QString& projectDirOrRel) const {
    if (!hasOpenProject()) return {};
    const QString key = normalizeNodeKey(projectDirOrRel);
    const ProjectNode* n = nodeForKey(key);
    return n ? n->rootPath : m_rootNode.rootPath;
}

bool ProjectManager::createSubproject(const QString& dirPath) {
    if (!hasOpenProject() || dirPath.isEmpty()) return false;
    QFileInfo fi(dirPath);
    if (!fi.exists() || !fi.isDir()) return false;

    const QString subKey = normalizeNodeKey(fi.absoluteFilePath());
    if (subKey.isEmpty() || m_subprojects.contains(subKey)) return false;

    m_reconciling = true;
    ProjectNode newNode;
    newNode.rootPath = QDir::cleanPath(fi.absoluteFilePath());
    newNode.aideFilePath = defaultAideFilePathForDir(newNode.rootPath);
    newNode.relPathFromRoot = subKey;
    newNode.projectSummaryDirty = true;

    if (QFile::exists(newNode.aideFilePath)) {
        loadNodeFromDisk(newNode.aideFilePath, newNode);
    } else if (!saveNodeToDisk(newNode)) {
        m_reconciling = false;
        return false;
    }

    m_subprojects.insert(subKey, newNode);
    markNodeAndAncestorsDirty(subKey);
    m_reconciling = false;

    // reconcileProjectStructure() автоматически перенесёт саммари файлов этой папки из родительского .aide в новый подпроект
    reconcileProjectStructure();
    saveProjectFile();

    emit subprojectsChanged();
    emit projectContextChanged();
    return true;
}

bool ProjectManager::removeSubproject(const QString& dirPath) {
    if (!hasOpenProject() || dirPath.isEmpty()) return false;
    const QString subKey = normalizeNodeKey(dirPath);
    if (subKey.isEmpty() || !m_subprojects.contains(subKey)) return false;

    m_reconciling = true;
    ProjectNode removedNode = m_subprojects.take(subKey);
    const QString parentKey = findParentNodeKey(subKey);
    ProjectNode* parentNode = nodeForKey(parentKey);

    // Переносим все саммари файлов из удаляемого подпроекта в родительский проект (.aide)
    if (parentNode) {
        for (auto it = removedNode.files.constBegin(); it != removedNode.files.constEnd(); ++it) {
            const QString absFile = QDir::cleanPath(QDir(removedNode.rootPath).filePath(it.key()));
            QString newLocalRel = QDir(parentNode->rootPath).relativeFilePath(absFile);
            newLocalRel.replace('\\', '/');
            if (newLocalRel.isEmpty() || newLocalRel.startsWith("..")) continue;

            FileContextData movedFc = it.value();
            movedFc.localRelativePath = newLocalRel;
            movedFc.subprojectRelPath = parentNode->relPathFromRoot;
            movedFc.relativePath = toRelativePath(absFile);

            parentNode->files.insert(newLocalRel, movedFc);
        }
        markNodeAndAncestorsDirty(parentKey);
        saveNodeToDisk(*parentNode);
    }

    // Удаляем ТОЛЬКО .aide файлы в папке подпроекта, саму папку и файлы исходников не трогаем
    QDir subDir(removedNode.rootPath);
    for (const QString& aideFile : subDir.entryList({"*.aide"}, QDir::Files)) {
        QFile::remove(subDir.filePath(aideFile));
    }

    saveProjectFile();
    m_reconciling = false;

    emit subprojectsChanged();
    emit projectContextChanged();
    return true;
}

bool ProjectManager::renameProjectFile(const QString& oldFilePath, const QString& newFilePath) {
    if (oldFilePath.isEmpty() || newFilePath.isEmpty()) return false;
    const QString oldAbs = toAbsolutePath(oldFilePath);
    const QString newAbs = toAbsolutePath(newFilePath);
    if (oldAbs == newAbs) return true;

    m_reconciling = true;
    const QString oldOwnerKey = findOwningNodeKeyForAbsPath(oldAbs);
    ProjectNode* oldOwner = nodeForKey(oldOwnerKey);
    FileContextData savedData;
    bool hadContext = false;

    if (oldOwner) {
        QString oldLocalRel = QDir(oldOwner->rootPath).relativeFilePath(oldAbs);
        oldLocalRel.replace('\\', '/');
        if (oldOwner->files.contains(oldLocalRel)) {
            savedData = oldOwner->files.value(oldLocalRel);
            hadContext = true;
        }
    }

    if (!QFile::rename(oldAbs, newAbs)) {
        m_reconciling = false;
        return false;
    }

    if (oldOwner && hadContext) {
        QString oldLocalRel = QDir(oldOwner->rootPath).relativeFilePath(oldAbs);
        oldLocalRel.replace('\\', '/');
        oldOwner->files.remove(oldLocalRel);
        markNodeAndAncestorsDirty(oldOwnerKey);
        saveNodeToDisk(*oldOwner);
    }

    const QString newOwnerKey = findOwningNodeKeyForAbsPath(newAbs);
    ProjectNode* newOwner = nodeForKey(newOwnerKey);
    if (newOwner && hadContext) {
        QString newLocalRel = QDir(newOwner->rootPath).relativeFilePath(newAbs);
        newLocalRel.replace('\\', '/');

        savedData.localRelativePath = newLocalRel;
        savedData.subprojectRelPath = newOwnerKey;
        savedData.relativePath = toRelativePath(newAbs);
        newOwner->files.insert(newLocalRel, savedData);
        markNodeAndAncestorsDirty(newOwnerKey);
        saveNodeToDisk(*newOwner);
    }

    m_reconciling = false;

    const QString oldRootRel = toRelativePath(oldAbs);
    const QString newRootRel = toRelativePath(newAbs);
    emit fileRenamedInProject(oldRootRel, newRootRel, newAbs);
    emit fileContextChanged(newRootRel);
    emit projectContextChanged();
    return true;
}

bool ProjectManager::deleteProjectFile(const QString& filePath) {
    if (filePath.isEmpty()) return false;
    const QString absPath = toAbsolutePath(filePath);
    const QString rootRel = toRelativePath(absPath);

    m_reconciling = true;
    if (QFileInfo::exists(absPath) && !QFile::remove(absPath)) {
        m_reconciling = false;
        return false;
    }

    const QString ownerKey = findOwningNodeKeyForAbsPath(absPath);
    if (ProjectNode* owner = nodeForKey(ownerKey)) {
        QString localRel = QDir(owner->rootPath).relativeFilePath(absPath);
        localRel.replace('\\', '/');
        if (owner->files.remove(localRel) > 0) {
            markNodeAndAncestorsDirty(ownerKey);
            saveNodeToDisk(*owner);
        }
    }
    saveNodeToDisk(m_rootNode);
    m_reconciling = false;

    emit fileRemovedFromProject(rootRel, absPath);
    emit fileContextChanged(rootRel);
    emit projectContextChanged();
    return true;
}

QString ProjectManager::projectSystemPrompt(const QString& projectDirOrRel) const {
    const QString key = normalizeNodeKey(projectDirOrRel);
    const ProjectNode* n = nodeForKey(key);
    return n ? n->projectSystemPrompt : QString();
}

void ProjectManager::setProjectSystemPrompt(const QString& prompt, const QString& projectDirOrRel) {
    if (!hasOpenProject()) return;
    const QString key = normalizeNodeKey(projectDirOrRel);
    ProjectNode* n = nodeForKey(key);
    if (!n || n->projectSystemPrompt == prompt) return;
    n->projectSystemPrompt = prompt;
    m_reconciling = true;
    saveNodeToDisk(*n);
    m_reconciling = false;
    emit projectContextChanged();
}

QString ProjectManager::projectSummary(const QString& projectDirOrRel) const {
    const QString key = normalizeNodeKey(projectDirOrRel);
    const ProjectNode* n = nodeForKey(key);
    return n ? n->projectSummary : QString();
}

QString ProjectManager::projectSummaryLlm(const QString& projectDirOrRel) const {
    const QString key = normalizeNodeKey(projectDirOrRel);
    const ProjectNode* n = nodeForKey(key);
    return n ? n->projectSummaryLlm : QString();
}

void ProjectManager::setProjectSummary(const QString& summary,
                                       const QString& llmModel,
                                       const QString& projectDirOrRel) {
    if (!hasOpenProject()) return;
    const QString key = normalizeNodeKey(projectDirOrRel);
    ProjectNode* n = nodeForKey(key);
    if (!n) return;

    n->projectSummary = summary;
    if (!llmModel.isEmpty()) {
        n->projectSummaryLlm = llmModel;
    }
    n->projectSummaryDirty = false;

    // Обновление саммари вложенного подпроекта делает неактуальным саммари родительского проекта
    if (!key.isEmpty()) {
        const QString parentKey = findParentNodeKey(key);
        markNodeAndAncestorsDirty(parentKey);
    }

    m_reconciling = true;
    saveProjectFile();
    m_reconciling = false;
    emit projectContextChanged();
}

bool ProjectManager::isProjectSummaryDirty(const QString& projectDirOrRel) const {
    if (!hasOpenProject()) return false;
    if (projectDirOrRel.trimmed().isEmpty()) {
        if (m_rootNode.projectSummaryDirty) return true;
        for (auto it = m_subprojects.constBegin(); it != m_subprojects.constEnd(); ++it) {
            if (it.value().projectSummaryDirty) return true;
        }
        return false;
    }
    const QString key = normalizeNodeKey(projectDirOrRel);
    const ProjectNode* n = nodeForKey(key);
    return n ? n->projectSummaryDirty : false;
}

FileContextData ProjectManager::fileContext(const QString& absoluteOrRelPath) const {
    const QString absPath = toAbsolutePath(absoluteOrRelPath);
    const QString rootRel = toRelativePath(absPath);
    const QString ownerKey = findOwningNodeKeyForAbsPath(absPath);
    const ProjectNode* owner = nodeForKey(ownerKey);

    if (owner && !owner->rootPath.isEmpty()) {
        QString localRel = QDir(owner->rootPath).relativeFilePath(absPath);
        localRel.replace('\\', '/');
        while (localRel.startsWith("./")) localRel.remove(0, 2);

        if (owner->files.contains(localRel)) {
            FileContextData fc = owner->files.value(localRel);
            fc.relativePath = rootRel;
            fc.localRelativePath = localRel;
            fc.subprojectRelPath = ownerKey;
            return fc;
        }
        FileContextData emptyFc;
        emptyFc.relativePath = rootRel;
        emptyFc.localRelativePath = localRel;
        emptyFc.subprojectRelPath = ownerKey;
        return emptyFc;
    }

    FileContextData fallback;
    fallback.relativePath = rootRel;
    fallback.localRelativePath = rootRel;
    return fallback;
}

void ProjectManager::updateFileUserPrompt(const QString& filePath, const QString& prompt) {
    if (!hasOpenProject() || filePath.isEmpty()) return;
    const QString absPath = toAbsolutePath(filePath);
    const QString rootRel = toRelativePath(absPath);
    const QString ownerKey = findOwningNodeKeyForAbsPath(absPath);
    ProjectNode* owner = nodeForKey(ownerKey);
    if (!owner) return;

    QString localRel = QDir(owner->rootPath).relativeFilePath(absPath);
    localRel.replace('\\', '/');

    auto& entry = owner->files[localRel];
    entry.relativePath = rootRel;
    entry.localRelativePath = localRel;
    entry.subprojectRelPath = ownerKey;
    if (entry.userPrompt == prompt) return;
    entry.userPrompt = prompt;

    m_reconciling = true;
    saveNodeToDisk(*owner);
    m_reconciling = false;
    emit fileContextChanged(rootRel);
}

void ProjectManager::updateFileSummary(const QString& filePath,
                                       const QString& sha256,
                                       const QString& llmModel,
                                       const QString& summary) {
    if (!hasOpenProject() || filePath.isEmpty()) return;
    const QString absPath = toAbsolutePath(filePath);
    // Если файл был удалён пока считалось его саммари — не записываем мёртвую запись в .aide
    if (!QFileInfo::exists(absPath)) {
        removeFileSummary(filePath);
        return;
    }

    const QString rootRel = toRelativePath(absPath);
    const QString ownerKey = findOwningNodeKeyForAbsPath(absPath);
    ProjectNode* owner = nodeForKey(ownerKey);
    if (!owner) return;

    QString localRel = QDir(owner->rootPath).relativeFilePath(absPath);
    localRel.replace('\\', '/');

    auto& entry = owner->files[localRel];
    entry.relativePath = rootRel;
    entry.localRelativePath = localRel;
    entry.subprojectRelPath = ownerKey;
    entry.sha256 = sha256;
    entry.llmModel = llmModel;
    entry.aiSummary = summary;
    entry.summaryCreated = true;
    entry.updatedAt = QDateTime::currentDateTimeUtc();
    entry.isSummaryPending = false;
    entry.pendingSha256.clear();
    entry.pendingLlmModel.clear();

    markNodeAndAncestorsDirty(ownerKey);

    m_reconciling = true;
    saveProjectFile();
    m_reconciling = false;

    emit fileContextChanged(rootRel);
    emit projectContextChanged();
}

void ProjectManager::clearFileSummary(const QString& filePath, const QString& sha256) {
    if (!hasOpenProject() || filePath.isEmpty()) return;
    const QString absPath = toAbsolutePath(filePath);
    const QString rootRel = toRelativePath(absPath);
    const QString ownerKey = findOwningNodeKeyForAbsPath(absPath);
    ProjectNode* owner = nodeForKey(ownerKey);
    if (!owner) return;

    QString localRel = QDir(owner->rootPath).relativeFilePath(absPath);
    localRel.replace('\\', '/');

    auto& entry = owner->files[localRel];
    entry.relativePath = rootRel;
    entry.localRelativePath = localRel;
    entry.subprojectRelPath = ownerKey;
    if (!sha256.isEmpty()) entry.sha256 = sha256;
    entry.aiSummary.clear();
    entry.llmModel.clear();
    entry.summaryCreated = true;
    entry.updatedAt = QDateTime::currentDateTimeUtc();
    entry.isSummaryPending = false;
    entry.pendingSha256.clear();
    entry.pendingLlmModel.clear();

    markNodeAndAncestorsDirty(ownerKey);

    m_reconciling = true;
    saveProjectFile();
    m_reconciling = false;

    emit fileContextChanged(rootRel);
    emit projectContextChanged();
}

void ProjectManager::removeFileSummary(const QString& filePath) {
    if (!hasOpenProject() || filePath.isEmpty()) return;
    const QString absPath = toAbsolutePath(filePath);
    const QString rootRel = toRelativePath(absPath);
    const QString ownerKey = findOwningNodeKeyForAbsPath(absPath);
    ProjectNode* owner = nodeForKey(ownerKey);
    if (!owner) return;

    QString localRel = QDir(owner->rootPath).relativeFilePath(absPath);
    localRel.replace('\\', '/');
    if (!owner->files.contains(localRel)) return;

    auto& entry = owner->files[localRel];
    if (entry.userPrompt.trimmed().isEmpty()) {
        owner->files.remove(localRel);
    } else {
        entry.sha256.clear();
        entry.llmModel.clear();
        entry.aiSummary.clear();
        entry.summaryCreated = false;
        entry.isSummaryPending = false;
        entry.pendingSha256.clear();
        entry.pendingLlmModel.clear();
    }

    markNodeAndAncestorsDirty(ownerKey);

    m_reconciling = true;
    saveProjectFile();
    m_reconciling = false;

    emit fileContextChanged(rootRel);
    emit projectContextChanged();
}

void ProjectManager::setFileSummaryPending(const QString& filePath,
                                           bool pending,
                                           const QString& pendingSha256,
                                           const QString& pendingLlmModel) {
    if (!hasOpenProject() || filePath.isEmpty()) return;
    const QString absPath = toAbsolutePath(filePath);
    const QString rootRel = toRelativePath(absPath);
    const QString ownerKey = findOwningNodeKeyForAbsPath(absPath);
    ProjectNode* owner = nodeForKey(ownerKey);
    if (!owner) return;

    QString localRel = QDir(owner->rootPath).relativeFilePath(absPath);
    localRel.replace('\\', '/');

    if (!pending && !owner->files.contains(localRel)) {
        return;
    }

    auto& entry = owner->files[localRel];
    entry.relativePath = rootRel;
    entry.localRelativePath = localRel;
    entry.subprojectRelPath = ownerKey;
    entry.isSummaryPending = pending;
    if (pending) {
        entry.pendingSha256 = pendingSha256;
        entry.pendingLlmModel = pendingLlmModel;
    } else {
        entry.pendingSha256.clear();
        entry.pendingLlmModel.clear();
        if (!entry.summaryCreated && entry.aiSummary.trimmed().isEmpty() && entry.userPrompt.trimmed().isEmpty()) {
            owner->files.remove(localRel);
        }
    }
    emit fileContextChanged(rootRel);
}

bool ProjectManager::isFileSummaryOutdated(const QString& filePath,
                                           const QByteArray& currentContent,
                                           const QString& currentLlm) const {
    const FileContextData fc = fileContext(filePath);
    const QString curSha = computeSha256(currentContent);
    if (fc.summaryCreated && fc.aiSummary.trimmed().isEmpty() && fc.sha256 == curSha) {
        return false;
    }
    if (fc.aiSummary.trimmed().isEmpty()) return true;
    if (fc.sha256 != curSha) return true;
    if (!currentLlm.isEmpty() && !fc.llmModel.isEmpty() && fc.llmModel != currentLlm) return true;
    return false;
}

int ProjectManager::indexedFilesCount(const QString& projectDirOrRel, bool recursive) const {
    if (!hasOpenProject()) return 0;
    const QString targetKey = normalizeNodeKey(projectDirOrRel);
    const ProjectNode* targetNode = nodeForKey(targetKey);
    if (!targetNode) return 0;

    auto countInNode = [](const ProjectNode& n) {
        int c = 0;
        for (auto it = n.files.constBegin(); it != n.files.constEnd(); ++it) {
            if (it.value().summaryCreated || !it.value().aiSummary.trimmed().isEmpty()) {
                ++c;
            }
        }
        return c;
    };

    int total = countInNode(*targetNode);
    if (recursive) {
        for (auto it = m_subprojects.constBegin(); it != m_subprojects.constEnd(); ++it) {
            const QString& subKey = it.key();
            if (targetKey.isEmpty() || subKey.startsWith(targetKey + "/", Qt::CaseInsensitive)) {
                total += countInNode(it.value());
            }
        }
    }
    return total;
}

QString ProjectManager::buildEffectiveSystemContext(const QString& filePath) const {
    const auto beh = ConfigManager::instance().aiBehavior();
    QStringList sections;

    // 1. Инструкции и саммари корневого проекта
    if (!m_rootNode.projectSystemPrompt.trimmed().isEmpty()) {
        sections << "=== PROJECT SYSTEM INSTRUCTIONS ===\n" + m_rootNode.projectSystemPrompt.trimmed();
    }
    if (beh.includeProjectSummaryInPrompt && !m_rootNode.projectSummary.trimmed().isEmpty()) {
        sections << "=== PROJECT ARCHITECTURE SUMMARY ===\n" + m_rootNode.projectSummary.trimmed();
    }

    if (!filePath.isEmpty()) {
        const QString absPath = toAbsolutePath(filePath);
        const QString ownerKey = findOwningNodeKeyForAbsPath(absPath);

        // 2. Если файл находится во вложенном подпроекте (или цепочке подпроектов) —
        //    добавляем инструкции и саммари каждого вложенного подпроекта сверху вниз
        if (!ownerKey.isEmpty()) {
            QStringList chainKeys;
            QString cur = ownerKey;
            while (!cur.isEmpty()) {
                chainKeys.prepend(cur);
                cur = findParentNodeKey(cur);
            }
            for (const QString& subKey : chainKeys) {
                if (const ProjectNode* subNode = nodeForKey(subKey)) {
                    if (!subNode->projectSystemPrompt.trimmed().isEmpty()) {
                        sections << QString("=== SUBPROJECT INSTRUCTIONS (%1) ===\n%2")
                        .arg(subKey, subNode->projectSystemPrompt.trimmed());
                    }
                    if (beh.includeProjectSummaryInPrompt && !subNode->projectSummary.trimmed().isEmpty()) {
                        sections << QString("=== SUBPROJECT ARCHITECTURE SUMMARY (%1) ===\n%2")
                        .arg(subKey, subNode->projectSummary.trimmed());
                    }
                }
            }
        }

        // 3. Инструкции и саммари самого документа
        const FileContextData fc = fileContext(absPath);
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

QString ProjectManager::buildAggregatedFilesDigest(const QString& projectDirOrRel) const {
    if (!hasOpenProject()) return {};
    const QString targetKey = normalizeNodeKey(projectDirOrRel);
    const ProjectNode* targetNode = nodeForKey(targetKey);
    if (!targetNode) return {};

    QStringList digest;

    // 1. Саммари файлов, принадлежащих непосредственно этому уровню проекта/подпроекта
    QStringList localKeys = targetNode->files.keys();
    std::sort(localKeys.begin(), localKeys.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    for (const QString& localKey : localKeys) {
        const FileContextData& fc = targetNode->files.value(localKey);
        if (fc.aiSummary.trimmed().isEmpty()) continue;

        const QString absPath = QDir::cleanPath(QDir(targetNode->rootPath).filePath(localKey));
        if (!QFileInfo::exists(absPath)) continue;

        digest << QString("## File: %1\n%2").arg(localKey, fc.aiSummary.trimmed());
    }

    // 2. Саммари прямых дочерних подпроектов являются составной частью саммари вышестоящего проекта
    const QStringList childSubs = directChildSubprojects(targetKey);
    for (const QString& childSubKey : childSubs) {
        const ProjectNode* childNode = nodeForKey(childSubKey);
        if (!childNode) continue;

        QString relSubName = targetKey.isEmpty()
                                 ? childSubKey
                                 : childSubKey.mid(targetKey.length() + 1);

        if (!childNode->projectSummary.trimmed().isEmpty()) {
            digest << QString("## Subproject: %1\n%2")
            .arg(relSubName, childNode->projectSummary.trimmed());
        } else {
            // Если у подпроекта ещё не синтезировано собственное общее саммари,
            // рекурсивно подтягиваем дайджест его файлов/подпроектов
            const QString fallbackChildDigest = buildAggregatedFilesDigest(childSubKey);
            if (!fallbackChildDigest.trimmed().isEmpty()) {
                digest << QString("## Subproject: %1 (Components Digest)\n%2")
                .arg(relSubName, fallbackChildDigest.trimmed());
            }
        }
    }

    return digest.join("\n\n");
}

QString ProjectManager::buildProjectCustomPromptContext(ProjectCustomPromptContextMode mode,
                                                        const QStringList& selectedFiles,
                                                        const QMap<QString, QString>& openBuffers,
                                                        int* outIncludedFiles) const {
    if (outIncludedFiles) *outIncludedFiles = 0;
    if (!hasOpenProject()) return {};

    QStringList sections;
    const bool includeSummary = (mode == ProjectCustomPromptContextMode::ProjectSummaryOnly ||
                                 mode == ProjectCustomPromptContextMode::BothSummaryAndSourcesMarkdown);
    const bool includeSources = (mode == ProjectCustomPromptContextMode::IndexedSourcesMarkdownOnly ||
                                 mode == ProjectCustomPromptContextMode::BothSummaryAndSourcesMarkdown);

    if (includeSummary) {
        if (!m_rootNode.projectSummary.trimmed().isEmpty()) {
            sections << "=== PROJECT ARCHITECTURE SUMMARY ===\n" + m_rootNode.projectSummary.trimmed();
        }
        // Также включаем саммари вложенных подпроектов
        for (const QString& subKey : subprojectRelPaths()) {
            const ProjectNode* subNode = nodeForKey(subKey);
            if (subNode && !subNode->projectSummary.trimmed().isEmpty()) {
                sections << QString("=== SUBPROJECT ARCHITECTURE SUMMARY (%1) ===\n%2")
                .arg(subKey, subNode->projectSummary.trimmed());
            }
        }
        if (sections.isEmpty()) {
            const QString fallbackDigest = buildAggregatedFilesDigest();
            if (!fallbackDigest.trimmed().isEmpty()) {
                sections << "=== PROJECT FILE & SUBPROJECT SUMMARIES ===\n" + fallbackDigest;
            }
        }
    }

    if (includeSources && !selectedFiles.isEmpty()) {
        QStringList fileBlocks;
        int count = 0;
        for (const QString& rawPath : selectedFiles) {
            const QString absPath = toAbsolutePath(rawPath);
            QString content;
            if (openBuffers.contains(absPath)) {
                content = openBuffers.value(absPath);
            } else {
                QFile f(absPath);
                if (!f.open(QIODevice::ReadOnly)) continue;
                content = QString::fromUtf8(f.readAll());
            }
            const QString relPath = toRelativePath(absPath);
            const QString langId = ConfigManager::instance().languageIdForFile(absPath);
            const FileContextData fc = fileContext(absPath);

            QString fileSection = QString("### File: `%1`").arg(relPath);
            if (!fc.subprojectRelPath.isEmpty()) {
                fileSection += QString(" *(Subproject: `%1`)*").arg(fc.subprojectRelPath);
            }
            if (!fc.aiSummary.trimmed().isEmpty()) {
                fileSection += QString("\n**Summary:**\n%1").arg(fc.aiSummary.trimmed());
            }
            fileSection += QString("\n```%1\n%2\n```").arg(langId, content);
            fileBlocks << fileSection;
            ++count;
        }
        if (outIncludedFiles) *outIncludedFiles = count;
        if (!fileBlocks.isEmpty()) {
            sections << "=== PROJECT SOURCE FILES ===\n\n" + fileBlocks.join("\n\n");
        }
    }

    return sections.join("\n\n");
}
