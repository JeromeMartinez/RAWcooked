/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GUI_MainH
#define GUI_MainH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Package.h"
#include "GUI/Qt/GUI_Options.h"
#include <QMainWindow>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QToolBar>
#include <QMessageBox>
#include <QPointer>
#include <QHash>
#include <QStringList>
#include <deque>
#include <utility>
#include <vector>
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
class GUI_Job;
class GUI_Main_Files;
class GUI_Main_Metadata;
class GUI_Main_Output_Log;
class QStackedWidget;
class QProgressDialog;
class QProgressBar;
class QPushButton;
class QLabel;
class QTimer;
class QDropEvent;
class QDragEnterEvent;
class QCloseEvent;
class QResizeEvent;
//---------------------------------------------------------------------------

//***************************************************************************
// GUI_Main
//***************************************************************************

class GUI_Main : public QMainWindow
{
    Q_OBJECT

public:
    // Constructor/Destructor
    GUI_Main();
    ~GUI_Main();

    //-----------------------------------------------------------------------
    // Content, read (and for the modified fields, written) by the views
    std::vector<gui_package>    Packages;
    GUI_Options                 Options;
    bool                        IsBusy() const { return Job != nullptr; } // An operation is running, the content must not be changed

    // Actions
    void                        Open(const QStringList& Paths); // Each directory is a package, the files together are one package
    void                        Menu_Update(); // After a change of content, of selection or of modified fields

public Q_SLOTS:
    // Menu - Actions
    void                        OnMenu_File_Open_Files();
    void                        OnMenu_File_Open_Directory();
    void                        OnMenu_File_Close_Files();
    void                        OnMenu_File_Close_All();
    void                        OnMenu_File_Save_All();
    void                        OnMenu_Actions_Encode();
    void                        OnMenu_Actions_Decode();
    void                        OnMenu_Actions_Decode_To();
    void                        OnMenu_Actions_Check();
    void                        OnMenu_Actions_Analyze();
    void                        OnMenu_Actions_Command();
    void                        OnMenu_View_Files();
    void                        OnMenu_View_Metadata();
    void                        OnMenu_View_Log();
    void                        OnMenu_Options_Preferences();
    void                        OnMenu_Options_License();
    void                        OnMenu_Help_Help();
    void                        OnMenu_Help_About();
    void                        OnMenu_Help_Website();

    // Operations
    void                        OnJob_Timer();
    void                        OnJob_Finished();

private Q_SLOTS:
    // Operations
    void                        OnJob_Cancel();

    // Views
    void                        OnFiles_Track_Activated(size_t Package, size_t Track);
    void                        OnMetadata_Modified();

    // Status bar
    void                        OnStatus_MessageChanged(const QString& Message);

private:
    // Menu - List
    void                        Menu_Create();
    QMenu*                      Menu_File;
    QAction*                    Menu_File_Open_Files;
    QAction*                    Menu_File_Open_Directory;
    QAction*                    Menu_File_Close_Files;
    QAction*                    Menu_File_Close_All;
    QAction*                    Menu_File_Save_All;
    QAction*                    Menu_File_Quit;
    QMenu*                      Menu_Actions;
    QAction*                    Menu_Actions_Encode;
    QAction*                    Menu_Actions_Decode;
    QAction*                    Menu_Actions_Decode_To;
    QAction*                    Menu_Actions_Check;
    QAction*                    Menu_Actions_Analyze;
    QAction*                    Menu_Actions_Command;
    QMenu*                      Menu_View;
    QAction*                    Menu_View_Files;
    QAction*                    Menu_View_Metadata;
    QAction*                    Menu_View_Log;
    QActionGroup*               Menu_View_Group;
    QMenu*                      Menu_Options;
    QAction*                    Menu_Options_Preferences;
    QAction*                    Menu_Options_License;
    QMenu*                      Menu_Help;
    QAction*                    Menu_Help_Help;
    QAction*                    Menu_Help_About;
    QAction*                    Menu_Help_Website;
    QHash<QAction*, QString>    Menu_StatusTips; // Status tip of each action while it is enabled
    void                        Menu_Enable(QAction* Action, bool IsEnabled, const QString& Reason = QString()); // Reason: why the action is disabled, in place of its status tip
    void                        Menu_ToolTip(QAction* Action); // Tool tip of the action: its name, its shortcut and its status tip

    // ToolBar
    QToolBar*                   ToolBar;
    void                        ToolBar_Create();

