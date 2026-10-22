// src/ui/AiDebugConsoleDock.h
#pragma once
#include <QDockWidget>

class QPlainTextEdit;
class QCheckBox;

class AiDebugConsoleDock : public QDockWidget {
    Q_OBJECT
public:
    explicit AiDebugConsoleDock(QWidget* parent = nullptr);

public slots:
    void appendLog(const QString& category, const QString& summary, const QString& payload);
    void clearConsole();

private:
    QPlainTextEdit* m_logView = nullptr;
    QCheckBox* m_logStreamsCheck = nullptr;
};
