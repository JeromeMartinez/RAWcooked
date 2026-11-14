/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_License.h"
#include "GUI/Qt/GUI_Job.h"
#include "GUI/Qt/GUI_Options.h"
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Configuration file of the library, as the help of the command line names it
#if defined(_WIN32) || defined(_WINDOWS)
    static const char* const Config_FileName = "%APPDATA%\\RAWcooked\\Config.txt";
#else
    static const char* const Config_FileName = "~/.config/RAWcooked/Config.txt";
#endif

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
// Constructor
GUI_License::GUI_License(GUI_Options* Options_, QWidget* Parent)
: QDialog(Parent), Options(Options_)
{
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
    setWindowTitle(tr("RAWcooked - License"));
    setWindowIcon(QIcon(":/Image/Icon.png"));
    setModal(true); // No operation is started while a job of the dialog runs, however the dialog is shown

    // Status
    Status = new QPlainTextEdit();
    Status->setObjectName(QStringLiteral("Status"));
    Status->setReadOnly(true);
    Status->setLineWrapMode(QPlainTextEdit::NoWrap);
    Status->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); // The lists of the library are aligned with spaces
    Status->setMinimumHeight(Status->fontMetrics().lineSpacing() * 8);
    Message = new QLabel();
    Message->setObjectName(QStringLiteral("Message"));
    Message->setTextFormat(Qt::PlainText);
    Message->setWordWrap(true);

    QVBoxLayout* Status_Layout = new QVBoxLayout();
    Status_Layout->addWidget(Status);
    Status_Layout->addWidget(Message);
    QGroupBox* Status_Box = new QGroupBox(tr("Status"));
    Status_Box->setLayout(Status_Layout);

    // License key
    Key = new QLineEdit();
    Key->setObjectName(QStringLiteral("Key"));
    Key->setEchoMode(QLineEdit::Password);
    Key->setText(Options->LicenseKey);
    connect(Key, &QLineEdit::textChanged, this, &GUI_License::OnKey_Changed);
    Key_Show = new QCheckBox(tr("Show"));
    Key_Show->setObjectName(QStringLiteral("Key_Show"));
    connect(Key_Show, &QCheckBox::toggled, this, &GUI_License::OnKey_Show);
    Key_Use = new QPushButton(tr("Use for this session"));
    Key_Use->setObjectName(QStringLiteral("Key_Use"));
    Key_Use->setToolTip(tr("The key is used until the program is closed, it is not stored. Without key, the stored key is used again."));
    connect(Key_Use, &QPushButton::clicked, this, &GUI_License::OnKey_Use);
    Key_Store = new QPushButton(tr("Store"));
    Key_Store->setObjectName(QStringLiteral("Key_Store"));
    Key_Store->setToolTip(tr("The key is stored in the configuration file of RAWcooked and used from now on."));
    connect(Key_Store, &QPushButton::clicked, this, &GUI_License::OnKey_Store);
    QLabel* Key_Help = new QLabel(tr("A key used for this session is not stored anywhere: it is used until the program is closed. "
                                     "A stored key is in the configuration file of RAWcooked (%1), the same as the command line program uses.").arg(QLatin1String(Config_FileName)));
    Key_Help->setTextFormat(Qt::PlainText);
    Key_Help->setWordWrap(true);

    QHBoxLayout* Key_Buttons = new QHBoxLayout();
    Key_Buttons->addWidget(Key_Use);
    Key_Buttons->addWidget(Key_Store);
    Key_Buttons->addStretch();
    QGridLayout* Key_Layout = new QGridLayout();
    Key_Layout->addWidget(Key, 0, 0);
    Key_Layout->addWidget(Key_Show, 0, 1);
    Key_Layout->addLayout(Key_Buttons, 1, 0, 1, 2);
    Key_Layout->addWidget(Key_Help, 2, 0, 1, 2);
    QGroupBox* Key_Box = new QGroupBox(tr("License key"));
    Key_Box->setLayout(Key_Layout);

    // Sub-license
    SubLicense_Id = new QSpinBox();
    SubLicense_Id->setObjectName(QStringLiteral("SubLicense_Id"));
    SubLicense_Id->setRange(1, 126);
    SubLicense_Id->setToolTip(tr("ID of the sub-licensee the key is created for, from 1 to 126."));
    SubLicense_Dur = new QSpinBox();
    SubLicense_Dur->setObjectName(QStringLiteral("SubLicense_Dur"));
    SubLicense_Dur->setRange(0, 12);
    SubLicense_Dur->setValue(1);
    SubLicense_Dur->setToolTip(tr("Duration of the sub-license, in months. The end date is the last day of the month."));
    SubLicense_Create = new QPushButton(tr("Create sub-license key"));
    SubLicense_Create->setObjectName(QStringLiteral("SubLicense_Create"));
    connect(SubLicense_Create, &QPushButton::clicked, this, &GUI_License::OnSubLicense_Create);
    SubLicense_Key = new QLineEdit();
    SubLicense_Key->setObjectName(QStringLiteral("SubLicense_Key"));
    SubLicense_Key->setReadOnly(true);
    SubLicense_Key->setPlaceholderText(tr("No sub-license key created"));
    SubLicense_Copy = new QPushButton(tr("Copy"));
    SubLicense_Copy->setObjectName(QStringLiteral("SubLicense_Copy"));
    connect(SubLicense_Copy, &QPushButton::clicked, this, &GUI_License::OnSubLicense_Copy);
    QLabel* SubLicense_Id_Label = new QLabel(tr("ID:"));
    SubLicense_Id_Label->setBuddy(SubLicense_Id);
    QLabel* SubLicense_Dur_Label = new QLabel(tr("Duration in months:"));
    SubLicense_Dur_Label->setBuddy(SubLicense_Dur);

    QHBoxLayout* SubLicense_Values = new QHBoxLayout();
    SubLicense_Values->addWidget(SubLicense_Id_Label);
    SubLicense_Values->addWidget(SubLicense_Id);
    SubLicense_Values->addSpacing(12);
    SubLicense_Values->addWidget(SubLicense_Dur_Label);
    SubLicense_Values->addWidget(SubLicense_Dur);
    SubLicense_Values->addSpacing(12);
    SubLicense_Values->addWidget(SubLicense_Create);
    SubLicense_Values->addStretch();
    QGridLayout* SubLicense_Layout = new QGridLayout();
    SubLicense_Layout->addLayout(SubLicense_Values, 0, 0, 1, 2);
    SubLicense_Layout->addWidget(SubLicense_Key, 1, 0);
    SubLicense_Layout->addWidget(SubLicense_Copy, 1, 1);
    QGroupBox* SubLicense_Box = new QGroupBox(tr("Sub-license"));
    SubLicense_Box->setLayout(SubLicense_Layout);

    // Dialog
    QDialogButtonBox* Dialog = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(Dialog, &QDialogButtonBox::rejected, this, &GUI_License::reject);

    QVBoxLayout* L = new QVBoxLayout();
    L->addWidget(Status_Box, 1);
    L->addWidget(Key_Box);
    L->addWidget(SubLicense_Box);
    L->addWidget(Dialog);
    setLayout(L);

    // The Return key in the field of the key neither chooses between the 2 uses of the key nor closes the dialog
    const QList<QPushButton*> Buttons = findChildren<QPushButton*>();
    for (QPushButton* Button : Buttons)
        Button->setAutoDefault(false);

    QSize Size(680, 640);
    const QScreen* Screen = Parent ? Parent->screen() : QGuiApplication::primaryScreen();
    if (Screen)
        Size = Size.boundedTo(Screen->availableGeometry().size() - QSize(40, 80)); // Small screen
    resize(Size);
    Key->setFocus();
    Buttons_Update();
}

