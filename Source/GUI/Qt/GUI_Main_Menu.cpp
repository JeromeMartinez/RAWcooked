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
#include "GUI/Qt/GUI_Main_Metadata.h"
#include "GUI/Qt/GUI_Preferences.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QStatusTipEvent>
#include <QStyle>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
//---------------------------------------------------------------------------

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
static const char* const Website = "https://mediaarea.net/RAWcooked";

//---------------------------------------------------------------------------
// Count of lines the list of the output files shows without scrolling
static const int Outputs_Lines_Max = 16;

//---------------------------------------------------------------------------
static QIcon Menu_Icon(const char* Name)
{
    return QIcon(QStringLiteral(":/Image/Menu/") + QLatin1String(Name));
}

//---------------------------------------------------------------------------
// Text of an action without what is for its menu: the mark of its key and the ellipsis
static QString Menu_Name(const QAction* Action)
{
    QString Text = Action->text();
    if (Text.endsWith(QLatin1String("...")))
        Text.chop(3);
    QString Name;
    for (int Pos = 0; Pos < Text.size(); Pos++)
    {
        if (Text.at(Pos) == QLatin1Char('&') && ++Pos >= Text.size())
            break;
        Name += Text.at(Pos);
    }
    return Name;
}

//---------------------------------------------------------------------------
// Root of a volume, also of a network share: a file next to it can not have its name
static bool Path_IsRoot(const QString& Path)
{
    const QString Clean = QDir::cleanPath(QDir::fromNativeSeparators(Path));
    if (QDir(Clean).isRoot())
        return true;
    #if defined(Q_OS_WIN)
        // Share of a server (//server/share)
        if (Clean.startsWith(QLatin1String("//")) && Clean.count(QLatin1Char('/')) == 3)
            return true;
    #endif
    return false;
}

//---------------------------------------------------------------------------
// Output file of an encoding when the user does not choose it, the same as the one of the library. No name for the root of a volume
static QString Output_Default(const gui_package& Package)
{
    if (Package.Inputs.isEmpty() || Path_IsRoot(Package.Inputs.first()))
        return QString();
    QString Output = QDir::toNativeSeparators(Package.Inputs.first());
    while (Output.size() > 1 && Output.endsWith(QDir::separator()))
        Output.chop(1);
    return Output + QStringLiteral(".mkv");
}

//---------------------------------------------------------------------------
// A name of file may be the one of a file of a sequence, whatever its number
static bool Template_Match(const QString& Template, const QString& Name)
{
    QString Pattern;
    for (int Pos = 0; Pos < Template.size(); Pos++)
    {
        if (Template.at(Pos) == QLatin1Char('%'))
        {
            // %d, %06d: the number of the file. %%: the character
            int End = Pos + 1;
            while (End < Template.size() && Template.at(End).isDigit())
                End++;
            if (End < Template.size() && Template.at(End) == QLatin1Char('d'))
            {
                const int Digits = Template.mid(Pos + 1, End - Pos - 1).toInt();
                Pattern += Digits ? QStringLiteral("\\d{%1,}").arg(Digits) : QStringLiteral("\\d+");
                Pos = End;
                continue;
            }
            if (End == Pos + 1 && End < Template.size() && Template.at(End) == QLatin1Char('%'))
                Pos++;
        }
        Pattern += QRegularExpression::escape(QString(Template.at(Pos)));
    }
    #if defined(Q_OS_WIN)
        const QRegularExpression::PatternOptions Options = QRegularExpression::CaseInsensitiveOption;
    #else
        const QRegularExpression::PatternOptions Options = QRegularExpression::NoPatternOption;
    #endif
    return QRegularExpression(QRegularExpression::anchoredPattern(Pattern), Options).match(Name).hasMatch();
}

//---------------------------------------------------------------------------
// Options of the analysis as one text, for knowing if they change
static QString Analysis_Options(const GUI_Options& Options)
{
    return Options.Arguments(Operation_Analyze).join(QLatin1Char('\n'));
}

//***************************************************************************
// Status tips of the menus
//***************************************************************************

//---------------------------------------------------------------------------
namespace
{

// The status tip of an item which is disabled says why it is, and some styles do not show the status tip of such an item:
// it is shown when the pointer is over the item
class menu_tips : public QObject
{
public:
    explicit menu_tips(QWidget* Window_) : QObject(Window_), Window(Window_) {}

protected:
    bool eventFilter(QObject* Object, QEvent* Event) override
    {
        QMenu* Menu = qobject_cast<QMenu*>(Object);
        if (Menu && Event->type() == QEvent::MouseMove)
        {
            #if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                const QPoint Position = static_cast<QMouseEvent*>(Event)->position().toPoint();
            #else
                const QPoint Position = static_cast<QMouseEvent*>(Event)->pos();
            #endif
            QAction* Action = Menu->actionAt(Position);
            const bool IsDisabled = Action && !Action->isSeparator() && !Action->isEnabled();
            if (IsDisabled)
                Action->showStatusText(Window);
            else if (!Action || Action->isSeparator())
                Clear(); // An item which is enabled has its status tip from the menu
            IsShown = IsDisabled;
        }
        else if (Menu && (Event->type() == QEvent::Leave || Event->type() == QEvent::Hide))
            Clear();
        return false;
    }

private:
    QWidget*                    Window;
    bool                        IsShown = false;

