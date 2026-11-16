// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/rigorwallet/widgets/warningbanner.h>
#include <qt/rigorwallet/rigortheme.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace RigorWallet {

WarningBanner::WarningBanner(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("RigorBanner"));
    setFrameShape(QFrame::StyledPanel);
    setVisible(false);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(RigorTheme::spacing().unit);

    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(m_label, 1);

    m_dismissButton = new QPushButton(tr("Dismiss"), this);
    m_dismissButton->setVisible(false);
    m_dismissButton->setFlat(true);
    connect(m_dismissButton, &QPushButton::clicked, this, &WarningBanner::onDismissClicked);
    row->addWidget(m_dismissButton);

    applyLevelStyle();
}

QString WarningBanner::message() const
{
    return m_label ? m_label->text() : QString();
}

void WarningBanner::setLevel(WarningLevel level)
{
    m_level = level;
    applyLevelStyle();
}

void WarningBanner::setMessage(const QString& message)
{
    m_label->setText(message);
    setVisible(!message.isEmpty());
}

void WarningBanner::setDismissible(bool dismissible)
{
    m_dismissButton->setVisible(dismissible);
}

void WarningBanner::showBanner(WarningLevel level, const QString& message)
{
    setLevel(level);
    setMessage(message);
}

void WarningBanner::hideBanner()
{
    setMessage(QString());
}

void WarningBanner::applyLevelStyle()
{
    const ColorPalette p = RigorTheme::colors();
    const QColor chip(QStringLiteral("#6A3018"));
    const QColor chipText(QStringLiteral("#F4E8D6"));
    QColor bg = p.bgElevated;
    QColor fg = p.textPrimary;
    QColor border = p.borderSubtle;

    switch (m_level) {
    case WarningLevel::Info:
        bg = p.bgElevated;
        fg = p.textPrimary;
        border = p.borderSubtle;
        break;
    case WarningLevel::Warning:
        bg = chip;
        fg = chipText;
        border = chip;
        break;
    case WarningLevel::Critical:
        bg = chip;
        fg = chipText;
        border = chip;
        break;
    case WarningLevel::Verification:
        bg = p.bgElevated;
        fg = p.textSecondary;
        border = p.borderSubtle;
        break;
    }

    setStyleSheet(RigorTheme::bannerStylesheet(bg, fg, border));
}

void WarningBanner::onDismissClicked()
{
    hideBanner();
    Q_EMIT dismissed();
}

} // namespace RigorWallet
