// src/ui/AiTaskQueueDock.h
#pragma once
#include <QDockWidget>

class QTableWidget;
class QLabel;
class QPushButton;
class AiTaskQueue;
struct AiTask;

class AiTaskQueueDock : public QDockWidget {
    Q_OBJECT
public:
    explicit AiTaskQueueDock(AiTaskQueue* taskQueue, QWidget* parent = nullptr);

public slots:
    void refreshQueueView();

private:
    static QString targetTypeToString(const AiTask& task);
    void appendTaskRow(const AiTask& task, const QString& statusText, bool isRunning);

    AiTaskQueue* m_taskQueue = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_cancelSelectedBtn = nullptr;
    QPushButton* m_cancelBgBtn = nullptr;
    QPushButton* m_cancelAllBtn = nullptr;
};