    void Clear()
    {
        if (!IsShown)
            return;
        IsShown = false;
        QStatusTipEvent Tip((QString()));
        QCoreApplication::sendEvent(Window, &Tip);
    }
};

}

//***************************************************************************
// Menu creation
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::Menu_Create()
{
    //Menu File
    Menu_File_Open_Files = new QAction(Menu_Icon("File_Open_File.svg"), tr("&Open file(s)..."), this);
    Menu_File_Open_Files->setShortcut(QKeySequence(tr("Ctrl+O")));
    Menu_File_Open_Files->setStatusTip(tr("Open file(s): the files opened together are one package"));
    connect(Menu_File_Open_Files, &QAction::triggered, this, &GUI_Main::OnMenu_File_Open_Files);

    Menu_File_Open_Directory = new QAction(Menu_Icon("File_Open_Directory.svg"), tr("Open &directory..."), this);
    Menu_File_Open_Directory->setShortcut(QKeySequence(tr("Ctrl+D")));
    Menu_File_Open_Directory->setStatusTip(tr("Open a directory: the whole directory is one package"));
    connect(Menu_File_Open_Directory, &QAction::triggered, this, &GUI_Main::OnMenu_File_Open_Directory);

    Menu_File_Close_Files = new QAction(tr("&Close"), this);
    Menu_File_Close_Files->setShortcut(QKeySequence(tr("Ctrl+W")));
    Menu_File_Close_Files->setStatusTip(tr("Close the selected packages"));
    connect(Menu_File_Close_Files, &QAction::triggered, this, &GUI_Main::OnMenu_File_Close_Files);

    Menu_File_Close_All = new QAction(tr("Close &all"), this);
    Menu_File_Close_All->setStatusTip(tr("Close all packages"));
    connect(Menu_File_Close_All, &QAction::triggered, this, &GUI_Main::OnMenu_File_Close_All);

    Menu_File_Save_All = new QAction(Menu_Icon("File_Save.svg"), tr("&Save modified metadata"), this);
    Menu_File_Save_All->setShortcut(QKeySequence(tr("Ctrl+S")));
    Menu_File_Save_All->setStatusTip(tr("Write the modified metadata to the files, in place: it can not be undone"));
    Menu_File_Save_All->setIconText(tr("Save")); // The text of the menu has the count of modified fields
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
    Menu_Actions_Encode = new QAction(Menu_Icon("Action_Encode.svg"), tr("&Encode..."), this);
    Menu_Actions_Encode->setShortcut(QKeySequence(tr("Ctrl+E")));
    Menu_Actions_Encode->setStatusTip(tr("Encode the selected packages to Matroska files (FFV1, FLAC)"));
    connect(Menu_Actions_Encode, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Encode);

    Menu_Actions_Decode = new QAction(Menu_Icon("Action_Decode.svg"), tr("&Decode"), this);
    Menu_Actions_Decode->setStatusTip(tr("Restore the original files next to the Matroska file"));
    connect(Menu_Actions_Decode, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Decode);

    Menu_Actions_Decode_To = new QAction(tr("Decode &to..."), this);
    Menu_Actions_Decode_To->setStatusTip(tr("Restore the original files in another directory"));
    connect(Menu_Actions_Decode_To, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Decode_To);

    Menu_Actions_Check = new QAction(Menu_Icon("Action_Check.svg"), tr("&Check"), this);
    Menu_Actions_Check->setStatusTip(tr("Check that the Matroska file can be decoded to the original files"));
    connect(Menu_Actions_Check, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Check);

    Menu_Actions_Check_Originals = new QAction(tr("Check against the &original files..."), this);
    Menu_Actions_Check_Originals->setStatusTip(tr("Check that the Matroska file decodes to the original files: choose the directory which holds their directory"));
    connect(Menu_Actions_Check_Originals, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Check_Originals);

    Menu_Actions_Analyze = new QAction(tr("&Analyze again"), this);
    Menu_Actions_Analyze->setShortcut(QKeySequence(tr("F5")));
    Menu_Actions_Analyze->setStatusTip(tr("Read the selected packages again"));
    connect(Menu_Actions_Analyze, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Analyze);

    Menu_Actions_Command = new QAction(tr("&Show the FFmpeg command..."), this);
    Menu_Actions_Command->setStatusTip(tr("FFmpeg command which would be used for the encoding (--display-command)"));
    connect(Menu_Actions_Command, &QAction::triggered, this, &GUI_Main::OnMenu_Actions_Command);

    Menu_Actions = menuBar()->addMenu(tr("&Actions"));
    Menu_Actions->addAction(Menu_Actions_Encode);
    Menu_Actions->addAction(Menu_Actions_Decode);
    Menu_Actions->addAction(Menu_Actions_Decode_To);
    Menu_Actions->addAction(Menu_Actions_Check);
    Menu_Actions->addAction(Menu_Actions_Check_Originals);
    Menu_Actions->addSeparator();
    Menu_Actions->addAction(Menu_Actions_Analyze);
    Menu_Actions->addAction(Menu_Actions_Command);

    //Menu View
    Menu_View_Files = new QAction(Menu_Icon("View_Files.svg"), tr("&Files"), this);
    Menu_View_Files->setCheckable(true);
    Menu_View_Files->setShortcut(QKeySequence(tr("Ctrl+1")));
    Menu_View_Files->setStatusTip(tr("Open packages, with their tracks and attachments"));
    connect(Menu_View_Files, &QAction::triggered, this, &GUI_Main::OnMenu_View_Files);

    Menu_View_Metadata = new QAction(Menu_Icon("View_Metadata.svg"), tr("&Metadata"), this);
    Menu_View_Metadata->setCheckable(true);
    Menu_View_Metadata->setShortcut(QKeySequence(tr("Ctrl+2")));
    Menu_View_Metadata->setStatusTip(tr("Metadata of each track (editable)"));
    connect(Menu_View_Metadata, &QAction::triggered, this, &GUI_Main::OnMenu_View_Metadata);

    Menu_View_Log = new QAction(Menu_Icon("View_Log.svg"), tr("&Log"), this);
    Menu_View_Log->setCheckable(true);
    Menu_View_Log->setShortcut(QKeySequence(tr("Ctrl+3")));
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
    Menu_Options_Preferences = new QAction(Menu_Icon("Options_Prefs.svg"), tr("&Preferences..."), this);
    Menu_Options_Preferences->setStatusTip(tr("Analysis, encoding, decoding and check options"));
    Menu_Options_Preferences->setMenuRole(QAction::PreferencesRole);
    connect(Menu_Options_Preferences, &QAction::triggered, this, &GUI_Main::OnMenu_Options_Preferences);

    Menu_Options_License = new QAction(tr("&License..."), this);
    Menu_Options_License->setStatusTip(tr("Status of the license, license key"));
    Menu_Options_License->setMenuRole(QAction::NoRole);
    connect(Menu_Options_License, &QAction::triggered, this, &GUI_Main::OnMenu_Options_License);

    Menu_Options = menuBar()->addMenu(tr("&Options"));
    Menu_Options->addAction(Menu_Options_Preferences);
    Menu_Options->addAction(Menu_Options_License);

    //Menu Help
    Menu_Help_Help = new QAction(Menu_Icon("Help.svg"), tr("&Help"), this);
    Menu_Help_Help->setShortcut(QKeySequence(tr("F1")));
    Menu_Help_Help->setStatusTip(tr("How to use this program, options, metadata fields"));
    connect(Menu_Help_Help, &QAction::triggered, this, &GUI_Main::OnMenu_Help_Help);

    Menu_Help_About = new QAction(Menu_Icon("About.svg"), tr("&About"), this);
    Menu_Help_About->setStatusTip(tr("Version, authors, license, contact"));
    Menu_Help_About->setMenuRole(QAction::AboutRole);
    connect(Menu_Help_About, &QAction::triggered, this, &GUI_Main::OnMenu_Help_About);

    Menu_Help_Website = new QAction(tr("RAWcooked &website"), this);
    Menu_Help_Website->setStatusTip(QLatin1String(Website));
    Menu_Help_Website->setMenuRole(QAction::NoRole);
    connect(Menu_Help_Website, &QAction::triggered, this, &GUI_Main::OnMenu_Help_Website);

    Menu_Help = menuBar()->addMenu(tr("&Help"));
    Menu_Help->addAction(Menu_Help_Help);
    Menu_Help->addAction(Menu_Help_About);
    Menu_Help->addAction(Menu_Help_Website);

    //Status tips of the disabled items
    QObject* Tips = new menu_tips(this);
    for (QMenu* Menu : { Menu_File, Menu_Actions, Menu_View, Menu_Options, Menu_Help })
        Menu->installEventFilter(Tips);
}

