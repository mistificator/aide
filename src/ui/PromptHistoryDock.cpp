// src/ui/PromptHistoryDock.cpp
#include "PromptHistoryDock.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QMenu>

PromptHistoryDock::PromptHistoryDock(QWidget* parent)
    : QDockWidget("AI Prompt History", parent) {
    setObjectName("PromptHistoryDock");
    setMinimumWidth(220);

    auto* container = new QWidget(this);
    container->setMinimumWidth(0);
    auto* mainLay = new QVBoxLayout(container);
    mainLay->setContentsMargins(6, 6, 6, 6);
    mainLay->setSpacing(6);

    // 1. Строка поиска + счётчик лимита
    auto* searchRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit(container);
    m_searchEdit->setPlaceholderText("Search prompts in history...");
    m_searchEdit->setClearButtonEnabled(true);
    m_countLabel = new QLabel("0 / 50", container);
    m_countLabel->setStyleSheet("color: #888899; font-size: 11px; font-weight: bold;");
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_countLabel);
    mainLay->addLayout(searchRow);

    // 2. Панель действий над выбранным промптом
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(4);
    m_useBtn = new QPushButton("Use Prompt...", container);
    m_useBtn->setToolTip("Open Custom Prompt dialog with this prompt text");
    m_useBtn->setStyleSheet("QPushButton { background-color: #bd93f9; color: #191a24; font-weight: bold; border-radius: 3px; padding: 3px 8px; }"
                            "QPushButton:hover { background-color: #caa6ff; }"
                            "QPushButton:disabled { background-color: #44475a; color: #888899; }");

    m_pinBtn = new QPushButton("Pin", container);
    m_pinBtn->setToolTip("Pin or unpin prompt at the top of the list");

    m_upBtn = new QPushButton("Up", container);
    m_upBtn->setToolTip("Move prompt up in history");

    m_downBtn = new QPushButton("Down", container);
    m_downBtn->setToolTip("Move prompt down in history");

    m_deleteBtn = new QPushButton("Del", container);
    m_deleteBtn->setToolTip("Delete selected prompt from history");

    btnRow->addWidget(m_useBtn, 1);
    btnRow->addWidget(m_pinBtn);
    btnRow->addWidget(m_upBtn);
    btnRow->addWidget(m_downBtn);
    btnRow->addWidget(m_deleteBtn);
    mainLay->addLayout(btnRow);

    // 3. Вертикальный сплиттер: список истории сверху, редактор промпта снизу
    auto* split = new QSplitter(Qt::Vertical, container);

    m_listWidget = new QListWidget(split);
    m_listWidget->setMinimumWidth(0);
    m_listWidget->setWordWrap(true);
    m_listWidget->setAlternatingRowColors(true);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* editBox = new QWidget(split);
    editBox->setMinimumWidth(0);
    auto* editLay = new QVBoxLayout(editBox);
    editLay->setContentsMargins(0, 4, 0, 0);
    editLay->setSpacing(4);

    auto* editHdr = new QHBoxLayout();
    auto* editTitle = new QLabel("Edit Selected / New Prompt:", editBox);
    editTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_addNewBtn = new QPushButton("+ Add as New", editBox);
    m_saveEditBtn = new QPushButton("Save Changes", editBox);
    editHdr->addWidget(editTitle, 1);
    editHdr->addWidget(m_addNewBtn);
    editHdr->addWidget(m_saveEditBtn);

    m_editorEdit = new QPlainTextEdit(editBox);
    m_editorEdit->setPlaceholderText("Select a prompt above to edit its text directly, or type a new prompt and click '+ Add as New'...");

    editLay->addLayout(editHdr);
    editLay->addWidget(m_editorEdit, 1);

    split->addWidget(m_listWidget);
    split->addWidget(editBox);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 1);
    split->setSizes({360, 170});

    mainLay->addWidget(split, 1);
    setWidget(container);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &PromptHistoryDock::onSearchTextChanged);
    connect(m_listWidget, &QListWidget::currentRowChanged, this, [this](int) { onSelectionChanged(); });
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { onUseSelectedClicked(); });
    connect(m_listWidget, &QListWidget::customContextMenuRequested, this, &PromptHistoryDock::onContextMenuRequested);

    connect(m_useBtn, &QPushButton::clicked, this, &PromptHistoryDock::onUseSelectedClicked);
    connect(m_pinBtn, &QPushButton::clicked, this, &PromptHistoryDock::onTogglePinClicked);
    connect(m_upBtn, &QPushButton::clicked, this, &PromptHistoryDock::onMoveUpClicked);
    connect(m_downBtn, &QPushButton::clicked, this, &PromptHistoryDock::onMoveDownClicked);
    connect(m_deleteBtn, &QPushButton::clicked, this, &PromptHistoryDock::onDeleteSelectedClicked);
    connect(m_saveEditBtn, &QPushButton::clicked, this, &PromptHistoryDock::onSaveEditClicked);
    connect(m_addNewBtn, &QPushButton::clicked, this, &PromptHistoryDock::onAddNewPromptClicked);

    connect(m_editorEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_updatingUi) updateButtonsState();
    });

    connect(&ConfigManager::instance(), &ConfigManager::promptHistoryChanged, this, &PromptHistoryDock::refreshList);

    refreshList();
}

