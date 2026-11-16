// Copyright (c) 2011-2022 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/rigor-config.h>
#endif

#include <qt/utilitydialog.h>

#include <qt/forms/ui_helpmessagedialog.h>

#include <qt/guiutil.h>
#include <qt/rigorwallet/walletidentity.h>

#include <clientversion.h>
#include <init.h>
#include <shutdown.h>
#include <util/system.h>
#include <util/strencodings.h>

#include <cstdio>

#include <QCloseEvent>
#include <QColor>
#include <QLabel>
#include <QMainWindow>
#include <QPalette>
#include <QString>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextTable>
#include <QVBoxLayout>

/** "Help message" or "About" dialog box */
HelpMessageDialog::HelpMessageDialog(QWidget *parent, bool about) :
    QDialog(parent, GUIUtil::dialog_flags),
    ui(new Ui::HelpMessageDialog)
{
    ui->setupUi(this);
    GUIUtil::ApplyRigorDialogChrome(this);

    const QString productName = RigorWallet::WalletIdentity::brand().productName;
    QString version = productName + " " + tr("version") + " " + QString::fromUtf8(RIGOR_RELEASE_VERSION);

    if (about)
    {
        setWindowTitle(tr("About %1").arg(productName) + QStringLiteral(" ") + QString::fromUtf8(RIGOR_RELEASE_VERSION));

        // Product About copy only — no Bitcoin Core / upstream MIT dump.
        const QString compliance = RigorWallet::WalletIdentity::aboutComplianceHtml();
        ui->aboutMessage->setTextFormat(Qt::RichText);
        ui->aboutMessage->setOpenExternalLinks(true);
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        text = version + "\n" + productName;
        ui->aboutMessage->setText(version + "<br><br>" + compliance);
        ui->aboutMessage->setWordWrap(true);
        ui->helpMessage->setVisible(false);
    } else {
        setWindowTitle(tr("Command-line options"));
        // Read-only help body: dark surface + light text (overrides dialog Base/Text for inputs).
        {
            QPalette help_pal = ui->helpMessage->palette();
            help_pal.setColor(QPalette::Base, QColor(QStringLiteral("#F3E6D0")));
            help_pal.setColor(QPalette::Text, QColor(QStringLiteral("#26394B")));
            ui->helpMessage->setPalette(help_pal);
        }
        QTextCharFormat body_fmt;
        body_fmt.setForeground(QColor(QStringLiteral("#26394B")));

        QString header = "Usage:  rigor-qt [command-line options]                     \n";
        QTextCursor cursor(ui->helpMessage->document());
        cursor.setCharFormat(body_fmt);
        cursor.insertText(version, body_fmt);
        cursor.insertBlock();
        cursor.insertText(header, body_fmt);
        cursor.insertBlock();

        std::string strUsage = gArgs.GetHelpMessage();
        QString coreOptions = QString::fromStdString(strUsage);
        text = version + "\n\n" + header + "\n" + coreOptions;

        QTextTableFormat tf;
        tf.setBorderStyle(QTextFrameFormat::BorderStyle_None);
        tf.setCellPadding(2);
        QVector<QTextLength> widths;
        widths << QTextLength(QTextLength::PercentageLength, 35);
        widths << QTextLength(QTextLength::PercentageLength, 65);
        tf.setColumnWidthConstraints(widths);

        QTextCharFormat bold = body_fmt;
        bold.setFontWeight(QFont::Bold);

        for (const QString &line : coreOptions.split("\n")) {
            if (line.startsWith("  -"))
            {
                cursor.currentTable()->appendRows(1);
                cursor.movePosition(QTextCursor::PreviousCell);
                cursor.movePosition(QTextCursor::NextRow);
                cursor.insertText(line.trimmed(), body_fmt);
                cursor.movePosition(QTextCursor::NextCell);
            } else if (line.startsWith("   ")) {
                cursor.insertText(line.trimmed()+' ', body_fmt);
            } else if (line.size() > 0) {
                //Title of a group
                if (cursor.currentTable())
                    cursor.currentTable()->appendRows(1);
                cursor.movePosition(QTextCursor::Down);
                cursor.insertText(line.trimmed(), bold);
                cursor.insertTable(1, 2, tf);
            }
        }

        ui->helpMessage->moveCursor(QTextCursor::Start);
        ui->scrollArea->setVisible(false);
        ui->aboutLogo->setVisible(false);
    }

    GUIUtil::handleCloseWindowShortcut(this);
}

HelpMessageDialog::~HelpMessageDialog()
{
    delete ui;
}

void HelpMessageDialog::printToConsole()
{
    // On other operating systems, the expected action is to print the message to the console.
    tfm::format(std::cout, "%s\n", qPrintable(text));
}

void HelpMessageDialog::showOrPrint()
{
#if defined(WIN32)
    // On Windows, show a message box, as there is no stderr/stdout in windowed applications
    exec();
#else
    // On other operating systems, print help text to console
    printToConsole();
#endif
}

void HelpMessageDialog::on_okButton_accepted()
{
    close();
}


/** "Shutdown" window */
ShutdownWindow::ShutdownWindow(QWidget *parent, Qt::WindowFlags f):
    QWidget(parent, f)
{
    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(new QLabel(
        tr("%1 is shutting down…").arg(RigorWallet::WalletIdentity::brand().productName) + "<br /><br />" +
        tr("Do not shut down the computer until this window disappears.")));
    setLayout(layout);

    GUIUtil::handleCloseWindowShortcut(this);
}

QWidget* ShutdownWindow::showShutdownWindow(QMainWindow* window)
{
    assert(window != nullptr);

    // Show a simple window indicating shutdown status
    QWidget *shutdownWindow = new ShutdownWindow();
    shutdownWindow->setWindowTitle(window->windowTitle());

    // Center shutdown window at where main window was
    const QPoint global = window->mapToGlobal(window->rect().center());
    shutdownWindow->move(global.x() - shutdownWindow->width() / 2, global.y() - shutdownWindow->height() / 2);
    shutdownWindow->show();
    return shutdownWindow;
}

void ShutdownWindow::closeEvent(QCloseEvent *event)
{
    // Prefer waiting for a clean exit; if shutdown is stuck (e.g. old solo-miner
    // longpoll deadlock), allow the user to dismiss after StartShutdown so Task
    // Manager is not the only escape. Process may still need End Task if hung.
    if (ShutdownRequested()) {
        event->accept();
        return;
    }
    event->ignore();
}
