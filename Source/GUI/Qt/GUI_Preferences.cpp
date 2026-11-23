/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Preferences.h"
#include "GUI/Qt/GUI_Options.h"
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Part of the screen left around the dialog
static const QSize Screen_Margin(40, 80);

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
// Constructor
GUI_Preferences::GUI_Preferences(GUI_Options* Options_, QWidget* Parent)
: QDialog(Parent), Options(Options_)
{
    List = GUI_Options::List(List_Count);

    setWindowTitle(tr("RAWcooked - Preferences"));

    Dialog = new QDialogButtonBox(QDialogButtonBox::RestoreDefaults | QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    Dialog->button(QDialogButtonBox::RestoreDefaults)->setToolTip(tr("The options of all the tabs get their default value. Nothing is saved before Save."));
    connect(Dialog, &QDialogButtonBox::accepted, this, &GUI_Preferences::OnSave);
    connect(Dialog, &QDialogButtonBox::rejected, this, &GUI_Preferences::reject);
    connect(Dialog, &QDialogButtonBox::clicked, this, &GUI_Preferences::OnDialogClicked);

    QVBoxLayout* L = new QVBoxLayout();
    Central = new QTabWidget();

    Create();

    L->addWidget(Central);
    L->addWidget(Dialog);

    setLayout(L);

    // Room for the longest labels and for most of the options of a tab, on a small screen too
    QSize Size(860, 680);
    const QScreen* Screen = Parent ? Parent->screen() : QGuiApplication::primaryScreen();
    const QSize Available = Screen ? Screen->availableGeometry().size() : QSize();
    if (Screen)
    {
        Size.setHeight(qBound(480, Available.height() * 3 / 4, 840));
        Size = Size.boundedTo(Available - Screen_Margin);
    }

    // The help texts are wrapped at the width of their tab: a tab is not narrower than its widest option, if the screen permits it
    int Page_Width = 0;
    for (QScrollArea* Page : Pages)
        Page_Width = qMax(Page_Width, Page->widget()->minimumSizeHint().width() + Page->verticalScrollBar()->sizeHint().width());
    if (!Screen || Page_Width + 2 * Screen_Margin.width() <= Available.width())
        for (QScrollArea* Page : Pages)
            Page->setMinimumWidth(Page_Width);

    resize(Size);

    // Values
    for (size_t Pos = 0; Pos < List_Count; Pos++)
        Value_Set(Pos, Options->Value(QString::fromLatin1(List[Pos].Name)));
    Preset_Apply(false);
}

//***************************************************************************
// Actions
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Preferences::OnSave()
{
    // Nothing is saved if a value would be refused, e.g. a text which is not finished
    for (size_t Pos = 0; Pos < List_Count; Pos++)
    {
        QString Why;
        if (GUI_Options::Value_IsValid(List[Pos], Value(Pos), &Why))
            continue;

        Central->setCurrentIndex(Widgets_Page[Pos]);
        Pages[Widgets_Page[Pos]]->ensureWidgetVisible(Widgets[Pos]);
        Widgets[Pos]->setFocus();
        QMessageBox Box(QMessageBox::Warning, windowTitle(), tr("The value of \"%1\" is not valid.").arg(GUI_Options::tr(List[Pos].Label)), QMessageBox::Ok, this);
        Box.setTextFormat(Qt::PlainText);
        Box.setInformativeText(Why.isEmpty() ? GUI_Options::tr(List[Pos].Help) : Why); // Without a reason to tell, the help says what the value can be
        Box.exec();
        return;
    }

    for (size_t Pos = 0; Pos < List_Count; Pos++)
        Options->Value_Set(QString::fromLatin1(List[Pos].Name), Value(Pos));
    if (!Options->Save())
    {
        // The dialog stays: the values are used but would be lost without a word at the end of the program
        QMessageBox Box(QMessageBox::Warning, windowTitle(), tr("The preferences can not be saved."), QMessageBox::Ok, this);
        Box.setTextFormat(Qt::PlainText);
        Box.setInformativeText(tr("The file %1 can not be written. The preferences are used until the program is closed.").arg(GUI_Options::Settings_FileName()));
        Box.exec();
        Dialog->button(QDialogButtonBox::Cancel)->setText(tr("Close")); // Nothing is canceled any more
        return;
    }

    accept();
}

//---------------------------------------------------------------------------
void GUI_Preferences::OnDialogClicked(QAbstractButton* Button)
{
    if (Dialog->buttonRole(Button) != QDialogButtonBox::ResetRole)
        return;

    // Only the widgets, nothing is saved before the user asks for it
    for (size_t Pos = 0; Pos < List_Count; Pos++)
        Value_Set(Pos, QString::fromLatin1(List[Pos].Default));
    Preset_Apply(false);
}

//---------------------------------------------------------------------------
void GUI_Preferences::OnPreset_Activated()
{
    Preset_Apply(true);
}

//---------------------------------------------------------------------------
void GUI_Preferences::Browse(size_t Pos)
{
    const gui_option& Option = List[Pos];
    QLineEdit* LineEdit = static_cast<QLineEdit*>(Widgets[Pos]);

    const QString Title = tr("RAWcooked - %1").arg(GUI_Options::tr(Option.Label));
    const QString Current = QDir::fromNativeSeparators(Value(Pos));
    const QString Filter = GUI_Options::tr(GUI_Options::Values_Parts(Option).value(1).toUtf8().constData());
    QString FileName;
    if (Option.Type == gui_option::Type_File)
        FileName = QFileDialog::getOpenFileName(this, Title, Current, Filter);
    else
        FileName = QFileDialog::getSaveFileName(this, Title, Current, Filter, nullptr, QFileDialog::DontConfirmOverwrite); // Nothing is written now
    if (!FileName.isEmpty())
        LineEdit->setText(QDir::toNativeSeparators(FileName)); // As the library expects the paths
}

//***************************************************************************
// Events
//***************************************************************************

//---------------------------------------------------------------------------
bool GUI_Preferences::eventFilter(QObject* Object, QEvent* Event)
{
    // The wheel scrolls the tab, it does not change the list which happens to be under the pointer
    if (Event->type() == QEvent::Wheel && Object->isWidgetType() && !static_cast<QWidget*>(Object)->hasFocus())
    {
        Event->ignore();
        return true;
    }

    return QDialog::eventFilter(Object, Event);
}

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Preferences::Create()
{
    Widgets.resize(List_Count);
    Widgets_Page.resize(List_Count);

    // Help texts are aligned with the text of the check boxes
    const int Help_Indent = style()->pixelMetric(QStyle::PM_IndicatorWidth) + style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing);

    QStringList Groups; // A tab per group, in the order of the first option of each group
    std::vector<QGridLayout*> Grids;
    std::vector<int> Rows;
    for (size_t Pos = 0; Pos < List_Count; Pos++)
    {
        const gui_option& Option = List[Pos];

        // Tab
        int Page = Groups.indexOf(QLatin1String(Option.Group));
        if (Page < 0)
        {
            QWidget* Content = new QWidget();
            QGridLayout* Grid = new QGridLayout(Content);
            Grid->setColumnStretch(1, 1);
            QScrollArea* ScrollArea = new QScrollArea();
            ScrollArea->setFrameShape(QFrame::NoFrame);
            ScrollArea->setWidgetResizable(true); // The help texts are wrapped at the width of the tab
            ScrollArea->setWidget(Content);
            ScrollArea->viewport()->setAutoFillBackground(false); // Background of the tab, as for a tab without scroll area
            Content->setAutoFillBackground(false);
            Central->addTab(ScrollArea, GUI_Options::tr(Option.Group));

            Page = Groups.size();
            Groups += QLatin1String(Option.Group);
            Pages.push_back(ScrollArea);
            Grids.push_back(Grid);
            Rows.push_back(0);
        }
        Widgets_Page[Pos] = Page;
        QGridLayout* Grid = Grids[Page];
        int& Row = Rows[Page];

        // Label and widget
        const QString Help = GUI_Options::tr(Option.Help);
        const QString ToolTip = Help.isEmpty() ? QString() : (QStringLiteral("<qt>") + Help.toHtmlEscaped() + QStringLiteral("</qt>")); // Rich text, for a wrapped tool tip
        const QString Placeholder = Option.Placeholder ? GUI_Options::tr(Option.Placeholder) : QString(); // An empty field says what it stands for
        const QStringList Parts = GUI_Options::Values_Parts(Option);
        QWidget* Widget = nullptr;
        if (Option.Type == gui_option::Type_Bool)
        {
            Widget = new QCheckBox(GUI_Options::tr(Option.Label));
            Grid->addWidget(Widget, Row, 0, 1, 2);
        }
        else
        {
            QLabel* Label = new QLabel(tr("%1:").arg(GUI_Options::tr(Option.Label)));
            Label->setToolTip(ToolTip);
            Grid->addWidget(Label, Row, 0);

            switch (Option.Type)
            {
                case gui_option::Type_Choice:
                {
                    QComboBox* ComboBox = new QComboBox();
                    for (const QString& Choice : Parts)
                        ComboBox->addItem(GUI_Options::tr(Choice.section(QLatin1Char('='), 0, 0).toUtf8().constData()), Choice.section(QLatin1Char('='), 1));
                    ComboBox->setMinimumWidth(ComboBox->sizeHint().width() + ComboBox->fontMetrics().averageCharWidth() * 2); // Else the longest text is cut with some scale factors of the screen
                    ComboBox->setFocusPolicy(Qt::StrongFocus);
                    ComboBox->installEventFilter(this);
                    if (&Option == GUI_Options::Preset())
                        connect(ComboBox, QOverload<int>::of(&QComboBox::activated), this, &GUI_Preferences::OnPreset_Activated);
                    Grid->addWidget(ComboBox, Row, 1, Qt::AlignLeft);
                    Widget = ComboBox;
                    break;
                }
                case gui_option::Type_TextChoice:
                {
                    QComboBox* ComboBox = new QComboBox();
                    ComboBox->setEditable(true);
                    ComboBox->setInsertPolicy(QComboBox::NoInsert);
                    ComboBox->setCompleter(nullptr); // What the user types is not replaced by a suggestion
                    for (int i = 1; i < Parts.size(); i++)
                        ComboBox->addItem(Parts[i]);
                    ComboBox->lineEdit()->setPlaceholderText(Placeholder);
                    ComboBox->setMinimumContentsLength(26); // The longest suggestion; the width of the tab gives room for a longer text
                    ComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                    ComboBox->setFocusPolicy(Qt::StrongFocus);
                    ComboBox->installEventFilter(this);
                    Grid->addWidget(ComboBox, Row, 1);
                    Widget = ComboBox;
                    break;
                }
                case gui_option::Type_File:
                case gui_option::Type_FileSave:
                {
                    QLineEdit* LineEdit = new QLineEdit();
                    LineEdit->setPlaceholderText(Placeholder);
                    QToolButton* Button = new QToolButton();
                    Button->setObjectName(QString::fromLatin1(Option.Name) + QStringLiteral("_Browse"));
                    Button->setText(QStringLiteral("..."));
                    Button->setToolTip(tr("Choose a file"));
                    connect(Button, &QToolButton::clicked, this, [this, Pos]() { Browse(Pos); });
                    QHBoxLayout* Layout = new QHBoxLayout();
                    Layout->addWidget(LineEdit);
                    Layout->addWidget(Button);
                    Grid->addLayout(Layout, Row, 1);
                    Widget = LineEdit;
                    break;
                }
                default:
                {
                    QLineEdit* LineEdit = new QLineEdit();
                    LineEdit->setPlaceholderText(Placeholder);
                    const QString Pattern = GUI_Options::Values_Pattern(Option);
                    if (!Pattern.isEmpty())
                        LineEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("(?:") + Pattern + QStringLiteral(")?")), LineEdit)); // No text is fine: the option is not used
                    if (Option.Type == gui_option::Type_Arguments)
                        Grid->addWidget(LineEdit, Row, 1);
                    else
                    {
                        LineEdit->setFixedWidth(LineEdit->fontMetrics().averageCharWidth() * 24); // Numbers and short texts
                        Grid->addWidget(LineEdit, Row, 1, Qt::AlignLeft);
                    }
                    Widget = LineEdit;
                }
            }
            Label->setBuddy(Widget);
        }
        Widget->setObjectName(QString::fromLatin1(Option.Name));
        Widget->setToolTip(ToolTip);
        Widgets[Pos] = Widget;
        Row++;

        // Help
        if (!Help.isEmpty())
        {
            QLabel* Label = new QLabel(Help);
            Label->setTextFormat(Qt::PlainText);
            Label->setWordWrap(true);
            QPalette Palette = Label->palette();
            QColor Color = Palette.color(QPalette::WindowText);
            Color.setAlpha(160); // Grey, with a light or a dark theme
            Palette.setColor(QPalette::WindowText, Color);
            Label->setPalette(Palette);
            QFont Font = Label->font();
            if (Font.pointSizeF() > 8)
                Font.setPointSizeF(qMax(8.0, Font.pointSizeF() * 0.9)); // Smaller, as long as it stays readable
            Label->setFont(Font);
            Label->setContentsMargins(Help_Indent, 0, 0, Help_Indent / 2);
            Grid->addWidget(Label, Row, 0, 1, 2);
            Row++;
        }
    }

    // Options at the top of their tab
    for (size_t Page = 0; Page < Grids.size(); Page++)
        Grids[Page]->setRowStretch(Rows[Page], 1);
}

