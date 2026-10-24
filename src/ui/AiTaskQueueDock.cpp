// src/ui/AiTaskQueueDock.cpp
#include "AiTaskQueueDock.h"
#include "../ai/AiTaskQueue.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>

AiTaskQueueDock::AiTaskQueueDock(AiTaskQueue* taskQueue, QWidget* parent)
    : QDockWidget("AI Task Queue", parent), m_taskQueue(taskQueue) {
    auto* container = new QWidget(this);
    auto* lay = new QVBoxLayout(container);
    lay->setContentsMargins(6, 6, 6, 6);

    auto* topBar = new QHBoxLayout();
    m_summaryLabel = new QLabel("Idle (0 tasks)", container);
    m_summaryLabel->setStyleSheet("font-weight: bold;");

    m_cancelSelectedBtn = new QPushButton("Cancel Selected", container);
    m_cancelBgBtn = new QPushButton("Cancel All Background", container);
    m_cancelAllBtn = new QPushButton("Cancel All", container);

    topBar->addWidget(m_summaryLabel);
    topBar->addStretch();
    topBar->addWidget(m_cancelSelectedBtn);
    topBar->addWidget(m_cancelBgBtn);
    topBar->addWidget(m_cancelAllBtn);

    m_table = new QTableWidget(0, 5, container);
    m_table->setHorizontalHeaderLabels({"Status", "Type", "Target / File", "Model / Max Tokens", "Action"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    lay->addLayout(topBar);
    lay->addWidget(m_table, 1);
    setWidget(container);

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

    refreshQueueView();
}

QString AiTaskQueueDock::targetTypeToString(const AiTask& task) {
    switch (task.targetType) {
    case TaskTargetType::ModifyEditorCode: return "Code Edit (User)";
    case TaskTargetType::ExplainToDialog:  return "Explain Code (User)";
    case TaskTargetType::FileSummary:      return "File Summary (Bg)";
    case TaskTargetType::ProjectSummary:   return "Project Summary (Bg)";
    }
    return "Task";
}

void AiTaskQueueDock::appendTaskRow(const AiTask& task, const QString& statusText, bool isRunning) {
    int row = m_table->rowCount();
    m_table->insertRow(row);

    auto* statusItem = new QTableWidgetItem(statusText);
    statusItem->setData(Qt::UserRole, task.taskId);
    if (isRunning) {
        // Голубой (#8be9fd) при Thinking, зелёный (#50fa7b) при генерации результата
        statusItem->setForeground(task.isThinking ? QColor("#8be9fd") : QColor("#50fa7b"));
        QFont f = statusItem->font();
        f.setBold(true);
        statusItem->setFont(f);
    } else {
        statusItem->setForeground(task.priority == TaskPriority::High_UserAction ? QColor("#bd93f9") : QColor("#ffb86c"));
    }

    auto* typeItem = new QTableWidgetItem(targetTypeToString(task));
    auto* fileItem = new QTableWidgetItem(task.filePath.isEmpty() ? "(Untitled)" : task.filePath);
    auto* modelItem = new QTableWidgetItem(QString("%1 (%2 tok)").arg(task.llmModel).arg(task.maxOutputTokens));

    m_table->setItem(row, 0, statusItem);
    m_table->setItem(row, 1, typeItem);
    m_table->setItem(row, 2, fileItem);
    m_table->setItem(row, 3, modelItem);

    auto* cancelBtn = new QPushButton("Cancel", m_table);
    cancelBtn->setStyleSheet("QPushButton { background-color: #ff5555; color: #ffffff; border: none; border-radius: 3px; padding: 2px 8px; font-weight: bold; }"
                             "QPushButton:hover { background-color: #ff6e6e; }");
    connect(cancelBtn, &QPushButton::clicked, this, [this, id = task.taskId]() {
        m_taskQueue->cancelTaskById(id);
    });
    m_table->setCellWidget(row, 4, cancelBtn);
}

void AiTaskQueueDock::refreshQueueView() {
    m_table->setRowCount(0);

    bool running = m_taskQueue->isProcessing();
    const auto highList = m_taskQueue->highQueueTasks();
    const auto lowList = m_taskQueue->lowQueueTasks();

    if (running) {
        const AiTask cur = m_taskQueue->currentTask();
        appendTaskRow(cur, cur.isThinking ? "Thinking" : "Running...", true);
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
        m_summaryLabel->setText(QString("Active: %1 | User Queue: %2 | Background Queue: %3")
                                    .arg(running ? 1 : 0)
                                    .arg(highList.size())
                                    .arg(lowList.size()));
    }

    m_cancelSelectedBtn->setEnabled(total > 0);
    m_cancelBgBtn->setEnabled(bgTotal > 0);
    m_cancelAllBtn->setEnabled(total > 0);
}
