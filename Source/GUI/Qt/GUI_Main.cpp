/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Main.h"
#include "GUI/Qt/GUI_Job.h"
#include "GUI/Qt/GUI_Main_Files.h"
#include "GUI/Qt/GUI_Main_Metadata.h"
#include "GUI/Qt/GUI_Main_Output_Log.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
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
// Range of the progress bar
static const int Progress_Max = 1000;

//---------------------------------------------------------------------------
// Count of characters kept from what a job wrote since its last question (a job may write a lot)
static const int Job_Text_Max = 1 << 20;

//---------------------------------------------------------------------------
// Kinds of messages of the library, each one starts a line
enum message
{
    Message_Error   = 1 << 0,
    Message_Warning = 1 << 1,
    Message_Info    = 1 << 2,
};

//---------------------------------------------------------------------------
static QStringList Text_Lines(const QString& Text)
{
    QStringList Lines = Text.split(QLatin1Char('\n'));
    for (QString& Line : Lines)
        if (Line.endsWith(QLatin1Char('\r')))
            Line.chop(1);
    return Lines;
}

//---------------------------------------------------------------------------
static int Message_Kind(const QString& Line)
{
    if (Line.startsWith(QLatin1String("Error:")))
        return Message_Error;
    if (Line.startsWith(QLatin1String("Warning:")))
        return Message_Warning;
    if (Line.startsWith(QLatin1String("Info:")))
        return Message_Info;
    return 0;
}

//---------------------------------------------------------------------------
// The library writes the list of the tracks and of the attachments without an empty line before it
static bool Message_IsList(const QString& Line)
{
    return Line == QLatin1String("Attachments:") || (Line.startsWith(QLatin1String("Track ")) && Line.endsWith(QLatin1Char(':')));
}

//---------------------------------------------------------------------------
// Messages of the wanted kinds: their first line and the lines after it, up to an empty line or another message
static QString Messages_Get(const QString& Text, int Kinds)
{
    QStringList Result;
    bool Take = false;
    const QStringList Lines = Text_Lines(Text);
    for (const QString& Line : Lines)
    {
        if (Line.trimmed().isEmpty())
        {
            Take = false;
            continue;
        }
        if (const int Kind = Message_Kind(Line))
            Take = (Kind & Kinds) != 0;
        else if (Message_IsList(Line))
            Take = false;
        if (Take)
            Result.push_back(Line);
    }
    return Result.join(QLatin1Char('\n'));
}

//---------------------------------------------------------------------------
// Last lines of a text, the list of the tracks and of the attachments apart:
// where the reason of a failure is when the library has no error message for it (e.g. messages of the encoder)
static QString Messages_Last(const QString& Text)
{
    QStringList Last, Current;
    bool IsList = false;
    const QStringList Lines = Text_Lines(Text);
    for (const QString& Line : Lines)
    {
        const bool IsEmpty = Line.trimmed().isEmpty();
        if (IsEmpty || (!IsList && Message_IsList(Line)))
        {
            if (!Current.isEmpty())
            {
                Last = Current;
                Current.clear();
            }
            IsList = !IsEmpty;
            continue;
        }
        if (!IsList)
            Current.push_back(Line);
    }
    if (!Current.isEmpty())
        Last = Current;
    while (Last.size() > 10)
        Last.removeFirst();
    return Last.join(QLatin1Char('\n'));
}

//---------------------------------------------------------------------------
// Result of a check, as the library says it
static QString Message_Check(const QString& Text)
{
    const QStringList Lines = Text_Lines(Text);
    for (const QString& Line : Lines)
        if (Message_Kind(Line) == Message_Info && Line.contains(QLatin1String(" was checked, ")))
            return Line.mid(5).trimmed();
    return QString();
}

//---------------------------------------------------------------------------
// Directories of the decoded files, as the library says them
static QStringList Message_Directories(const QString& Text)
{
    QStringList Directories;
    const QString Begin = QStringLiteral("Info: Files are in ");
    const QStringList Lines = Text_Lines(Text);
    for (const QString& Line : Lines)
    {
        if (!Line.startsWith(Begin) || !Line.endsWith(QLatin1Char('.')))
            continue;
        QString Directory = Line.mid(Begin.size(), Line.size() - Begin.size() - 1);
        while (Directory.size() > 1 && (Directory.endsWith(QLatin1Char('/')) || Directory.endsWith(QLatin1Char('\\'))))
            Directory.chop(1);
        if (!Directory.isEmpty() && !Directories.contains(Directory))
            Directories.push_back(Directory);
    }
    return Directories;
}

//---------------------------------------------------------------------------
// Text of a part of a menu: not too long, and & is not a shortcut
static QString Menu_Text(QString Text)
{
    if (Text.size() > 60)
        Text = Text.left(28) + QStringLiteral("...") + Text.right(28);
    return Text.replace(QLatin1Char('&'), QStringLiteral("&&"));
}

//---------------------------------------------------------------------------
static QFont Font_Fixed()
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

//***************************************************************************
// Progress dialog
//***************************************************************************

//---------------------------------------------------------------------------
namespace
{

// Stays until the job has finished: closing it is only a request of cancellation
class progress_dialog : public QProgressDialog
{
public:
    explicit progress_dialog(QWidget* Parent) : QProgressDialog(Parent) {}

    void reject() override
    {
        Q_EMIT canceled();
    }

protected:
    void closeEvent(QCloseEvent* Event) override
    {
        Q_EMIT canceled();
        Event->ignore();
    }
};

}

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
GUI_Main::GUI_Main()
{
    // Views, created once
    Views = new QStackedWidget(this);
    Files = new GUI_Main_Files(this);
    Metadata = new GUI_Main_Metadata(this);
    Log = new GUI_Main_Output_Log(this);
    Views->addWidget(Files);
    Views->addWidget(Metadata);
    Views->addWidget(Log);
    setCentralWidget(Views);
    View_Current = View_Files;

    // Menu and ToolBar
    Menu_Create();
    ToolBar_Create();

    // Status bar, with the counts always visible
    setStatusBar(new QStatusBar());
    Status_Counts = new QLabel(this);
    Status_Counts->setContentsMargins(fontMetrics().averageCharWidth() * 3, 0, 0, 0); // A message which is cut where the counts start does not touch them
    statusBar()->addPermanentWidget(Status_Counts);

    // Progress of the operations
    Timer = new QTimer(this);
    connect(Timer, &QTimer::timeout, this, &GUI_Main::OnJob_Timer);

    // Views
    connect(Files, &GUI_Main_Files::Selection_Changed, this, &GUI_Main::Menu_Update);
    connect(Files, &GUI_Main_Files::Track_Activated, this, &GUI_Main::OnFiles_Track_Activated);
    connect(Files, &GUI_Main_Files::Close_Requested, this, &GUI_Main::OnMenu_File_Close_Files);
    connect(Metadata, &GUI_Main_Metadata::Modified, this, &GUI_Main::Menu_Update);

    // Window
    setWindowTitle(QStringLiteral("RAWcooked"));
    setWindowIcon(QIcon(QStringLiteral(":/Image/Icon.png")));
    setMinimumSize(800, 500);
    QSize Size(1100, 650);
    Size.setHeight(qMax(Size.height(), sizeHint().height())); // High enough for the whole tool bar, if the screen is
    QScreen* Screen = screen();
    if (!Screen)
        Screen = QGuiApplication::primaryScreen();
    if (Screen)
    {
        const QRect Available = Screen->availableGeometry();
        Size = Size.boundedTo(QSize(Available.width() - 40, Available.height() - 80)).expandedTo(minimumSize()); // With room for the frame of the window
        setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, Size, Available));
    }
    else
        resize(Size);

    // Drag n drop
    setAcceptDrops(true);

    // Content
    View_Refresh(View_Files);
}