int PromptHistoryDock::currentHistoryIndex() const {
    auto* item = m_listWidget->currentItem();
    if (!item) return -1;
    QString hash = item->data(Qt::UserRole).toString();
    const auto list = ConfigManager::instance().promptHistory();
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].sha256 == hash) return i;
    }
    return -1;
}

void PromptHistoryDock::selectByHash(const QString& sha256) {
    if (sha256.isEmpty()) return;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        auto* it = m_listWidget->item(i);
        if (it && it->data(Qt::UserRole).toString() == sha256) {
            m_listWidget->setCurrentItem(it);
            return;
        }
    }
}

void PromptHistoryDock::refreshList() {
    m_updatingUi = true;
    QString prevHash;
    if (auto* cur = m_listWidget->currentItem()) {
        prevHash = cur->data(Qt::UserRole).toString();
    }

    m_listWidget->clear();
    const auto& cfg = ConfigManager::instance();
    const auto history = cfg.promptHistory();
    const QString filter = m_searchEdit->text().trimmed();

    m_countLabel->setText(QString("%1 / %2").arg(history.size()).arg(cfg.promptHistoryLimit()));

    for (const auto& entry : history) {
        if (!filter.isEmpty() && !entry.text.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }

        QString singleLinePreview = entry.text;
        singleLinePreview.replace('\n', ' ');
        if (singleLinePreview.length() > 140) {
            singleLinePreview = singleLinePreview.left(140) + "...";
        }

        QString display = entry.pinned ? ("[PINNED] " + singleLinePreview) : singleLinePreview;
        auto* item = new QListWidgetItem(display, m_listWidget);
        item->setData(Qt::UserRole, entry.sha256);
        item->setToolTip(entry.text);

        if (entry.pinned) {
            // Подсветка приколотых промптов в начале списка
            item->setForeground(QColor("#f1fa8c"));
            item->setBackground(QColor(189, 147, 249, 45));
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
    }

    if (!prevHash.isEmpty()) {
        selectByHash(prevHash);
    } else if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }

    m_updatingUi = false;
    onSelectionChanged();
}

void PromptHistoryDock::onSearchTextChanged(const QString&) {
    refreshList();
}

void PromptHistoryDock::onSelectionChanged() {
    if (m_updatingUi) return;
    m_updatingUi = true;
    int idx = currentHistoryIndex();
    const auto history = ConfigManager::instance().promptHistory();
    if (idx >= 0 && idx < history.size()) {
        m_editorEdit->setPlainText(history[idx].text);
        m_pinBtn->setText(history[idx].pinned ? "Unpin" : "Pin");
    } else {
        m_pinBtn->setText("Pin");
    }
    m_updatingUi = false;
    updateButtonsState();
}

void PromptHistoryDock::updateButtonsState() {
    int idx = currentHistoryIndex();
    const auto history = ConfigManager::instance().promptHistory();
    bool hasSel = (idx >= 0 && idx < history.size());
    QString editedText = m_editorEdit->toPlainText().trimmed();

    m_useBtn->setEnabled(hasSel || !editedText.isEmpty());
    m_pinBtn->setEnabled(hasSel);
    m_deleteBtn->setEnabled(hasSel);
    m_addNewBtn->setEnabled(!editedText.isEmpty());
    m_saveEditBtn->setEnabled(hasSel && !editedText.isEmpty() && editedText != history[idx].text);

    bool canMoveUp = false;
    bool canMoveDown = false;
    if (hasSel) {
        bool isPinned = history[idx].pinned;
        if (idx > 0 && history[idx - 1].pinned == isPinned) canMoveUp = true;
        if (idx + 1 < history.size() && history[idx + 1].pinned == isPinned) canMoveDown = true;
    }
    m_upBtn->setEnabled(canMoveUp);
    m_downBtn->setEnabled(canMoveDown);
}

