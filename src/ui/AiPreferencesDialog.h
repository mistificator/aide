// src/ui/AiPreferencesDialog.h
#pragma once
#include <QDialog>
#include "../core/ConfigManager.h"

class QComboBox;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QLabel;
class AiProvider;

class AiPreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit AiPreferencesDialog(AiProvider* provider, QWidget* parent = nullptr);
    AiConnectionConfig resultConfig() const;

private slots:
    void onBackendChanged(int index);
    void onFetchModelsClicked();

private:
    AiProvider* m_provider = nullptr;
    QComboBox* m_backendCombo = nullptr;
    QLineEdit* m_endpointEdit = nullptr;
    QLineEdit* m_apiKeyEdit = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QPushButton* m_fetchBtn = nullptr;
    QLabel* m_statusLabel = nullptr;
    QDoubleSpinBox* m_tempSpin = nullptr;
    QDoubleSpinBox* m_topPSpin = nullptr;
    QSpinBox* m_maxTokensSpin = nullptr;
    QSpinBox* m_ctxWindowSpin = nullptr;
    QSpinBox* m_timeoutSpin = nullptr;
    QSpinBox* m_summaryIntervalSpin = nullptr;
};