//***************************************************************************
// Actions
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_License::OnStatus_Refresh()
{
    if (IsBusy)
        return;

    Status_Read(Options->LicenseKey);
    Message->setText(Options->LicenseKey.isEmpty() ? QString() : tr("A license key is used for this session, it is not stored."));
}

//---------------------------------------------------------------------------
void GUI_License::OnKey_Changed()
{
    Buttons_Update();
}

//---------------------------------------------------------------------------
void GUI_License::OnKey_Show(bool Checked)
{
    Key->setEchoMode(Checked ? QLineEdit::Normal : QLineEdit::Password);
}

//---------------------------------------------------------------------------
void GUI_License::OnKey_Use()
{
    if (IsBusy)
        return;

    // The status with the key tells if the key is fine: only then it is used by the operations
    const QString NewKey = Key_Get();
    if (Status_Read(NewKey))
    {
        Options->LicenseKey = NewKey;
        Message->setText(NewKey.isEmpty() ? tr("No license key for this session any more.") : tr("This license key is used for this session, it is not stored."));
    }
    else
        Message->setText(tr("This license key is not accepted (see above), the license in use is not changed."));
    Buttons_Update();
}

//---------------------------------------------------------------------------
void GUI_License::OnKey_Store()
{
    if (IsBusy)
        return;
    const QString NewKey = Key_Get();
    if (NewKey.isEmpty())
        return;

    QString Out, Err;
    if (!Run(QStringList() << QStringLiteral("--store-license") << NewKey << QStringLiteral("--show-license"), Out, Err))
    {
        Status_Show(Err);
        Message->setText(tr("The license key is not stored (see above)."));
        return;
    }

    // The stored key is the one of any run from now on: no key of the session, and the status as a run without key has it
    Options->LicenseKey.clear();
    Key->clear();
    Status_Read(QString());
    Message->setText(tr("The license key is stored."));
    Buttons_Update();
}

