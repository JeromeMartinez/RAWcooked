/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Main.h"
#include "GUI/Qt/GUI_About.h"
#include "GUI/Qt/GUI_Help.h"
#include "GUI/Qt/GUI_License.h"
#include "GUI/Qt/GUI_Preferences.h"
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QRadioButton>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
//---------------------------------------------------------------------------

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
#if defined(Q_OS_WIN)
    static const Qt::CaseSensitivity Path_Sensitivity = Qt::CaseInsensitive;
#else
    static const Qt::CaseSensitivity Path_Sensitivity = Qt::CaseSensitive;
#endif

//---------------------------------------------------------------------------
static QIcon Menu_Icon(const char* Name)
{
    return QIcon(QStringLiteral(":/Image/Menu/") + QLatin1String(Name));
}

//---------------------------------------------------------------------------
// Output file of an encoding when the user does not choose it, the same as the one of the library
static QString Output_Default(const gui_package& Package)
{
    if (Package.Inputs.isEmpty())
        return QString();
    QString Output = QDir::toNativeSeparators(Package.Inputs.first());
    while (Output.size() > 1 && Output.endsWith(QDir::separator()))
        Output.chop(1);
    return Output + QStringLiteral(".mkv");
}

//---------------------------------------------------------------------------
// Options of the analysis, for knowing if they change (as a text: the comparison of lists of Qt 5 does not build with recent compilers)
static QString Analysis_Options(const GUI_Options& Options)
{
    return Options.Arguments(Operation_Analyze).join(QLatin1Char('\n'));
}

