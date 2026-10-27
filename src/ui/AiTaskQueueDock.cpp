// src/ui/AiTaskQueueDock.cpp
#include "AiTaskQueueDock.h"
#include "../ai/AiTaskQueue.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>

AiTaskQueueDock::AiTaskQueueDock(AiTaskQueue* taskQueue, QWidget* parent)
    : QDockWidget("AI Task Queue", parent), m_taskQueue(taskQueue) {
    auto* container = new QWidget(this);
    auto* lay = new QVBoxLayout(container);
    lay->setContentsMargins(6, 6, 6, 6);

    auto* topBar = new QHBoxLayout();
    m_summaryLabel = new QLabel("Idle (0 tasks)", container);
    m_summaryLabel->setStyleSheet("font-weight: bold;");

    m_timeStatsLabel = new QLabel("Total Elapsed: 0.0s", container);
    m_timeStatsLabel->setStyleSheet("color: #8be9fd; font-weight: bold; margin-left: 8px;");

    m_overallProgressBar = new QProgressBar(container);
    m_overallProgressBar->setMinimumWidth(180);
    m_overallProgressBar->setMaximumWidth(260);
    m_overallProgressBar->setMaximumHeight(18);
    m_overallProgressBar->setRange(0, 100);
    m_overallProgressBar->setValue(0);
    m_overallProgressBar->setTextVisible(true);
    m_overallProgressBar->setFormat("Batch: %p%");
    m_overallProgressBar->setStyleSheet(
        "QProgressBar { border: 1px solid #44475a; border-radius: 4px; background: #191a22; text-align: center; color: #f8f8f2; font-size: 11px; }"
        "QProgressBar::chunk { background-color: #bd93f9; border-radius: 3px; }");

    m_cancelSelectedBtn = new QPushButton("Cancel Selected", container);
    m_cancelBgBtn = new QPushButton("Cancel All Background", container);
    m_cancelAllBtn = new QPushButton("Cancel All", container);

    topBar->addWidget(m_summaryLabel);
    topBar->addWidget(m_timeStatsLabel);
    topBar->addWidget(m_overallProgressBar);
    topBar->addStretch();
    topBar->addWidget(m_cancelSelectedBtn);
    topBar->addWidget(m_cancelBgBtn);
    topBar->addWidget(m_cancelAllBtn);

    m_table = new QTableWidget(0, 7, container);
    m_table->setHorizontalHeaderLabels({"Status", "Progress / Speed", "Elapsed", "Type", "Target / File", "Model / Max Tokens", "Action"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->setColumnWidth(1, 210);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    lay->addLayout(topBar);
    lay->addWidget(m_table, 1);
    setWidget(container);

    m_liveTickTimer.setInterval(200);
    connect(&m_liveTickTimer, &QTimer::timeout, this, &AiTaskQueueDock::updateLiveMetrics);

    connect(m_cancelSelectedBtn, &QPushButton::clicked, this, [this]() {
        int row = m_table->currentRow();
        if (row < 0) return;
        if (auto* item = m_table->item(row, 0)) {
            QString taskId = item->data(Qt::UserRole).toString();
            m_taskQueue->cancelTaskById(taskId);
        }
    });
    connect(m_cancelBgBtn, &QPushButton::clicked, m_taskQueue, &AiTaskQueue::cancelAllBackgroundTasks);
    connect(m_cancelAllBtn, &QPushButton::clicked, m_taskQueue, &AiTaskQueue::cancelAllTasks);
    connect(m_taskQueue, &AiTaskQueue::queueChanged, this, &AiTaskQueueDock::refreshQueueView);
    connect(m_taskQueue, &AiTaskQueue::taskProgressChanged, this, [this](const AiTask&) {
        updateLiveMetrics();
    });

    refreshQueueView();
}

QString AiTaskQueueDock::formatDurationMs(qint64 ms) {
    if (ms < 0) ms = 0;
    double totalSec = ms / 1000.0;
    if (totalSec < 60.0) {
        return QString("%1s").arg(totalSec, 0, 'f', 1);
    }
    int mins = static_cast<int>(totalSec) / 60;
    double remSec = totalSec - (mins * 60);
    return QString("%1m %2s").arg(mins).arg(remSec, 0, 'f', 1);
}

QString AiTaskQueueDock::targetTypeToString(const AiTask& task) {
    switch (task.targetType) {
    case TaskTargetType::ModifyEditorCode:    return "Code Edit (User)";
    case TaskTargetType::ExplainToDialog:     return "Explain Code (User)";
    case TaskTargetType::ProjectCustomPrompt: return "Project Prompt (User)";
    case TaskTargetType::FileSummary:         return "File Summary (Bg)";
    case TaskTargetType::ProjectSummary:      return "Project Summary (Bg)";
    }
    return "Task";
}

void AiTaskQueueDock::updateActiveRowWidgets(const AiTask& cur) {
    if (m_table->rowCount() == 0) return;

    if (auto* statusItem = m_table->item(0, 0)) {
        statusItem->setText(cur.isThinking ? "Thinking" : "Running...");
        statusItem->setForeground(cur.isThinking ? QColor("#8be9fd") : QColor("#50fa7b"));
    }

    if (auto* elapsedItem = m_table->item(0, 2)) {
        elapsedItem->setText(formatDurationMs(cur.elapsedMs));
    }

    if (m_activeTaskProgressBar) {
        int totalTok = cur.generatedTokens + cur.thinkingTokens;
        if (totalTok == 0) {
            m_activeTaskProgressBar->setRange(0, 0); // Индикация обработки промпта
            m_activeTaskProgressBar->setFormat("Prompt Eval...");
        } else {
            int targetMax = qMax(cur.expectedTokens, totalTok + 32);
            if (cur.maxOutputTokens > 0) targetMax = qMin(targetMax, cur.maxOutputTokens);
            targetMax = qMax(targetMax, totalTok);

            m_activeTaskProgressBar->setRange(0, targetMax);
            m_activeTaskProgressBar->setValue(totalTok);
            QString speedStr = cur.tokensPerSec > 0.1
                                   ? QString(" (%1 tok/s)").arg(cur.tokensPerSec, 0, 'f', 1)
                                   : QString();
            if (cur.isThinking) {
                m_activeTaskProgressBar->setFormat(QString("Think: %1 tok%2").arg(cur.thinkingTokens).arg(speedStr));
            } else {
                m_activeTaskProgressBar->setFormat(QString("%1 tok%2").arg(cur.generatedTokens).arg(speedStr));
            }
        }
    }
}

void AiTaskQueueDock::updateLiveMetrics() {
    bool running = m_taskQueue->isProcessing();
    qint64 totalMs = m_taskQueue->totalBatchElapsedMs();
    int doneCount = m_taskQueue->batchCompletedCount();
    int totalCount = m_taskQueue->batchTotalCount();

    if (running) {
        AiTask cur = m_taskQueue->currentTask();
        updateActiveRowWidgets(cur);

        m_timeStatsLabel->setText(QString("Task: %1 | Total: %2")
                                      .arg(formatDurationMs(cur.elapsedMs), formatDurationMs(totalMs)));

        // Вычисляем общий прогресс пакета (завершённые задачи + доля текущей задачи)
        double currentFraction = 0.1;
        int totalTok = cur.generatedTokens + cur.thinkingTokens;
        if (totalTok > 0 && cur.expectedTokens > 0) {
            currentFraction = qBound(0.1, static_cast<double>(totalTok) / cur.expectedTokens, 0.95);
        }
        int overallPct = (totalCount > 0)
                             ? qRound(((doneCount + currentFraction) * 100.0) / totalCount)
                             : 0;
        m_overallProgressBar->setRange(0, 100);
        m_overallProgressBar->setValue(qBound(0, overallPct, 99));
        m_overallProgressBar->setFormat(QString("Batch %1/%2 (%p%)").arg(doneCount + 1).arg(totalCount));
    } else {
        if (doneCount > 0) {
            m_timeStatsLabel->setText(QString("Last batch (%1 task%2): %3")
                                          .arg(doneCount)
                                          .arg(doneCount == 1 ? "" : "s")
                                          .arg(formatDurationMs(totalMs)));
            m_overallProgressBar->setRange(0, 100);
            m_overallProgressBar->setValue(100);
            m_overallProgressBar->setFormat(QString("Done %1/%1 (100%)").arg(doneCount));
        } else {
            m_timeStatsLabel->setText("Total Elapsed: 0.0s");
            m_overallProgressBar->setRange(0, 100);
            m_overallProgressBar->setValue(0);
            m_overallProgressBar->setFormat("Idle");
        }
    }
}

void AiTaskQueueDock::appendTaskRow(const AiTask& task, const QString& statusText, bool isRunning) {
    int row = m_table->rowCount();
    m_table->insertRow(row);

    auto* statusItem = new QTableWidgetItem(statusText);
    statusItem->setData(Qt::UserRole, task.taskId);
    if (isRunning) {
        statusItem->setForeground(task.isThinking ? QColor("#8be9fd") : QColor("#50fa7b"));
        QFont f = statusItem->font();
        f.setBold(true);
        statusItem->setFont(f);
    } else {
        statusItem->setForeground(task.priority == TaskPriority::High_UserAction ? QColor("#bd93f9") : QColor("#ffb86c"));
    }

    auto* elapsedItem = new QTableWidgetItem(isRunning ? formatDurationMs(task.elapsedMs) : "-");
    elapsedItem->setTextAlignment(Qt::AlignCenter);

    auto* typeItem = new QTableWidgetItem(targetTypeToString(task));
    auto* fileItem = new QTableWidgetItem(task.filePath.isEmpty() ? "(Untitled)" : task.filePath);
    auto* modelItem = new QTableWidgetItem(QString("%1 (%2 tok)").arg(task.llmModel).arg(task.maxOutputTokens));

    m_table->setItem(row, 0, statusItem);
    m_table->setItem(row, 2, elapsedItem);
    m_table->setItem(row, 3, typeItem);
    m_table->setItem(row, 4, fileItem);
    m_table->setItem(row, 5, modelItem);

    if (isRunning) {
        m_activeTaskProgressBar = new QProgressBar(m_table);
        m_activeTaskProgressBar->setTextVisible(true);
        m_activeTaskProgressBar->setStyleSheet(
            "QProgressBar { border: 1px solid #44475a; border-radius: 3px; background: #191a22; text-align: center; color: #f8f8f2; font-size: 11px; margin: 2px; }"
            "QProgressBar::chunk { background-color: #50fa7b; border-radius: 2px; }");
        m_table->setCellWidget(row, 1, m_activeTaskProgressBar);
        updateActiveRowWidgets(task);
    } else {
        auto* waitItem = new QTableWidgetItem("Waiting...");
        waitItem->setForeground(QColor("#888899"));
        m_table->setItem(row, 1, waitItem);
    }

    auto* cancelBtn = new QPushButton("Cancel", m_table);
    cancelBtn->setStyleSheet("QPushButton { background-color: #ff5555; color: #ffffff; border: none; border-radius: 3px; padding: 2px 8px; font-weight: bold; }"
                             "QPushButton:hover { background-color: #ff6e6e; }");
    connect(cancelBtn, &QPushButton::clicked, this, [this, id = task.taskId]() {
        m_taskQueue->cancelTaskById(id);
    });
    m_table->setCellWidget(row, 6, cancelBtn);
}

void AiTaskQueueDock::refreshQueueView() {
    m_activeTaskProgressBar = nullptr;
    m_table->setRowCount(0);

    bool running = m_taskQueue->isProcessing();
    const auto highList = m_taskQueue->highQueueTasks();
    const auto lowList = m_taskQueue->lowQueueTasks();

    if (running) {
        const AiTask cur = m_taskQueue->currentTask();
        appendTaskRow(cur, cur.isThinking ? "Thinking" : "Running...", true);
        if (!m_liveTickTimer.isActive()) m_liveTickTimer.start();
    } else {
        m_liveTickTimer.stop();
    }

    for (const auto& t : highList) {
        appendTaskRow(t, "Queued (High)", false);
    }
    for (const auto& t : lowList) {
        appendTaskRow(t, "Queued (Background)", false);
    }

    int total = (running ? 1 : 0) + highList.size() + lowList.size();
    int bgTotal = lowList.size() + (running && m_taskQueue->currentTask().priority == TaskPriority::Low_BackgroundSummary ? 1 : 0);

    if (total == 0) {
        m_summaryLabel->setText("Idle (0 tasks in queue)");
    } else {
        m_summaryLabel->setText(QString("Active: %1 | User: %2 | Bg: %3")
                                    .arg(running ? 1 : 0)
                                    .arg(highList.size())
                                    .arg(lowList.size()));
    }

    m_cancelSelectedBtn->setEnabled(total > 0);
    m_cancelBgBtn->setEnabled(bgTotal > 0);
    m_cancelAllBtn->setEnabled(total > 0);

    updateLiveMetrics();
}