//---------------------------------------------------------------------------
GUI_Main::~GUI_Main()
{
    // Not expected (the window is not closed while an operation runs): the job is stopped while the window it asks its questions to is still there
    if (Job)
    {
        const bool IsDone = Job->isFinished() && !Job->ExitCode; // Its end was not handled yet
        Job->disconnect(this);
        delete Job; // Canceled by its destructor
        Job = nullptr;
        if (!IsDone && Operation_Current.Kind == operation::Encode && !Operation_Current.Output_Existed && !Operation_Current.Output.isEmpty())
            QFile::remove(Operation_Current.Output); // Incomplete
    }
}

//***************************************************************************
// View_Refresh
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::View_Refresh(view View_New)
{
    if (View_New != View_None)
    {
        View_Current = View_New;
        switch (View_Current)
        {
            case View_Metadata  : Views->setCurrentWidget(Metadata); Menu_View_Metadata->setChecked(true); break;
            case View_Log       : Views->setCurrentWidget(Log); Menu_View_Log->setChecked(true); break;
            default             : Views->setCurrentWidget(Files); Menu_View_Files->setChecked(true);
        }
    }

    // Content
    Files->Refresh();
    Metadata->Refresh();

    // Menu
    Menu_Update();
}

//***************************************************************************
// Events
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::dragEnterEvent(QDragEnterEvent* Event)
{
    if (!IsBusy() && Event->mimeData()->hasUrls())
        Event->acceptProposedAction();
}

//---------------------------------------------------------------------------
void GUI_Main::dropEvent(QDropEvent* Event)
{
    // Ignored while an operation is running
    if (IsBusy() || !Event->mimeData()->hasUrls())
        return;

    QStringList Paths;
    const QList<QUrl> Urls = Event->mimeData()->urls();
    for (const QUrl& Url : Urls)
    {
        const QString Path = Url.toLocalFile();
        if (!Path.isEmpty())
            Paths.push_back(Path);
    }
    if (Paths.isEmpty())
        return;
    Event->acceptProposedAction();

    // After the end of the drop: the program the files come from waits for it, and opening may ask something to the user
    QMetaObject::invokeMethod(this, [this, Paths]() { Open(Paths); }, Qt::QueuedConnection);
}