//***************************************************************************
// Menu creation
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::Menu_Create()
{
    //Menu File
    Menu_File_Open_Files = new QAction(Menu_Icon("File_Open_File.svg"), tr("Open file(s)..."), this);
    Menu_File_Open_Files->setShortcut(QKeySequence(tr("Ctrl+O")));
    Menu_File_Open_Files->setStatusTip(tr("Open file(s), the files opened together are one package"));
    connect(Menu_File_Open_Files, &QAction::triggered, this, &GUI_Main::OnMenu_File_Open_Files);

    Menu_File_Open_Directory = new QAction(Menu_Icon("File_Open_Directory.svg"), tr("Open directory..."), this);
    Menu_File_Open_Directory->setShortcut(QKeySequence(tr("Ctrl+D")));
    Menu_File_Open_Directory->setStatusTip(tr("Open a directory, the whole directory is one package"));
    connect(Menu_File_Open_Directory, &QAction::triggered, this, &GUI_Main::OnMenu_File_Open_Directory);

    Menu_File_Close_Files = new QAction(tr("Close"), this);
    Menu_File_Close_Files->setShortcut(QKeySequence(tr("Ctrl+W")));
    Menu_File_Close_Files->setStatusTip(tr("Close the selected packages"));
    connect(Menu_File_Close_Files, &QAction::triggered, this, &GUI_Main::OnMenu_File_Close_Files);

    Menu_File_Close_All = new QAction(tr("Close all"), this);
    Menu_File_Close_All->setStatusTip(tr("Close all packages"));
    connect(Menu_File_Close_All, &QAction::triggered, this, &GUI_Main::OnMenu_File_Close_All);

    Menu_File_Save_All = new QAction(Menu_Icon("File_Save.svg"), tr("Save modified metadata"), this);
    Menu_File_Save_All->setShortcut(QKeySequence(tr("Ctrl+S")));
    Menu_File_Save_All->setStatusTip(tr("Write the modified metadata in the files"));
    Menu_File_Save_All->setIconText(tr("Save")); // Text in the tool bar, the one of the menu has the count of modified fields
    Menu_File_Save_All->setEnabled(false);
    connect(Menu_File_Save_All, &QAction::triggered, this, &GUI_Main::OnMenu_File_Save_All);

    Menu_File_Quit = new QAction(tr("&Quit"), this);
    Menu_File_Quit->setShortcut(QKeySequence(tr("Ctrl+Q")));
    Menu_File_Quit->setStatusTip(tr("Quit the application"));
    Menu_File_Quit->setMenuRole(QAction::QuitRole);
    connect(Menu_File_Quit, &QAction::triggered, this, &GUI_Main::close);

    Menu_File = menuBar()->addMenu(tr("&File"));
    Menu_File->addAction(Menu_File_Open_Files);
    Menu_File->addAction(Menu_File_Open_Directory);
    Menu_File->addSeparator();
    Menu_File->addAction(Menu_File_Close_Files);
    Menu_File->addAction(Menu_File_Close_All);
    Menu_File->addSeparator();
    Menu_File->addAction(Menu_File_Save_All);
    Menu_File->addSeparator();
    Menu_File->addAction(Menu_File_Quit);

    //Menu Actions
    Menu_Actions_Encode = new QAction(Menu_Icon("Action_Encode.svg"), tr("Encode..."), this);
    Menu_Actions_Encode->setShortcut(QKeySequence(tr("Ctrl+E")));
    Menu_Actions_Encode->setStatusTip(tr("Compress the content to a Matroska file (FFV1, FLAC)"));
    connect(Menu_Actions_Encode, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Encode);

    Menu_Actions_Decode = new QAction(Menu_Icon("Action_Decode.svg"), tr("Decode"), this);
    Menu_Actions_Decode->setStatusTip(tr("Restore the original files next to the Matroska file"));
    connect(Menu_Actions_Decode, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Decode);

    Menu_Actions_Decode_To = new QAction(tr("Decode to..."), this);
    Menu_Actions_Decode_To->setStatusTip(tr("Restore the original files in another directory"));
    connect(Menu_Actions_Decode_To, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Decode_To);

    Menu_Actions_Check = new QAction(Menu_Icon("Action_Check.svg"), tr("Check"), this);
    Menu_Actions_Check->setStatusTip(tr("Check that the Matroska file can be decoded to the original files"));
    connect(Menu_Actions_Check, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Check);

    Menu_Actions_Analyze = new QAction(tr("Analyze again"), this);
    Menu_Actions_Analyze->setShortcut(QKeySequence(tr("F5")));
    Menu_Actions_Analyze->setStatusTip(tr("Read again the content of the selected packages"));
    connect(Menu_Actions_Analyze, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Analyze);

    Menu_Actions_Command = new QAction(tr("Show the command of the encoder..."), this);
    Menu_Actions_Command->setStatusTip(tr("FFmpeg command which would be used for the encoding (--display-command)"));
    connect(Menu_Actions_Command, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Command);

    Menu_Actions = menuBar()->addMenu(tr("&Actions"));
    Menu_Actions->addAction(Menu_Actions_Encode);
    Menu_Actions->addAction(Menu_Actions_Decode);
    Menu_Actions->addAction(Menu_Actions_Decode_To);
    Menu_Actions->addAction(Menu_Actions_Check);
    Menu_Actions->addSeparator();
    Menu_Actions->addAction(Menu_Actions_Analyze);
    Menu_Actions->addAction(Menu_Actions_Command);

    //Menu View
    Menu_View_Files = new QAction(Menu_Icon("View_Files.svg"), tr("Files"), this);
    Menu_View_Files->setCheckable(true);
    Menu_View_Files->setStatusTip(tr("Opened packages, with their tracks and attachments"));
    connect(Menu_View_Files, &QAction::triggered, this, &GUI_Main::OnMenu_View_Files);

    Menu_View_Metadata = new QAction(Menu_Icon("View_Metadata.svg"), tr("Metadata"), this);
    Menu_View_Metadata->setCheckable(true);
    Menu_View_Metadata->setStatusTip(tr("Metadata of each track, it can be modified"));
    connect(Menu_View_Metadata, &QAction::triggered, this, &GUI_Main::OnMenu_View_Metadata);

    Menu_View_Log = new QAction(Menu_Icon("View_Log.svg"), tr("Log"), this);
    Menu_View_Log->setCheckable(true);
    Menu_View_Log->setStatusTip(tr("Commands and their messages"));
    connect(Menu_View_Log, &QAction::triggered, this, &GUI_Main::OnMenu_View_Log);

    Menu_View_Group = new QActionGroup(this);
    Menu_View_Group->setExclusive(true);
    Menu_View_Group->addAction(Menu_View_Files);
    Menu_View_Group->addAction(Menu_View_Metadata);
    Menu_View_Group->addAction(Menu_View_Log);
    Menu_View = menuBar()->addMenu(tr("&View"));
    Menu_View->addAction(Menu_View_Files);
    Menu_View->addAction(Menu_View_Metadata);
    Menu_View->addAction(Menu_View_Log);

    //Menu Options
    Menu_Options_Preferences = new QAction(Menu_Icon("Options_Prefs.svg"), tr("Preferences..."), this);
    Menu_Options_Preferences->setStatusTip(tr("Options of the analysis, of the encoding, of the decoding and of the checks"));
    Menu_Options_Preferences->setMenuRole(QAction::PreferencesRole);
    connect(Menu_Options_Preferences, &QAction::triggered, this, &GUI_Main::OnMenu_Options_Preferences);

    Menu_Options_License = new QAction(tr("License..."), this);
    Menu_Options_License->setStatusTip(tr("Status of the license, license key"));
    Menu_Options_License->setMenuRole(QAction::NoRole);
    connect(Menu_Options_License, &QAction::triggered, this, &GUI_Main::OnMenu_Options_License);

    Menu_Options = menuBar()->addMenu(tr("&Options"));
    Menu_Options->addAction(Menu_Options_Preferences);
    Menu_Options->addAction(Menu_Options_License);

    //Menu Help
    Menu_Help_Help = new QAction(Menu_Icon("Help.svg"), tr("Help..."), this);
    Menu_Help_Help->setShortcut(QKeySequence(tr("F1")));
    Menu_Help_Help->setStatusTip(tr("How to use this program, options, metadata fields"));
    connect(Menu_Help_Help, &QAction::triggered, this, &GUI_Main::OnMenu_Help_Help);

    Menu_Help_About = new QAction(Menu_Icon("About.svg"), tr("About..."), this);
    Menu_Help_About->setStatusTip(tr("Version, authors, license, contact"));
    Menu_Help_About->setMenuRole(QAction::AboutRole);
    connect(Menu_Help_About, &QAction::triggered, this, &GUI_Main::OnMenu_Help_About);

    Menu_Help_Website = new QAction(tr("RAWcooked website"), this);
    Menu_Help_Website->setStatusTip(QStringLiteral("https://mediaarea.net/RAWcooked"));
    Menu_Help_Website->setMenuRole(QAction::NoRole);
    connect(Menu_Help_Website, &QAction::triggered, this, &GUI_Main::OnMenu_Help_Website);

    Menu_Help = menuBar()->addMenu(tr("&Help"));
    Menu_Help->addAction(Menu_Help_Help);
    Menu_Help->addAction(Menu_Help_About);
    Menu_Help->addAction(Menu_Help_Website);
}

