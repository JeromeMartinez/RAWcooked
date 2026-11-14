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
#include <QDeadlineTimer>
#include <QStringList>
#include <deque>
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

    //-----------------------------------------------------------------------
    // Everything below is internal to GUI_Main (its implementer may change it)
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

private:
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
    view                        View_Current;
    void                        View_Refresh(view View_New = View_None);
    QLabel*                     Status_Counts;  // Counts of packages and of modified fields, in the status bar
    QString                     Status_Text;    // Temporary message of the status bar, in full
    QString                     Status_Shown;   // What the status bar has of it, for the place it had
    QDeadlineTimer              Status_End;     // When the status bar removes it
    void                        Status_Message(const QString& Text, int Timeout); // Temporary message of the status bar, within the place it has
    QPointer<QWidget>           Help;           // Help window, if it is open

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
            Command,            // Command of the encoder shown instead of run
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
    QString                     Operations_Name; // Name of the current batch in the message about its errors
    QString                     Operations_Message; // Message when the current batch ends without error
    QString                     Operations_ErrorText; // Errors of the failed operations of the current batch
    QString                     Operations_Command; // Command of the encoder, shown when the current batch ends
    QString                     Job_Text;       // Text of the current job moved to the log since its last question
    bool                        Job_Refused = false; // The user answered no to a question of the current job
    QString                     Check_Failed;   // Check of a package file by file: results for its files which failed, so far
    QString                     Check_Passed;   // Check of a package file by file: results for its other files, so far
    QProgressBar*               ProgressBar = nullptr;
    QPushButton*                ProgressCancel = nullptr;
    QString                     Progress_Title; // Current operation, as shown with the progress
    QPointer<QMessageBox>       Question;       // Question of the current job, while it is shown
    bool                        Close_Requested = false; // The window is closed when the current job has finished
    bool                        Close_Asking = false; // The question about closing while an operation runs is shown
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
    QMessageBox::StandardButton Job_Ask(const QString& Text, const QString& Details, QMessageBox::StandardButtons Buttons); // While the job waits for the answer, No if the job is canceled
    void                        Errors_Show(const QString& Name, const QString& Text);
    void                        Command_Show(const QString& Command, const QString& Output);

    // Events
    void                        dragEnterEvent(QDragEnterEvent* Event) override;
    void                        dropEvent(QDropEvent* Event) override;
    void                        closeEvent(QCloseEvent* Event) override;
    void                        resizeEvent(QResizeEvent* Event) override;

    // Helpers
    std::vector<size_t>         Selection(gui_package::kind Kind) const; // Selected packages of this kind, all packages of this kind if none is selected
    bool                        Close(const std::vector<size_t>& List); // Asks about unsaved modifications, false if the user cancels
    std::vector<size_t>         Selected(bool AllIfNone) const; // Selected packages, all packages if none is selected and AllIfNone
    size_t                      Modified_Count(const std::vector<size_t>& List) const; // Modified fields of the packages
    bool                        Modified_Lose(const std::vector<size_t>& List); // Asks before an analysis loses unsaved modifications, false if the user refuses
    void                        Save(const std::vector<size_t>& List); // Modified fields written in the files, then the packages are analyzed again
    bool                        Action_Begin(); // Before an action of the user: false if an operation is running, a value being edited is committed
    QMessageBox::StandardButton Message_Ask(QMessageBox::Icon Icon, const QString& Text, const QString& Informative, QMessageBox::StandardButtons Buttons, QMessageBox::StandardButton Default);
    bool                        Encode_Prepare(operation::kind Kind, const std::vector<size_t>& List, std::vector<operation>& Result); // Choices of the user before an encoding, false if the user cancels
    bool                        Padding_Ask(QStringList& Arguments, bool CanCheck); // Choice about padding bits (CanCheck: the full check after encoding is possible), false if the user cancels
};

#endif