//---------------------------------------------------------------------------
void GUI_License::OnSubLicense_Create()
{
    if (IsBusy)
        return;

    // From the license in use, so with the key of the session if any
    QStringList Arguments;
    Arguments << QStringLiteral("--sublicense") << QString::number(SubLicense_Id->value()) << QStringLiteral("--sublicense-dur") << QString::number(SubLicense_Dur->value());
    if (!Options->LicenseKey.isEmpty())
        Arguments << QStringLiteral("--license") << Options->LicenseKey;
    QString Out, Err;
    const bool IsOK = Run(Arguments, Out, Err);

    // The key is the only thing the library writes on its standard output
    const QString NewKey = Out.trimmed();
    if (IsOK && !NewKey.isEmpty())
    {
        SubLicense_Key->setText(NewKey);
        SubLicense_Key->setCursorPosition(0);
        Message->setText(tr("Sub-license key created for the sub-licensee %1.").arg(SubLicense_Id->value()));
    }
    else
    {
        SubLicense_Key->clear();
        Status_Show(Err);
        Message->setText(tr("No sub-license key is created (see above)."));
    }
    Buttons_Update();
}

//---------------------------------------------------------------------------
void GUI_License::OnSubLicense_Copy()
{
    if (!SubLicense_Key->text().isEmpty())
        QGuiApplication::clipboard()->setText(SubLicense_Key->text());
}

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_License::Buttons_Update()
{
    const QString NewKey = Key_Get();
    Key_Use->setEnabled(NewKey != Options->LicenseKey); // Also without key, for no more key of the session
    Key_Store->setEnabled(!NewKey.isEmpty());
    SubLicense_Copy->setEnabled(!SubLicense_Key->text().isEmpty());
}

//---------------------------------------------------------------------------
bool GUI_License::Run(const QStringList& Arguments, QString& Out, QString& Err)
{
    // The events are handled while the job runs (a fraction of a second), the actions of the user are then ignored
    IsBusy = true;
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);

    GUI_Job Job;
    Job.Arguments = Arguments;
    QEventLoop Loop;
    connect(&Job, &GUI_Job::finished, &Loop, &QEventLoop::quit);
    Job.start();
    if (Job.isRunning() || Job.isFinished()) // Else the thread could not be started, nothing would end the wait
        Loop.exec();

    QGuiApplication::restoreOverrideCursor();
    IsBusy = false;

    // The wait may end without the job, e.g. when the program is closed: its results are not ready
    if (!Job.isFinished())
    {
        Out.clear();
        Err.clear();
        return false;
    }

    // Nothing of a job is kept elsewhere: its texts may contain a key
    Out = Job.Out;
    Err = Job.Err;
    return Job.ExitCode == 0;
}

//---------------------------------------------------------------------------
bool GUI_License::Status_Read(const QString& LicenseKey)
{
    QStringList Arguments;
    Arguments << QStringLiteral("--show-license");
    if (!LicenseKey.isEmpty())
        Arguments << QStringLiteral("--license") << LicenseKey;
    QString Out, Err;
    const bool IsOK = Run(Arguments, Out, Err);
    Status_Show(Err);
    return IsOK;
}

//---------------------------------------------------------------------------
void GUI_License::Status_Show(const QString& Text)
{
    // The messages of the library end with the list of the tracks, which is empty here
    int Begin = 0;
    int End = Text.size();
    while (End && Text[End - 1].isSpace())
        End--;
    while (Begin < End && (Text[Begin] == QLatin1Char('\n') || Text[Begin] == QLatin1Char('\r')))
        Begin++;

    if (Begin == End)
        Status->setPlainText(tr("No message about the license."));
    else
        Status->setPlainText(Text.mid(Begin, End - Begin));
}

//---------------------------------------------------------------------------
QString GUI_License::Key_Get() const
{
    return Key->text().simplified().remove(QLatin1Char(' ')); // A key has no space
}

//***************************************************************************
// Events
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_License::showEvent(QShowEvent* Event)
{
    QDialog::showEvent(Event);

    // Once the dialog is visible, the job needs the event loop
    if (!IsStatusRead)
    {
        IsStatusRead = true;
        QTimer::singleShot(0, this, &GUI_License::OnStatus_Refresh);
    }
}

//---------------------------------------------------------------------------
void GUI_License::done(int Result)
{
    // The job which is running uses the dialog at its end
    if (IsBusy)
        return;

    QDialog::done(Result);
}