//---------------------------------------------------------------------------
QString GUI_Preferences::Value(size_t Pos) const
{
    switch (List[Pos].Type)
    {
        case gui_option::Type_Bool:
            return static_cast<QCheckBox*>(Widgets[Pos])->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
        case gui_option::Type_Choice:
            return static_cast<QComboBox*>(Widgets[Pos])->currentData().toString();
        case gui_option::Type_TextChoice:
            return static_cast<QComboBox*>(Widgets[Pos])->currentText().trimmed();
        case gui_option::Type_File:
        case gui_option::Type_FileSave:
        {
            // A path copied from elsewhere may come with its quotes
            QString Text = static_cast<QLineEdit*>(Widgets[Pos])->text().trimmed();
            if (Text.size() >= 2 && Text.startsWith(QLatin1Char('"')) && Text.endsWith(QLatin1Char('"')))
                Text = Text.mid(1, Text.size() - 2).trimmed();
            return Text;
        }
        default:
            return static_cast<QLineEdit*>(Widgets[Pos])->text().trimmed();
    }
}

//---------------------------------------------------------------------------
void GUI_Preferences::Value_Set(size_t Pos, const QString& Value)
{
    switch (List[Pos].Type)
    {
        case gui_option::Type_Bool:
            static_cast<QCheckBox*>(Widgets[Pos])->setChecked(Value == QLatin1String("1"));
            break;
        case gui_option::Type_Choice:
        {
            QComboBox* ComboBox = static_cast<QComboBox*>(Widgets[Pos]);
            ComboBox->setCurrentIndex(ComboBox->findData(Value));
            break;
        }
        case gui_option::Type_TextChoice:
        {
            QComboBox* ComboBox = static_cast<QComboBox*>(Widgets[Pos]);
            ComboBox->setCurrentIndex(ComboBox->findText(Value)); // No suggestion selected for another text
            ComboBox->setEditText(Value);
            break;
        }
        default:
            static_cast<QLineEdit*>(Widgets[Pos])->setText(Value);
    }
}

//---------------------------------------------------------------------------
void GUI_Preferences::Preset_Apply(bool SetValues)
{
    QString Preset;
    for (size_t Pos = 0; Pos < List_Count; Pos++)
        if (List + Pos == GUI_Options::Preset())
            Preset = Value(Pos);

    // What is shown is what is run: a value which can not follow the preset can not be chosen
    for (size_t Pos = 0; Pos < List_Count; Pos++)
    {
        const QStringList Values = GUI_Options::Preset_Values(Preset, List[Pos]); // Empty: any value
        if (!Values.isEmpty() && (SetValues || !Values.contains(Value(Pos))))
            Value_Set(Pos, Values.first());

        if (List[Pos].Type == gui_option::Type_Bool)
            Widgets[Pos]->setEnabled(Values.size() != 1);
        if (List[Pos].Type == gui_option::Type_Choice)
        {
            const QComboBox* ComboBox = static_cast<QComboBox*>(Widgets[Pos]);
            const QStandardItemModel* Model = qobject_cast<QStandardItemModel*>(ComboBox->model());
            for (int i = 0; Model && i < ComboBox->count(); i++)
                Model->item(i)->setEnabled(Values.isEmpty() || Values.contains(ComboBox->itemData(i).toString()));
        }
    }
}