//---------------------------------------------------------------------------
void GUI_Main::Menu_Enable(QAction* Action, bool IsEnabled, const QString& Reason)
{
    auto Tip = Menu_StatusTips.find(Action);
    if (Tip == Menu_StatusTips.end())
        Tip = Menu_StatusTips.insert(Action, Action->statusTip());

    // The status tip of an action which can not be used says why
    Action->setEnabled(IsEnabled);
    Action->setStatusTip(IsEnabled || Reason.isEmpty() ? Tip.value() : Reason);
    Menu_ToolTip(Action);
}

//---------------------------------------------------------------------------
void GUI_Main::Menu_ToolTip(QAction* Action)
{
    // The tool bar has no text: the tool tip has the name, as the menu has it, and what the status bar says
    QString Tip = QStringLiteral("<b>") + Menu_Name(Action).toHtmlEscaped() + QStringLiteral("</b>");
    if (!Action->shortcut().isEmpty())
        Tip += QStringLiteral(" (") + Action->shortcut().toString(QKeySequence::NativeText).toHtmlEscaped() + QLatin1Char(')');
    if (!Action->statusTip().isEmpty())
        Tip += QStringLiteral("<br>") + Action->statusTip().toHtmlEscaped();
    Action->setToolTip(Tip);
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

    if (!Close(All()))
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
    Save(All());
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Actions_Encode()
{
    if (!Action_Begin())
        return;

    //User interaction
    std::vector<operation> List;
    if (!Encode_Prepare(operation::Encode, Selection_Encode(), List))
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
    Operations_Add_Compressed(operation::Decode, Selection_Decode());
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Actions_Decode_To()
{
    if (!Action_Begin())
        return;
    const std::vector<size_t> List = Selection_Decode();
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
    Operations_Add_Compressed(operation::Decode, List, QDir::toNativeSeparators(Directory));
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Actions_Check()
{
    if (!Action_Begin())
        return;

    //Running
    Operations_Begin(tr("checking"), tr("Check finished"));
    Operations_Add_Compressed(operation::Check, Selection_Decode());
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Actions_Check_Originals()
{
    if (!Action_Begin())
        return;
    const std::vector<size_t> List = Selection_Decode();
    if (List.empty())
        return;

    //User interaction, once for all the packages: the names of the files are relative to the directory which holds their directory
    const QString Directory = QFileDialog::getExistingDirectory(this,
                                                                tr("RAWcooked - Check against the original files: the directory which holds their directory"),
                                                                QFileInfo(Packages[List.front()].Inputs.value(0)).absolutePath(),
                                                                QFileDialog::ShowDirsOnly
                                                              | QFileDialog::DontResolveSymlinks);
    if (Directory.isEmpty())
        return;

    //Running
    Operations_Begin(tr("checking"), tr("Check finished"));
    for (const size_t Pos : List)
        Operations_Add_Compressed(operation::Check, { Pos }, QDir::toNativeSeparators(Originals_Directory(Packages[Pos], Directory)));
    Operations_Start();
}

//---------------------------------------------------------------------------
QString GUI_Main::Originals_Directory(const gui_package& Package, const QString& Chosen)
{
    // The directory of the files may be chosen instead of the one which holds it. With the names RAWcooked proposes,
    // a Matroska file has the name of the directory of the original files, or of their first file
    const QDir Directory(Chosen);
    const QString Parent = QFileInfo(Directory.absolutePath()).absolutePath();
    if (Directory.isRoot() || Parent == Directory.absolutePath())
        return Chosen;
    for (const gui_compressed& Compressed : Package.Compressed)
    {
        if (!Compressed.HasReversibilityData)
            continue;
        QString Name = QFileInfo(Compressed.FileName).fileName();
        if (Name.endsWith(QLatin1String(".mkv"), Qt::CaseInsensitive))
            Name.chop(4);
        if (Name.isEmpty())
            continue;
        const QFileInfo Inside(Directory.filePath(Name));
        if (Inside.isDir())
            return Chosen; // The directory of the original files is in the chosen one
        if (Inside.isFile() || Directory.dirName().compare(Name, gui_package::Path_Sensitivity()) == 0)
            return Parent; // The original files are directly in the chosen directory
    }
    return Chosen;
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Actions_Analyze()
{
    if (!Action_Begin())
        return;

    //User interaction
    const std::vector<size_t> List = Selected(true);
    if (!Modified_Lose(List, tr("The modifications are lost if the packages are analyzed again."), tr("&Discard and analyze")))
        return;

    //Running
    Operations_Begin(tr("analyzing"), tr("Analysis finished"));
    Operations_Add(operation::Analyze, List);
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_Actions_Command()
{
    if (!Action_Begin())
        return;
    const std::vector<size_t> Uncompressed = Selection_Encode();
    if (Uncompressed.size() != 1)
        return; // The command is the one of a package

    //User interaction
    std::vector<operation> List;
    if (!Encode_Prepare(operation::Command, Uncompressed, List))
        return;

    //Running, the command is shown at the end
    Operations_Begin(tr("preparing the FFmpeg command"), QString());
    for (const operation& Operation : List)
        Operations_Add(Operation);
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMenu_View_Files()
{
    //A value being typed which is not valid stays in its editor
    if (!Metadata->Editor_Close())
    {
        View_Refresh(View_Metadata);
        return;
    }

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
    //A value being typed which is not valid stays in its editor
    if (!Metadata->Editor_Close())
    {
        View_Refresh(View_Metadata);
        return;
    }

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

    //The open packages were analyzed with the options as they were
    if (!Packages.empty() && Analysis_Options(Options) != Before)
        Status_Set(tr("Analysis options changed: use Actions > Analyze again to apply them to the open packages"));
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

    //The open packages were analyzed with the license as it was
    bool IsOutdated = !Packages.empty() && Analysis_Options(Options) != Before; // Other key for this session
    for (const gui_package& Package : Packages)
        for (const gui_track& Track : Package.Tracks)
            if (Track.IsNotLicensed && !Package.IsLicenseIgnored)
                IsOutdated = true; // A key stored meanwhile may have it
    if (IsOutdated)
        Status_Set(tr("If the license changed, use Actions > Analyze again to check the open packages against it"));
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
    QDesktopServices::openUrl(QUrl(QLatin1String(Website), QUrl::TolerantMode));
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
    if (!Modified_Lose(List, tr("The files are encoded as they are on the disk, without these modifications."), Kind == operation::Command ? tr("&Continue") : tr("&Encode anyway")))
        return false;

    for (const size_t Pos : List)
    {
        operation Operation;
        Operation.Kind = Kind;
        Operation.Package = Pos;
        Operation.Output = Output_Default(Packages[Pos]);
        Result.push_back(Operation);
    }

    // What the library would refuse is said before anything is asked
    if (!Encode_License(Kind, Result))
        return false;

    // Output files. The preferences may answer the questions about the existing ones, as they do for the questions of the library
    const QStringList Options_Encode = Options.Arguments(Operation_Encode);
    const auto Yes_Pos = Options_Encode.lastIndexOf(QStringLiteral("-y"));
    const auto No_Pos = Options_Encode.lastIndexOf(QStringLiteral("-n"));
    const bool AlwaysYes = Kind == operation::Encode && Yes_Pos > No_Pos;
    const bool AlwaysNo = Kind == operation::Encode && No_Pos > Yes_Pos;
    if (!Encode_Outputs(Kind, Result, AlwaysYes, AlwaysNo))
        return false;
    if (Kind == operation::Encode && !Encode_FrameMd5(Result, AlwaysYes, AlwaysNo))
        return false;

    // Checks the content needs
    return Encode_Checks(Kind, Result);
}

//---------------------------------------------------------------------------
bool GUI_Main::Encode_License(operation::kind Kind, std::vector<operation>& List)
{
    // A track which is not in the license is refused by the library, unless it ignores the license
    QStringList Refused;
    std::vector<operation> Kept;
    for (const operation& Operation : List)
    {
        const gui_package& Package = Packages[Operation.Package];
        bool IsRefused = false;
        if (!Package.IsLicenseIgnored)
            for (const gui_track& Track : Package.Tracks)
                if (Track.IsNotLicensed)
                    IsRefused = true;
        if (IsRefused)
            Refused += Package.Name();
        else
            Kept.push_back(Operation);
    }
    if (Refused.isEmpty())
        return true;

    const QString Text = Refused.size() == 1 ? tr("%1 can not be encoded: a track is not in the license.").arg(Refused.first())
                                             : tr("%1 packages can not be encoded: they have tracks which are not in the license.").arg(Refused.size());
    const QString Informative = tr("Options > License... shows the license and takes a key. Contact info@mediaarea.net for a quote or a temporary key.");
    if (Kept.empty())
    {
        Message_Ask(QMessageBox::Warning, Text, Informative, QMessageBox::Ok, QMessageBox::Ok);
        return false;
    }
    if (Message_Ask(QMessageBox::Warning, Text, Informative, QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel,
                    { { QMessageBox::Ok, Kind == operation::Command ? tr("&Continue") : tr("&Encode the other packages") } }) != QMessageBox::Ok)
        return false;
    List.swap(Kept);
    return true;
}

//---------------------------------------------------------------------------
bool GUI_Main::Encode_Outputs(operation::kind Kind, std::vector<operation>& List, bool AlwaysYes, bool AlwaysNo)
{
    if (List.size() != 1)
        return Encode_Ask(List, AlwaysYes, AlwaysNo);

    // The dialog asks itself before replacing a file, unless the preferences have the answer. It is shown again while the name can not be used
    operation& Operation = List.front();
    const gui_package& Package = Packages[Operation.Package];
    const QString Proposed = Operation.Output.isEmpty() ? QDir::homePath() : Operation.Output;
    for (;;)
    {
        const QString FileName = QFileDialog::getSaveFileName(this,
                                                              Kind == operation::Command ? tr("RAWcooked - FFmpeg command, output file") : tr("RAWcooked - Encode to"),
                                                              Proposed,
                                                              tr("Matroska files (*.mkv);;All files (*)"),
                                                              nullptr,
                                                              AlwaysYes || AlwaysNo ? QFileDialog::DontConfirmOverwrite : QFileDialog::Options());
        if (FileName.isEmpty())
            return false;
        Operation.Output = QDir::toNativeSeparators(FileName);
        QString Refused = Output_Refused(Package, Operation.Output);
        QString Refused_Why;
        if (Refused.isEmpty() && AlwaysNo && QFileInfo::exists(Operation.Output))
        {
            // The encoder would refuse it, with messages of its own
            Refused = tr("The output file already exists.");
            Refused_Why = tr("It is not replaced because the preference Questions is Always no (-n).");
        }
        if (Refused.isEmpty())
            return true;
        Message_Ask(QMessageBox::Warning, Refused, Refused_Why, QMessageBox::Ok, QMessageBox::Ok);
    }
}

//---------------------------------------------------------------------------
bool GUI_Main::Encode_FrameMd5(const std::vector<operation>& List, bool AlwaysYes, bool AlwaysNo)
{
    // The encoder replaces its outputs without asking: the user accepted the Matroska file, not this file
    if (!Options.FrameMd5_IsSet())
        return true;
    const QString Name = Options.FrameMd5_FileName();
    QStringList Existing;
    for (const operation& Operation : List)
    {
        QString FileName = Name;
        if (FileName.isEmpty())
        {
            // As the library names it: after the first input
            FileName = QDir::toNativeSeparators(Packages[Operation.Package].Inputs.value(0));
            while (FileName.size() > 1 && FileName.endsWith(QDir::separator()))
                FileName.chop(1);
            FileName += QStringLiteral(".framemd5");
        }
        FileName = QDir::toNativeSeparators(FileName);
        if (QFileInfo::exists(FileName) && !Existing.contains(FileName))
            Existing += FileName;
    }
    if (Existing.isEmpty() || AlwaysYes)
        return true;

    const QString Text = Existing.size() == 1 ? tr("The framemd5 file %1 already exists.").arg(Existing.first())
                                              : tr("%1 framemd5 files already exist, e.g. %2.").arg(Existing.size()).arg(Existing.first());
    if (AlwaysNo)
    {
        Message_Ask(QMessageBox::Warning, Text, Existing.size() == 1 ? tr("It is not replaced because the preference Questions is Always no (-n).") : tr("They are not replaced because the preference Questions is Always no (-n)."), QMessageBox::Ok, QMessageBox::Ok);
        return false;
    }
    QString Informative = Existing.size() == 1 ? tr("The encoding replaces it.") : tr("The encodings replace them.");
    if (!Name.isEmpty() && List.size() > 1)
        Informative += QLatin1Char(' ') + tr("Each encoding writes the same file (preference %1): it has the frames of the last package only.").arg(GUI_Options::Where(QStringLiteral("FrameMd5Name")));
    return Message_Ask(QMessageBox::Warning, Text, Informative, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel, { { QMessageBox::Yes, tr("&Replace") } }) == QMessageBox::Yes;
}

//---------------------------------------------------------------------------
bool GUI_Main::Encode_Checks(operation::kind Kind, std::vector<operation>& List)
{
    const QString Accept = Kind == operation::Command ? tr("&Continue") : tr("&Encode");
    const auto Has = [this](const operation& Operation, bool gui_track::*Member)
    {
        for (const gui_track& Track : Packages[Operation.Package].Tracks)
            if (Track.*Member)
                return true;
        return false;
    };
    const auto Names = [this](const std::vector<const operation*>& Items)
    {
        return Items.size() == 1 ? Packages[Items.front()->Package].Name() : tr("%1 packages").arg(Items.size());
    };

    // The library checks the reversibility after an encoding only for a directory or a single file
    const auto CanCheck = [this](const operation& Operation)
    {
        const gui_package& Package = Packages[Operation.Package];
        return Package.IsDirectory || Package.Inputs.size() == 1;
    };

    // Content encoded only with the full check: refused where the check is not possible
    std::vector<operation> Kept;
    std::vector<const operation*> Refused;
    for (const operation& Operation : List)
    {
        if (Has(Operation, &gui_track::NeedsCheck) && !CanCheck(Operation))
            Refused.push_back(&Operation);
        else
            Kept.push_back(Operation);
    }
    if (!Refused.empty())
    {
        const QString Text = tr("%1 can not be encoded: its content is encoded only with the full check of reversibility, which is possible only for a directory or a single file.").arg(Names(Refused));
        if (Kept.empty())
        {
            Message_Ask(QMessageBox::Warning, Text, QString(), QMessageBox::Ok, QMessageBox::Ok);
            return false;
        }
        if (Message_Ask(QMessageBox::Warning, Text, QString(), QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel, { { QMessageBox::Ok, Kind == operation::Command ? tr("&Continue") : tr("&Encode the other packages") } }) != QMessageBox::Ok)
            return false;
        List.swap(Kept);
    }

    // Content encoded only with the full check: asked as for the padding bits, the encoding then has the check
    if (!Options.Check_IsFull())
    {
        std::vector<const operation*> Needing;
        for (const operation& Operation : List)
            if (Has(Operation, &gui_track::NeedsCheck))
                Needing.push_back(&Operation);
        if (!Needing.empty())
        {
            const QPointer<QMessageBox> Box = Message_Create(this, QMessageBox::Question, tr("The content of %1 is encoded only with the full check of reversibility after the encoding.").arg(Names(Needing)),
                                                             QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel, { { QMessageBox::Yes, Kind == operation::Command ? tr("&Continue with the full check") : tr("&Encode with the full check") } });
            Box->setInformativeText(tr("The Matroska file is decoded and compared with the files after the encoding: it takes more time."));
            QCheckBox* Remember_Box = new QCheckBox(tr("Remember this choice (it can be changed in the preferences)"), Box);
            Box->setCheckBox(Remember_Box);
            const bool IsYes = Box->exec() == QMessageBox::Yes;
            const bool IsRemembered = Box && Remember_Box->isChecked();
            delete Box;
            if (!IsYes)
                return false;
            if (!IsRemembered || !Remember(QStringLiteral("Check"), QStringLiteral("--check")))
                for (operation& Operation : List)
                    if (Has(Operation, &gui_track::NeedsCheck))
                        Operation.MoreArguments << QStringLiteral("--check");
        }
    }

    // The full check requested for a package where the library can not do it: the quick check instead, if the user wants
    if (Options.Check_IsFull())
    {
        std::vector<const operation*> Quick;
        for (const operation& Operation : List)
            if (!CanCheck(Operation))
                Quick.push_back(&Operation);
        if (!Quick.empty())
        {
            if (Message_Ask(QMessageBox::Question, tr("The full check of reversibility is not possible for %1: it is only for a directory or a single file.").arg(Names(Quick)),
                            tr("The preferences ask for it (%1).").arg(GUI_Options::Where(QStringLiteral("Check"))),
                            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel, { { QMessageBox::Yes, Kind == operation::Command ? tr("&Continue with the quick check") : tr("&Encode with the quick check") } }) != QMessageBox::Yes)
                return false;
            for (operation& Operation : List)
                if (!CanCheck(Operation))
                    Operation.MoreArguments << QStringLiteral("--quick-check");
        }
    }

    // Padding bits: the library refuses to encode without a choice, unless all the files are checked or the full check follows
    if (!Options.CheckPadding_IsSet())
    {
        // The choice is for the packages which need it
        bool HasPadding = false;
        bool CanCheck_All = true;
        for (const operation& Operation : List)
            if (Has(Operation, &gui_track::MayHavePaddingBits) && !Check_IsFull(Operation))
            {
                HasPadding = true;
                if (!CanCheck(Operation))
                    CanCheck_All = false;
            }
        if (HasPadding)
        {
            QStringList Choice;
            if (!Padding_Ask(Choice, CanCheck_All, Accept))
                return false;
            for (operation& Operation : List)
                if (Has(Operation, &gui_track::MayHavePaddingBits) && !Check_IsFull(Operation))
                    Operation.MoreArguments << Choice;
        }
    }

    return true;
}

//---------------------------------------------------------------------------
bool GUI_Main::Check_IsFull(const operation& Operation) const
{
    return Operation.MoreArguments.contains(QStringLiteral("--check")) || (Options.Check_IsFull() && !Operation.MoreArguments.contains(QStringLiteral("--quick-check")));
}

//---------------------------------------------------------------------------
QString GUI_Main::Output_Refused(const gui_package& Package, const QString& Output) const
{
    path_cache Cache;
    const QString Key = Path_Key(Output, false, &Cache);

    // A file which is encoded, or a file in the directory which is encoded
    for (const QString& Input : Package.Inputs)
    {
        const QString Input_Key = Path_Key(Input, Package.IsDirectory, &Cache);
        if (!Package.IsDirectory && Key == Input_Key)
            return tr("The output file can not be one of the files to encode.");
        if (Package.IsDirectory)
            for (QString Parent = Path_Key_Parent(Key); !Parent.isEmpty(); Parent = Path_Key_Parent(Parent))
                if (Parent == Input_Key)
                    return tr("The output file can not be in the directory to encode.");
    }

    // The other files of a sequence: a file which is opened stands for its sequence
    if (!Package.IsDirectory)
        for (const gui_track& Track : Package.Tracks)
        {
            const QString First_Key = Path_Key(Track.FileName, false, &Cache);
            if (Key == First_Key || (Path_Key_Parent(Key) == Path_Key_Parent(First_Key) && Template_Match(Track.Name, QFileInfo(Output).fileName())))
                return tr("The output file can not have the name of a file of the track %1.").arg(Track.Name);
        }

    return QString();
}

//---------------------------------------------------------------------------
bool GUI_Main::Encode_Ask(std::vector<operation>& List, bool AlwaysYes, bool AlwaysNo)
{
    QDialog Dialog(this);
    Dialog.setWindowTitle(tr("RAWcooked - Encode"));
    Dialog.setWindowIcon(windowIcon());
    Dialog.setModal(true);

    QVBoxLayout* Layout = new QVBoxLayout(&Dialog);

    // Message
    QLabel* MessageLabel = new QLabel(&Dialog);
    Layout->addWidget(MessageLabel);

    // Output file of each package, and what happens to the ones which exist
    enum column
    {
        Column_Package,
        Column_Output,
        Column_State,
        Column_Max,
    };
    QTreeWidget* Outputs = new QTreeWidget(&Dialog);
    Outputs->setColumnCount(Column_Max);
    Outputs->setHeaderLabels(QStringList() << tr("Package") << tr("Matroska file") << QString());
    Outputs->setRootIsDecorated(false);
    Outputs->setUniformRowHeights(true);
    Outputs->setSelectionMode(QAbstractItemView::NoSelection);
    Outputs->setFocusPolicy(Qt::NoFocus);
    Outputs->setTextElideMode(Qt::ElideMiddle);
    std::vector<bool> Refused, Exists;
    size_t Existing = 0;
    for (const operation& Operation : List)
    {
        const gui_package& Package = Packages[Operation.Package];
        QTreeWidgetItem* Item = new QTreeWidgetItem(Outputs);
        Item->setText(Column_Package, Package.Name());
        Item->setToolTip(Column_Package, Package.Inputs.value(0));
        Item->setText(Column_Output, Operation.Output);
        Item->setToolTip(Column_Output, Operation.Output);

        // A package whose output file can not have the usual name needs the choice of a name
        QString Why;
        if (Operation.Output.isEmpty())
            Why = tr("Not encoded: no name for the root of a volume, encode it alone to choose one");
        else if (!Output_Refused(Package, Operation.Output).isEmpty())
            Why = tr("Not encoded: this name is the one of a file to encode, encode it alone to choose another one");
        Item->setText(Column_State, Why);
        Item->setToolTip(Column_State, Why);
        Refused.push_back(!Why.isEmpty());
        Exists.push_back(Why.isEmpty() && QFileInfo::exists(Operation.Output));
        if (Exists.back())
            Existing++;
    }
    Outputs->header()->setStretchLastSection(false);
    Outputs->header()->setSectionResizeMode(Column_Package, QHeaderView::ResizeToContents);
    Outputs->header()->setSectionResizeMode(Column_Output, QHeaderView::Stretch);
    Outputs->header()->setSectionResizeMode(Column_State, QHeaderView::ResizeToContents);
    const int Lines = qMin((int)List.size(), Outputs_Lines_Max) + 1;
    Outputs->setMinimumHeight(Outputs->header()->sizeHint().height() + Lines * Outputs->fontMetrics().lineSpacing() * 3 / 2);
    Layout->addWidget(Outputs, 1);

    // The files which exist: the choice of the user, unless the preferences have the answer
    QCheckBox* Replace = nullptr;
    if (Existing && !AlwaysYes && !AlwaysNo)
    {
        Replace = new QCheckBox(Existing == 1 ? tr("&Replace the Matroska file which already exists") : tr("&Replace the %1 Matroska files which already exist").arg(Existing), &Dialog);
        Layout->addWidget(Replace);
    }
    else if (Existing)
    {
        QLabel* NoteLabel = new QLabel(AlwaysYes ? tr("The Matroska files which already exist are replaced because the preference Questions is Always yes (-y).")
                                                 : tr("The Matroska files which already exist are not replaced because the preference Questions is Always no (-n)."), &Dialog);
        NoteLabel->setWordWrap(true);
        Layout->addWidget(NoteLabel);
    }

    // Buttons
    QDialogButtonBox* Buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &Dialog);
    QPushButton* Encode = Buttons->addButton(tr("&Encode"), QDialogButtonBox::AcceptRole);
    Encode->setDefault(true);
    connect(Buttons, &QDialogButtonBox::accepted, &Dialog, &QDialog::accept);
    connect(Buttons, &QDialogButtonBox::rejected, &Dialog, &QDialog::reject);
    Layout->addWidget(Buttons);

    // What the dialog says follows the choice
    const auto IsReplaced = [&]() { return AlwaysYes || (Replace && Replace->isChecked()); };
    const auto Update = [&]()
    {
        size_t Count = 0;
        for (size_t Pos = 0; Pos < List.size(); Pos++)
        {
            if (Exists[Pos])
                Outputs->topLevelItem((int)Pos)->setText(Column_State, IsReplaced() ? tr("Already exists: replaced") : tr("Already exists: not encoded"));
            if (!Refused[Pos] && (!Exists[Pos] || IsReplaced()))
                Count++;
        }
        if (!Count)
            MessageLabel->setText(tr("No package is encoded:"));
        else
            MessageLabel->setText(Count == 1 ? tr("1 package is encoded to a Matroska file:") : tr("%1 packages are encoded, each one to a Matroska file:").arg(Count));
        Encode->setEnabled(Count != 0);
    };
    if (Replace)
        connect(Replace, &QCheckBox::toggled, &Dialog, Update);
    Update();

    // Size of the dialog: wide enough for names of files
    Dialog.resize(Dialog.fontMetrics().averageCharWidth() * 130, Dialog.sizeHint().height());

    if (Dialog.exec() != QDialog::Accepted)
        return false;

    // The packages which are encoded
    std::vector<operation> Kept;
    for (size_t Pos = 0; Pos < List.size(); Pos++)
        if (!Refused[Pos] && (!Exists[Pos] || IsReplaced()))
            Kept.push_back(List[Pos]);
    List.swap(Kept);
    return !List.empty();
}

//---------------------------------------------------------------------------
bool GUI_Main::Padding_Ask(QStringList& Arguments, bool CanCheck, const QString& Accept)
{
    QDialog Dialog(this);
    Dialog.setWindowTitle(tr("RAWcooked - Padding bits"));
    Dialog.setWindowIcon(windowIcon());
    Dialog.setModal(true);

    QVBoxLayout* Layout = new QVBoxLayout(&Dialog);

    // Message
    QLabel* MessageLabel = new QLabel(tr("Image files to encode (DPX or TIFF) have padding bits (bits of the file which are not part of the image). "
                                         "They are almost always zero; the ones which are not zero are restored by the decoding only if they were found during the encoding. "
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
                                            tr("Slower: all the files are read before the encoding. The decoding then always restores the files exactly."));
    QRadioButton* Check = Choice_Add(tr("Check after encoding, with the full check of reversibility (--check)"),
                                     CanCheck ? tr("The Matroska file is compared to the files. If padding bits are not zero the check fails, and the encoding has to be done again with the first choice.")
                                              : tr("Not available: this check is only for a directory or a single file."),
                                     CanCheck);
    QRadioButton* NoCheckPadding = Choice_Add(tr("Do not check (--no-check-padding)"),
                                              tr("Quicker, but the decoding does not restore exactly the files whose padding bits are not zero."));
    CheckPadding->setChecked(true);

    // Remember
    QCheckBox* Remember = new QCheckBox(tr("Remember this choice (it can be changed in the preferences)"), &Dialog);
    Layout->addSpacing(Layout->spacing());
    Layout->addWidget(Remember);

    // Buttons
    QDialogButtonBox* Buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &Dialog);
    Buttons->button(QDialogButtonBox::Ok)->setText(Accept);
    connect(Buttons, &QDialogButtonBox::accepted, &Dialog, &QDialog::accept);
    connect(Buttons, &QDialogButtonBox::rejected, &Dialog, &QDialog::reject);
    Layout->addWidget(Buttons);

    // Wide enough for the wrapped texts, and as high as they need at this width (the layout would spread more height between them)
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
    if (!Remember->isChecked() || !GUI_Main::Remember(Option, Value))
        Arguments << Value; // Also when it can not be remembered, for this encoding
    return true;
}

//---------------------------------------------------------------------------
bool GUI_Main::Remember(const QString& Option, const QString& Value)
{
    Options.Value_Set(Option, Value);
    if (Options.Save())
        return true;
    Message_Ask(QMessageBox::Warning, tr("The choice can not be saved in the preferences."),
                tr("The file %1 can not be written. The choice is used until the program is closed.").arg(QDir::toNativeSeparators(GUI_Options::Settings_FileName())),
                QMessageBox::Ok, QMessageBox::Ok);
    return false;
}

//***************************************************************************
// ToolBar creation
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::ToolBar_Create()
{
    ToolBar = new QToolBar(tr("ToolBar"));
    ToolBar->setIconSize(QSize(32, 32));

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

    // Icons only: the names are in the tool tips
    const QList<QAction*> Actions = ToolBar->actions();
    for (QAction* Action : Actions)
        if (!Action->isSeparator())
            Menu_ToolTip(Action);

    #ifndef __APPLE__
        addToolBar(Qt::LeftToolBarArea, ToolBar);
    #else //__APPLE__
        addToolBar(ToolBar);
    #endif //__APPLE__
}
