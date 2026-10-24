// src/ui/AiTaskQueueDock.h
#pragma once
#include <QDockWidget>
#include <QTimer>

class QTableWidget;
class QLabel;
class QPushButton;
class QProgressBar;
class AiTaskQueue;
struct AiTask;

class AiTaskQueueDock : public QDockWidget {
    Q_OBJECT
public:
    explicit AiTaskQueueDock(AiTaskQueue* taskQueue, QWidget* parent = nullptr);

public slots:
    void refreshQueueView();
    void updateLiveMetrics();

private:
    static QString targetTypeToString(const AiTask& task);
    static QString formatDurationMs(qint64 ms);
    void appendTaskRow(const AiTask& task, const QString& statusText, bool isRunning);
    void updateActiveRowWidgets(const AiTask& cur);

    AiTaskQueue* m_taskQueue = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_timeStatsLabel = nullptr;
    QProgressBar* m_overallProgressBar = nullptr;
    QProgressBar* m_activeTaskProgressBar = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_cancelSelectedBtn = nullptr;
    QPushButton* m_cancelBgBtn = nullptr;
    QPushButton* m_cancelAllBtn = nullptr;
    QTimer m_liveTickTimer;
};