//***************************************************************************
// Menu actions
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_File_Open_Files()
{
    if (!Action_Begin())
        return;

    //User interaction
    const QStringList FileNames = QFileDialog::getOpenFileNames(this,
                                                                tr("RAWcooked - Open file(s)"),
                                                                Options.Directory_Get(),
                                                                tr("All files (*);;Audio-visual files (*.dpx *.tif *.tiff *.exr *.wav *.aif *.aiff *.avi *.mkv)"));
    if (FileNames.isEmpty())
        return;

    //Showing
    Open(FileNames);
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_File_Open_Directory()
{
    if (!Action_Begin())
        return;

    //User interaction
    const QString Directory = QFileDialog::getExistingDirectory(this,
                                                                tr("RAWcooked - Open directory"),
                                                                Options.Directory_Get(),
                                                                QFileDialog::ShowDirsOnly
                                                              | QFileDialog::DontResolveSymlinks);
    if (Directory.isEmpty())
        return;

    //Showing
    Open(QStringList(Directory));
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_File_Close_Files()
{
    if (!Action_Begin())
        return;

    if (!Close(Selected(false)))
        return;

    //GUI
    View_Refresh();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_File_Close_All()
{
    if (!Action_Begin())
        return;

    std::vector<size_t> All;
    for (size_t Pos = 0; Pos < Packages.size(); Pos++)
        All.push_back(Pos);
    if (!Close(All))
        return;

    //GUI
    View_Refresh();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_File_Save_All()
{
    if (!Action_Begin())
        return;

    //Running
    std::vector<size_t> All;
    for (size_t Pos = 0; Pos < Packages.size(); Pos++)
        All.push_back(Pos);
    Save(All);
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Actions_Encode()
{
    if (!Action_Begin())
        return;

    //User interaction
    std::vector<operation> List;
    if (!Encode_Prepare(operation::Encode, Selection(gui_package::Kind_Uncompressed), List))
        return;

    //Running
    Operations_Begin(tr("encoding"), tr("Encoding finished"));
    for (const operation& Operation : List)
        Operations_Add(Operation);
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Actions_Decode()
{
    if (!Action_Begin())
        return;

    //Running
    Operations_Begin(tr("decoding"), tr("Decoding finished"));
    Operations_Add(operation::Decode, Selection(gui_package::Kind_Compressed));
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Actions_Decode_To()
{
    if (!Action_Begin())
        return;
    const std::vector<size_t> List = Selection(gui_package::Kind_Compressed);
    if (List.empty())
        return;

    //User interaction, once for all the packages
    const QString Directory = QFileDialog::getExistingDirectory(this,
                                                                tr("RAWcooked - Decode to"),
                                                                QFileInfo(Packages[List.front()].Inputs.value(0)).absolutePath(),
                                                                QFileDialog::ShowDirsOnly
                                                              | QFileDialog::DontResolveSymlinks);
    if (Directory.isEmpty())
        return;

    //Running
    Operations_Begin(tr("decoding"), tr("Decoding finished"));
    Operations_Add(operation::Decode, List, QDir::toNativeSeparators(Directory));
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Actions_Check()
{
    if (!Action_Begin())
        return;

    //Running, file by file: the library checks 1 file per run
    Operations_Begin(tr("check"), tr("Check finished"));
    const std::vector<size_t> List = Selection(gui_package::Kind_Compressed);
    for (const size_t Pos : List)
    {
        operation Operation;
        Operation.Kind = operation::Check;
        Operation.Package = Pos;
        const size_t Count = Packages[Pos].IsDirectory ? 1 : (size_t)Packages[Pos].Inputs.size();
        if (Count < 2)
            Operations_Add(Operation);
        else
            for (Operation.Input = 0; Operation.Input < Count; Operation.Input++)
                Operations_Add(Operation);
    }
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Actions_Analyze()
{
    if (!Action_Begin())
        return;

    //User interaction
    const std::vector<size_t> List = Selected(true);
    if (!Modified_Lose(List))
        return;

    //Running
    Operations_Begin(tr("analysis"), tr("Analysis finished"));
    Operations_Add(operation::Analyze, List);
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Actions_Command()
{
    if (!Action_Begin())
        return;
    const std::vector<size_t> Uncompressed = Selection(gui_package::Kind_Uncompressed);
    if (Uncompressed.size() != 1)
        return; // The command is the one of a package

    //User interaction
    std::vector<operation> List;
    if (!Encode_Prepare(operation::Command, Uncompressed, List))
        return;

    //Running, the command is shown at the end
    Operations_Begin(tr("preparation of the command of the encoder"), QString());
    for (const operation& Operation : List)
        Operations_Add(Operation);
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_View_Files()
{
    //Showing
    View_Refresh(View_Files);
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_View_Metadata()
{
    //Showing
    View_Refresh(View_Metadata);
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_View_Log()
{
    //Showing
    View_Refresh(View_Log);
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Options_Preferences()
{
    if (!Action_Begin())
        return;

    //Showing
    const QString Before = Analysis_Options(Options);
    GUI_Preferences Preferences(&Options, this);
    Preferences.exec();

    //The opened content was analyzed with the options as they were
    if (!Packages.empty() && Analysis_Options(Options) != Before)
        Status_Message(tr("The options of the analysis changed: Actions > Analyze again applies them to the opened content"), 10000);
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Options_License()
{
    if (!Action_Begin())
        return; // The dialog runs the library itself

    //Showing
    const QString Before = Analysis_Options(Options);
    GUI_License License(&Options, this);
    License.exec();

    //The opened content was analyzed with the license as it was
    bool IsOutdated = !Packages.empty() && Analysis_Options(Options) != Before; // Other key for this session
    for (const gui_package& Package : Packages)
        for (const gui_track& Track : Package.Tracks)
            if (Track.IsNotLicensed)
                IsOutdated = true; // A key stored meanwhile may support it
    if (IsOutdated)
        Status_Message(tr("After a change of the license: Actions > Analyze again checks the opened content with it"), 10000);
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Help_Help()
{
    //The window which is already open
    if (Help)
    {
        Help->setWindowState(Help->windowState() & ~Qt::WindowMinimized);
        Help->show();
        Help->raise();
        Help->activateWindow();
        return;
    }
    if (IsBusy())
        return; // Its content comes from the library

    //Showing
    GUI_Help* Frame = new GUI_Help(this);
    Frame->setAttribute(Qt::WA_DeleteOnClose);
    Frame->show();
    Help = Frame;
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Help_About()
{
    if (IsBusy())
        return; // Its content comes from the library

    //Showing
    GUI_About About(this);
    About.exec();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Help_Website()
{
    //Showing
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://mediaarea.net/RAWcooked"), QUrl::TolerantMode));
}

//***************************************************************************
// Choices before an encoding
//***************************************************************************

//---------------------------------------------------------------------------
bool GUI_Main::Encode_Prepare(operation::kind Kind, const std::vector<size_t>& List, std::vector<operation>& Result)
{
    if (List.empty())
        return false;

    // The modified metadata is not in the files
    const size_t Modified = Modified_Count(List);
    if (Modified && Message_Ask(QMessageBox::Warning,
                                Modified == 1 ? tr("1 modified field is not saved.") : tr("%1 modified fields are not saved.").arg(Modified),
                                tr("The files are encoded as they are on the disk, without the modifications. Do you want to continue?"),
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return false;

    // Output files. The preferences may answer the questions about the existing ones, as they do for the questions of the library
    const QStringList Options_Encode = Options.Arguments(Operation_Encode);
    const auto Yes_Pos = Options_Encode.lastIndexOf(QStringLiteral("-y"));
    const auto No_Pos = Options_Encode.lastIndexOf(QStringLiteral("-n"));
    const bool AlwaysYes = Kind == operation::Encode && Yes_Pos > No_Pos;
    const bool AlwaysNo = Kind == operation::Encode && No_Pos > Yes_Pos;
    for (const size_t Pos : List)
    {
        operation Operation;
        Operation.Kind = Kind;
        Operation.Package = Pos;
        Operation.Output = Output_Default(Packages[Pos]);
        Result.push_back(Operation);
    }
    if (Result.size() == 1)
    {
        // The dialog asks itself before replacing a file, unless the preferences have the answer
        const gui_package& Package = Packages[Result.front().Package];
        const QString FileName = QFileDialog::getSaveFileName(this,
                                                              Kind == operation::Command ? tr("RAWcooked - Command of the encoder, output file") : tr("RAWcooked - Encode to"),
                                                              Result.front().Output,
                                                              tr("Matroska files (*.mkv);;All files (*)"),
                                                              nullptr,
                                                              AlwaysYes || AlwaysNo ? QFileDialog::DontConfirmOverwrite : QFileDialog::Options());
        if (FileName.isEmpty())
            return false;
        Result.front().Output = QDir::toNativeSeparators(FileName);
        if (Package.Inputs.contains(Result.front().Output, Path_Sensitivity))
        {
            Message_Ask(QMessageBox::Warning, tr("The output file can not be one of the files to encode."), QString(), QMessageBox::Ok, QMessageBox::Ok);
            return false;
        }
        if (AlwaysNo && QFileInfo::exists(Result.front().Output))
        {
            // The encoder would refuse it, with messages of its own
            Message_Ask(QMessageBox::Warning, tr("The output file already exists."), tr("It is not replaced: the preferences answer no to all questions (-n)."), QMessageBox::Ok, QMessageBox::Ok);
            return false;
        }
    }
    else
    {
        size_t Existing = 0;
        for (const operation& Operation : Result)
            if (QFileInfo::exists(Operation.Output))
                Existing++;
        if (Existing && !AlwaysYes)
        {
            QMessageBox::StandardButton Answer;
            if (AlwaysNo)
                Answer = Message_Ask(QMessageBox::Warning,
                                     Existing == 1 ? tr("1 output file already exists.") : tr("%1 output files already exist.").arg(Existing),
                                     tr("The packages whose output file exists are not encoded: the preferences answer no to all questions (-n)."),
                                     QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) == QMessageBox::Ok ? QMessageBox::No : QMessageBox::Cancel;
            else
                Answer = Message_Ask(QMessageBox::Warning,
                                     Existing == 1 ? tr("1 output file already exists. Overwrite it?") : tr("%1 output files already exist. Overwrite them?").arg(Existing),
                                     tr("No: the packages whose output file exists are not encoded."),
                                     QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
            switch (Answer)
            {
                case QMessageBox::Yes   : break;
                case QMessageBox::No    : Result.erase(std::remove_if(Result.begin(), Result.end(), [](const operation& Operation) { return QFileInfo::exists(Operation.Output); }), Result.end());
                                          if (Result.empty())
                                              return false;
                                          break;
                default                 : return false;
            }
        }
    }
    for (operation& Operation : Result)
        Operation.Output_Existed = QFileInfo::exists(Operation.Output);

    // Padding bits: the library refuses to encode without a choice
    const auto HasPadding = [this](const operation& Operation)
    {
        for (const gui_track& Track : Packages[Operation.Package].Tracks)
            if (Track.MayHavePaddingBits)
                return true;
        return false;
    };
    if (std::any_of(Result.begin(), Result.end(), HasPadding))
    {
        const QStringList Arguments = Options.Arguments(Operation_Encode);
        if (!Arguments.contains(QStringLiteral("--check-padding"))
         && !Arguments.contains(QStringLiteral("--no-check-padding"))
         && !Arguments.contains(QStringLiteral("--check"))
         && !Arguments.contains(QStringLiteral("--all")))
        {
            // The library has the full check after encoding only for a package with 1 input
            bool CanCheck = true;
            for (const operation& Operation : Result)
                if (HasPadding(Operation) && Packages[Operation.Package].Inputs.size() != 1)
                    CanCheck = false;

            QStringList Choice;
            if (!Padding_Ask(Choice, CanCheck))
                return false;
            for (operation& Operation : Result)
                if (HasPadding(Operation))
                    Operation.MoreArguments << Choice;
        }
    }

    return true;
}

//---------------------------------------------------------------------------
bool GUI_Main::Padding_Ask(QStringList& Arguments, bool CanCheck)
{
    QDialog Dialog(this);
    Dialog.setWindowTitle(tr("RAWcooked - Padding bits"));
    Dialog.setWindowIcon(windowIcon());
    Dialog.setModal(true);

    QVBoxLayout* Layout = new QVBoxLayout(&Dialog);

    // Message
    QLabel* MessageLabel = new QLabel(tr("The content has DPX files with padding bits (bits of the file which are not part of the image). "
                                         "They are nearly always zero, and they are restored by the decoding only if the encoding knows the ones which are not zero. "
                                         "How do you want to handle them?"), &Dialog);
    MessageLabel->setWordWrap(true);
    Layout->addWidget(MessageLabel);

    // Choices, each one with its explanation
    const int Indent = Dialog.style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, nullptr, &Dialog)
                     + Dialog.style()->pixelMetric(QStyle::PM_RadioButtonLabelSpacing, nullptr, &Dialog);
    const auto Choice_Add = [&](const QString& Text, const QString& Explanation, bool IsEnabled = true)
    {
        QRadioButton* Button = new QRadioButton(Text, &Dialog);
        Button->setEnabled(IsEnabled);
        Layout->addSpacing(Layout->spacing());
        Layout->addWidget(Button);
        QLabel* ExplanationLabel = new QLabel(Explanation, &Dialog);
        ExplanationLabel->setWordWrap(true);
        ExplanationLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        ExplanationLabel->setContentsMargins(Indent, 0, 0, 0);
        ExplanationLabel->setEnabled(IsEnabled);
        Layout->addWidget(ExplanationLabel);
        return Button;
    };
    QRadioButton* CheckPadding = Choice_Add(tr("Check all files before encoding (--check-padding)"),
                                            tr("Slower, all the files are read before the encoding. The decoding restores the files in all cases."));
    QRadioButton* Check = Choice_Add(tr("Check after encoding, with the full check of reversibility (--check)"),
                                     CanCheck ? tr("The encoded file is compared to the files. If padding bits are not zero the check fails, and the encoding has to be done again with the first choice.")
                                              : tr("Not available: this check is only for a directory or a single file."),
                                     CanCheck);
    QRadioButton* NoCheckPadding = Choice_Add(tr("Do not check (--no-check-padding)"),
                                              tr("Quicker, but the decoding does not restore exactly the files whose padding bits are not zero."));
    CheckPadding->setChecked(true);

    // Remember
    QCheckBox* Remember = new QCheckBox(tr("Remember (preferences)"), &Dialog);
    Layout->addSpacing(Layout->spacing());
    Layout->addWidget(Remember);

    // Buttons
    QDialogButtonBox* Buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &Dialog);
    connect(Buttons, &QDialogButtonBox::accepted, &Dialog, &QDialog::accept);
    connect(Buttons, &QDialogButtonBox::rejected, &Dialog, &QDialog::reject);
    Layout->addWidget(Buttons);

    // Size of the dialog: wide enough for the wrapped texts, and as high as they need at this width (the layout would spread a height in excess between them)
    const int Width = Dialog.fontMetrics().averageCharWidth() * 100;
    Dialog.setMinimumWidth(Width);
    if (Layout->hasHeightForWidth())
        Dialog.resize(Width, Layout->heightForWidth(Width));

    if (Dialog.exec() != QDialog::Accepted)
        return false;

    // Choice, for this encoding or for all of them
    const QString Option = Check->isChecked() ? QStringLiteral("Check") : QStringLiteral("CheckPadding");
    const QString Value = Check->isChecked() ? QStringLiteral("--check") : (NoCheckPadding->isChecked() ? QStringLiteral("--no-check-padding") : QStringLiteral("--check-padding"));
    Arguments.clear();
    if (Remember->isChecked())
    {
        Options.Value_Set(Option, Value);
        Options.Save();
    }
    else
        Arguments << Value;
    return true;
}

//***************************************************************************
// ToolBar creation
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::ToolBar_Create()
{
    ToolBar = new QToolBar(tr("ToolBar"));
    ToolBar->setIconSize(QSize(32, 32));
    ToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    ToolBar->addAction(Menu_File_Open_Files);
    ToolBar->addAction(Menu_File_Open_Directory);
    ToolBar->addAction(Menu_File_Save_All);
    ToolBar->addSeparator();
    ToolBar->addAction(Menu_Actions_Encode);
    ToolBar->addAction(Menu_Actions_Decode);
    ToolBar->addAction(Menu_Actions_Check);
    ToolBar->addSeparator();
    ToolBar->addAction(Menu_View_Files);
    ToolBar->addAction(Menu_View_Metadata);
    ToolBar->addAction(Menu_View_Log);
    ToolBar->addSeparator();
    ToolBar->addAction(Menu_Options_Preferences);
    ToolBar->addSeparator();
    ToolBar->addAction(Menu_Help_Help);
    ToolBar->addAction(Menu_Help_About);

    // Same width for all the buttons, their texts have different lengths
    int Width = 0;
    const QList<QAction*> Actions = ToolBar->actions();
    for (QAction* Action : Actions)
        if (!Action->isSeparator())
            if (const QWidget* Button = ToolBar->widgetForAction(Action))
                Width = qMax(Width, Button->sizeHint().width());
    for (QAction* Action : Actions)
        if (!Action->isSeparator())
            if (QWidget* Button = ToolBar->widgetForAction(Action))
                Button->setMinimumWidth(Width);

    #ifndef __APPLE__
        addToolBar(Qt::LeftToolBarArea, ToolBar);
    #else //__APPLE__
        addToolBar(ToolBar);
    #endif //__APPLE__
}
