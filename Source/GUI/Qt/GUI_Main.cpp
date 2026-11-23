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
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
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
#if defined(Q_OS_WIN)
    #include <qt_windows.h>
#endif
//---------------------------------------------------------------------------

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
// Range of the progress bar, and time between two readings of the progress of a job, in ms
static const int Progress_Max = 1000;
static const int Progress_Period = 100;

//---------------------------------------------------------------------------
// Count of warnings shown with a question of a job, the other ones are in its details
static const int Warnings_Max = 12;
static const int Warnings_Short = 10;

//---------------------------------------------------------------------------
// Room kept on the screen around the window when it is first shown, for its frame
static const QSize Screen_Margin(40, 80);

//---------------------------------------------------------------------------
// Count of characters of the name of a package in a menu
static const int Menu_Text_Max = 60;

//---------------------------------------------------------------------------
// Opening: count of paths of a same directory from which the list of the directory is read,
// and count of entries read from this list for each of these paths
static const int Listing_Min = 64;
static const int Listing_Factor = 16;

//---------------------------------------------------------------------------
// Dialog with a text in a fixed font: count of characters of its lines (longer ones are wrapped) and count of its lines
static const int Text_Columns_Min = 60;
static const int Text_Columns_Max = 100;
static const int Text_Lines_Min = 6;
static const int Text_Lines_Max = 30;

//---------------------------------------------------------------------------
// Text of a part of a menu: not too long, and & is not a shortcut
static QString Menu_Text(QString Text)
{
    if (Text.size() > Menu_Text_Max)
        Text = Text.left(Menu_Text_Max / 2 - 2) + QStringLiteral("...") + Text.right(Menu_Text_Max / 2 - 2);
    return Text.replace(QLatin1Char('&'), QStringLiteral("&&"));
}

//---------------------------------------------------------------------------
// Name of a file or of a directory in a message
static QString Path_Name(const QString& Path)
{
    const QString Name = QFileInfo(Path).fileName();
    return Name.isEmpty() ? QDir::toNativeSeparators(Path) : Name; // A root has no name
}

//---------------------------------------------------------------------------
// Status naming a file or a directory: the views cut a status at its end, the name comes before its directory
static QString Status_File(const QString& Result, const QString& Path)
{
    const QFileInfo Info(Path);
    if (Info.fileName().isEmpty())
        return Result + QStringLiteral(": ") + QDir::toNativeSeparators(Path);
    return GUI_Main::tr("%1: %2 (in %3)").arg(Result, Info.fileName(), QDir::toNativeSeparators(Info.absolutePath()));
}

//---------------------------------------------------------------------------
// Status with the names of the files of a package: the directory which holds the package is not repeated
static QString Status_Short(const QString& Status, const gui_package& Package)
{
    if (Package.Inputs.isEmpty() || QFileInfo(Package.Inputs.first()).fileName().isEmpty())
        return Status; // A root has no name to keep
    // A directory package: its name stays; the files of a package of files: in the directory of the first one
    QString Directory = QDir::toNativeSeparators(Package.IsDirectory ? Package.Inputs.first() : QFileInfo(Package.Inputs.first()).absolutePath());
    if (!Directory.endsWith(QDir::separator()))
        Directory += QDir::separator();
    const QString Name = Package.IsDirectory ? QFileInfo(Package.Inputs.first()).fileName() + QDir::separator() : QString();
    return QString(Status).replace(Directory, Name, gui_package::Path_Sensitivity());
}

//---------------------------------------------------------------------------
// First line of a text
static QString Line_First(const QString& Text)
{
    return Text.section(QLatin1Char('\n'), 0, 0).trimmed();
}

//---------------------------------------------------------------------------
// Arguments as on a command line, license keys hidden
static QString Arguments_Text(const QStringList& Arguments)
{
    QStringList Result;
    bool IsKey = false;
    for (const QString& Argument : Arguments)
    {
        if (IsKey)
            Result += QStringLiteral("***");
        else
            Result += Argument.contains(QLatin1Char(' ')) ? QLatin1Char('"') + Argument + QLatin1Char('"') : Argument;
        IsKey = Argument == QLatin1String("--license") || Argument == QLatin1String("--licence") || Argument == QLatin1String("--store-license") || Argument == QLatin1String("--store-licence");
    }
    return Result.join(QLatin1Char(' '));
}

//---------------------------------------------------------------------------
// Inputs of a package in a line of the log: a package may have thousands of files
static QString Inputs_Text(const QStringList& Inputs)
{
    if (Inputs.size() <= 1)
        return Inputs.value(0);
    return Inputs.size() == 2 ? GUI_Main::tr("%1 and 1 other file").arg(Inputs.first()) : GUI_Main::tr("%1 and %2 other files").arg(Inputs.first(), QString::number(Inputs.size() - 1));
}

//---------------------------------------------------------------------------
// Text of a button, without the mark of its key
static QString Button_Text(const QAbstractButton* Button)
{
    QString Text;
    const QString Source = Button->text();
    for (int Pos = 0; Pos < Source.size(); Pos++)
    {
        if (Source.at(Pos) == QLatin1Char('&') && ++Pos >= Source.size())
            break;
        Text += Source.at(Pos);
    }
    return Text;
}

//---------------------------------------------------------------------------
// The weakest result of checks
static gui_compressed::checked Checked_Min(gui_compressed::checked Left, gui_compressed::checked Right)
{
    return Left < Right ? Left : Right;
}