//---------------------------------------------------------------------------
void GUI_Main::closeEvent(QCloseEvent* Event)
{
    // An operation is running: it is canceled first, the window is closed when the job has finished
    if (IsBusy())
    {
        Event->ignore();
        if (Close_Requested || Close_Asking)
            return;
        Close_Asking = true;
        const QMessageBox::StandardButton Answer = Message_Ask(QMessageBox::Question, tr("An operation is running."), tr("Do you want to cancel it and quit?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        Close_Asking = false;
        if (Answer != QMessageBox::Yes)
            return;
        if (IsBusy())
        {
            Close_Requested = true;
            Operations_Cancel();
        }
        else
            QMetaObject::invokeMethod(this, &QWidget::close, Qt::QueuedConnection); // Finished while the question was shown
        return;
    }

    // Unsaved modifications
    Action_Begin();
    std::vector<size_t> All;
    for (size_t Pos = 0; Pos < Packages.size(); Pos++)
        All.push_back(Pos);
    if (!Close(All))
    {
        Event->ignore();
        return;
    }
    View_Refresh();
    Event->accept();
}

//---------------------------------------------------------------------------
void GUI_Main::resizeEvent(QResizeEvent* Event)
{
    QMainWindow::resizeEvent(Event);

    // The temporary message of the status bar was shortened for the width the window had
    const qint64 Remaining = Status_End.remainingTime();
    if (Remaining > 0 && statusBar()->currentMessage() == Status_Shown)
        Status_Message(Status_Text, (int)Remaining);
}

//***************************************************************************
// Actions
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::Open(const QStringList& Paths)
{
    // While an operation is running the analysis is added to the current batch, without question to the user
    const bool Busy = IsBusy();
    if (!Busy)
        Action_Begin();

    // Each directory is a package, all the files together are one package
    QStringList Directories, FileNames;
    QString First;
    for (const QString& Path : Paths)
    {
        if (Path.isEmpty())
            continue;
        const QFileInfo Info(Path);
        const QString Name = QDir::toNativeSeparators(QDir::cleanPath(Info.absoluteFilePath())); // Never a relative name: the library would take a name starting with - for an option
        QStringList& List = Info.isDir() ? Directories : FileNames;
        if (!List.contains(Name, Path_Sensitivity))
            List.push_back(Name);
        if (First.isEmpty())
            First = Name;
    }
    if (First.isEmpty())
        return;
    Options.Directory_Set(QFileInfo(First).absolutePath());

    // A path already open is not opened twice, its package is analyzed again
    std::vector<size_t> Again;
    const auto Find = [this, &Again](const QString& Name, bool IsDirectory)
    {
        for (size_t Pos = 0; Pos < Packages.size(); Pos++)
            if (Packages[Pos].IsDirectory == IsDirectory && Packages[Pos].Inputs.contains(Name, Path_Sensitivity))
            {
                if (std::find(Again.begin(), Again.end(), Pos) == Again.end())
                    Again.push_back(Pos);
                return true;
            }
        return false;
    };
    QStringList Directories_New, FileNames_New;
    for (const QString& Name : Directories)
        if (!Find(Name, true))
            Directories_New.push_back(Name);
    for (const QString& Name : FileNames)
        if (!Find(Name, false))
            FileNames_New.push_back(Name);
    if (Modified_Count(Again) && (Busy || !Modified_Lose(Again)))
        Again.erase(std::remove_if(Again.begin(), Again.end(), [this](size_t Pos) { return Packages[Pos].IsModified(); }), Again.end()); // Unsaved modifications are kept

    // Packages, each one with its analysis
    std::vector<size_t> Added;
    for (const QString& Name : Directories_New)
    {
        gui_package Package;
        Package.Inputs.push_back(Name);
        Package.IsDirectory = true;
        Package.Status = tr("Not analyzed");
        Packages.push_back(Package);
        Added.push_back(Packages.size() - 1);
    }
    if (!FileNames_New.isEmpty())
    {
        gui_package Package;
        Package.Inputs = FileNames_New;
        Package.Status = tr("Not analyzed");
        Packages.push_back(Package);
        Added.push_back(Packages.size() - 1);
    }
    if (!Added.empty())
        View_Refresh(); // Listed at once
    Operations_Begin(tr("opening"), tr("Opening finished"));
    Operations_Add(operation::Analyze, Again);
    Operations_Add(operation::Analyze, Added);
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::Menu_Update()
{
    // Everything which changes the content or uses the library waits for the end of the operations
    const bool Busy = IsBusy();

    // File
    Menu_File_Open_Files->setEnabled(!Busy);
    Menu_File_Open_Directory->setEnabled(!Busy);
    const std::vector<size_t> List = Selected(false);
    if (List.empty())
        Menu_File_Close_Files->setText(tr("Close"));
    else if (List.size() == 1)
        Menu_File_Close_Files->setText(tr("Close %1").arg(Menu_Text(Packages[List.front()].Name())));
    else
        Menu_File_Close_Files->setText(tr("Close %1 packages").arg(List.size()));
    Menu_File_Close_Files->setEnabled(!Busy && !List.empty());
    Menu_File_Close_All->setEnabled(!Busy && !Packages.empty());
    size_t Modified = 0;
    for (const gui_package& Package : Packages)
        Modified += Package.Modified_Count();
    Menu_File_Save_All->setText(Modified ? tr("Save modified metadata (%1)").arg(Modified) : tr("Save modified metadata"));
    Menu_File_Save_All->setEnabled(!Busy && Modified != 0);

    // Actions
    const std::vector<size_t> Uncompressed = Selection(gui_package::Kind_Uncompressed);
    const bool HasCompressed = !Selection(gui_package::Kind_Compressed).empty();
    Menu_Actions_Encode->setEnabled(!Busy && !Uncompressed.empty());
    Menu_Actions_Decode->setEnabled(!Busy && HasCompressed);
    Menu_Actions_Decode_To->setEnabled(!Busy && HasCompressed);
    Menu_Actions_Check->setEnabled(!Busy && HasCompressed);
    Menu_Actions_Analyze->setEnabled(!Busy && !Packages.empty());
    Menu_Actions_Command->setEnabled(!Busy && Uncompressed.size() == 1);

    // Options, Help
    Menu_Options_Preferences->setEnabled(!Busy);
    Menu_Options_License->setEnabled(!Busy);
    Menu_Help_Help->setEnabled(!Busy);
    Menu_Help_About->setEnabled(!Busy);

    // Status bar
    Status_Counts->setText((Packages.size() == 1 ? tr("1 package") : tr("%1 packages").arg(Packages.size()))
                         + QStringLiteral(", ")
                         + (Modified == 1 ? tr("1 modified field") : tr("%1 modified fields").arg(Modified)));
}

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
std::vector<size_t> GUI_Main::Selected(bool AllIfNone) const
{
    std::vector<size_t> List = Files->Selection();
    List.erase(std::remove_if(List.begin(), List.end(), [this](size_t Pos) { return Pos >= Packages.size(); }), List.end()); // The view may show a previous content
    std::sort(List.begin(), List.end());
    List.erase(std::unique(List.begin(), List.end()), List.end());
    if (List.empty() && AllIfNone)
        for (size_t Pos = 0; Pos < Packages.size(); Pos++)
            List.push_back(Pos);
    return List;
}

//---------------------------------------------------------------------------
std::vector<size_t> GUI_Main::Selection(gui_package::kind Kind) const
{
    std::vector<size_t> List = Selected(true);
    List.erase(std::remove_if(List.begin(), List.end(), [this, Kind](size_t Pos) { return Packages[Pos].Kind != Kind; }), List.end());
    return List;
}

//---------------------------------------------------------------------------
size_t GUI_Main::Modified_Count(const std::vector<size_t>& List) const
{
    size_t Count = 0;
    for (const size_t Pos : List)
        if (Pos < Packages.size())
            Count += Packages[Pos].Modified_Count();
    return Count;
}

//---------------------------------------------------------------------------
bool GUI_Main::Modified_Lose(const std::vector<size_t>& List)
{
    const size_t Count = Modified_Count(List);
    if (!Count)
        return true;
    return Message_Ask(QMessageBox::Warning,
                       Count == 1 ? tr("1 modified field is not saved.") : tr("%1 modified fields are not saved.").arg(Count),
                       tr("The modifications are lost if the content is analyzed again. Do you want to continue?"),
                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

//---------------------------------------------------------------------------
void GUI_Main::Status_Message(const QString& Text, int Timeout)
{
    // The status bar cuts a message where the counts start, without a mark: the message is shortened before, in its middle
    // (its start says what it is about, and the end of the name of a file is the name)
    const QFontMetrics Metrics(statusBar()->font());
    const int Width = statusBar()->width() - Status_Counts->sizeHint().width() - Metrics.averageCharWidth() * 8;
    Status_Text = Text;
    Status_Shown = Metrics.elidedText(Text, Qt::ElideMiddle, qMax(Width, Metrics.averageCharWidth() * 20));
    Status_End = QDeadlineTimer(Timeout);
    statusBar()->showMessage(Status_Shown, Timeout);
}

//---------------------------------------------------------------------------
bool GUI_Main::Action_Begin()
{
    if (IsBusy())
        return false;

    // A value being edited in a view is taken as if its editing was finished: the view commits it when its editor loses the focus
    QWidget* Editor = QApplication::focusWidget();
    QWidget* Viewport = Editor ? Editor->parentWidget() : nullptr;
    QAbstractItemView* View = Viewport ? qobject_cast<QAbstractItemView*>(Viewport->parentWidget()) : nullptr;
    if (View && View->viewport() == Viewport && isAncestorOf(View))
        View->setFocus();

    return !IsBusy();
}

//---------------------------------------------------------------------------
QMessageBox::StandardButton GUI_Main::Message_Ask(QMessageBox::Icon Icon, const QString& Text, const QString& Informative, QMessageBox::StandardButtons Buttons, QMessageBox::StandardButton Default)
{
    QMessageBox Box(Icon, QStringLiteral("RAWcooked"), Text, Buttons, this);
    Box.setTextFormat(Qt::PlainText); // Names of files are not rich text
    Box.setInformativeText(Informative);
    Box.setDefaultButton(Default);
    Box.setWindowIcon(windowIcon());
    return (QMessageBox::StandardButton)Box.exec();
}

//---------------------------------------------------------------------------
bool GUI_Main::Close(const std::vector<size_t>& List)
{
    // Not while an operation runs: its results are for a package at a given place in the list
    if (IsBusy())
        return false;

    // Unsaved modifications
    size_t Modified = 0;
    QString Name;
    for (const size_t Pos : List)
        if (Pos < Packages.size() && Packages[Pos].IsModified())
        {
            Modified++;
            Name = Packages[Pos].Name();
        }
    if (Modified)
    {
        switch (Message_Ask(QMessageBox::Warning,
                            Modified == 1 ? tr("%1 has been modified.").arg(Name) : tr("%1 packages have been modified.").arg(Modified),
                            tr("Do you want to save your changes?"),
                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save))
        {
            case QMessageBox::Save      : // The files are being written, the packages stay open
                                          Save(List);
                                          return false;
            case QMessageBox::Discard   : break;
            default                     : return false;
        }
        if (IsBusy())
            return false;
    }

    // Removed from the list, last first
    std::vector<size_t> Sorted(List);
    std::sort(Sorted.begin(), Sorted.end());
    Sorted.erase(std::unique(Sorted.begin(), Sorted.end()), Sorted.end());
    if (!Sorted.empty())
    {
        Files->clearSelection(); // The selection is by place in the list
        statusBar()->clearMessage(); // It may be about a package which is closed
    }
    for (auto Pos = Sorted.rbegin(); Pos != Sorted.rend(); ++Pos)
        if (*Pos < Packages.size())
            Packages.erase(Packages.begin() + (std::vector<gui_package>::difference_type)*Pos);
    return true;
}

//---------------------------------------------------------------------------
void GUI_Main::Save(const std::vector<size_t>& List)
{
    if (IsBusy())
        return;

    Operations_Begin(tr("saving of the metadata"), tr("Metadata saved"));
    for (const size_t Pos : List)
    {
        if (Pos >= Packages.size() || !Packages[Pos].IsModified())
            continue;

        // The modified fields of each track, in its files
        operation Operation;
        Operation.Package = Pos;
        Operation.Kind = operation::Edit;
        for (Operation.Track = 0; Operation.Track < Packages[Pos].Tracks.size(); Operation.Track++)
            if (Packages[Pos].Tracks[Operation.Track].IsModified())
                Operations_Add(Operation);

        // Then the values are read back from the files
        Operation.Kind = operation::Analyze;
        Operation.Track = 0;
        Operation.Modified_Keep = true;
        Operations_Add(Operation);
    }
    Operations_Start();
}

//***************************************************************************
// Operations
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::Operations_Begin(const QString& Name, const QString& Message)
{
    if (Operations_Count)
        return;
    Operations_Name = Name;
    Operations_Message = Message;
}

//---------------------------------------------------------------------------
void GUI_Main::Operations_Add(const operation& Operation)
{
    Operations.push_back(Operation);
    Operations_Count++;
}

//---------------------------------------------------------------------------
void GUI_Main::Operations_Add(operation::kind Kind, const std::vector<size_t>& List, const QString& Output)
{
    operation Operation;
    Operation.Kind = Kind;
    Operation.Output = Output;
    for (const size_t Pos : List)
    {
        Operation.Package = Pos;
        Operations_Add(Operation);
    }
}

//---------------------------------------------------------------------------
QString GUI_Main::Operation_Title(const operation& Operation) const
{
    QString Name;
    if (Operation.Package < Packages.size())
    {
        const gui_package& Package = Packages[Operation.Package];
        Name = Package.Name();
        if (Operation.Kind == operation::Edit && Package.Tracks.size() > 1 && Operation.Track < Package.Tracks.size())
            Name += QStringLiteral(" / ") + Package.Tracks[Operation.Track].Name;
        if (Operation.Kind == operation::Check && Operation.Input < (size_t)Package.Inputs.size())
            Name = QFileInfo(Package.Inputs.at((int)Operation.Input)).fileName(); // A file of the package
    }
    switch (Operation.Kind)
    {
        case operation::Analyze : return tr("Analyzing %1").arg(Name);
        case operation::Edit    : return tr("Saving the metadata of %1").arg(Name);
        case operation::Encode  : return tr("Encoding %1").arg(Name);
        case operation::Decode  : return tr("Decoding %1").arg(Name);
        case operation::Check   : return tr("Checking %1").arg(Name);
        case operation::Command : return tr("Preparing the command of the encoder for %1").arg(Name);
    }
    return Name;
}

//---------------------------------------------------------------------------
QStringList GUI_Main::Operation_Arguments(const operation& Operation) const
{
    const gui_package& Package = Packages[Operation.Package];
    QStringList Arguments;
    switch (Operation.Kind)
    {
        case operation::Analyze :
            Arguments = Options.Arguments(Operation_Analyze);
            Arguments << QStringLiteral("--info") << QStringLiteral("--encode");
            if (!Package.IsDirectory)
                Arguments << QStringLiteral("--file");
            Arguments << Package.Inputs;
            break;
        case operation::Edit :
            Arguments = Options.Arguments(Operation_Edit);
            for (const gui_field& Field : Package.Tracks[Operation.Track].Fields)
                if (Field.IsModified)
                    Arguments << QStringLiteral("--edit") << Field.Name + QLatin1Char('=') + Field.NewValue;
            if (!Package.IsDirectory)
                Arguments << QStringLiteral("--file");
            Arguments << Package.Inputs;
            break;
        case operation::Encode :
        case operation::Command :
        {
            Arguments = Options.Arguments(Operation_Encode);
            Arguments << Operation.MoreArguments;
            if (Operation.Kind == operation::Command)
                Arguments << QStringLiteral("--display-command");
            Arguments << QStringLiteral("--encode");
            const QStringList MoreOutputOptions = QProcess::splitCommand(Options.Value(QStringLiteral("MoreOutputOptions")));
            if (MoreOutputOptions.isEmpty())
            {
                if (!Package.IsDirectory)
                    Arguments << QStringLiteral("--file");
                Arguments << Package.Inputs;
                Arguments << QStringLiteral("-o") << Operation.Output;
            }
            else
            {
                // Form of the command line of the encoder: what follows the output file is for the encoder
                for (const QString& Input : Package.Inputs)
                    Arguments << QStringLiteral("-i") << Input;
                Arguments << Operation.Output;
                for (QString Option : MoreOutputOptions)
                {
                    // The library puts these arguments as they are in the command of the encoder: the quotes an argument needs there are part of it
                    if (Option.contains(QLatin1Char(' ')) || Option.contains(QLatin1Char('\t')) || Option.contains(QLatin1Char('"')))
                        Option = QLatin1Char('"') + Option.replace(QLatin1Char('"'), QLatin1String("\"\"\"")) + QLatin1Char('"');
                    Arguments << Option;
                }
            }
            break;
        }
        case operation::Decode :
            Arguments = Options.Arguments(Operation_Decode);
            Arguments << QStringLiteral("--decode");
            Arguments << Package.Inputs;
            if (!Operation.Output.isEmpty())
                Arguments << QStringLiteral("-o") << Operation.Output;
            break;
        case operation::Check :
            Arguments = Options.Arguments(Operation_Check);
            Arguments << QStringLiteral("--check");
            if (Operation.Input < (size_t)Package.Inputs.size())
                Arguments << Package.Inputs.at((int)Operation.Input);
            else
                Arguments << Package.Inputs;
            break;
    }
    return Arguments;
}

//---------------------------------------------------------------------------
void GUI_Main::Operations_Start()
{
    while (!Job)
    {
        // End of the batch
        if (Operations.empty())
        {
            if (Operations_Count)
                Operations_End();
            return;
        }

        // Next operation
        Operation_Current = Operations.front();
        Operations.pop_front();
        Operations_Pos++;
        if (Operation_Current.Package >= Packages.size())
            continue;
        const gui_package& Package = Packages[Operation_Current.Package];
        if (Operation_Current.Kind == operation::Edit && (Operation_Current.Track >= Package.Tracks.size() || !Package.Tracks[Operation_Current.Track].IsModified()))
            continue; // Nothing to save

        // Job
        Job = new GUI_Job(this);
        Job->Arguments = Operation_Arguments(Operation_Current);
        switch (Operation_Current.Kind)
        {
            case operation::Analyze : Job->AnalyzeOnly = true;
                                      Job->Metadata = true;
                                      break;
            case operation::Edit    : Job->Edits_FileName = Package.Tracks[Operation_Current.Track].FileName;
                                      break;
            case operation::Encode  : Job->Overwrite = true; // Accepted by the user when the output file was chosen
                                      break;
            default                 : ;
        }
        Job->Ask_Overwrite = [this](const QString& FileName, const QString& ExtraText, bool CanApplyToAll, bool& ApplyToAll) { return Job_Ask_Overwrite(FileName, ExtraText, CanApplyToAll, ApplyToAll); };
        Job->Ask_Question = [this](const QString& Text) { return Job_Ask_Question(Text); };
        connect(Job, &QThread::finished, this, &GUI_Main::OnJob_Finished);

        // Log, with an empty line between the operations
        Job_Text.clear();
        Job_Refused = false;
        const QTextDocument* Document = Log->document();
        const bool IsSeparated = Document->isEmpty() || Document->lastBlock().text().trimmed().isEmpty();
        Log->Add((IsSeparated ? QStringLiteral("> ") : QStringLiteral("\n> ")) + Job->CommandLine() + QLatin1Char('\n'));

        // Progress
        Progress_Show();
        if (!Timer->isActive())
        {
            // First job of the batch
            Timer->start(100);
            statusBar()->clearMessage(); // The one of a previous batch
            Menu_Update();
        }

        Job->start();
    }
}

//---------------------------------------------------------------------------
void GUI_Main::Progress_Show()
{
    if (!ProgressDialog)
    {
        ProgressDialog = new progress_dialog(this);
        ProgressDialog->setWindowModality(Qt::WindowModal);
        ProgressDialog->setWindowIcon(windowIcon());
        ProgressDialog->setMinimumDuration(0);
        ProgressDialog->setAutoClose(false);
        ProgressDialog->setAutoReset(false);
        ProgressBar = new QProgressBar(ProgressDialog); // Used directly: setting the value through the dialog handles pending events, e.g. the end of the job
        ProgressDialog->setBar(ProgressBar);
        ProgressCancel = new QPushButton(tr("Cancel"), ProgressDialog);
        ProgressDialog->setCancelButton(ProgressCancel);
        disconnect(ProgressDialog, &QProgressDialog::canceled, ProgressDialog, nullptr); // The dialog would hide itself
        connect(ProgressDialog, &QProgressDialog::canceled, this, &GUI_Main::OnJob_Cancel);
        ProgressDialog->setMinimumWidth(ProgressDialog->fontMetrics().horizontalAdvance(QStringLiteral("frame=123456 fps=123 q=-0.0 size=12345678KiB time=00:00:00.00 bitrate=123456.7kbits/s speed=12.3x elapsed=0:00:00.00"))
                                      + ProgressDialog->fontMetrics().averageCharWidth() * 8); // Wide enough for a progress line of the encoder
    }

    Progress_Title = Operation_Title(Operation_Current);
    if (Operations_Count > 1)
        Progress_Title += QStringLiteral(" (%1/%2)").arg(Operations_Pos).arg(Operations_Count);
    ProgressDialog->setWindowTitle(QStringLiteral("RAWcooked - ") + Progress_Title);
    ProgressBar->setRange(0, 0);
    Progress_Text(QString());
    ProgressDialog->show();
}

//---------------------------------------------------------------------------
void GUI_Main::Progress_Text(const QString& Text)
{
    if (!ProgressDialog)
        return;

    // A progress line longer than the dialog (e.g. the one of the encoder for several video tracks) makes the dialog wider:
    // by steps and never back, else the dialog would be resized all the time; and not wider than the window
    const QFontMetrics Metrics(ProgressDialog->font());
    const int Margin = Metrics.averageCharWidth() * 6;
    const int Step = Metrics.averageCharWidth() * 10;
    const int Wanted = Metrics.horizontalAdvance(Text) + Margin;
    if (Wanted > ProgressDialog->minimumWidth() && ProgressDialog->minimumWidth() < width() - Step)
    {
        const int Width_Before = ProgressDialog->width();
        ProgressDialog->setMinimumWidth(qMin((Wanted / Step + 1) * Step, width() - Step));
        ProgressDialog->move(ProgressDialog->x() - (ProgressDialog->width() - Width_Before) / 2, ProgressDialog->y()); // Its center stays where it is
    }

    // Each line fits in the dialog
    const int Width = qMax(ProgressDialog->width(), ProgressDialog->minimumWidth()) - Margin;
    ProgressDialog->setLabelText(Metrics.elidedText(Progress_Title, Qt::ElideMiddle, Width) + QLatin1Char('\n') + Metrics.elidedText(Text, Qt::ElideRight, Width));
}

//---------------------------------------------------------------------------
void GUI_Main::Log_Move()
{
    if (!Job)
        return;
    const QString Text = Job->Log_Take();
    if (Text.isEmpty())
        return;
    Job_Text += Text;
    if (Job_Text.size() > Job_Text_Max)
        Job_Text.remove(0, Job_Text.size() - Job_Text_Max / 2); // A question is about the last messages
    Log->Add(Text);
}

//---------------------------------------------------------------------------
void GUI_Main::OnJob_Timer()
{
    if (!Job || !ProgressDialog)
        return;

    // Log
    Log_Move();

    // Progress
    quint64 Current = 0;
    quint64 Total = 0;
    QString Text;
    Job->Progress(Current, Total, Text);
    if (Total)
    {
        ProgressBar->setRange(0, Progress_Max);
        ProgressBar->setValue(Current >= Total ? Progress_Max : (int)((double)Current / (double)Total * Progress_Max));
    }
    else
        ProgressBar->setRange(0, 0); // Unknown
    Progress_Text(Operations_Canceled ? tr("Canceling...") : Text);
}

//---------------------------------------------------------------------------
void GUI_Main::OnJob_Cancel()
{
    Operations_Cancel();
}

//---------------------------------------------------------------------------
void GUI_Main::Operations_Cancel()
{
    if (!Job || Operations_Canceled)
        return;
    Operations_Canceled = true;

    // The other operations of the batch are dropped
    Operations.clear();

    // The dialog stays until the job has finished
    Job->Cancel();
    Log->Add(tr("Canceled by the user") + QLatin1Char('\n'));
    if (ProgressCancel)
        ProgressCancel->setEnabled(false);
    Progress_Text(tr("Canceling..."));

    // A question of the job is no more relevant, its answer is no
    if (Question)
        Question->reject();
}

//---------------------------------------------------------------------------
void GUI_Main::OnJob_Finished()
{
    if (!Job || !Job->isFinished())
        return;

    // Results
    Log_Move();
    Operation_Finish(Operation_Current);

    // The job is destroyed before the next one is created
    delete Job;
    Job = nullptr;

    // Next operation, without return to the event loop: no moment without operation running before the end of the batch
    if (Operations_Canceled)
        Operations.clear();
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::Operation_Finish(const operation& Operation)
{
    if (!Job || Operation.Package >= Packages.size())
        return;
    gui_package& Package = Packages[Operation.Package];
    const bool IsOk = !Job->ExitCode;

    // Error messages of the library
    QString Errors;
    if (!IsOk)
    {
        Errors = Messages_Get(Job->Err, Message_Error);
        const QString Errors_Out = Messages_Get(Job->Out, Message_Error); // Some errors are in the standard output
        if (!Errors.isEmpty() && !Errors_Out.isEmpty())
            Errors += QLatin1Char('\n');
        Errors += Errors_Out;
    }

    // Stopped by the user: a cancellation, or no as answer to a question of the job when the job has no error message about its end
    const bool IsCanceled = !IsOk && (Job->IsCanceled() || (Job_Refused && Errors.isEmpty()));

    // A package of several files is checked file by file, its status is then about all of them
    const bool IsPart = Operation.Kind == operation::Check && Operation.Input < (size_t)Package.Inputs.size();
    const QString Messages_Before = Package.Messages;

    if (!IsOk && !IsCanceled)
    {
        // The reason of the failure is in the error messages of the library, else in what was written last
        QString Failed;
        switch (Operation.Kind)
        {
            case operation::Analyze : Failed = tr("Analysis failed"); break;
            case operation::Edit    : Failed = tr("Metadata not saved"); break;
            case operation::Encode  : Failed = tr("Encoding failed"); break;
            case operation::Decode  : Failed = tr("Decoding failed"); break;
            case operation::Check   : Failed = tr("Check failed"); break;
            case operation::Command : Failed = tr("Command of the encoder not available"); break;
        }
        if (Errors.isEmpty())
        {
            Package.Status = Failed;
            Errors = Failed + QLatin1Char('.');
            const QString Last = Messages_Last(Job->Err);
            if (!Last.isEmpty())
                Errors += QLatin1Char('\n') + Last;
        }
        else
            Package.Status = Errors.section(QLatin1Char('\n'), 0, 0);
        Package.IsError = true;
        Package.Messages = Errors;

        // For the message at the end of the batch
        Operations_Errors++;
        if (!Operations_ErrorText.isEmpty())
            Operations_ErrorText += QStringLiteral("\n\n");
        if (Operations_Count > 1)
            Operations_ErrorText += Operation_Title(Operation) + QStringLiteral(":\n");
        Operations_ErrorText += Errors;
    }
    else if (IsCanceled && Operation.Kind != operation::Command)
    {
        switch (Operation.Kind)
        {
            case operation::Analyze : Package.Status = Package.Kind == gui_package::Kind_Unknown ? tr("Not analyzed") : tr("Analysis canceled"); break;
            case operation::Edit    : Package.Status = tr("Saving of the metadata canceled"); break;
            case operation::Encode  : Package.Status = tr("Encoding canceled"); break;
            case operation::Decode  : Package.Status = tr("Decoding canceled"); break;
            default                 : Package.Status = tr("Check canceled");
        }
        Package.IsError = false;
        Package.Messages = Messages_Get(Job->Err, Message_Warning | Message_Info);
    }

    switch (Operation.Kind)
    {
        case operation::Analyze :
            if (IsOk)
            {
                // Fields whose edit failed stay modified
                struct modified
                {
                    QString     FileName;
                    QString     Group;
                    QString     Name;
                    QString     NewValue;
                };
                std::vector<modified> Kept;
                if (Operation.Modified_Keep)
                    for (const gui_track& Track : Package.Tracks)
                        for (const gui_field& Field : Track.Fields)
                            if (Field.IsModified)
                                Kept.push_back({ Track.FileName, Field.Group, Field.Name, Field.NewValue });

                // Content, the modified fields of the previous content are dropped
                Package.Kind = Job->IsCompressed ? gui_package::Kind_Compressed : gui_package::Kind_Uncompressed;
                Package.Content_Set(Job->Tracks, Job->Attachments);
                Package.Info = Job->Info;
                for (const modified& Modified : Kept)
                    for (gui_track& Track : Package.Tracks)
                        if (Track.FileName == Modified.FileName)
                            for (gui_field& Field : Track.Fields)
                                if (Field.IsEditable && Field.Group == Modified.Group && Field.Name == Modified.Name
                                 && (Field.Kind != gui_field::Kind_Same || Field.First != Modified.NewValue))
                                {
                                    Field.IsModified = true;
                                    Field.NewValue = Modified.NewValue;
                                }

                // Status, the one of a failed edit is kept
                if (!Operation.Edit_Failed)
                {
                    Package.Status = Messages_Get(Job->Err, Message_Warning).isEmpty() ? tr("Analyzed") : tr("Analyzed, with warnings");
                    Package.IsError = false;
                    Package.Messages = Messages_Get(Job->Err, Message_Warning | Message_Info);
                }
            }
            else if (!IsCanceled)
            {
                // The content is not known
                Package.Kind = gui_package::Kind_Unknown;
                Package.Tracks.clear();
                Package.Attachments.clear();
                Package.Info.clear();
            }
            break;
        case operation::Edit :
            if (IsOk)
            {
                // The values are in the files (the analysis which follows reads them back)
                if (Operation.Track < Package.Tracks.size())
                    for (gui_field& Field : Package.Tracks[Operation.Track].Fields)
                        if (Field.IsModified)
                        {
                            Field.First = Field.NewValue;
                            Field.Last = Field.NewValue;
                            Field.Kind = gui_field::Kind_Same;
                            Field.IsModified = false;
                            Field.NewValue.clear();
                        }
            }
            else if (!IsCanceled)
            {
                // The analysis which follows must not hide this failure
                for (operation& Next : Operations)
                    if (Next.Kind == operation::Analyze && Next.Package == Operation.Package)
                    {
                        Next.Edit_Failed = true;
                        break;
                    }
            }
            break;
        case operation::Encode :
            if (IsOk)
            {
                Package.Status = tr("Encoded: %1").arg(Operation.Output);
                const QString Checked = Message_Check(Job->Out);
                if (!Checked.isEmpty())
                    Package.Status += QStringLiteral(". ") + Checked;
                Package.IsError = false;
                Package.Messages = Messages_Get(Job->Err, Message_Warning | Message_Info);
            }
            else if (!Operation.Output_Existed && !Operation.Output.isEmpty())
                QFile::remove(Operation.Output); // Incomplete
            break;
        case operation::Decode :
            if (IsOk)
            {
                // Where the files are: as the library says it, else as it names the directories
                QStringList Directories = Message_Directories(Job->Out);
                if (Directories.isEmpty())
                {
                    if (!Operation.Output.isEmpty())
                        Directories.push_back(Operation.Output);
                    else if (!Package.IsDirectory)
                        for (const QString& Input : Package.Inputs)
                            Directories.push_back(Input + QStringLiteral(".RAWcooked"));
                }
                if (Directories.isEmpty())
                    Package.Status = tr("Decoded");
                else
                    Package.Status = tr("Decoded: %1").arg(Directories.size() == 1 ? Directories.first() : Directories.first() + QStringLiteral(", ..."));
                Package.IsError = false;
                Package.Messages = Messages_Get(Job->Err, Message_Warning | Message_Info);
            }
            break;
        case operation::Check :
            if (IsOk)
            {
                Package.Status = Message_Check(Job->Out);
                if (Package.Status.isEmpty())
                    Package.Status = tr("Checked");
                Package.IsError = false;
                Package.Messages = Messages_Get(Job->Err, Message_Warning | Message_Info);
            }
            break;
        case operation::Command :
            if (IsOk)
                Operations_Command = Job->Out.trimmed();
            break;
    }

    // Result for this file, with the results for the previous files of the package: the failures first, they are what must be seen
    if (IsPart)
    {
        if (!Operation.Input)
        {
            Check_Failed.clear();
            Check_Passed.clear();
        }
        const QString Name = QFileInfo(Package.Inputs.at((int)Operation.Input)).fileName() + QStringLiteral(": ");
        QString Details = Package.Messages;
        if (!Details.startsWith(Package.Status))
            Details = Details.isEmpty() ? Package.Status : (Package.Status + QLatin1Char('\n') + Details);
        QString& Results = Package.IsError ? Check_Failed : Check_Passed;
        if (!Results.isEmpty())
            Results += QLatin1Char(' ');
        Results += Name + Package.Status;
        Package.Status = Check_Failed;
        if (!Check_Failed.isEmpty() && !Check_Passed.isEmpty())
            Package.Status += QLatin1Char(' ');
        Package.Status += Check_Passed;
        Package.IsError = !Check_Failed.isEmpty();
        Package.Messages = Name + Details;
        if (Operation.Input)
            Package.Messages = Messages_Before + QLatin1Char('\n') + Package.Messages;
    }
}

//---------------------------------------------------------------------------
void GUI_Main::Operations_End()
{
    // Progress
    Timer->stop();
    if (ProgressDialog)
    {
        ProgressDialog->hide();
        ProgressDialog->deleteLater();
        ProgressDialog = nullptr;
        ProgressBar = nullptr;
        ProgressCancel = nullptr;
    }

    // The batch is finished before anything is shown about it
    const size_t Errors = Operations_Errors;
    const bool Canceled = Operations_Canceled;
    const QString Name = Operations_Name;
    const QString ErrorText = Operations_ErrorText;
    const QString Command = Operations_Command;
    const QString Command_Output = Operation_Current.Output;
    QString Message = Operations_Message;
    if (Operations_Count == 1 && Operation_Current.Kind != operation::Command && Operation_Current.Package < Packages.size())
        Message = tr("%1: %2").arg(Packages[Operation_Current.Package].Name(), Packages[Operation_Current.Package].Status); // The command does not change the status
    Operations_Count = 0;
    Operations_Pos = 0;
    Operations_Errors = 0;
    Operations_Canceled = false;
    Operations_Name.clear();
    Operations_Message.clear();
    Operations_ErrorText.clear();
    Operations_Command.clear();

    // Views
    View_Refresh();

    // The window was waiting for the end of the job
    if (Close_Requested)
    {
        Close_Requested = false;
        QMetaObject::invokeMethod(this, &QWidget::close, Qt::QueuedConnection); // Not from here: the request may come from a close event still in progress
        return;
    }

    // Result
    if (Errors)
        Errors_Show(Name, ErrorText);
    else if (!Command.isEmpty())
        Command_Show(Command, Command_Output);
    else if (Canceled)
        Status_Message(tr("Canceled"), 5000);
    else if (!Message.isEmpty())
        Status_Message(Message, 5000);
}

//---------------------------------------------------------------------------
QMessageBox::StandardButton GUI_Main::Job_Ask(const QString& Text, const QString& Details, QMessageBox::StandardButtons Buttons)
{
    // The job waits for the answer in its thread, it can not finish meanwhile
    // The question is above the progress, which is above the window
    QWidget* Parent = this;
    if (ProgressDialog && ProgressDialog->isVisible())
        Parent = ProgressDialog;
    QPointer<QMessageBox> Box = new QMessageBox(QMessageBox::Question, QStringLiteral("RAWcooked"), Text, Buttons, Parent);
    Box->setTextFormat(Qt::PlainText); // Names of files are not rich text
    Box->setDetailedText(Details);
    Box->setDefaultButton(QMessageBox::No);
    Box->setEscapeButton(QMessageBox::No);
    Box->setWindowIcon(windowIcon());
    Question = Box;
    Box->exec();
    QMessageBox::StandardButton Answer = QMessageBox::No;
    if (Box)
    {
        if (Box->clickedButton())
            Answer = Box->standardButton(Box->clickedButton());
        delete Box;
    }

    // Canceled while the question was shown
    if (!Job || Operations_Canceled || Job->IsCanceled())
        return QMessageBox::No;

    // The log has what was asked and answered
    QString Log_Text = Text;
    switch (Answer)
    {
        case QMessageBox::Yes       : Log_Text += QLatin1Char(' ') + tr("Yes"); break;
        case QMessageBox::YesToAll  : Log_Text += QLatin1Char(' ') + tr("Yes to all"); break;
        case QMessageBox::NoToAll   : Log_Text += QLatin1Char(' ') + tr("No to all"); break;
        default                     : Log_Text += QLatin1Char(' ') + tr("No");
    }
    Log->Add(Log_Text + QLatin1Char('\n'));
    return Answer;
}

//---------------------------------------------------------------------------
bool GUI_Main::Job_Ask_Overwrite(const QString& FileName, const QString& ExtraText, bool CanApplyToAll, bool& ApplyToAll)
{
    ApplyToAll = false;
    if (!Job || Operations_Canceled || Job->IsCanceled())
        return false;
    Log_Move(); // The question comes after what the job already wrote

    QMessageBox::StandardButtons Buttons = QMessageBox::Yes | QMessageBox::No;
    if (CanApplyToAll)
        Buttons |= QMessageBox::YesToAll | QMessageBox::NoToAll;
    switch (Job_Ask(tr("File '%1' already exists%2. Overwrite?").arg(FileName, ExtraText), QString(), Buttons))
    {
        case QMessageBox::Yes       : return true;
        case QMessageBox::YesToAll  : ApplyToAll = true;
                                      return true;
        case QMessageBox::NoToAll   : ApplyToAll = true;
                                      return false;
        default                     : return false;
    }
}

//---------------------------------------------------------------------------
bool GUI_Main::Job_Ask_Question(const QString& Text)
{
    if (!Job || Operations_Canceled || Job->IsCanceled())
        return false;
    Log_Move(); // The question comes after what the job already wrote

    // The question may be about warnings the job just wrote (the log is not visible while the question is shown)
    const QStringList Warnings = Messages_Get(Job_Text, Message_Warning).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    Job_Text.clear();
    QString Text_Full;
    QString Details;
    if (Warnings.size() > 12)
    {
        Text_Full = QStringList(Warnings.mid(0, 10)).join(QLatin1Char('\n')) + QStringLiteral("\n...\n\n");
        Details = Warnings.join(QLatin1Char('\n'));
    }
    else if (!Warnings.isEmpty())
        Text_Full = Warnings.join(QLatin1Char('\n')) + QStringLiteral("\n\n");
    Text_Full += Text;

    const bool IsYes = Job_Ask(Text_Full, Details, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
    if (!IsYes)
        Job_Refused = true; // If the job stops there, it is the choice of the user, not a failure
    return IsYes;
}

//---------------------------------------------------------------------------
void GUI_Main::Errors_Show(const QString& Name, const QString& Text)
{
    QDialog ErrorDialog(this);
    ErrorDialog.setWindowTitle(tr("RAWcooked - Errors"));
    ErrorDialog.setWindowIcon(windowIcon());
    ErrorDialog.setModal(true);

    QVBoxLayout* Layout = new QVBoxLayout(&ErrorDialog);

    // Message
    QLabel* MessageLabel = new QLabel(tr("The following error(s) have occurred during the %1:").arg(Name), &ErrorDialog);
    Layout->addWidget(MessageLabel);

    // Error text
    QPlainTextEdit* ErrorTextEdit = new QPlainTextEdit(&ErrorDialog);
    ErrorTextEdit->setReadOnly(true);
    ErrorTextEdit->setFont(Font_Fixed());
    ErrorTextEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); // A line with a long name of file is wrapped: its end says what the problem is
    ErrorTextEdit->setPlainText(Text);
    Layout->addWidget(ErrorTextEdit);

    // Buttons
    QHBoxLayout* ButtonLayout = new QHBoxLayout;
    QPushButton* LogButton = new QPushButton(tr("Show &log"), &ErrorDialog);
    connect(LogButton, &QPushButton::clicked, &ErrorDialog, &QDialog::accept);
    ButtonLayout->addWidget(LogButton);
    ButtonLayout->addStretch(1);
    QPushButton* CloseButton = new QPushButton(tr("&Close"), &ErrorDialog);
    CloseButton->setDefault(true);
    connect(CloseButton, &QPushButton::clicked, &ErrorDialog, &QDialog::reject);
    ButtonLayout->addWidget(CloseButton);
    Layout->addLayout(ButtonLayout);

    // Size of the dialog: the one of the text, limited by the screen
    const QScreen* Screen = screen();
    const int MaxWidth = Screen ? (Screen->availableGeometry().width() * 2 / 3) : 800;
    const int MaxHeight = Screen ? (Screen->availableGeometry().height() * 2 / 4) : 600;
    const QFontMetrics Metrics(ErrorTextEdit->font());
    const QSize TextSize = Metrics.size(0, Text);
    const int Width = qBound(480, TextSize.width() + 100, qMax(480, MaxWidth));
    int Height = TextSize.height() + 150;
    const QStringList Lines = Text_Lines(Text);
    for (const QString& Line : Lines)
        Height += Metrics.lineSpacing() * (Metrics.horizontalAdvance(Line) / (Width - 100)); // Wrapped lines
    ErrorDialog.resize(Width, qBound(240, Height, qMax(240, MaxHeight)));

    if (ErrorDialog.exec() == QDialog::Accepted)
        View_Refresh(View_Log);
}

//---------------------------------------------------------------------------
void GUI_Main::Command_Show(const QString& Command, const QString& Output)
{
    QDialog CommandDialog(this);
    CommandDialog.setWindowTitle(tr("RAWcooked - Command of the encoder"));
    CommandDialog.setWindowIcon(windowIcon());
    CommandDialog.setModal(true);

    QVBoxLayout* Layout = new QVBoxLayout(&CommandDialog);

    // Message
    QLabel* MessageLabel = new QLabel(tr("FFmpeg command which would be used for the encoding:"), &CommandDialog);
    Layout->addWidget(MessageLabel);

    // Command
    QPlainTextEdit* CommandTextEdit = new QPlainTextEdit(&CommandDialog);
    CommandTextEdit->setReadOnly(true);
    CommandTextEdit->setFont(Font_Fixed());
    CommandTextEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    CommandTextEdit->setPlainText(Command);
    CommandTextEdit->setMinimumHeight(CommandTextEdit->fontMetrics().lineSpacing() * 12);
    Layout->addWidget(CommandTextEdit);

    // The command does not work without the file written for it
    QString FileName = Options.Value(QStringLiteral("ReversibilityFileName"));
    QLabel* NoteLabel = new QLabel(&CommandDialog);
    if (FileName.isEmpty())
    {
        FileName = Output + QStringLiteral(".rawcooked_reversibility_data");
        NoteLabel->setText(tr("The reversibility data file was written next to the output file, the command needs it:"));
    }
    else
        NoteLabel->setText(tr("The reversibility data file was written, the command needs it:"));
    NoteLabel->setWordWrap(true);
    Layout->addWidget(NoteLabel);
    QLabel* FileLabel = new QLabel(QDir::toNativeSeparators(FileName), &CommandDialog);
    FileLabel->setTextFormat(Qt::PlainText);
    FileLabel->setWordWrap(true);
    FileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    Layout->addWidget(FileLabel);

    // Buttons
    QHBoxLayout* ButtonLayout = new QHBoxLayout;
    QPushButton* CopyButton = new QPushButton(tr("C&opy"), &CommandDialog);
    connect(CopyButton, &QPushButton::clicked, &CommandDialog, [Command]() { QGuiApplication::clipboard()->setText(Command); });
    ButtonLayout->addWidget(CopyButton);
    ButtonLayout->addStretch(1);
    QPushButton* CloseButton = new QPushButton(tr("&Close"), &CommandDialog);
    CloseButton->setDefault(true);
    connect(CloseButton, &QPushButton::clicked, &CommandDialog, &QDialog::reject);
    ButtonLayout->addWidget(CloseButton);
    Layout->addLayout(ButtonLayout);

    // Size of the dialog
    const QScreen* Screen = screen();
    const int MaxWidth = Screen ? (Screen->availableGeometry().width() * 2 / 3) : 800;
    CommandDialog.resize(qMin(qMax(480, MaxWidth), QFontMetrics(CommandTextEdit->font()).averageCharWidth() * 110), CommandDialog.sizeHint().height());

    CommandDialog.exec();
}

//---------------------------------------------------------------------------
void GUI_Main::OnFiles_Track_Activated(size_t Package, size_t Track)
{
    View_Refresh(View_Metadata);
    Metadata->Track_Show(Package, Track);
}
