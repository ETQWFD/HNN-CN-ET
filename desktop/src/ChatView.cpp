#include "ChatView.h"

#include <QHBoxLayout>
#include <QScrollBar>
#include <QTimer>
#include <QFrame>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include "ThemeManager.h"

ChatView::ChatView(QWidget* parent) : QWidget(parent) {
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_container = new QWidget;
    m_layout = new QVBoxLayout(m_container);
    m_layout->setContentsMargins(24, 20, 24, 20);
    m_layout->setSpacing(12);
    m_layout->addStretch(1);

    m_scroll->setWidget(m_container);

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(m_scroll);

    applyTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &ChatView::applyTheme);
}

void ChatView::applyTheme() {
    auto& tm = ThemeManager::instance();
    QString bg = tm.windowColor().name();
    if (!tm.backgroundImage().isEmpty()) {
        // semi-transparent tint so the image shows through at chosen opacity
        int op = qBound(10, tm.backgroundOpacity(), 90);
        bg = QStringLiteral("rgba(%1, %2, %3, %4)").arg(tm.windowColor().red())
             .arg(tm.windowColor().green()).arg(tm.windowColor().blue()).arg(op / 100.0);
    }
    setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"
                                 "QWidget#container { background: %1; }").arg(bg));
    m_container->setObjectName("container");
    m_container->setStyleSheet(QStringLiteral("background: %1;").arg(bg));
}

void ChatView::scrollToBottom() {
    QTimer::singleShot(0, this, [this]() {
        m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
    });
}

QWidget* ChatView::makeBubble(const QString& text, bool user) {
    auto& tm = ThemeManager::instance();
    QWidget* row = new QWidget(m_container);
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* label = new QLabel(text, row);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setMaximumWidth(640);

    if (user) {
        label->setStyleSheet(QStringLiteral(
            "background: %1; color: #FFFFFF; border-radius: 14px; padding: 10px 14px; font-size: 14px;")
            .arg(tm.userBubbleColor().name()));
        rowLayout->addStretch(1);
        rowLayout->addWidget(label);
    } else {
        label->setStyleSheet(QStringLiteral(
            "background: %1; color: %2; border: 1px solid %3; border-radius: 14px; padding: 10px 14px; font-size: 14px;")
            .arg(tm.cardColor().name(), tm.textColor().name(), tm.borderColor().name()));
        rowLayout->addWidget(label);
        rowLayout->addStretch(1);
    }
    return row;
}

void ChatView::addUserMessage(const QString& text) {
    m_layout->insertWidget(m_layout->count() - 1, makeBubble(text, true));
    scrollToBottom();
}

void ChatView::addAssistantMessage(const QString& text) {
    m_layout->insertWidget(m_layout->count() - 1, makeBubble(text, false));
    scrollToBottom();
}

void ChatView::addSystemMessage(const QString& text) {
    QLabel* label = new QLabel(text, m_container);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    auto& tm = ThemeManager::instance();
    label->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; padding: 6px;")
                         .arg(tm.subTextColor().name()));
    m_layout->insertWidget(m_layout->count() - 1, label);
}

void ChatView::clear() {
    while (m_layout->count() > 1) {
        QLayoutItem* item = m_layout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void ChatView::beginAssistantStream() {
    m_streamText.clear();
    m_streamLabel = new QLabel(QStringLiteral("正在思考…"), m_container);
    m_streamLabel->setWordWrap(true);
    m_streamLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_streamLabel->setMaximumWidth(640);
    auto& tm = ThemeManager::instance();
    m_streamLabel->setStyleSheet(QStringLiteral(
        "background: %1; color: %2; border: 1px solid %3; border-radius: 14px; padding: 10px 14px; font-size: 14px;")
        .arg(tm.cardColor().name(), tm.textColor().name(), tm.borderColor().name()));
    QWidget* row = new QWidget(m_container);
    QHBoxLayout* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(m_streamLabel);
    rl->addStretch(1);
    m_layout->insertWidget(m_layout->count() - 1, row);
    scrollToBottom();
}

void ChatView::appendAssistantStream(const QString& delta) {
    if (!m_streamLabel) return;
    m_streamText += delta;
    m_streamLabel->setText(m_streamText);
    scrollToBottom();
}

void ChatView::endAssistantStream() {
    if (!m_streamLabel) return;
    if (m_streamText.trimmed().isEmpty()) {
        m_streamLabel->setText(QStringLiteral("（暂无回复，请换个说法重试）"));
    }
    m_streamLabel = nullptr;
}
