/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GUI_OptionsH
#define GUI_OptionsH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QMap>
#include <cstddef>
//---------------------------------------------------------------------------

//***************************************************************************
// Options of the library, as the command line of rawcooked has them:
// one table describes each option (how it is shown in the preferences and
// which arguments it gives to the library), so every option of the command
// line is reachable from the user interface.
//***************************************************************************

//---------------------------------------------------------------------------
// Operations of the library, an option is given to the operations it is relevant for
enum gui_operation
{
    Operation_Analyze   = 1 << 0,   // Opening: analysis of the inputs, nothing is written
    Operation_Edit      = 1 << 1,   // Saving the modified metadata
    Operation_Encode    = 1 << 2,   // Package to Matroska file (also for showing the FFmpeg command)
    Operation_Decode    = 1 << 3,   // Matroska file to the original files
    Operation_Check     = 1 << 4,   // Check of a Matroska file
};

//---------------------------------------------------------------------------
struct gui_option
{
    // Widget of the option, and what Values holds for it (parts separated by |)
    enum type
    {
        Type_Bool,      // Check box: arguments if checked, arguments if not checked
        Type_Choice,    // List: a "label=arguments" per choice, the value is the arguments part
        Type_Text,      // Text: arguments with %1 for the text, then the regular expression of the text if any
        Type_Number,    // As Type_Text, digits only
        Type_File,      // As Type_Text, an existing file: arguments, filter of the file names, regular expression of the file name
        Type_FileSave,  // As Type_File, a file to create
        Type_TextChoice,// As Type_Text, with suggestions: arguments, then the suggested texts
        Type_Arguments, // Arguments typed as on a command line and given as is, Values is not used
    };

    // Group, Label, Help, Placeholder and the labels of the choices are translated with GUI_Options::tr()
    const char*                 Name;           // Key in the settings
    const char*                 Group;          // Tab in the preferences
    const char*                 Label;
    const char*                 Help;
    type                        Type;
    const char*                 Values;
    const char*                 Default;        // Type_Bool: "1" or "0", Type_Choice: arguments part of the default choice, else the default text
    int                         Operations;     // Combination of gui_operation
    const char*                 Placeholder;    // Text shown by an empty field, may be null
};

//---------------------------------------------------------------------------
class GUI_Options
{
    Q_DECLARE_TR_FUNCTIONS(GUI_Options)

public:
    GUI_Options(); // Values are loaded from the settings

    // Description
    static const gui_option*    List(size_t& Count); // In the order of the preferences and of the arguments
    static QStringList          Values_Parts(const gui_option& Option); // Parts of gui_option::Values
    static QString              Values_Pattern(const gui_option& Option); // Regular expression of a text (of the name of a file, without its directory), empty if any text is fine
    static bool                 Value_IsValid(const gui_option& Option, const QString& Value, QString* Why = nullptr); // False for a value the option can not have, Why then gets the reason if there is one to tell
    static QString              Where(const QString& Name); // Tab and label of an option in the preferences, for a message which sends the user to it

    // Preset of the checks: the option which decides the value of other options
    static const gui_option*    Preset();
    static QStringList          Preset_Values(const QString& Preset, const gui_option& Option); // Values the option can have after this preset, the one the preset gives first; empty if the preset does not decide it

    // Values
    QString                     Value(const QString& Name) const; // If not set: the value the preset gives, else the default value
    void                        Value_Set(const QString& Name, const QString& Value);
    bool                        Save(); // False if the settings can not be written
    static QString              Settings_FileName();

    // Arguments for the library, for an operation (options only; actions and inputs are added by the caller)
    QStringList                 Arguments(gui_operation Operation) const;

    // What the arguments of an encoding ask for, free options included
    bool                        Check_IsFull() const;           // Full check after the encoding
    bool                        CheckPadding_IsSet() const;     // Padding bits checked in all the files or not checked, else the library decides from the first file
    bool                        FrameMd5_IsSet() const;         // A framemd5 file is written
    QString                     FrameMd5_FileName() const;      // Empty: name of the first input followed by .framemd5
    QString                     Log_FileName() const;           // Also the one of a decoding and of a check; empty: no log file

    // Not stored in the settings
    QString                     LicenseKey;     // License key for this session (--license), if any

    // Other settings of the user interface
    QString                     Directory_Get() const; // Last directory used for opening
    void                        Directory_Set(const QString& Directory);

private:
    QMap<QString, QString>      Values;         // Only the values which are set
    static const gui_option*    Find(const QString& Name); // nullptr if unknown
    void                        Load();
};

#endif
