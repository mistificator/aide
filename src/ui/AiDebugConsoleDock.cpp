// src/ui/AiDebugConsoleDock.cpp
#include "AiDebugConsoleDock.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QDateTime>
#include <QFontDatabase>
#include <QScrollBar>

AiDebugConsoleDock::AiDebugConsoleDock(QWidget* parent)
    : QDockWidget("AI API Debug Console", parent) {
    auto* container = new QWidget(this);
    auto* lay = new QVBoxLayout(container);
    lay->setContentsMargins(6, 6, 6, 6);

    auto* topBar = new QHBoxLayout();
    m_logStreamsCheck = new QCheckBox("Log individual stream chunks", container);
    m_logStreamsCheck->setChecked(false);
    auto* clearBtn = new QPushButton("Clear Log", container);
    topBar->addWidget(m_logStreamsCheck);
    topBar->addStretch();
    topBar->addWidget(clearBtn);

    m_logView = new QPlainTextEdit(container);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(5000);
    m_logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_logView->setStyleSheet("background-color: #191a22; color: #f8f8f2;");

    lay->addLayout(topBar);
    lay->addWidget(m_logView, 1);
    setWidget(container);

    connect(clearBtn, &QPushButton::clicked, this, &AiDebugConsoleDock::clearConsole);
}

void AiDebugConsoleDock::appendLog(const QString& category, const QString& summary, const QString& payload) {
    if (category == "STREAM" && !m_logStreamsCheck->isChecked()) return;

    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString entry = QString("[%1] [%2] %3").arg(ts, category, summary);
    if (!payload.trimmed().isEmpty()) {
        entry += "\n" + payload.trimmed() + "\n" + QString(60, '-');
    }
    m_logView->appendPlainText(entry);
    m_logView->verticalScrollBar()->setValue(m_logView->verticalScrollBar()->maximum());
}

void AiDebugConsoleDock::clearConsole() {
    m_logView->clear();
}