void PromptHistoryDock::onUseSelectedClicked() {
    int idx = currentHistoryIndex();
    const auto history = ConfigManager::instance().promptHistory();
    QString textToUse;
    if (idx >= 0 && idx < history.size()) {
        textToUse = history[idx].text;
    } else {
        textToUse = m_editorEdit->toPlainText().trimmed();
    }
    if (!textToUse.isEmpty()) {
        emit usePromptRequested(textToUse);
    }
}

void PromptHistoryDock::onTogglePinClicked() {
    int idx = currentHistoryIndex();
    auto history = ConfigManager::instance().promptHistory();
    if (idx < 0 || idx >= history.size()) return;

    PromptHistoryItem item = history.takeAt(idx);
    item.pinned = !item.pinned;
    QString hash = item.sha256;

    if (item.pinned) {
        // Приколотый помещаем в начало списка
        history.prepend(item);
    } else {
        // Откреплённый помещаем сразу после всех приколотых
        int insertPos = 0;
        while (insertPos < history.size() && history[insertPos].pinned) {
            ++insertPos;
        }
        history.insert(insertPos, item);
    }

    ConfigManager::instance().setPromptHistory(history);
    selectByHash(hash);
}

void PromptHistoryDock::onMoveUpClicked() {
    int idx = currentHistoryIndex();
    auto history = ConfigManager::instance().promptHistory();
    if (idx <= 0 || idx >= history.size()) return;
    if (history[idx - 1].pinned != history[idx].pinned) return;

    QString hash = history[idx].sha256;
    history.swapItemsAt(idx - 1, idx);
    ConfigManager::instance().setPromptHistory(history);
    selectByHash(hash);
}

void PromptHistoryDock::onMoveDownClicked() {
    int idx = currentHistoryIndex();
    auto history = ConfigManager::instance().promptHistory();
    if (idx < 0 || idx + 1 >= history.size()) return;
    if (history[idx + 1].pinned != history[idx].pinned) return;

    QString hash = history[idx].sha256;
    history.swapItemsAt(idx, idx + 1);
    ConfigManager::instance().setPromptHistory(history);
    selectByHash(hash);
}

void PromptHistoryDock::onDeleteSelectedClicked() {
    int idx = currentHistoryIndex();
    auto history = ConfigManager::instance().promptHistory();
    if (idx < 0 || idx >= history.size()) return;

    history.removeAt(idx);
    ConfigManager::instance().setPromptHistory(history);
}

void PromptHistoryDock::onSaveEditClicked() {
    int idx = currentHistoryIndex();
    auto history = ConfigManager::instance().promptHistory();
    if (idx < 0 || idx >= history.size()) return;

    QString updatedText = m_editorEdit->toPlainText().trimmed();
    if (updatedText.isEmpty()) return;

    QString newHash = ConfigManager::computePromptHash(updatedText);
    history[idx].text = updatedText;
    history[idx].sha256 = newHash;
    history[idx].updatedAt = QDateTime::currentDateTimeUtc();

    ConfigManager::instance().setPromptHistory(history);
    selectByHash(newHash);
}

void PromptHistoryDock::onAddNewPromptClicked() {
    QString text = m_editorEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;
    QString hash = ConfigManager::computePromptHash(text);
    ConfigManager::instance().addPromptToHistory(text);
    selectByHash(hash);
}

void PromptHistoryDock::onContextMenuRequested(const QPoint& pos) {
    int idx = currentHistoryIndex();
    const auto history = ConfigManager::instance().promptHistory();
    bool hasSel = (idx >= 0 && idx < history.size());

    QMenu menu(this);
    if (hasSel) {
        menu.addAction("Use in Custom Prompt...", this, &PromptHistoryDock::onUseSelectedClicked);
        menu.addAction(history[idx].pinned ? "Unpin Prompt" : "Pin Prompt to Top",
                       this, &PromptHistoryDock::onTogglePinClicked);
        menu.addSeparator();
        QAction* upAct = menu.addAction("Move Up", this, &PromptHistoryDock::onMoveUpClicked);
        upAct->setEnabled(m_upBtn->isEnabled());
        QAction* downAct = menu.addAction("Move Down", this, &PromptHistoryDock::onMoveDownClicked);
        downAct->setEnabled(m_downBtn->isEnabled());
        menu.addSeparator();
        menu.addAction("Delete Prompt", this, &PromptHistoryDock::onDeleteSelectedClicked);
    }
    menu.addSeparator();
    menu.addAction("Clear All Unpinned Prompts", this, []() {
        auto list = ConfigManager::instance().promptHistory();
        QVector<PromptHistoryItem> pinnedOnly;
        for (const auto& it : list) {
            if (it.pinned) pinnedOnly.append(it);
        }
        ConfigManager::instance().setPromptHistory(pinnedOnly);
    });
    menu.exec(m_listWidget->viewport()->mapToGlobal(pos));
}