    // Views
    enum view
    {
        View_None,
        View_Files,
        View_Metadata,
        View_Log,
    };
    QStackedWidget*             Views;
    GUI_Main_Files*             Files;
    GUI_Main_Metadata*          Metadata;
    GUI_Main_Output_Log*        Log;
    bool                        Files_IsOutdated = false; // Fields were modified since the Files view was filled
    void                        View_Refresh(view View_New = View_None); // Content of the views; with a view: this view is shown, the content of the views is not read again
    QPointer<QWidget>           Help;           // Help window, if it is open

    // Status bar
    QLabel*                     Status_Result;  // Result of the last operation or last message, it stays until the next one
    QLabel*                     Status_Counts;  // Counts of packages and of modified fields
    QString                     Status_Tip;     // Status tip of the status bar, in full
    QString                     Status_Tip_Shown; // What the status bar has of it, for the place it has
    void                        Status_Set(const QString& Text, const QStringList& Paths = QStringList()); // Result or message. A path of Paths named by Text is shown by its name only, its directory is said last
    void                        Status_Update(); // Places of the result and of the status tip, for the width of the window

    // Operations: each one is a job of the library, they run one after the other
    struct operation
    {
        enum kind
        {
            Analyze,            // Opening or refresh of a package
            Edit,               // Modified fields of a track written in its files
            Encode,
            Decode,
            Check,
            Command,            // FFmpeg command shown instead of run
        };
        kind                    Kind = Analyze;
        size_t                  Package = 0;    // Index in Packages
        size_t                  Track = 0;      // Edit: index of the track in the package
        size_t                  Input = (size_t)-1; // Check: index of the file to check when the package is checked file by file, else all its inputs
        QString                 Output;         // Encode, Decode, Command: output file or directory, empty for the default one
        QStringList             MoreArguments;  // For this operation only (e.g. choice about padding bits)
        bool                    Output_Existed = false; // Encode: the output file was there before, not removed if the operation is canceled
        bool                    Modified_Keep = false; // Analyze after edits: the fields whose edit failed stay modified
        bool                    Edit_Failed = false; // Analyze after edits: an edit failed, the package keeps the status of this failure
    };
    std::deque<operation>       Operations;
    size_t                      Operations_Count = 0; // Count of operations of the current batch, for the progress
    size_t                      Operations_Errors = 0; // Count of failed operations of the current batch
    GUI_Job*                    Job = nullptr;  // Current operation, nullptr if none
    QProgressDialog*            ProgressDialog = nullptr;
    QTimer*                     Timer = nullptr;
    void                        Operations_Start(); // First of the list, if nothing is running
    void                        Operation_Finish(const operation& Operation); // Results of the job in the package

    // Operations: current batch (what one action of the user queued) and current job
    operation                   Operation_Current; // Operation of Job
    size_t                      Operations_Pos = 0; // Position of the current operation in the current batch, from 1
    bool                        Operations_Canceled = false; // The user canceled the current batch
    QString                     Operations_Name; // What the current batch does, in the message about its errors
    QString                     Operations_Message; // Message when the current batch ends without error
    QString                     Operations_Note; // Said after the result of the current batch (e.g. what was not opened)
    QString                     Operations_ErrorText; // Errors of the failed operations of the current batch
    QString                     Operations_Command; // FFmpeg command, shown when the current batch ends
    QString                     Job_Text;       // Text of the current job moved to the log since its last question
    bool                        Job_Refused = false; // The user answered no to a question of the current job
    QString                     Check_Failed;   // Check of a package file by file: results for its files which failed, so far
    QString                     Check_Passed;   // Check of a package file by file: results for its other files, so far
    QProgressBar*               ProgressBar = nullptr;
    QPushButton*                ProgressCancel = nullptr;
    QString                     Progress_Title; // Current operation, as shown with the progress
    QPointer<QMessageBox>       Question;       // Question of the current job, while it is shown
    QPointer<QMessageBox>       Cancel_Question; // Question about the cancellation of the current operation, while it is shown
    bool                        Close_Requested = false; // The window is closed when the current job has finished
    bool                        Close_Asking = false; // The question about closing while an operation runs is shown
    std::vector<size_t>         Close_Saved;    // Packages to close when the current batch, their save, ends without issue
    bool                        Close_Saved_Window = false; // Same for the window
    void                        Operations_Begin(const QString& Name, const QString& Message); // Names of a new batch, nothing if a batch is running
    void                        Operations_Add(const operation& Operation);
    void                        Operations_Add(operation::kind Kind, const std::vector<size_t>& List, const QString& Output = QString()); // Same operation for each package
    void                        Operations_Cancel(); // Stops the job, the other operations are dropped
    void                        Operations_End(); // Batch finished: views, messages
    QStringList                 Operation_Arguments(const operation& Operation) const; // As on the command line
    QString                     Operation_Title(const operation& Operation) const;
    void                        Progress_Show();
    void                        Progress_Text(const QString& Text); // Line under the title
    void                        Log_Move(); // Text written by the job, to the log
    bool                        Job_Ask_Overwrite(const QString& FileName, const QString& ExtraText, bool CanApplyToAll, bool& ApplyToAll);
    bool                        Job_Ask_Question(const QString& Text);
    typedef std::vector<std::pair<QMessageBox::StandardButton, QString>> button_texts; // Texts of buttons which say what they do
    QMessageBox::StandardButton Job_Ask(const QString& Text, const QString& Details, QMessageBox::StandardButtons Buttons, const button_texts& Texts = button_texts()); // While the job waits for the answer, No if the job is canceled
    bool                        Text_Show(const QString& Title, const QString& Message, const QString& Text, const QStringList& Notes, const QString& Copy, const QString& Other); // Dialog with a text in a fixed font. Buttons, if they have a text: Copy copies it, Other closes the dialog (true)
    void                        Errors_Show(const QString& Name, const QString& Text);
    void                        Command_Show(const QString& Command, const QString& Output);