//---------------------------------------------------------------------------
// Name of an existing directory as the system has it: links are followed. Empty if the system does not give it
static QString Directory_System(const QString& Directory)
{
    #if defined(Q_OS_WIN)
        // Qt keeps the junctions, the short names and the case as they are given; the system has the real ones.
        // The form of the name which has no limit of length is the one given to the system
        const QString Separators(2, QLatin1Char('\\'));
        const QString Long = Separators + QStringLiteral("?\\");
        const QString Long_Server = Long + QStringLiteral("UNC\\");
        QString Name = QDir::toNativeSeparators(Directory);
        Name = Name.startsWith(Separators) ? Long_Server + Name.mid(Separators.size()) : Long + Name;
        const HANDLE Handle = CreateFileW((const wchar_t*)Name.utf16(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (Handle != INVALID_HANDLE_VALUE)
        {
            QString Result;
            const DWORD Size = GetFinalPathNameByHandleW(Handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS); // With the end of the text
            if (Size)
            {
                std::vector<wchar_t> Buffer(Size);
                const DWORD Length = GetFinalPathNameByHandleW(Handle, Buffer.data(), Size, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                if (Length && Length < Size)
                    Result = QString::fromWCharArray(Buffer.data(), (int)Length);
            }
            CloseHandle(Handle);
            if (Result.startsWith(Long_Server))
                return QDir::fromNativeSeparators(Separators + Result.mid(Long_Server.size()));
            if (Result.startsWith(Long))
                return QDir::fromNativeSeparators(Result.mid(Long.size()));
        }
    #endif
    return QFileInfo(Directory).canonicalFilePath();
}

//---------------------------------------------------------------------------
// A name in a directory
static QString Path_Add(const QString& Directory, const QString& Name)
{
    return Directory.endsWith(QLatin1Char('/')) ? Directory + Name : Directory + QLatin1Char('/') + Name;
}

//---------------------------------------------------------------------------
// A name for comparing: on Windows the case makes no difference
static QString Name_Fold(const QString& Name)
{
    #if defined(Q_OS_WIN)
        return Name.toCaseFolded();
    #else
        return Name;
    #endif
}

//---------------------------------------------------------------------------
// Which paths are directories, for the paths which are many in a same directory: read from the list of this directory,
// much faster than a question to the system for each path. Not all the paths are in the result
static QHash<QString, bool> Paths_Kinds(const QStringList& Paths)
{
    QHash<QString, int> Counts;
    for (const QString& Path : Paths)
        Counts[QFileInfo(Path).absolutePath()]++;
    QHash<QString, bool> Kinds;
    for (auto Count = Counts.constBegin(); Count != Counts.constEnd(); ++Count)
    {
        if (Count.value() < Listing_Min)
            continue;
        QDirIterator Entries(Count.key(), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (int Left = Count.value() * Listing_Factor; Left && Entries.hasNext(); Left--) // Not the whole list of a directory which has much more than what is asked
        {
            Entries.next();
            Kinds.insert(Name_Fold(Entries.filePath()), Entries.fileInfo().isDir());
        }
    }
    return Kinds;
}

//---------------------------------------------------------------------------
// Name of a directory for comparing: as the system has it, and from its parent directory if it does not exist
static QString Directory_Key(const QString& Directory, QHash<QString, QString>& Cache)
{
    const auto Known = Cache.constFind(Directory);
    if (Known != Cache.constEnd())
        return Known.value();
    QString Key = Directory_System(Directory);
    if (Key.isEmpty())
    {
        const QFileInfo Info(Directory);
        const QString Parent = Info.absolutePath();
        Key = Info.fileName().isEmpty() || Parent == Directory ? Directory : Path_Add(Directory_Key(Parent, Cache), Info.fileName());
    }
    Cache.insert(Directory, Key);
    return Key;
}

//***************************************************************************
// Status bar
//***************************************************************************

//---------------------------------------------------------------------------
namespace
{

// Text which stays in the status bar: cut at its end, with a mark, when the place is missing
class status_label : public QLabel
{
public:
    explicit status_label(QWidget* Parent) : QLabel(Parent) {}

    QSize minimumSizeHint() const override
    {
        return QSize(0, QLabel::minimumSizeHint().height());
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter Painter(this);
        const QRect Rect = contentsRect();
        Painter.drawText(Rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, fontMetrics().elidedText(text(), Qt::ElideRight, Rect.width()));
    }
};

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

    // Menu and ToolBar
    Menu_Create();
    ToolBar_Create();

    // Status bar: the last result and the counts stay, the status tips of the actions are at their left
    setStatusBar(new QStatusBar());
    const int Margin = fontMetrics().averageCharWidth() * 3; // The texts do not touch each other
    Status_Result = new status_label(this);
    Status_Result->setTextFormat(Qt::PlainText);
    Status_Result->setContentsMargins(Margin, 0, 0, 0);
    statusBar()->addPermanentWidget(Status_Result);
    Status_Counts = new QLabel(this);
    Status_Counts->setContentsMargins(Margin, 0, 0, 0);
    statusBar()->addPermanentWidget(Status_Counts);
    connect(statusBar(), &QStatusBar::messageChanged, this, &GUI_Main::OnStatus_MessageChanged);

    // Progress of the operations
    Timer = new QTimer(this);
    connect(Timer, &QTimer::timeout, this, &GUI_Main::OnJob_Timer);

    // Views
    connect(Files, &GUI_Main_Files::Selection_Changed, this, &GUI_Main::Menu_Update);
    connect(Files, &GUI_Main_Files::Track_Activated, this, &GUI_Main::OnFiles_Track_Activated);
    connect(Files, &GUI_Main_Files::Close_Requested, this, &GUI_Main::OnMenu_File_Close_Files);
    connect(Metadata, &GUI_Main_Metadata::Modified, this, &GUI_Main::OnMetadata_Modified);

    // Window
    setWindowTitle(QStringLiteral("RAWcooked"));
    setMinimumSize(800, 500);
    QSize Size(1100, 650);
    Size.setHeight(qMax(Size.height(), sizeHint().height())); // High enough for the whole tool bar, if the screen is
    QScreen* Screen = screen();
    if (!Screen)
        Screen = QGuiApplication::primaryScreen();
    if (Screen)
    {
        const QRect Available = Screen->availableGeometry();
        Size = Size.boundedTo(Available.size() - Screen_Margin).expandedTo(minimumSize());
        setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, Size, Available));
    }
    else
        resize(Size);

    // Drag n drop
    setAcceptDrops(true);

    // Content
    View_Refresh();
    View_Refresh(View_Files);
}

//---------------------------------------------------------------------------
GUI_Main::~GUI_Main()
{
    // Not expected (the window is not closed while an operation runs): the job is stopped while the window it asks its questions to is still there
    if (Job)
    {
        Job->disconnect(this);
        Job->Cancel(); // The questions of the job are answered no
        Job->wait();
        if (Operation_Current.Kind == operation::Encode && Job->IsEncoderStarted && !Job->IsEncoded)
            Output_Remove(Operation_Current); // Incomplete
        delete Job;
        Job = nullptr;
    }
}

//***************************************************************************
// View_Refresh
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::View_Refresh(view View_New)
{
    // Another view is shown: the views have their content
    if (View_New != View_None)
    {
        switch (View_New)
        {
            case View_Metadata  : Views->setCurrentWidget(Metadata); Menu_View_Metadata->setChecked(true); break;
            case View_Log       : Views->setCurrentWidget(Log); Menu_View_Log->setChecked(true); break;
            default             : if (Files_IsOutdated)
                                  {
                                      Files_IsOutdated = false;
                                      Files->Refresh();
                                  }
                                  Views->setCurrentWidget(Files);
                                  Menu_View_Files->setChecked(true);
        }
        return;
    }

    // Content
    Files_IsOutdated = false;
    Files->Refresh();
    Metadata->Refresh();

    // Menu
    Menu_Update();
}

//---------------------------------------------------------------------------
void GUI_Main::OnMetadata_Modified()
{
    // The Files view has the count of modified fields of each package: it is filled again now if it is shown, else when it is shown
    if (Views->currentWidget() == Files)
        Files->Refresh();
    else
        Files_IsOutdated = true;

    Menu_Update();
}

//***************************************************************************
// Status bar
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::Status_Set(const QString& Text)
{
    Status_Result->setText(Text);
    Status_Result->setToolTip(Text.isEmpty() ? QString() : QStringLiteral("<span>") + Text.toHtmlEscaped() + QStringLiteral("</span>")); // The whole text, on several lines if it is long
    Status_Update();
}

//---------------------------------------------------------------------------
void GUI_Main::Status_Update()
{
    // The status bar shows a status tip up to where the result starts, and would cut it there without a mark.
    // While a tip is shown it has the place it needs, the result keeps a third of the place at least; then the tip is shortened
    QStatusBar* Bar = statusBar();
    const QFontMetrics Metrics(Bar->font());
    const int Room = Bar->width() - Status_Counts->sizeHint().width() - Metrics.averageCharWidth() * 6;
    const int Tip = Status_Tip.isEmpty() ? 0 : (Metrics.horizontalAdvance(Status_Tip) + Metrics.averageCharWidth() * 2);
    const int Result_Max = qMax(qMax(Room - Tip, Room / 3), 0);
    Status_Result->setMaximumWidth(Result_Max);
    const QString Shown = Metrics.elidedText(Status_Tip, Qt::ElideRight, qMax(Room - qMin(Status_Result->sizeHint().width(), Result_Max), 0));
    if (Shown != Bar->currentMessage())
    {
        Status_Tip_Shown = Shown;
        Bar->showMessage(Shown);
    }
}

//---------------------------------------------------------------------------
void GUI_Main::OnStatus_MessageChanged(const QString& Message)
{
    // A status tip of an action, by the status bar itself: it gets its place
    if (Message == Status_Tip_Shown)
        return; // The one shortened here
    Status_Tip = Message;
    Status_Tip_Shown = Message;
    Status_Update();
}

//***************************************************************************
// Events
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::dragEnterEvent(QDragEnterEvent* Event)
{
    // Only local files can be opened
    if (IsBusy() || !Event->mimeData()->hasUrls())
        return;
    const QList<QUrl> Urls = Event->mimeData()->urls();
    for (const QUrl& Url : Urls)
        if (Url.isLocalFile())
        {
            Event->acceptProposedAction();
            return;
        }
}

//---------------------------------------------------------------------------
void GUI_Main::dropEvent(QDropEvent* Event)
{
    // Ignored while an operation is running
    if (IsBusy() || !Event->mimeData()->hasUrls())
        return;

    QStringList Paths;
    int NotLocal = 0;
    const QList<QUrl> Urls = Event->mimeData()->urls();
    for (const QUrl& Url : Urls)
    {
        const QString Path = Url.isLocalFile() ? Url.toLocalFile() : QString();
        if (Path.isEmpty())
            NotLocal++; // E.g. on a server which is not mounted
        else
            Paths.push_back(Path);
    }
    if (Paths.isEmpty())
        return;
    Event->acceptProposedAction();

    // After the end of the drop: the program the files come from waits for it
    QMetaObject::invokeMethod(this, [this, Paths, NotLocal]() { Open(Paths, NotLocal); }, Qt::QueuedConnection);
}

//---------------------------------------------------------------------------
void GUI_Main::closeEvent(QCloseEvent* Event)
{
    // An operation is running: it is canceled first, the window is closed when the job has finished
    if (IsBusy())
    {
        Event->ignore();
        if (Close_Requested || Close_Question)
            return;
        const QPointer<QMessageBox> Box = Message_Create(this, QMessageBox::Question, tr("An operation is running."), QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
                                                         { { QMessageBox::Yes, tr("&Cancel it and quit") }, { QMessageBox::No, tr("C&ontinue") } });
        Box->setInformativeText(tr("Do you want to cancel it and quit?"));
        Close_Question = Box;
        Box->exec();
        const bool IsYes = Box && Box->clickedButton() && Box->clickedButton() == Box->button(QMessageBox::Yes); // Closed without answer when the operations end meanwhile
        delete Box;
        if (!IsYes)
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

    // Unsaved modifications, also a value being typed
    if (!Action_Begin() || !Close(All(), true))
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

    Status_Update();
}

//***************************************************************************
// Paths
//***************************************************************************

//---------------------------------------------------------------------------
QString GUI_Main::Path_Key(const QString& Path, bool IsDirectory, path_cache* Cache)
{
    // A directory is named as the system names it, so that links, other spellings and other ways to it give the same key.
    // A file is its name in its directory: its directory is asked once for all its files
    path_cache Local;
    path_cache& Known = Cache ? *Cache : Local;
    const QFileInfo Info(QDir::cleanPath(QFileInfo(Path).absoluteFilePath()));
    QString Key;
    if (IsDirectory || Info.fileName().isEmpty())
        Key = Directory_Key(Info.filePath(), Known);
    else
    {
        Key = Path_Add(Directory_Key(Info.absolutePath(), Known), Info.fileName());
        #if defined(Q_OS_WIN)
            // It may be the short name of the file
            if (Info.fileName().contains(QLatin1Char('~')))
            {
                const QString Name = Directory_System(Info.filePath());
                if (!Name.isEmpty())
                    Key = Name;
            }
        #endif
    }
    return Name_Fold(Key);
}

//---------------------------------------------------------------------------
QString GUI_Main::Path_Key_Parent(const QString& Key)
{
    const auto Pos = Key.lastIndexOf(QLatin1Char('/'), -2);
    if (Pos < 0)
        return QString();
    return !Pos || Key.at(Pos - 1) == QLatin1Char(':') ? Key.left(Pos + 1) : Key.left(Pos); // The separator is a part of the name of a root
}

//***************************************************************************
// Actions
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main::Open(const QStringList& Paths)
{
    Open(Paths, 0);
}

//---------------------------------------------------------------------------
void GUI_Main::Open(const QStringList& Paths, int NotLocal)
{
    // While an operation is running the analysis is added to the current batch
    const bool Busy = IsBusy();
    if (!Busy && !Action_Begin())
        return; // A value being typed is not valid, the views are not filled again

    // What is asked: each directory is a package, all the files together are one package.
    // With many files nothing is searched in a list: the count of files of a sequence has no limit
    struct item
    {
        QString                 Name;
        QString                 Key;
        bool                    IsOpened = false;
    };
    std::vector<item> Directories, Files_New;
    path_cache Cache;
    QSet<QString> Keys;
    QString First;
    const QHash<QString, bool> Kinds = Paths_Kinds(Paths);
    for (const QString& Path : Paths)
    {
        if (Path.isEmpty())
            continue;
        QFileInfo Info(Path);
        #if defined(Q_OS_WIN)
            // A shortcut is opened as what it points to
            if (Path.endsWith(QLatin1String(".lnk"), Qt::CaseInsensitive) && Info.isShortcut() && !Info.symLinkTarget().isEmpty())
                Info.setFile(Info.symLinkTarget());
        #endif
        const auto Kind = Kinds.constFind(Name_Fold(Info.absoluteFilePath()));
        const bool IsDirectory = Kind != Kinds.constEnd() ? Kind.value() : Info.isDir();
        item Item;
        Item.Name = QDir::toNativeSeparators(QDir::cleanPath(Info.absoluteFilePath())); // Never a relative name: the library would take a name starting with - for an option
        Item.Key = Path_Key(Item.Name, IsDirectory, &Cache);
        if (First.isEmpty())
            First = Item.Name;
        if (Keys.contains(Item.Key))
            continue;
        Keys.insert(Item.Key);
        (IsDirectory ? Directories : Files_New).push_back(Item);
    }
    if (!First.isEmpty())
        Options.Directory_Set(QFileInfo(First).absolutePath());

    // What is open: the files, the directories, and the directories which hold them
    QSet<QString> Open_Files;
    QHash<QString, QString> Open_Directories, Open_Parents; // Name of the package, by key
    const auto Parents_Add = [&Open_Parents](const QString& Key, const QString& Package)
    {
        for (QString Parent = Path_Key_Parent(Key); !Parent.isEmpty() && !Open_Parents.contains(Parent); Parent = Path_Key_Parent(Parent))
            Open_Parents.insert(Parent, Package);
    };
    for (const gui_package& Package : Packages)
    {
        const QString Name = Package.Name();
        for (const QString& Input : Package.Inputs)
        {
            const QString Key = Path_Key(Input, Package.IsDirectory, &Cache);
            if (Package.IsDirectory)
                Open_Directories.insert(Key, Name);
            else
                Open_Files.insert(Key);
            Parents_Add(Key, Name);
        }
    }
    QHash<QString, QString> Inside_Known; // Name of the open directory which holds a directory, by key; empty if none
    const auto Inside = [&Open_Directories, &Inside_Known](const QString& Key)
    {
        const QString Directory = Path_Key_Parent(Key);
        const auto Known = Inside_Known.constFind(Directory);
        if (Known != Inside_Known.constEnd())
            return Known.value();
        QString Package;
        for (QString Parent = Directory; !Parent.isEmpty() && Package.isEmpty(); Parent = Path_Key_Parent(Parent))
            Package = Open_Directories.value(Parent);
        Inside_Known.insert(Directory, Package);
        return Package;
    };

    // The same content is not opened twice: not what is open or in a directory which is open, not a directory which holds something open.
    // The directories first, the ones which hold other ones before them: what they hold is then not opened
    enum reason
    {
        Reason_Open,
        Reason_Inside,
        Reason_Holds,
    };
    QStringList Refused;
    size_t Refused_Count = 0;
    const auto Refuse = [this, &Refused, &Refused_Count](reason Reason, const QString& Path, const QString& Package)
    {
        // The first one is named, the other ones are counted
        if (Refused_Count++)
            return;
        const QString Name = Path_Name(Path);
        switch (Reason)
        {
            case Reason_Open    : Refused.push_back(Package.isEmpty() || Package == Name ? tr("%1 is already open").arg(Name) : tr("%1 is already open, as %2").arg(Name, Package)); break; // Another way to the same directory
            case Reason_Inside  : Refused.push_back(tr("%1 is already open, in %2").arg(Name, Package)); break;
            case Reason_Holds   : Refused.push_back(tr("%1 is not opened: %2, which is open, is in it").arg(Name, Package)); break;
        }
    };
    std::vector<item*> Sorted;
    for (item& Directory : Directories)
        Sorted.push_back(&Directory);
    std::stable_sort(Sorted.begin(), Sorted.end(), [](const item* Left, const item* Right) { return Left->Key.size() < Right->Key.size(); });
    for (item* Directory : Sorted)
    {
        if (Open_Directories.contains(Directory->Key))
            Refuse(Reason_Open, Directory->Name, Open_Directories.value(Directory->Key));
        else if (!Inside(Directory->Key).isEmpty())
            Refuse(Reason_Inside, Directory->Name, Inside(Directory->Key));
        else if (Open_Parents.contains(Directory->Key))
            Refuse(Reason_Holds, Directory->Name, Open_Parents.value(Directory->Key));
        else
        {
            Directory->IsOpened = true;
            Open_Directories.insert(Directory->Key, Path_Name(Directory->Name));
            Parents_Add(Directory->Key, Path_Name(Directory->Name));
            Inside_Known.clear();
        }
    }

    // Packages, in the order they were asked
    std::vector<size_t> Added;
    for (const item& Directory : Directories)
    {
        if (!Directory.IsOpened)
            continue;
        gui_package Package;
        Package.Inputs.push_back(Directory.Name);
        Package.IsDirectory = true;
        Package.Status = tr("Not analyzed");
        Packages.push_back(Package);
        Added.push_back(Packages.size() - 1);
    }
    QStringList FileNames;
    for (const item& File : Files_New)
    {
        if (Open_Files.contains(File.Key))
            Refuse(Reason_Open, File.Name, QString());
        else if (!Inside(File.Key).isEmpty())
            Refuse(Reason_Inside, File.Name, Inside(File.Key));
        else
            FileNames.push_back(File.Name);
    }
    if (!FileNames.isEmpty())
    {
        gui_package Package;
        Package.Inputs = FileNames;
        Package.Status = tr("Not analyzed");
        Packages.push_back(Package);
        Added.push_back(Packages.size() - 1);
    }

    // What is not opened is said: now, or with the result of the operations
    if (Refused_Count > 1)
        Refused.push_back(Refused_Count == 2 ? tr("1 other item is not opened either") : tr("%1 other items are not opened either").arg(Refused_Count - 1));
    if (NotLocal)
        Refused.push_back(NotLocal == 1 ? tr("1 dropped item is not a local file") : tr("%1 dropped items are not local files").arg(NotLocal));
    const QString Note = Refused.join(QStringLiteral("; "));
    if (Added.empty() && !Busy)
    {
        if (!Note.isEmpty())
            Status_Set(Note);
        return;
    }

    // Packages, each one with its analysis
    if (!Added.empty())
        View_Refresh(); // Listed at once
    Operations_Begin(tr("opening"), tr("Opening finished"));
    if (!Note.isEmpty())
        Operations_Note = Operations_Note.isEmpty() ? Note : (Operations_Note + QStringLiteral("; ") + Note);
    Operations_Add(operation::Analyze, Added);
    Operations_Start();
}

//---------------------------------------------------------------------------
void GUI_Main::Menu_Update()
{
    // Everything which changes the content or uses the library waits for the end of the operations.
    // An action which can not be used says why
    const bool Busy = IsBusy();
    const QString Running = tr("Not available while an operation runs");
    const QString Nothing = Busy ? Running : tr("No package is open");

    // File
    Menu_Enable(Menu_File_Open_Files, !Busy, Running);
    Menu_Enable(Menu_File_Open_Directory, !Busy, Running);
    const std::vector<size_t> List = Selected(false);
    if (List.empty())
        Menu_File_Close_Files->setText(tr("&Close"));
    else if (List.size() == 1)
        Menu_File_Close_Files->setText(tr("&Close %1").arg(Menu_Text(Packages[List.front()].Name())));
    else
        Menu_File_Close_Files->setText(tr("&Close %1 packages").arg(List.size()));
    Menu_Enable(Menu_File_Close_Files, !Busy && !List.empty(), Busy || Packages.empty() ? Nothing : tr("Select a package in the Files view to close it"));
    Menu_Enable(Menu_File_Close_All, !Busy && !Packages.empty(), Nothing);
    size_t Modified = 0;
    for (const gui_package& Package : Packages)
        Modified += Package.Modified_Count();
    Menu_File_Save_All->setText(Modified ? tr("&Save modified metadata (%1)").arg(Modified) : tr("&Save modified metadata"));
    Menu_Enable(Menu_File_Save_All, !Busy && Modified != 0, Busy ? Running : tr("No modified metadata to save (values are edited in the Metadata view)"));

    // Actions
    const std::vector<size_t> Uncompressed = Selection_Encode();
    const bool HasCompressed = !Selection_Decode().empty();
    const QString NoEncode = Busy || Packages.empty() ? Nothing : (List.empty() ? tr("No package to encode") : tr("No package to encode in the selection"));
    const QString NoMatroska = Busy || Packages.empty() ? Nothing : (List.empty() ? tr("No Matroska file created by RAWcooked is open") : tr("No Matroska file created by RAWcooked in the selection"));
    Menu_Enable(Menu_Actions_Encode, !Busy && !Uncompressed.empty(), NoEncode);
    Menu_Enable(Menu_Actions_Decode, !Busy && HasCompressed, NoMatroska);
    Menu_Enable(Menu_Actions_Decode_To, !Busy && HasCompressed, NoMatroska);
    Menu_Enable(Menu_Actions_Check, !Busy && HasCompressed, NoMatroska);
    Menu_Enable(Menu_Actions_Check_Originals, !Busy && HasCompressed, NoMatroska);
    Menu_Enable(Menu_Actions_Analyze, !Busy && !Packages.empty(), Nothing);
    Menu_Enable(Menu_Actions_Command, !Busy && Uncompressed.size() == 1, !Busy && Uncompressed.size() > 1 ? tr("Select one package: the FFmpeg command is the one of a package") : NoEncode);

    // Options, Help
    Menu_Enable(Menu_Options_Preferences, !Busy, Running);
    Menu_Enable(Menu_Options_License, !Busy, Running);
    Menu_Enable(Menu_Help_Help, !Busy, Running);
    Menu_Enable(Menu_Help_About, !Busy, Running);

    // Status bar
    Status_Counts->setText((Packages.size() == 1 ? tr("1 package") : tr("%1 packages").arg(Packages.size()))
                         + QStringLiteral(", ")
                         + (Modified == 1 ? tr("1 modified field") : tr("%1 modified fields").arg(Modified)));
    Status_Update();
}

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
std::vector<size_t> GUI_Main::All() const
{
    std::vector<size_t> List(Packages.size());
    for (size_t Pos = 0; Pos < List.size(); Pos++)
        List[Pos] = Pos;
    return List;
}

//---------------------------------------------------------------------------
std::vector<size_t> GUI_Main::Selected(bool AllIfNone) const
{
    std::vector<size_t> List = Files->Selection();
    List.erase(std::remove_if(List.begin(), List.end(), [this](size_t Pos) { return Pos >= Packages.size(); }), List.end()); // The view may show a previous content
    std::sort(List.begin(), List.end());
    List.erase(std::unique(List.begin(), List.end()), List.end());
    if (List.empty() && AllIfNone)
        return All();
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
std::vector<size_t> GUI_Main::Selection_Encode() const
{
    std::vector<size_t> List = Selection(gui_package::Kind_Uncompressed);
    List.erase(std::remove_if(List.begin(), List.end(), [this](size_t Pos) { return !IsEncodable(Packages[Pos]); }), List.end());
    return List;
}

//---------------------------------------------------------------------------
std::vector<size_t> GUI_Main::Selection_Decode() const
{
    std::vector<size_t> List = Selection(gui_package::Kind_Compressed);
    List.erase(std::remove_if(List.begin(), List.end(), [this](size_t Pos) { return !IsDecodable(Packages[Pos]); }), List.end());
    return List;
}

//---------------------------------------------------------------------------
bool GUI_Main::IsEncodable(const gui_package& Package)
{
    // Content which the library refuses to encode, or nothing to encode
    return Package.Kind == gui_package::Kind_Uncompressed && !Package.IsNotEncodable && !Package.Tracks.empty();
}

//---------------------------------------------------------------------------
bool GUI_Main::IsDecodable(const gui_package& Package)
{
    // A Matroska file not made by RAWcooked can not be decoded to the original files
    if (Package.Kind != gui_package::Kind_Compressed)
        return false;
    for (const gui_compressed& Compressed : Package.Compressed)
        if (Compressed.HasReversibilityData)
            return true;
    return Package.Compressed.empty(); // Not known
}

//---------------------------------------------------------------------------
QStringList GUI_Main::Decodable_Files(const gui_package& Package)
{
    // The library finds the files of a directory, but fails on the ones not created by RAWcooked: then they are given one by one
    QStringList List;
    const bool IsAll = std::all_of(Package.Compressed.begin(), Package.Compressed.end(), [](const gui_compressed& Compressed) { return Compressed.HasReversibilityData; });
    if (Package.IsDirectory && IsAll)
        return List;
    const QDir Directory(Package.IsDirectory ? Package.Inputs.value(0) : QString());
    for (const gui_compressed& Compressed : Package.Compressed)
        if (Compressed.HasReversibilityData)
            List += QDir::toNativeSeparators(Package.IsDirectory ? Directory.absoluteFilePath(Compressed.FileName) : Compressed.FileName);
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
bool GUI_Main::Modified_Lose(const std::vector<size_t>& List, const QString& Consequence, const QString& Accept)
{
    const size_t Count = Modified_Count(List);
    if (!Count)
        return true;
    return Message_Ask(QMessageBox::Warning,
                       Count == 1 ? tr("1 modified field is not saved.") : tr("%1 modified fields are not saved.").arg(Count),
                       Consequence,
                       QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel,
                       { { QMessageBox::Yes, Accept } }) == QMessageBox::Yes;
}

//---------------------------------------------------------------------------
bool GUI_Main::Action_Begin()
{
    if (IsBusy())
        return false;

    // A value being edited is taken as if its editing was finished; a value which is not valid stays in its editor, which says why
    if (!Metadata->Editor_Close())
    {
        View_Refresh(View_Metadata);
        return false;
    }

    return !IsBusy();
}

//---------------------------------------------------------------------------
QMessageBox* GUI_Main::Message_Create(QWidget* Parent, QMessageBox::Icon Icon, const QString& Text, QMessageBox::StandardButtons Buttons, QMessageBox::StandardButton Default, const button_texts& Texts) const
{
    QMessageBox* Box = new QMessageBox(Icon, QStringLiteral("RAWcooked"), Text, Buttons, Parent);
    Box->setTextFormat(Qt::PlainText); // Names of files are not rich text
    for (const std::pair<QMessageBox::StandardButton, QString>& Item : Texts)
        if (QAbstractButton* Button = Box->button(Item.first))
            Button->setText(Item.second); // A verb says what the button does
    Box->setDefaultButton(Default);
    Box->setEscapeButton(Buttons.testFlag(QMessageBox::Cancel) ? QMessageBox::Cancel : Default); // Never the answer which changes something
    Box->setWindowIcon(windowIcon());
    return Box;
}

//---------------------------------------------------------------------------
QMessageBox::StandardButton GUI_Main::Message_Ask(QMessageBox::Icon Icon, const QString& Text, const QString& Informative, QMessageBox::StandardButtons Buttons, QMessageBox::StandardButton Default, const button_texts& Texts)
{
    const QPointer<QMessageBox> Box = Message_Create(this, Icon, Text, Buttons, Default, Texts);
    Box->setInformativeText(Informative);
    const QMessageBox::StandardButton Answer = (QMessageBox::StandardButton)Box->exec();
    delete Box;
    return Answer;
}

//---------------------------------------------------------------------------
bool GUI_Main::Close(const std::vector<size_t>& List, bool IsWindow)
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
            case QMessageBox::Save      : // Closed when the files are written, if they are
                                          Save(List);
                                          if (IsBusy())
                                          {
                                              Close_Saved = List;
                                              Close_Saved_Window = IsWindow;
                                          }
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
        Status_Set(QString()); // It may be about a package which is closed
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

    Operations_Begin(tr("saving the metadata"), tr("Metadata saved"));
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
void GUI_Main::Operations_Add_Compressed(operation::kind Kind, const std::vector<size_t>& List, const QString& Output)
{
    // Only the Matroska files created by RAWcooked of a package of files; the library checks 1 file per run
    operation Operation;
    Operation.Kind = Kind;
    Operation.Output = Output;
    for (const size_t Pos : List)
    {
        Operation.Package = Pos;
        const QStringList FileNames = Decodable_Files(Packages[Pos]);
        if (Kind != operation::Check || FileNames.size() < 2)
        {
            Operation.Inputs = FileNames;
            Operations_Add(Operation);
            continue;
        }
        Operation.Parts = (size_t)FileNames.size();
        for (Operation.Part = 0; Operation.Part < Operation.Parts; Operation.Part++)
        {
            Operation.Inputs = QStringList(FileNames.at((int)Operation.Part));
            Operations_Add(Operation);
        }
        Operation.Part = 0;
        Operation.Parts = 1;
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
        if (Operation.Kind == operation::Check && Operation.Parts > 1 && Operation.Inputs.size() == 1)
            Name = QFileInfo(Operation.Inputs.first()).fileName(); // A file of the package
    }
    switch (Operation.Kind)
    {
        case operation::Analyze : return tr("Analyzing %1").arg(Name);
        case operation::Edit    : return tr("Saving the metadata of %1").arg(Name);
        case operation::Encode  : return tr("Encoding %1").arg(Name);
        case operation::Decode  : return tr("Decoding %1").arg(Name);
        case operation::Check   : return Operation.Output.isEmpty() ? tr("Checking %1").arg(Name) : tr("Checking %1 against the original files").arg(Name);
        case operation::Command : return tr("Preparing the FFmpeg command for %1").arg(Name);
    }
    return Name;
}

//---------------------------------------------------------------------------
QStringList GUI_Main::Operation_Arguments(const operation& Operation) const
{
    // One action per run: the library has the encoding and the decoding on by default
    const gui_package& Package = Packages[Operation.Package];
    const QStringList& Inputs = Operation.Inputs.isEmpty() ? Package.Inputs : Operation.Inputs;
    QStringList Arguments;
    switch (Operation.Kind)
    {
        case operation::Analyze :
            // As for an encoding, nothing is written; else only what the content is
            Arguments = Options.Arguments(Operation_Analyze);
            Arguments << QStringLiteral("--info");
            if (!Operation.InfoOnly)
                Arguments << QStringLiteral("--encode");
            if (!Package.IsDirectory)
                Arguments << QStringLiteral("--file");
            Arguments << Inputs;
            break;
        case operation::Edit :
            Arguments = Options.Arguments(Operation_Edit);
            for (const gui_field& Field : Package.Tracks[Operation.Track].Fields)
                if (Field.IsModified)
                    Arguments << QStringLiteral("--edit") << Field.Name + QLatin1Char('=') + Field.NewValue;
            if (!Package.IsDirectory)
                Arguments << QStringLiteral("--file");
            Arguments << Inputs;
            break;
        case operation::Encode :
        case operation::Command :
        {
            Arguments = Options.Arguments(Operation_Encode);
            Arguments << Operation.MoreArguments;
            if (Operation.Kind == operation::Command)
                Arguments << QStringLiteral("--display-command");
            Arguments << QStringLiteral("--no-decode") << QStringLiteral("--encode"); // The Matroska files of the package are not decoded
            const QStringList MoreOutputOptions = QProcess::splitCommand(Options.Value(QStringLiteral("MoreOutputOptions")));
            if (MoreOutputOptions.isEmpty())
            {
                if (!Package.IsDirectory)
                    Arguments << QStringLiteral("--file");
                Arguments << Inputs;
                Arguments << QStringLiteral("-o") << Operation.Output;
            }
            else
            {
                // Form of the command line of the encoder: what follows the output file is for the encoder
                for (const QString& Input : Inputs)
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
            Arguments << QStringLiteral("--no-encode") << QStringLiteral("--decode");
            if (!Package.IsDirectory || !Operation.Inputs.isEmpty()) // Also files of a directory
                Arguments << QStringLiteral("--file");
            Arguments << Inputs;
            if (!Operation.Output.isEmpty())
                Arguments << QStringLiteral("-o") << Operation.Output;
            break;
        case operation::Check :
            Arguments = Options.Arguments(Operation_Check);
            Arguments << QStringLiteral("--no-encode") << QStringLiteral("--check");
            if (!Package.IsDirectory || !Operation.Inputs.isEmpty())
                Arguments << QStringLiteral("--file");
            Arguments << Inputs;
            if (!Operation.Output.isEmpty())
                Arguments << QStringLiteral("-o") << Operation.Output; // The original files, compared with the decoded ones
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
                                      Job->Edits_FileCount = Package.Tracks[Operation_Current.Track].FileCount; // Nothing is written if the files changed since they were analyzed
                                      if (std::find(Edits_Started.begin(), Edits_Started.end(), Operation_Current.Package) == Edits_Started.end())
                                          Edits_Started.push_back(Operation_Current.Package);
                                      break;
            case operation::Encode  :
            {
                Job->Overwrite = true; // Accepted by the user when the output file was chosen
                const QFileInfo Info(Operation_Current.Output); // What the run changes in a file which has this name
                Operation_Current.Output_Existed = Info.exists();
                Operation_Current.Output_Size = Info.size();
                Operation_Current.Output_Time = Info.lastModified();
                break;
            }
            default                 : ;
        }
        Job->Ask_Overwrite = [this](const QString& FileName, const QString& ExtraText, bool CanApplyToAll, bool& ApplyToAll) { return Job_Ask_Overwrite(FileName, ExtraText, CanApplyToAll, ApplyToAll); };
        Job->Ask_Question = [this](const QString& Text) { return Job_Ask_Question(Text); };
        connect(Job, &QThread::finished, this, &GUI_Main::OnJob_Finished);
        Job_Refused = false;
        Warnings_Shown = 0;
        Log_Begin(Operation_Current);

        // Progress
        Progress_Show();
        if (!Timer->isActive())
        {
            // First job of the batch
            Timer->start(Progress_Period);
            Status_Set(QString()); // The result of the previous batch
            Menu_Update();
        }

        if (!Job->Start())
            Job_End(); // It never runs, its results are the ones of a failure
    }
}

//---------------------------------------------------------------------------
void GUI_Main::Job_End()
{
    Log_Move();
    Operation_Finish(Operation_Current);

    // The job is destroyed before the next one is created
    delete Job;
    Job = nullptr;
}

//---------------------------------------------------------------------------
void GUI_Main::Log_Begin(const operation& Operation)
{
    // Each operation after an empty line, with the time it starts
    QString Text;
    const QTextDocument* Document = Log->document();
    if (!Document->isEmpty() && !Document->lastBlock().text().trimmed().isEmpty())
        Text += QLatin1Char('\n');
    Text += QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QLatin1Char('\n');

    const gui_package& Package = Packages[Operation.Package];
    switch (Operation.Kind)
    {
        case operation::Analyze :
        {
            // Not as a command line: the same command would encode
            const QStringList Arguments = Options.Arguments(Operation_Analyze);
            Text += (Operation.InfoOnly ? tr("Analysis of %1, without the options of an encoding") : tr("Analysis of %1")).arg(Inputs_Text(Package.Inputs));
            if (!Arguments.isEmpty())
                Text += tr(", options: %1").arg(Arguments_Text(Arguments));
            break;
        }
        case operation::Edit :
        {
            // What the files had, as the views showed it: a value saved by mistake can be found again
            const auto Value = [](const gui_field& Field)
            {
                const QString Shown = Field.Display();
                return Shown.isEmpty() ? tr("(not set)") : QLatin1Char('\'') + Shown + QLatin1Char('\'');
            };
            const gui_track& Track = Package.Tracks[Operation.Track];
            for (const gui_field& Field : Track.Fields)
                if (Field.IsModified)
                {
                    gui_field Before = Field;
                    Before.IsModified = false;
                    Text += tr("%1, %2: %3 => %4").arg(Track.Name, Field.Name, Value(Before), Value(Field)) + QLatin1Char('\n');
                }
            Text += QStringLiteral("> ") + Log_CommandLine(Operation);
            break;
        }
        default :
            Text += QStringLiteral("> ") + Log_CommandLine(Operation);
    }
    Log->Add(Text + QLatin1Char('\n'));
}

//---------------------------------------------------------------------------
QString GUI_Main::Log_CommandLine(const operation& Operation)
{
    // A package may have thousands of files: the first one and their count, as for an analysis
    const gui_package& Package = Packages[Operation.Package];
    const QStringList& Inputs = Operation.Inputs.isEmpty() ? Package.Inputs : Operation.Inputs;
    if (Inputs.size() < 2)
        return Job->CommandLine();
    const QStringList Arguments = Job->Arguments;
    QSet<QString> Others;
    for (int Pos = 1; Pos < Inputs.size(); Pos++)
        Others.insert(Inputs.at(Pos));
    QStringList Head, Tail; // Up to the first input, then without the other inputs
    bool IsTail = false;
    int Removed = 0;
    for (const QString& Argument : Arguments)
    {
        if (!IsTail)
        {
            Head += Argument;
            IsTail = Argument == Inputs.first();
            continue;
        }
        if (Others.contains(Argument))
        {
            if (!Tail.isEmpty() && Tail.last() == QLatin1String("-i"))
                Tail.removeLast(); // Form of the command line of the encoder
            Removed++;
            continue;
        }
        Tail += Argument;
    }
    if (!Removed)
        return Job->CommandLine();

    // The text of the job, for the same quotes and the license keys hidden
    Job->Arguments = Head;
    const QString Text_Head = Job->CommandLine();
    Job->Arguments = Head + Tail;
    const QString Text_All = Job->CommandLine();
    Job->Arguments = Arguments;
    return Text_Head + QLatin1Char(' ') + (Removed == 1 ? tr("(and 1 other file)") : tr("(and %1 other files)").arg(Removed)) + Text_All.mid(Text_Head.size());
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
        ProgressDialog->setCancelButtonText(tr("Cancel"));
        ProgressCancel = ProgressDialog->findChild<QPushButton*>(); // The one the dialog has: with another one the dialog would have the Escape key twice, and not react to it
        if (ProgressCancel)
        {
            // Neither Enter nor Space cancel: the keyboard has Escape for it
            ProgressCancel->setAutoDefault(false);
            ProgressCancel->setDefault(false);
            ProgressCancel->setFocusPolicy(Qt::NoFocus);
        }
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

    // Each line fits in the dialog. The part of the operation is said: the bar starts again with each part
    QString Title = Progress_Title;
    const QString Phase = Progress_Phase();
    if (!Phase.isEmpty())
        Title = tr("%1: %2").arg(Title, Phase);
    const int Width = qMax(ProgressDialog->width(), ProgressDialog->minimumWidth()) - Margin;
    ProgressDialog->setLabelText(Metrics.elidedText(Title, Qt::ElideMiddle, Width) + QLatin1Char('\n') + Metrics.elidedText(Text, Qt::ElideRight, Width));
}

//---------------------------------------------------------------------------
QString GUI_Main::Progress_Phase() const
{
    // An encoding has parts with their own progress; the other operations have one
    if (!Job || (Operation_Current.Kind != operation::Encode && Operation_Current.Kind != operation::Command))
        return QString();
    switch (Job->Phase())
    {
        case GUI_Job::Phase_Files   : return tr("reading the files");
        case GUI_Job::Phase_Encoder : return tr("encoding with FFmpeg");
        case GUI_Job::Phase_Check   : return tr("checking the Matroska file");
    }
    return QString();
}

//---------------------------------------------------------------------------
void GUI_Main::Log_Move()
{
    if (!Job)
        return;
    const QString Text = Job->Log_Take();
    if (!Text.isEmpty())
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
    Progress_Text(Operations_Canceled && Operation_Current.Kind != operation::Analyze ? tr("Canceling...") : Text); // After a canceled save, its analysis is not canceled
}

//---------------------------------------------------------------------------
void GUI_Main::OnJob_Cancel()
{
    if (!Job || Operations_Canceled || Cancel_Question)
        return;

    // Hours of work are not canceled by a key or a click alone. The operation goes on while the question is shown
    QString Text, Accept;
    switch (Operation_Current.Kind)
    {
        case operation::Encode  : Text = tr("Cancel the encoding of %1?"); Accept = tr("&Cancel the encoding"); break;
        case operation::Decode  : Text = tr("Cancel the decoding of %1?"); Accept = tr("&Cancel the decoding"); break;
        case operation::Check   : Text = tr("Cancel the check of %1?"); Accept = tr("&Cancel the check"); break;
        default                 : ; // Short, and done again at no cost
    }
    if (!Text.isEmpty())
    {
        QWidget* Parent = this;
        if (ProgressDialog && ProgressDialog->isVisible())
            Parent = ProgressDialog;
        const QString Name = Operation_Current.Package < Packages.size() ? Packages[Operation_Current.Package].Name() : QString();
        const QPointer<QMessageBox> Box = Message_Create(Parent, QMessageBox::Question, Text.arg(Name), QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
                                                         { { QMessageBox::Yes, Accept }, { QMessageBox::No, tr("C&ontinue") } });
        Cancel_Question = Box;
        Box->exec();
        const bool IsCanceled = Box && Box->clickedButton() && Box->clickedButton() == Box->button(QMessageBox::Yes); // Closed without answer when the operation ends meanwhile
        delete Box;
        if (!IsCanceled)
            return;
    }

    Operations_Cancel();
}

//---------------------------------------------------------------------------
void GUI_Main::Operations_Cancel()
{
    if (!Job || Operations_Canceled)
        return;
    Operations_Canceled = true;
    if (Cancel_Question)
        Cancel_Question->reject(); // Asked by another way meanwhile

    // The other operations of the batch are dropped, except the analysis after an edit which was started:
    // the views show what the files have
    Operations.erase(std::remove_if(Operations.begin(), Operations.end(), [this](const operation& Next)
    {
        return Next.Kind != operation::Analyze || !Next.Modified_Keep || std::find(Edits_Started.begin(), Edits_Started.end(), Next.Package) == Edits_Started.end();
    }), Operations.end());

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

    // A question about the cancellation of this job has no more object
    if (Cancel_Question)
        Cancel_Question->reject();

    // Results
    Job_End();

    // Next operation, without return to the event loop: no moment without operation running before the end of the batch
    Operations_Start();
}

//***************************************************************************
// Results of the operations
//***************************************************************************

//---------------------------------------------------------------------------
QString GUI_Main::Operation_Failed(operation::kind Kind)
{
    // What the status says when a job fails without telling why
    switch (Kind)
    {
        case operation::Analyze : return tr("Analysis failed");
        case operation::Edit    : return tr("Metadata not saved");
        case operation::Encode  : return tr("Encoding failed");
        case operation::Decode  : return tr("Decoding failed");
        case operation::Check   : return tr("Check failed");
        case operation::Command : return tr("FFmpeg command not available");
    }
    return QString();
}

//---------------------------------------------------------------------------
QString GUI_Main::Job_Messages() const
{
    QStringList Lines;
    Lines << Job->Messages_Error << Job->Messages_Warning << Job->Messages_Info;
    return Lines.join(QLatin1Char('\n'));
}

//---------------------------------------------------------------------------
QString GUI_Main::Job_Error() const
{
    // The first error; when the library has no error message, the last lines of the output say what failed
    const QStringList& Errors = Job->Messages_Error;
    if (Errors.isEmpty())
        return Operation_Failed(Operation_Current.Kind);
    if (Job->Messages_Error_IsOutput)
    {
        for (auto Line = Errors.crbegin(); Line != Errors.crend(); ++Line)
            if (!Line->trimmed().isEmpty())
                return tr("%1: %2").arg(Operation_Failed(Operation_Current.Kind), Line->section(QLatin1Char('\n'), -1).trimmed());
        return Operation_Failed(Operation_Current.Kind);
    }
    return Line_First(Errors.first());
}

//---------------------------------------------------------------------------
QString GUI_Main::Job_Errors(const operation& Operation) const
{
    QString Text = Job->Messages_Error.join(QLatin1Char('\n'));
    if (Text.isEmpty() || Job->Messages_Error_IsOutput)
        Text = Operation_Failed(Operation.Kind) + QLatin1Char('.') + (Text.isEmpty() ? QString() : QLatin1Char('\n') + Text);
    return Text;
}

//---------------------------------------------------------------------------
void GUI_Main::Job_Hints(const operation& Operation, gui_package& Package)
{
    // Messages of the library are the ones of the command line: what to do in this window
    QStringList Hints;
    if (Job->IsEncoderMissing)
        Hints += tr("FFmpeg can not be run: choose the FFmpeg program in Options > Preferences > %1, or put FFmpeg in a directory named ffmpeg next to this program.").arg(GUI_Options::Where(QStringLiteral("BinName")));
    if (Job->Messages_Error.join(QLatin1Char('\n')).contains(QLatin1String("--attachment-max-size")) || Operation.Encode_Messages.contains(QLatin1String("--attachment-max-size"))) // Also from the analysis as for an encoding
        Hints += tr("A file is bigger than the maximum size of attachments: Options > Preferences > %1, then Actions > Analyze again.").arg(GUI_Options::Where(QStringLiteral("AttachmentMaxSize")));
    if (Operation.Kind == operation::Edit)
        Hints += tr("A part of the files may already have the new values.");
    for (const QString& Hint : Hints)
    {
        if (!Package.Messages.contains(Hint))
            Package.Messages += (Package.Messages.isEmpty() ? QString() : QStringLiteral("\n")) + Hint;
        if (!Operations_Hints.contains(Hint))
            Operations_Hints += Hint;
    }
}

//---------------------------------------------------------------------------
QString GUI_Main::Decode_Directory(const operation& Operation) const
{
    // As the library names it: the directory given, else next to the Matroska file
    if (!Operation.Output.isEmpty())
        return Operation.Output;
    if (Operation.Package >= Packages.size())
        return QString();
    const gui_package& Package = Packages[Operation.Package];
    const QStringList& Inputs = Operation.Inputs.isEmpty() ? Package.Inputs : Operation.Inputs;
    if ((!Package.IsDirectory || !Operation.Inputs.isEmpty()) && Inputs.size() == 1)
        return Inputs.first() + QStringLiteral(".RAWcooked");
    if (Package.IsDirectory && Package.Compressed.size() == 1)
        return QDir::toNativeSeparators(QDir(Package.Inputs.value(0)).absoluteFilePath(Package.Compressed.front().FileName)) + QStringLiteral(".RAWcooked");
    return QString(); // One directory for each file
}

//---------------------------------------------------------------------------
int GUI_Main::Output_Remove(const operation& Operation)
{
    // The file is incomplete if the run changed it: also a previous file replaced by the encoder
    const QFileInfo Info(Operation.Output);
    if (Operation.Output.isEmpty() || !Info.exists())
        return 0;
    if (Operation.Output_Existed && Info.size() == Operation.Output_Size && Info.lastModified() == Operation.Output_Time)
        return 0; // Not touched by the encoder
    return QFile::remove(Operation.Output) ? 1 : -1;
}

//---------------------------------------------------------------------------
void GUI_Main::Operation_Finish(const operation& Operation)
{
    if (!Job || Operation.Package >= Packages.size())
        return;
    gui_package& Package = Packages[Operation.Package];

    // Stopped by the user: a cancellation, or no as answer to a question of the job when the job has no error message about its end
    result Result = Result_Ok;
    if (Job->ExitCode)
        Result = Job->IsCanceled() || (Job_Refused && Job->Messages_Error.isEmpty()) ? Result_Canceled : Result_Failed;

    // What the job says. The command does not change what the package shows, unless it fails; nor the analysis after a failed edit
    if (Result == Result_Failed && !(Operation.Kind == operation::Analyze && Operation.Status_Keep))
    {
        Package.Status = Status_Short(Job_Error(), Package);
        Package.IsError = true;
    }
    if (Result == Result_Failed || Operation.Kind != operation::Command)
    {
        if (Result == Result_Canceled)
            Package.IsError = false;
        Package.Messages = Job_Messages();
        Package.HasWarnings = !Job->Messages_Warning.isEmpty();
    }

    // Each kind of operation has its results
    bool IsHandled = false; // The failure is not one to report
    switch (Operation.Kind)
    {
        case operation::Analyze : IsHandled = Analyze_Finish(Operation, Package, Result); break;
        case operation::Edit    : Edit_Finish(Operation, Package, Result); break;
        case operation::Encode  : Encode_Finish(Operation, Package, Result); break;
        case operation::Decode  : Decode_Finish(Operation, Package, Result); break;
        case operation::Check   : Check_Finish(Operation, Package, Result); break;
        case operation::Command : if (Result == Result_Ok)
                                      Operations_Command = Job->Out.trimmed();
                                  break;
    }

    // For the message at the end of the batch
    if (Result == Result_Failed && !IsHandled)
    {
        Operations_Errors++;
        if (!Operations_ErrorText.isEmpty())
            Operations_ErrorText += QStringLiteral("\n\n");
        if (Operations_Count > 1)
            Operations_ErrorText += Operation_Title(Operation) + QStringLiteral(":\n");
        Operations_ErrorText += Operation.InfoOnly ? Operation.Encode_Messages : Job_Errors(Operation); // An analysis is first done as for an encoding
        if (Operation.Kind == operation::Encode && Job->IsEncoderStarted)
        {
            // What happened to the output file, without the first error which is already above
            QString Status = Package.Status;
            const QString Error = QStringLiteral(". ") + Status_Short(Job_Error(), Package);
            if (Job->IsEncoded && Status.endsWith(Error))
                Status.chop(Error.size());
            Operations_ErrorText += QLatin1Char('\n') + Status;
        }
        Job_Hints(Operation, Package);
    }
}

//---------------------------------------------------------------------------
bool GUI_Main::Analyze_Finish(const operation& Operation, gui_package& Package, result Result)
{
    // After an edit which was started: the files may have the new values
    const bool IsEdited = Operation.Modified_Keep && std::find(Edits_Started.begin(), Edits_Started.end(), Operation.Package) != Edits_Started.end();

    if (Result == Result_Canceled)
    {
        // The package can be analyzed again; content which may be the one of before an edit is not shown
        if (IsEdited || Package.Kind == gui_package::Kind_Unknown)
        {
            Package.Kind = gui_package::Kind_Unknown;
            Package.Tracks.clear();
            Package.Attachments.clear();
            Package.Compressed.clear();
            Package.IsNotEncodable = false;
        }
        if (IsEdited)
            Package.Status = tr("Not analyzed: the analysis after the save was canceled, a part of the files may have the new values (Actions > Analyze again)");
        else if (Package.Kind == gui_package::Kind_Unknown)
            Package.Status = tr("Not analyzed: the analysis was canceled (Actions > Analyze again)");
        else
            Package.Status = tr("Analysis canceled: what is shown may be outdated (Actions > Analyze again)");
        return false;
    }

    if (Result == Result_Failed)
    {
        // The content may not be encodable but readable: once more without the options of an encoding, so that it can be seen and edited.
        // Also after an edit in a canceled batch: the views show what the files have
        if (!Operation.InfoOnly && (!Operations_Canceled || IsEdited))
        {
            operation Next = Operation;
            Next.InfoOnly = true;
            Next.Encode_Error = Status_Short(Job_Error(), Package);
            Next.Encode_Messages = Job_Errors(Operation);
            Operations.push_front(Next);
            Operations_Pos--; // The same operation, for the progress
            return true;
        }

        // The content is not known
        Package.Kind = gui_package::Kind_Unknown;
        Package.Tracks.clear();
        Package.Attachments.clear();
        Package.Compressed.clear();
        Package.IsNotEncodable = false;
        Package.IsError = true;
        if (Operation.InfoOnly)
        {
            // The error of the analysis as for an encoding comes first; the status of a failed edit is kept
            if (!Operation.Status_Keep)
                Package.Status = Operation.Encode_Error;
            Package.Messages = Operation.Encode_Messages;
        }
        return false;
    }

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

    // Content, the modified fields of the previous content are dropped. The Matroska files of a package with tracks are listed, not encoded
    Package.Kind = Job->IsCompressed ? gui_package::Kind_Compressed : gui_package::Kind_Uncompressed;
    Package.Compressed_Set(Job->Compressed);
    Package.Content_Set(Job->Tracks, Job->Attachments);
    Package.IsLicenseIgnored = Job->IsLicenseIgnored;
    Package.IsNotEncodable = Operation.InfoOnly;
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

    // An input file stands for its whole sequence on the disk
    if (!Package.IsDirectory)
    {
        quint64 Files_Tracks = 0;
        for (const gui_track& Track : Package.Tracks)
            Files_Tracks += Track.FileCount;
        const quint64 Opened = (quint64)Package.Inputs.size();
        if (Files_Tracks + Package.Attachments.size() + Package.Compressed.size() > Opened)
            Package.Messages += (Package.Messages.isEmpty() ? QString() : QStringLiteral("\n")) + (Opened == 1 ? tr("1 file opened, the tracks have %1 files: a sequence is taken whole").arg(Files_Tracks)
                                                                                                             : tr("%1 files opened, the tracks have %2 files: a sequence is taken whole").arg(Opened).arg(Files_Tracks));
    }

    // Why the content can not be encoded
    if (Operation.InfoOnly)
    {
        Package.Messages = Operation.Encode_Messages + (Package.Messages.isEmpty() ? QString() : QStringLiteral("\n") + Package.Messages);
        Job_Hints(Operation, Package);
    }

    // Status, the one of a failed edit is kept: what can be done now
    if (Operation.Status_Keep)
        return false;
    Package.IsError = false;
    if (Operation.InfoOnly)
    {
        Package.Status = tr("Can not be encoded: %1").arg(Operation.Encode_Error);
        Package.IsError = true;
    }
    else if (Package.Kind == gui_package::Kind_Compressed)
    {
        const size_t Decodable = (size_t)std::count_if(Package.Compressed.begin(), Package.Compressed.end(), [](const gui_compressed& Compressed) { return Compressed.HasReversibilityData; });
        if (!IsDecodable(Package))
        {
            Package.Status = tr("Not created by RAWcooked: it can not be decoded to the original files");
            Package.IsError = true;
        }
        else if (Decodable < Package.Compressed.size()) // The other files are not decoded nor checked
            Package.Status = (Package.HasWarnings ? tr("Ready to decode or check %1 of %2 Matroska files, with warnings") : tr("Ready to decode or check %1 of %2 Matroska files")).arg(Decodable).arg(Package.Compressed.size());
        else
            Package.Status = Package.HasWarnings ? tr("Ready to decode or check, with warnings") : tr("Ready to decode or check");
    }
    else if (Package.Tracks.empty())
        Package.Status = tr("Analyzed, no audio or video content");
    else
        Package.Status = Package.HasWarnings ? tr("Ready to encode, with warnings") : tr("Ready to encode");
    return false;
}

//---------------------------------------------------------------------------
void GUI_Main::Edit_Finish(const operation& Operation, gui_package& Package, result Result)
{
    if (Result == Result_Ok)
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
        return;
    }

    // The files are written one after the other
    if (Result == Result_Canceled)
        Package.Status = tr("Save canceled, a part of the files may have the new values");
    else
        Package.Status = tr("Save failed, a part of the files may have the new values: %1").arg(Package.Status);

    // The analysis which follows must not hide it
    for (operation& Next : Operations)
        if (Next.Kind == operation::Analyze && Next.Package == Operation.Package)
        {
            Next.Status_Keep = true;
            break;
        }
}

//---------------------------------------------------------------------------
void GUI_Main::Encode_Finish(const operation& Operation, gui_package& Package, result Result)
{
    // A complete output file is kept, whatever comes after the encoder
    if (Job->IsEncoded)
    {
        gui_compressed::checked Checked = gui_compressed::Checked_No;
        path_cache Cache;
        const QString Key = Path_Key(Operation.Output, false, &Cache);
        for (const gui_compressed& Compressed : Job->Compressed)
            if (Path_Key(Compressed.FileName, false, &Cache) == Key)
                Checked = Compressed.Checked;
        QString Text;
        switch (Result)
        {
            case Result_Ok          : Text = Checked == gui_compressed::Checked_Reversibility ? tr("Encoded, reversibility checked") : (Checked == gui_compressed::Checked_Decoding ? tr("Encoded, decoding checked") : tr("Encoded")); break;
            case Result_Canceled    : Text = tr("Encoded, the check was canceled"); break;
            case Result_Failed      : Text = Checked == gui_compressed::Checked_No && Check_IsFull(Operation) ? tr("Encoded, the check failed") : tr("Encoded, with errors after the encoding"); break;
        }
        Package.Status = Status_File(Text, Operation.Output);
        if (Result == Result_Failed)
            Package.Status = tr("%1. %2").arg(Package.Status, Status_Short(Job_Error(), Package));
        Package.IsError = Result == Result_Failed;
        return;
    }

    // An incomplete output file is removed
    if (Job->IsEncoderStarted)
    {
        const bool IsCanceled = Result == Result_Canceled;
        switch (Output_Remove(Operation))
        {
            case 1  : Package.Status = Status_File(Operation.Output_Existed ? (IsCanceled ? tr("Encoding canceled, the incomplete file which had replaced the previous file was removed") : tr("Encoding failed, the incomplete file which had replaced the previous file was removed"))
                                                                            : (IsCanceled ? tr("Encoding canceled, the incomplete file was removed") : tr("Encoding failed, the incomplete file was removed")), Operation.Output);
                      return;
            case -1 : Package.Status = Status_File(IsCanceled ? tr("Encoding canceled, this file is incomplete") : tr("Encoding failed, this file is incomplete"), Operation.Output);
                      Package.IsError = true;
                      return;
            default : ;
        }
    }

    // Nothing was written
    if (Result == Result_Canceled)
    {
        Package.Status = tr("Encoding canceled");
        Package.IsError = false;
    }
    else if (Result == Result_Ok)
    {
        Package.Status = tr("Nothing was encoded: the encoder was not run"); // E.g. an option of the preferences changed the action
        Package.IsError = true;
    }
}

//---------------------------------------------------------------------------
// What the last run checked and decoded, in the Matroska files of the package
static void Compressed_Merge(gui_package& Package, const QStringList& Inputs, const std::vector<gui_compressed>& Results)
{
    // The files of the run which are not in its results (canceled, stopped) have nothing checked or decoded by it
    for (gui_compressed& Compressed : Package.Compressed)
        if (Inputs.isEmpty() || std::any_of(Inputs.begin(), Inputs.end(), [&](const QString& Input) { return QFileInfo(Input) == QFileInfo(Compressed.FileName); }))
        {
            Compressed.Checked = gui_compressed::Checked_No;
            Compressed.OutputDirectory.clear();
        }

    for (const gui_compressed& Result : Results)
        for (gui_compressed& Compressed : Package.Compressed)
            if (QFileInfo(Compressed.FileName) == QFileInfo(Result.FileName))
            {
                Compressed.Checked = Result.Checked;
                Compressed.OutputDirectory = Result.OutputDirectory;
            }
}

//---------------------------------------------------------------------------
void GUI_Main::Decode_Finish(const operation& Operation, gui_package& Package, result Result)
{
    Compressed_Merge(Package, Operation.Inputs, Job->Compressed);

    // Where the files are
    QStringList Directories;
    gui_compressed::checked Checked = gui_compressed::Checked_Reversibility;
    for (const gui_compressed& Compressed : Job->Compressed)
    {
        if (!Compressed.OutputDirectory.isEmpty() && !Directories.contains(Compressed.OutputDirectory))
            Directories += Compressed.OutputDirectory;
        Checked = Checked_Min(Checked, Compressed.Checked);
    }
    if (Directories.isEmpty())
    {
        const QString Directory = Decode_Directory(Operation);
        if (!Directory.isEmpty() && QFileInfo(Directory).isDir())
            Directories += Directory;
    }

    QString Text;
    if (Result == Result_Ok)
    {
        // A decoding may also check (preset of the checks)
        Text = Job->Compressed.empty() || Checked == gui_compressed::Checked_No ? tr("Decoded") : (Checked == gui_compressed::Checked_Reversibility ? tr("Decoded, reversibility checked") : tr("Decoded, decoding checked"));
        Package.IsError = false;
    }
    else if (Result == Result_Canceled)
    {
        Package.IsError = false;
        if (Directories.isEmpty())
        {
            Package.Status = tr("Decoding canceled");
            return;
        }
        Text = tr("Decoding canceled, the files already decoded stay");
    }
    else
        return;
    if (Directories.size() > 1)
        Text = tr("%1 (%2 directories)").arg(Text, QString::number(Directories.size()));
    Package.Status = Directories.isEmpty() ? Text : Status_File(Text, Directories.first());
}

//---------------------------------------------------------------------------
void GUI_Main::Check_Finish(const operation& Operation, gui_package& Package, result Result)
{
    Compressed_Merge(Package, Operation.Inputs, Job->Compressed);
    if (!Operation.Part)
        Check_Results.clear();
    if (Result == Result_Canceled)
    {
        Package.Status = tr("Check canceled");
        Package.IsError = false;
        return;
    }

    // Results of this run, for each file: a package of files is checked file by file, a directory in one run
    const QString Error = Result == Result_Failed ? Package.Status : QString();
    if (Job->Compressed.empty())
    {
        check_result Item;
        Item.Name = Operation.Inputs.size() == 1 ? QFileInfo(Operation.Inputs.first()).fileName() : Package.Name();
        Item.Checked = Result == Result_Ok ? gui_compressed::Checked_Decoding : gui_compressed::Checked_No;
        Item.Error = Error;
        Check_Results.push_back(Item);
    }
    for (const gui_compressed& Compressed : Job->Compressed)
    {
        check_result Item;
        Item.Name = QFileInfo(Compressed.FileName).fileName();
        Item.Checked = Compressed.Checked;
        Item.Error = Compressed.Checked == gui_compressed::Checked_No ? (Error.isEmpty() ? tr("Not checked") : Error) : QString();
        Check_Results.push_back(Item);
    }

    // The weakest result is the one of the package
    std::vector<const check_result*> Failed;
    gui_compressed::checked Checked = gui_compressed::Checked_Reversibility;
    size_t Reversibility = 0;
    for (const check_result& Item : Check_Results)
    {
        if (!Item.Error.isEmpty())
            Failed.push_back(&Item);
        Checked = Checked_Min(Checked, Item.Checked);
        if (Item.Checked == gui_compressed::Checked_Reversibility)
            Reversibility++;
    }
    if (Failed.empty())
    {
        if (Checked == gui_compressed::Checked_Reversibility)
            Package.Status = Operation.Output.isEmpty() ? tr("Reversibility was checked, no issue detected.") : tr("Reversibility was checked against the original files, no issue detected.");
        else
        {
            Package.Status = tr("Decoding was checked, no issue detected.");
            if (Reversibility)
                Package.Status += QLatin1Char(' ') + tr("Reversibility was checked for %1 of %2 files.").arg(Reversibility).arg(Check_Results.size());
        }
        Package.IsError = false;
    }
    else if (Check_Results.size() > 1)
    {
        Package.Status = tr("Check failed for %1 of %2 files: %3").arg(Failed.size()).arg(Check_Results.size()).arg(Failed.front()->Name);
        Package.IsError = true;
    }

    // Messages of each file, the ones of the previous files of the package first
    if (Operation.Parts > 1)
    {
        const check_result& Last = Check_Results.back();
        QString Details = Last.Name + QStringLiteral(": ");
        if (!Package.Messages.isEmpty())
            Details += Package.Messages; // With the result of the library
        else if (!Last.Error.isEmpty())
            Details += Last.Error;
        else
            Details += Last.Checked == gui_compressed::Checked_Reversibility ? tr("Reversibility was checked, no issue detected.") : tr("Decoding was checked, no issue detected.");
        if (!Operation.Part)
            Check_Messages.clear();
        Check_Messages += (Check_Messages.isEmpty() ? QString() : QStringLiteral("\n")) + Details;
        Package.Messages = Check_Messages;
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

    // The question about closing the window has no more object: the window stays
    if (Close_Question)
        Close_Question->reject();

    // The batch is finished before anything is shown about it
    const size_t Count = Operations_Count;
    const size_t Errors = Operations_Errors;
    const bool Canceled = Operations_Canceled;
    const QString Name = Operations_Name;
    const QString ErrorText = Operations_ErrorText;
    const QStringList Hints = Operations_Hints;
    const QString Command = Operations_Command;
    const QString Command_Output = Operation_Current.Output;
    const QString Note = Operations_Note;
    const std::vector<size_t> Saved = Close_Saved;
    const bool Saved_Window = Close_Saved_Window;
    QString Message = Operations_Message;
    if (Operations_Count == 1 && Operation_Current.Kind != operation::Command && Operation_Current.Package < Packages.size())
    {
        // The command does not change the status
        const gui_package& Package = Packages[Operation_Current.Package];
        Message = tr("%1: %2").arg(Package.Name(), Package.Status);
    }
    Operations_Count = 0;
    Operations_Pos = 0;
    Operations_Errors = 0;
    Operations_Canceled = false;
    Operations_Name.clear();
    Operations_Message.clear();
    Operations_Note.clear();
    Operations_ErrorText.clear();
    Operations_Hints.clear();
    Operations_Command.clear();
    Edits_Started.clear();
    Check_Results.clear();
    Check_Messages.clear();
    Close_Saved.clear();
    Close_Saved_Window = false;

    // Views
    View_Refresh();

    // The window was waiting for the end of the job
    if (Close_Requested)
    {
        Close_Requested = false;
        QMetaObject::invokeMethod(this, &QWidget::close, Qt::QueuedConnection); // Not from here: the request may come from a close event still in progress
        return;
    }

    // Result: it stays in the status bar
    if (Canceled)
        Message = Count == 1 ? Message : tr("Canceled");
    else if (Errors && Count > 1)
        Message = Errors == 1 ? tr("1 of %1 operations failed").arg(Count) : tr("%1 of %2 operations failed").arg(Errors).arg(Count);
    if (!Note.isEmpty())
        Message = Message.isEmpty() ? Note : (Message + QStringLiteral(" - ") + Note);
    Status_Set(Message);
    if (Errors)
        Errors_Show(Name, ErrorText, Hints);
    else if (!Command.isEmpty())
        Command_Show(Command, Command_Output);

    // The close which asked for this save, when everything is saved
    if ((Saved_Window || !Saved.empty()) && !Errors && !Canceled && !Modified_Count(Saved))
    {
        if (Saved_Window)
            QMetaObject::invokeMethod(this, &QWidget::close, Qt::QueuedConnection); // Not from here, as above
        else if (Close(Saved))
            View_Refresh();
    }
}

//---------------------------------------------------------------------------
QMessageBox::StandardButton GUI_Main::Job_Ask(const QString& Text, const QString& Informative, const QString& Details, QMessageBox::StandardButtons Buttons, const button_texts& Texts)
{
    // The job waits for the answer in its thread, it can not finish meanwhile
    // The question is above the progress, which is above the window
    QWidget* Parent = this;
    if (ProgressDialog && ProgressDialog->isVisible())
        Parent = ProgressDialog;
    QPointer<QMessageBox> Box = Message_Create(Parent, QMessageBox::Question, Text, Buttons, QMessageBox::No, Texts);
    Box->setInformativeText(Informative);
    Box->setDetailedText(Details);
    Question = Box;
    Box->exec();
    QMessageBox::StandardButton Answer = QMessageBox::No;
    QString Answer_Text; // As the button says it
    if (Box)
    {
        if (Box->clickedButton())
            Answer = Box->standardButton(Box->clickedButton());
        if (const QAbstractButton* Button = Box->button(Answer))
            Answer_Text = Button_Text(Button);
        delete Box;
    }

    // Canceled while the question was shown
    if (!Job || Operations_Canceled || Job->IsCanceled())
        return QMessageBox::No;

    // The log has what was asked and answered
    Log->Add(Text + QLatin1Char(' ') + (Answer_Text.isEmpty() ? tr("No") : Answer_Text) + QLatin1Char('\n'));
    return Answer;
}

//---------------------------------------------------------------------------
bool GUI_Main::Job_Ask_Overwrite(const QString& FileName, const QString& ExtraText, bool CanApplyToAll, bool& ApplyToAll)
{
    ApplyToAll = false;
    if (!Job || Operations_Canceled || Job->IsCanceled())
        return false;
    Log_Move(); // The question comes after what the job already wrote

    // The log file of the preferences is replaced by each operation: the user chose it for this
    const QString Log_FileName = Options.Log_FileName();
    if (!Log_FileName.isEmpty() && Path_Key(FileName, false) == Path_Key(Log_FileName, false))
    {
        Log->Add(tr("File '%1' already exists. Replace it? Replace (log file of the preferences)").arg(FileName) + QLatin1Char('\n'));
        return true;
    }

    // The library names a decoded file from the directory of the decoded files
    QString Informative;
    if (Operation_Current.Kind == operation::Decode)
    {
        const QString Directory = Decode_Directory(Operation_Current);
        Informative = Directory.isEmpty() ? tr("Directory of the decoded files: next to each Matroska file.") : tr("Directory of the decoded files: %1").arg(QDir::toNativeSeparators(Directory));
    }

    QMessageBox::StandardButtons Buttons = QMessageBox::Yes | QMessageBox::No;
    button_texts Texts = { { QMessageBox::Yes, tr("&Replace") }, { QMessageBox::No, tr("&Keep") } };
    if (CanApplyToAll)
    {
        Buttons |= QMessageBox::YesToAll | QMessageBox::NoToAll;
        Texts.push_back({ QMessageBox::YesToAll, tr("Replace &all") });
        Texts.push_back({ QMessageBox::NoToAll, tr("Keep a&ll") });
    }
    switch (Job_Ask(tr("File '%1' already exists%2. Replace it?").arg(FileName, ExtraText), Informative, QString(), Buttons, Texts))
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
    QStringList Warnings;
    for (int Pos = Warnings_Shown; Pos < Job->Messages_Warning.size(); Pos++)
        Warnings += Job->Messages_Warning.at(Pos).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    Warnings_Shown = (int)Job->Messages_Warning.size();
    QString Text_Full;
    QString Details;
    if (Warnings.size() > Warnings_Max)
    {
        Text_Full = QStringList(Warnings.mid(0, Warnings_Short)).join(QLatin1Char('\n')) + QStringLiteral("\n...\n\n");
        Details = Warnings.join(QLatin1Char('\n'));
    }
    else if (!Warnings.isEmpty())
        Text_Full = Warnings.join(QLatin1Char('\n')) + QStringLiteral("\n\n");
    Text_Full += Text;

    // The usual question, about going on, has answers which say what they do
    button_texts Texts;
    if (Text.startsWith(QLatin1String("Do you want to continue")))
        Texts = { { QMessageBox::Yes, tr("&Continue") }, { QMessageBox::No, tr("&Stop") } };

    const bool IsYes = Job_Ask(Text_Full, QString(), Details, QMessageBox::Yes | QMessageBox::No, Texts) == QMessageBox::Yes;
    if (!IsYes)
        Job_Refused = true; // If the job stops there, it is the choice of the user, not a failure
    return IsYes;
}

//---------------------------------------------------------------------------
bool GUI_Main::Text_Show(const QString& Title, const QString& Message, const QString& Text, const QStringList& Notes, const QString& Copy, const QString& Other)
{
    QDialog Dialog(this);
    Dialog.setWindowTitle(Title);
    Dialog.setWindowIcon(windowIcon());
    Dialog.setModal(true);

    QVBoxLayout* Layout = new QVBoxLayout(&Dialog);

    // Message
    QLabel* MessageLabel = new QLabel(Message, &Dialog);
    Layout->addWidget(MessageLabel);

    // Text
    QPlainTextEdit* TextEdit = new QPlainTextEdit(&Dialog);
    TextEdit->setReadOnly(true);
    TextEdit->setFont(GUI_Main_Output_Log::Font_Fixed());
    TextEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); // A line with a long name of file is wrapped: its end says what the problem is
    TextEdit->setPlainText(Text);
    Layout->addWidget(TextEdit, 1);

    // Notes
    for (const QString& Note : Notes)
    {
        QLabel* NoteLabel = new QLabel(Note, &Dialog);
        NoteLabel->setTextFormat(Qt::PlainText);
        NoteLabel->setWordWrap(true);
        NoteLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        Layout->addWidget(NoteLabel);
    }

    // Buttons
    QHBoxLayout* ButtonLayout = new QHBoxLayout;
    if (!Copy.isEmpty())
    {
        QPushButton* CopyButton = new QPushButton(Copy, &Dialog);
        connect(CopyButton, &QPushButton::clicked, &Dialog, [Text]() { QGuiApplication::clipboard()->setText(Text); });
        ButtonLayout->addWidget(CopyButton);
    }
    if (!Other.isEmpty())
    {
        QPushButton* OtherButton = new QPushButton(Other, &Dialog);
        connect(OtherButton, &QPushButton::clicked, &Dialog, &QDialog::accept);
        ButtonLayout->addWidget(OtherButton);
    }
    ButtonLayout->addStretch(1);
    QPushButton* CloseButton = new QPushButton(tr("&Close"), &Dialog);
    CloseButton->setDefault(true);
    connect(CloseButton, &QPushButton::clicked, &Dialog, &QDialog::reject);
    ButtonLayout->addWidget(CloseButton);
    Layout->addLayout(ButtonLayout);

    // Size of the dialog: the one its text needs, with the long lines wrapped, limited by the screen
    const QFontMetrics Metrics(TextEdit->font());
    const QStringList Lines = Text.split(QLatin1Char('\n'));
    int Columns = Text_Columns_Min;
    for (const QString& Line : Lines)
        Columns = qMax(Columns, qMin((int)Line.size(), Text_Columns_Max));
    int Count = 1;
    for (const QString& Line : Lines)
        Count += 1 + (int)Line.size() / Columns;
    Count = qBound(Text_Lines_Min, Count, Text_Lines_Max);
    const int Frame = Metrics.lineSpacing() + TextEdit->verticalScrollBar()->sizeHint().width(); // Margins and scroll bar of the text
    QSize Size = Dialog.sizeHint() - TextEdit->sizeHint() + QSize(Metrics.horizontalAdvance(QLatin1Char('0')) * Columns + Frame, Metrics.lineSpacing() * Count + Frame / 2);
    if (const QScreen* Screen = screen())
        Size = Size.boundedTo(Screen->availableGeometry().size() * 2 / 3);
    Dialog.resize(Size);

    return Dialog.exec() == QDialog::Accepted;
}

//---------------------------------------------------------------------------
void GUI_Main::Errors_Show(const QString& Name, const QString& Text, const QStringList& Hints)
{
    if (Text_Show(tr("RAWcooked - Errors"), tr("The following error(s) occurred while %1:").arg(Name), Text, Hints, QString(), tr("Show &log")))
        View_Refresh(View_Log);
}

//---------------------------------------------------------------------------
void GUI_Main::Command_Show(const QString& Command, const QString& Output)
{
    // The command does not work without the file written for it
    QString FileName = Options.Value(QStringLiteral("ReversibilityFileName"));
    QStringList Notes;
    if (FileName.isEmpty())
    {
        FileName = Output + QStringLiteral(".rawcooked_reversibility_data");
        Notes << tr("The reversibility data file was written next to the output file, the command needs it:");
    }
    else
        Notes << tr("The reversibility data file was written, the command needs it:");
    Notes << QDir::toNativeSeparators(FileName);

    Text_Show(tr("RAWcooked - FFmpeg command"), tr("FFmpeg command which would be used for the encoding:"), Command, Notes, tr("C&opy"), QString());
}

//---------------------------------------------------------------------------
void GUI_Main::OnFiles_Track_Activated(size_t Package, size_t Track)
{
    View_Refresh(View_Metadata);
    Metadata->Track_Show(Package, Track);
}
