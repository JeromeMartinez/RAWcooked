/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GUI_JobH
#define GUI_JobH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Package.h"
#include <QThread>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>
//---------------------------------------------------------------------------

//***************************************************************************
// GUI_Job: one run of the library (same options and inputs as a command line
// of rawcooked) in its own thread. It is the only part of the user interface
// using the library.
// The library has process-wide state: only one job runs at a time.
//***************************************************************************

class GUI_Job : public QThread
{
    Q_OBJECT

public:
    explicit GUI_Job(QObject* Parent = nullptr);
    ~GUI_Job();

    //-----------------------------------------------------------------------
    // Configuration, before start()
    QStringList                 Arguments;              // Options and inputs as on the command line of rawcooked, without the name of the program
    bool                        AnalyzeOnly = false;    // Inputs are analyzed as for an encoding but nothing is written or run
    bool                        Metadata = false;       // Tracks get the summary of the metadata of their files
    QString                     Edits_FileName;         // If not empty, edits (--edit) are only for the track having this first file (gui_track::FileName)
    bool                        Overwrite = false;      // The encoder overwrites its output files without asking (the user already accepted it)

    // Questions of the library, called in the thread of the creator of the job while the job waits for the answer
    // Without a function the answer is no
    std::function<bool(const QString& FileName, const QString& ExtraText, bool CanApplyToAll, bool& ApplyToAll)> Ask_Overwrite; // "File <FileName> already exists<ExtraText>. Overwrite?", true if yes
    std::function<bool(const QString& Question)> Ask_Question; // true if yes

    //-----------------------------------------------------------------------
    // Results, when finished
    int                         ExitCode = 1;           // 0 if no issue
    std::vector<gui_track>      Tracks;                 // Uncompressed content found in the inputs (Name is not yet relative to the package)
    QStringList                 Attachments;            // Other files found in the inputs, as the library names them
    bool                        IsCompressed = false;   // Inputs are compressed content (Matroska)
    QStringList                 Info;                   // Compressed content: information lines
    QString                     Out;                    // Standard output of the library (information, command with --display-command)
    QString                     Err;                    // Standard error of the library (messages, errors)

    //-----------------------------------------------------------------------
    // While running, from the thread of the creator of the job
    void                        Cancel();               // Stops the processing as soon as possible, the job then finishes with an error
    bool                        IsCanceled() const;
    void                        Progress(quint64& Current, quint64& Total, QString& Text) const; // Total is 0 if unknown; Text is the current progress line of the library or of the encoder, may be empty
    QString                     Log_Take();             // Text written by the library and the encoder since the previous call (whole lines), for the log

    // Command line equivalent to the job, for the log (license keys are hidden)
    QString                     CommandLine() const;

    //-----------------------------------------------------------------------
    // Library, outside of a job (never while a job is running)
    static void                 Console_Init();         // Takes the standard output and error streams of the library, once, before anything else
    static QString              Library_Name();
    static QString              Library_Version();
    static QString              Help_Options();         // Help of the command line options, as rawcooked --help
    static QString              Help_Edits();           // List of the fields which can be edited, as rawcooked --help-edit

protected:
    void                        run() override;

private:
    struct private_data;
    private_data*               Data;
};

#endif
