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
    // Configuration, before Start()
    QStringList                 Arguments;              // Options and inputs as on the command line of rawcooked, without the name of the program
    bool                        AnalyzeOnly = false;    // Inputs are analyzed as for an encoding but nothing is written or run
    bool                        Metadata = false;       // Tracks get the summary of the metadata of their files
    QString                     Edits_FileName;         // If not empty, edits (--edit) are only for the track having this first file (gui_track::FileName)
    quint64                     Edits_FileCount = 0;    // With Edits_FileName: count of files of this track as analyzed (sequences), nothing is written if it changed; 0 if unknown
    bool                        Overwrite = false;      // The encoder overwrites its output files without asking (the user already accepted it)

    // Questions of the library, called in the thread of the creator of the job while the job waits for the answer (true if yes)
    // Without a function the answer is no
    // Ask_Overwrite: "File <FileName> already exists<ExtraText>. Overwrite?", also for the log file of --log-name (FileName as given to the option)
    std::function<bool(const QString& FileName, const QString& ExtraText, bool CanApplyToAll, bool& ApplyToAll)> Ask_Overwrite;
    std::function<bool(const QString& Question)> Ask_Question;

    //-----------------------------------------------------------------------
    // Results, when finished
    int                         ExitCode = 1;           // 0 if no issue
    bool                        IsLicenseIgnored = false; // The library ignores the license (a build which does so, for each of its runs): tracks with IsNotLicensed are processed anyway
    std::vector<gui_track>      Tracks;                 // Uncompressed content found in the inputs (Name is not yet relative to the package)
    QStringList                 Attachments;            // Other files found in the inputs, as the library names them
    bool                        IsCompressed = false;   // Inputs are compressed content (Matroska): Compressed is not empty and there is no track, also when the run failed
    std::vector<gui_compressed> Compressed;             // Matroska files of the inputs (also with tracks), output of an encoding with the full check; also when a check found issues,
                                                        // not when the parsing was stopped (cancellation, decoders missing). Name is not yet relative
    QString                     Out;                    // Standard output of the library (information, command with --display-command)
    QString                     Err;                    // Standard error of the library (messages, errors) and output of the encoder

    // Results also known inside Ask_Overwrite and Ask_Question, as they are so far
    bool                        IsEncoderStarted = false; // The program of the encoding was started (not its version request): its output file may exist and be incomplete
    bool                        IsEncoded = false;      // The encoder ended with the exit status 0: its output file is complete (the check after it may still fail or be canceled)
    bool                        IsEncoderMissing = false; // The program of the encoder could not be run (not found, not executable)
    // Messages: lines, a message is its first line and the lines indented under it (e.g. file names), in the order they were written;
    // a message written on both streams, or before a question and again at the end, is there once
    QStringList                 Messages_Error;         // Errors; the last lines of the output if the run failed without any
    bool                        Messages_Error_IsOutput = false; // Messages_Error is the end of the output (never after Cancel() or the answer no to a question).
                                                        // Also some errors of the library without "Error:" (e.g. an invalid value of an edit, EXR refused without --check)
    QStringList                 Messages_Warning;
    QStringList                 Messages_Info;          // Results of checks, hashes, statistics... (none with --quiet)

    //-----------------------------------------------------------------------
    // While running, from the thread of the creator of the job
    enum phase
    {
        Phase_Files,            // The inputs are read (files analyzed or edited, start of any run)
        Phase_Encoder,          // The encoder runs
        Phase_Check,            // After the encoder (check of its output), or compressed content is read (check, decoding)
    };
    bool                        Start();                // Instead of start(). False if the thread can not be created: the job never finishes, its results are the ones of a failure
    phase                       Phase() const;          // Without encoder, Phase_Check is known from the progress line of the library: not with --quiet
    void                        Cancel();               // Stops the processing as soon as possible, the job then finishes with an error; a waiting question is answered no, wait() returns
    bool                        IsCanceled() const;
    void                        Progress(quint64& Current, quint64& Total, QString& Text) const; // Total is 0 if unknown; Text: progress line of the library or of the encoder, may be empty
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
