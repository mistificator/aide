// src/ui/AiPreferencesDialog.cpp
#include "AiPreferencesDialog.h"
#include "../ai/AiProvider.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QLabel>

AiPreferencesDialog::AiPreferencesDialog(AiProvider* provider, QWidget* parent)
    : QDialog(parent), m_provider(provider) {
    setWindowTitle("AI Preferences");
    resize(500, 450);

    auto cfg = ConfigManager::instance().aiConfig();
    auto* mainLay = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_backendCombo = new QComboBox(this);
    m_backendCombo->addItems({"Ollama (Native API)", "KoboldCpp (Native / SSE)", "OpenAI-Compatible (/v1/chat/completions)"});
    m_backendCombo->setCurrentIndex(static_cast<int>(cfg.backend));

    m_endpointEdit = new QLineEdit(cfg.endpointUrl, this);
    m_apiKeyEdit = new QLineEdit(cfg.apiKey, this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText("Optional Bearer API Key");

    auto* modelRow = new QHBoxLayout();
    m_modelCombo = new QComboBox(this);
    m_modelCombo->setEditable(true);
    m_modelCombo->setCurrentText(cfg.modelName);
    m_fetchBtn = new QPushButton("Test / Fetch Models", this);
    modelRow->addWidget(m_modelCombo, 1);
    modelRow->addWidget(m_fetchBtn);

    m_statusLabel = new QLabel(this);

    m_tempSpin = new QDoubleSpinBox(this);
    m_tempSpin->setRange(0.0, 2.0);
    m_tempSpin->setSingleStep(0.05);
    m_tempSpin->setValue(cfg.temperature);

    m_topPSpin = new QDoubleSpinBox(this);
    m_topPSpin->setRange(0.0, 1.0);
    m_topPSpin->setSingleStep(0.05);
    m_topPSpin->setValue(cfg.topP);

    m_maxCodeTokensSpin = new QSpinBox(this);
    m_maxCodeTokensSpin->setRange(64, 65536);
    m_maxCodeTokensSpin->setSingleStep(256);
    m_maxCodeTokensSpin->setValue(cfg.maxOutputCodeTokens);

    m_maxSummaryTokensSpin = new QSpinBox(this);
    m_maxSummaryTokensSpin->setRange(64, 65536);
    m_maxSummaryTokensSpin->setSingleStep(128);
    m_maxSummaryTokensSpin->setValue(cfg.maxOutputSummaryTokens);

    m_ctxWindowSpin = new QSpinBox(this);
    m_ctxWindowSpin->setRange(1024, 131072);
    m_ctxWindowSpin->setSingleStep(1024);
    m_ctxWindowSpin->setValue(cfg.contextWindow);

    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(10, 3600);
    m_timeoutSpin->setSuffix(" sec");
    m_timeoutSpin->setValue(cfg.timeoutSec);

    m_summaryIntervalSpin = new QSpinBox(this);
    m_summaryIntervalSpin->setRange(1, 120);
    m_summaryIntervalSpin->setSuffix(" min");
    m_summaryIntervalSpin->setValue(cfg.projectSummaryIntervalMin);

    form->addRow("Provider:", m_backendCombo);
    form->addRow("Endpoint URL:", m_endpointEdit);
    form->addRow("API Key:", m_apiKeyEdit);
    form->addRow("Model Name:", modelRow);
    form->addRow("", m_statusLabel);
    form->addRow("Temperature:", m_tempSpin);
    form->addRow("Top-P:", m_topPSpin);
    form->addRow("Max Output Code Tokens:", m_maxCodeTokensSpin);
    form->addRow("Max Output Summary Tokens:", m_maxSummaryTokensSpin);
    form->addRow("Context Window:", m_ctxWindowSpin);
    form->addRow("Request Timeout:", m_timeoutSpin);
    form->addRow("Project Summary Rebuild:", m_summaryIntervalSpin);
    mainLay->addLayout(form);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLay->addWidget(btns);

    connect(m_backendCombo, &QComboBox::currentIndexChanged, this, &AiPreferencesDialog::onBackendChanged);
    connect(m_fetchBtn, &QPushButton::clicked, this, &AiPreferencesDialog::onFetchModelsClicked);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_provider, &AiProvider::modelsFetched, this, [this](const QStringList& models) {
        m_fetchBtn->setEnabled(true);
        if (models.isEmpty()) {
            m_statusLabel->setStyleSheet("color: #ff5555;");
            m_statusLabel->setText("No models returned or connection failed.");
            return;
        }
        QString current = m_modelCombo->currentText();
        m_modelCombo->clear();
        m_modelCombo->addItems(models);
        if (models.contains(current)) m_modelCombo->setCurrentText(current);
        m_statusLabel->setStyleSheet("color: #50fa7b;");
        m_statusLabel->setText(QString("Connected! Found %1 model(s).").arg(models.size()));
    });
}

void AiPreferencesDialog::onBackendChanged(int index) {
    auto b = static_cast<AiBackendType>(index);
    QString url = m_endpointEdit->text().trimmed();
    if (b == AiBackendType::Ollama && (url.isEmpty() || url.contains(":5001")))
        m_endpointEdit->setText("http://127.0.0.1:11434");
    else if (b == AiBackendType::KoboldCpp && (url.isEmpty() || url.contains(":11434")))
        m_endpointEdit->setText("http://127.0.0.1:5001");
}

void AiPreferencesDialog::onFetchModelsClicked() {
    m_fetchBtn->setEnabled(false);
    m_statusLabel->setStyleSheet("color: #f1fa8c;");
    m_statusLabel->setText("Connecting to endpoint...");
    m_provider->fetchAvailableModels(resultConfig());
}

AiConnectionConfig AiPreferencesDialog::resultConfig() const {
    return {
        static_cast<AiBackendType>(m_backendCombo->currentIndex()),
        m_endpointEdit->text().trimmed(),
        m_apiKeyEdit->text().trimmed(),
        m_modelCombo->currentText().trimmed(),
        m_tempSpin->value(),
        m_topPSpin->value(),
        m_maxCodeTokensSpin->value(),
        m_maxSummaryTokensSpin->value(),
        m_ctxWindowSpin->value(),
        m_timeoutSpin->value(),
        m_summaryIntervalSpin->value()
    };
}
