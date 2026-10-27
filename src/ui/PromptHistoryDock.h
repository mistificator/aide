// src/ui/PromptHistoryDock.h
#pragma once
#include <QDockWidget>
#include <QVector>
#include "../core/ConfigManager.h"

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QLabel;

class PromptHistoryDock : public QDockWidget {
    Q_OBJECT
public:
    explicit PromptHistoryDock(QWidget* parent = nullptr);

public slots:
    void refreshList();

signals:
    void usePromptRequested(const QString& promptText);
    void useProjectPromptRequested(const QString& promptText);

private slots:
    void onSearchTextChanged(const QString& text);
    void onSelectionChanged();
    void onUseSelectedClicked();
    void onUseSelectedForProjectClicked();
    void onTogglePinClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onDeleteSelectedClicked();
    void onSaveEditClicked();
    void onAddNewPromptClicked();
    void onContextMenuRequested(const QPoint& pos);

private:
    int currentHistoryIndex() const;
    void selectByHash(const QString& sha256);
    void updateButtonsState();

    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_countLabel = nullptr;
    QListWidget* m_listWidget = nullptr;

    QPushButton* m_useBtn = nullptr;
    QPushButton* m_useProjectBtn = nullptr;
    QPushButton* m_pinBtn = nullptr;
    QPushButton* m_upBtn = nullptr;
    QPushButton* m_downBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;

    QPlainTextEdit* m_editorEdit = nullptr;
    QPushButton* m_saveEditBtn = nullptr;
    QPushButton* m_addNewBtn = nullptr;

    bool m_updatingUi = false;
};