    // Events
    void                        dragEnterEvent(QDragEnterEvent* Event) override;
    void                        dropEvent(QDropEvent* Event) override;
    void                        closeEvent(QCloseEvent* Event) override;
    void                        resizeEvent(QResizeEvent* Event) override;

    // Helpers
    void                        Open(const QStringList& Paths, int NotLocal); // NotLocal: count of dropped items which are not local files, for the message
    std::vector<size_t>         All() const; // All the packages
    std::vector<size_t>         Selection(gui_package::kind Kind) const; // Selected packages of this kind, all packages of this kind if none is selected
    bool                        Close(const std::vector<size_t>& List, bool IsWindow = false); // Asks about unsaved modifications, false if the packages are not closed now (IsWindow: the window is being closed)
    std::vector<size_t>         Selected(bool AllIfNone) const; // Selected packages, all packages if none is selected and AllIfNone
    size_t                      Modified_Count(const std::vector<size_t>& List) const; // Modified fields of the packages
    bool                        Modified_Lose(const std::vector<size_t>& List, const QString& Consequence, const QString& Accept); // Asks before unsaved modifications are ignored (Accept: text of the button which goes on), false if the user refuses
    void                        Save(const std::vector<size_t>& List); // Modified fields written in the files, then the packages are analyzed again
    bool                        Action_Begin(); // Before an action of the user: false if an operation is running, a value being edited is committed
    QMessageBox*                Message_Create(QWidget* Parent, QMessageBox::Icon Icon, const QString& Text, QMessageBox::StandardButtons Buttons, QMessageBox::StandardButton Default, const button_texts& Texts) const; // Question, as all the ones of the window. Escape: Cancel if there is one, else the default button
    QMessageBox::StandardButton Message_Ask(QMessageBox::Icon Icon, const QString& Text, const QString& Informative, QMessageBox::StandardButtons Buttons, QMessageBox::StandardButton Default, const button_texts& Texts = button_texts());
    bool                        Encode_Prepare(operation::kind Kind, const std::vector<size_t>& List, std::vector<operation>& Result); // Choices of the user before an encoding, false if the user cancels
    bool                        Encode_Ask(std::vector<operation>& List, bool AlwaysYes, bool AlwaysNo); // Confirmation of the encoding of several packages, with their output files; false if the user cancels
    QString                     Output_Refused(const gui_package& Package, const QString& Output) const; // Why a file can not be the output of an encoding of the package, empty if it can
    bool                        Padding_Ask(QStringList& Arguments, bool CanCheck, const QString& Accept); // Choice about padding bits (CanCheck: the full check after encoding is possible), false if the user cancels

    // Paths
    typedef QHash<QString, QString> path_cache; // Directories already asked to the system
    static QString              Path_Key(const QString& Path, bool IsDirectory, path_cache* Cache = nullptr); // What identifies a file or a directory, for comparing: its directory is named as the system names it (links followed)
    static QString              Path_Key_Parent(const QString& Key); // Key of the directory of a key, empty for a root
};

#endif
