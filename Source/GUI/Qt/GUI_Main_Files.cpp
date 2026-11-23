/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Main_Files.h"
#include "GUI/Qt/GUI_Main.h"
#include <QHeaderView>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QStyle>
#include <QStyledItemDelegate>
#include <climits>
//---------------------------------------------------------------------------

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
enum
{
    Column_Name,
    Column_Type,
    Column_Files,
    Column_FrameSize,
    Column_FrameRate,
    Column_Duration,
    Column_Status,
    Column_Max
};

//---------------------------------------------------------------------------
// Kind of an item (type of the item) and what it shows (data of its first column)
enum
{
    Item_Package = QTreeWidgetItem::UserType,
    Item_Track,
    Item_Attachment,
    Item_Compressed,
};
enum
{
    Role_Package = Qt::UserRole,    // Index in the packages
    Role_Track,                     // Tracks: index in the tracks of the package
    Role_Key,                       // Packages: what was opened, same after a rebuild of the tree
    Role_Elide,                     // Names: where the text is cut when the column is too narrow for it
};

//---------------------------------------------------------------------------
const int ToolTip_Lines_Max = 30;       // Lines of the messages of a package shown in a tool tip
const int Hint_Margin = 16;             // Pixels around the text of a view without content

//---------------------------------------------------------------------------
// Look of the cells
class GUI_Main_Files_Delegate : public QStyledItemDelegate
{
public:
    explicit GUI_Main_Files_Delegate(QObject* Parent) : QStyledItemDelegate(Parent) {}

protected:
    void initStyleOption(QStyleOptionViewItem* Option, const QModelIndex& Index) const override
    {
        QStyledItemDelegate::initStyleOption(Option, Index);

        const QVariant Elide = Index.data(Role_Elide);
        if (Elide.isValid())
            Option->textElideMode = (Qt::TextElideMode)Elide.toInt();
        GUI_Main_Files::FocusFrame_Hide(Option);
    }
};

//---------------------------------------------------------------------------
// Tool tip showing a text as it is (no markup, line ends kept), limited to its first lines
static QString ToolTip_Text(const QString& Text)
{
    QStringList Lines = Text.split(QLatin1Char('\n'));
    while (!Lines.isEmpty() && Lines.last().trimmed().isEmpty())
        Lines.removeLast();
    if (Lines.isEmpty())
        return QString();
    if (Lines.size() > ToolTip_Lines_Max)
    {
        Lines.erase(Lines.begin() + ToolTip_Lines_Max, Lines.end());
        Lines.append(QStringLiteral("..."));
    }
    return QStringLiteral("<p style='white-space:pre-wrap'>") + Lines.join(QLatin1Char('\n')).toHtmlEscaped() + QStringLiteral("</p>");
}

//---------------------------------------------------------------------------
// Count for a text with a plural (a count of files is far under the limit)
static int Count_Get(quint64 Count)
{
    return (int)qMin<quint64>(Count, INT_MAX);
}

//---------------------------------------------------------------------------
// Place in the packages of the track shown by an item, false if the item is not a track
static bool Item_Track_Get(const QTreeWidgetItem* Item, const GUI_Main* Main, size_t& Package, size_t& Track)
{
    if (!Item || Item->type() != Item_Track)
        return false;
    const size_t Package_Pos = (size_t)Item->data(Column_Name, Role_Package).toULongLong();
    const size_t Track_Pos = (size_t)Item->data(Column_Name, Role_Track).toULongLong();
    if (Package_Pos >= Main->Packages.size() || Track_Pos >= Main->Packages[Package_Pos].Tracks.size())
        return false; // The content changed since the last refresh
    Package = Package_Pos;
    Track = Track_Pos;
    return true;
}

//---------------------------------------------------------------------------
// Result of the last operation and what is waiting for a save
static QString Package_Status(const gui_package& Package)
{
    const int Modified = Count_Get(Package.Modified_Count());
    if (!Modified)
        return Package.Status;
    const QString Text = Modified == 1 ? GUI_Main_Files::tr("%n modified field not saved", nullptr, Modified) : GUI_Main_Files::tr("%n modified fields not saved", nullptr, Modified);
    return Package.Status.isEmpty() ? Text : Package.Status + QLatin1String("; ") + Text;
}

//---------------------------------------------------------------------------
// The status in full (its cell may be too narrow for it), then the messages of the last operation if they say more
static QString Package_Status_Details(const gui_package& Package)
{
    const QString Status = Package_Status(Package);
    const QString Messages = Package.Messages.trimmed();
    if (Messages.isEmpty() || Messages == Package.Status.trimmed())
        return Status;
    if (!Package.Status.trimmed().isEmpty() && Messages.startsWith(Package.Status.trimmed()) && !Package.Modified_Count())
        return Messages; // The status is the first message
    return Status + QLatin1Char('\n') + Messages;
}

//---------------------------------------------------------------------------
// What is known about a Matroska file to decode: in one line, and in full for its tool tip
static void Compressed_Status(const gui_compressed& Compressed, const QString& Name, QString& Text, QString& Details)
{
    if (!Compressed.HasReversibilityData)
    {
        Text = GUI_Main_Files::tr("%1: not created by RAWcooked, it can not be decoded to the original files").arg(Name);
        Details = GUI_Main_Files::tr("No reversibility data found: the file was not created by RAWcooked, it can not be decoded to the original files.");
        return;
    }
    const QString Creator = Compressed.LibraryNameVersion.isEmpty() ? QStringLiteral("RAWcooked") : Compressed.LibraryNameVersion;
    QStringList Texts, Lines;
    Texts += (Compressed.HasHashes ? GUI_Main_Files::tr("%1: created by %2, with the hashes of the original files") : GUI_Main_Files::tr("%1: created by %2, without the hashes of the original files")).arg(Name, Creator);
    Lines += GUI_Main_Files::tr("Reversibility data created by %1: the file can be decoded to the original files.").arg(Creator);
    Lines += Compressed.HasHashes ? GUI_Main_Files::tr("Hashes of the original files present: Actions > Check compares the decoded files with them.")
                                  : GUI_Main_Files::tr("No hashes of the original files: with Actions > Check only the decoding is checked, Actions > Check against the original files... compares the decoded files with them.");
    if (Compressed.HashFiles_Count)
    {
        const int Count = Count_Get(Compressed.HashFiles_Count);
        Texts += Count == 1 ? GUI_Main_Files::tr("%n hash file in the attachments", nullptr, Count) : GUI_Main_Files::tr("%n hash files in the attachments", nullptr, Count);
        Lines += Count == 1 ? GUI_Main_Files::tr("%n hash file in the attachments (used by the conformance check).", nullptr, Count)
                            : GUI_Main_Files::tr("%n hash files in the attachments (used by the conformance check).", nullptr, Count);
    }
    switch (Compressed.Checked)
    {
        case gui_compressed::Checked_Decoding:
            Texts += GUI_Main_Files::tr("decoding checked");
            Lines += GUI_Main_Files::tr("Last operation: the decoding was checked, no issue detected.");
            break;
        case gui_compressed::Checked_Reversibility:
            Texts += GUI_Main_Files::tr("reversibility checked");
            Lines += GUI_Main_Files::tr("Last operation: the reversibility was checked, no issue detected.");
            break;
        default:;
    }
    if (!Compressed.OutputDirectory.isEmpty())
    {
        Texts += GUI_Main_Files::tr("decoded to %1").arg(Compressed.OutputDirectory);
        Lines += GUI_Main_Files::tr("Last operation: files decoded to %1").arg(Compressed.OutputDirectory);
    }
    Text = Texts.join(QLatin1String("; "));
    Details = Lines.join(QLatin1Char('\n'));
}

//---------------------------------------------------------------------------
// HH:MM:SS.mmm, empty if not known
static QString Track_Duration(double Seconds)
{
    if (!(Seconds > 0) || Seconds >= 360000000) // Also if not a number
        return QString();
    const quint64 Milliseconds = (quint64)(Seconds * 1000 + 0.5);
    return QStringLiteral("%1:%2:%3.%4")
        .arg(Milliseconds / 3600000, 2, 10, QLatin1Char('0'))
        .arg(Milliseconds / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(Milliseconds / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(Milliseconds % 1000, 3, 10, QLatin1Char('0'));
}

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
GUI_Main_Files::GUI_Main_Files(GUI_Main* Main_)
    : QTreeWidget(Main_)
    , Main(Main_)
    , Icon_Directory(style()->standardIcon(QStyle::SP_DirIcon))
    , Icon_File(style()->standardIcon(QStyle::SP_FileIcon))
    , Columns_User(Column_Max, false)
{
    // Configuration
    setColumnCount(Column_Max);
    setHeaderLabels(QStringList() << tr("Name") << tr("Type") << tr("Files") << tr("Frame size") << tr("Frame rate") << tr("Duration") << tr("Status"));
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setAlternatingRowColors(true);
    setUniformRowHeights(true);
    setSortingEnabled(false);
    setItemDelegate(new GUI_Main_Files_Delegate(this));
    header()->setSectionsMovable(false);
    header()->setStretchLastSection(true); // The status, which may be long

    connect(this, &QTreeWidget::itemSelectionChanged, this, [this]()
    {
        if (!Refreshing)
            Q_EMIT Selection_Changed();
    });
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* Item, int)
    {
        size_t Package, Track;
        if (Item_Track_Get(Item, Main, Package, Track))
            Q_EMIT Track_Activated(Package, Track);
    });
    connect(this, &QTreeWidget::itemExpanded, this, [this]()
    {
        if (Refreshing)
            return;
        Columns_Measure(); // More content is shown
        Columns_Fit();
    });
    connect(header(), &QHeaderView::sectionResized, this, [this](int Column)
    {
        if (!Columns_Fitting && !Columns_Wanted.empty() && Column >= 0 && Column < Column_Status && isVisible())
            Columns_User[Column] = true; // A width set by the user, for the content which is shown
    });
}

//***************************************************************************
// Content
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main_Files::Refresh()
{
    // What the user selected is found again after the rebuild: by position in the list for the selection, by name for the closed packages
    struct place
    {
        int                     Package;
        int                     Child; // -1 for the package itself
    };
    const auto Place_Get = [this](QTreeWidgetItem* Item)
    {
        QTreeWidgetItem* Parent = Item->parent();
        return Parent ? place{ indexOfTopLevelItem(Parent), Parent->indexOfChild(Item) } : place{ indexOfTopLevelItem(Item), -1 };
    };
    const auto Place_Item = [this](const place& Place)
    {
        QTreeWidgetItem* Item = topLevelItem(Place.Package);
        if (Item && Place.Child >= 0 && Place.Child < Item->childCount())
            Item = Item->child(Place.Child); // Else the package: its content is not the same
        return Item;
    };
    const std::vector<size_t> Selection_Before = Selection();
    std::vector<place> Selected;
    const QList<QTreeWidgetItem*> Selected_Items = selectedItems();
    for (QTreeWidgetItem* Item : Selected_Items)
        Selected.push_back(Place_Get(Item));
    const place Current = currentItem() ? Place_Get(currentItem()) : place{ -1, -1 };
    const int Current_Column = currentColumn();
    QSet<QString> Collapsed;
    for (int i = 0; i < topLevelItemCount(); i++)
        if (!topLevelItem(i)->isExpanded())
            Collapsed.insert(topLevelItem(i)->data(Column_Name, Role_Key).toString());
    const int Scroll = verticalScrollBar()->value();

    // Rebuild
    Refreshing = true;
    setUpdatesEnabled(false);
    clear();
    for (size_t i = 0; i < Main->Packages.size(); i++)
    {
        QTreeWidgetItem* Item = Package_Add(i);
        Item->setExpanded(!Collapsed.contains(Item->data(Column_Name, Role_Key).toString()));
    }

    // Back to what the user selected
    if (QTreeWidgetItem* Item = Place_Item(Current))
        setCurrentItem(Item, Current_Column < 0 ? 0 : Current_Column, QItemSelectionModel::NoUpdate);
    for (const place& Place : Selected)
        if (QTreeWidgetItem* Item = Place_Item(Place))
            Item->setSelected(true);
    doItemsLayout(); // The range of the scroll bar is the one of the new content
    verticalScrollBar()->setValue(Scroll);
    setUpdatesEnabled(true);
    Refreshing = false;

    Columns_Measure();
    Columns_Fit();

    if (Selection() != Selection_Before)
        Q_EMIT Selection_Changed();
}

//---------------------------------------------------------------------------
// Items of a package and of its content, at the end of the list
QTreeWidgetItem* GUI_Main_Files::Package_Add(size_t Pos)
{
    const gui_package& Package = Main->Packages[Pos];
    const QString Key = Package.Inputs.join(QLatin1Char('\n'));
    const QBrush Error(Color_Error(palette()));
    const QBrush Warning(Color_Warning(palette()));

    // Package
    QTreeWidgetItem* Item = new QTreeWidgetItem(Item_Package);
    Item->setData(Column_Name, Role_Package, (qulonglong)Pos);
    Item->setData(Column_Name, Role_Key, Key);
    Item->setData(Column_Name, Role_Elide, (int)Qt::ElideMiddle);
    Item->setText(Column_Name, Package.Name());
    Item->setToolTip(Column_Name, ToolTip_Text(Key));
    Item->setIcon(Column_Name, Package.IsDirectory ? Icon_Directory : Icon_File);
    if (Package.Kind == gui_package::Kind_Compressed)
        Item->setText(Column_Type, Package.Inputs.size() > 1 ? tr("Matroska files") : tr("Matroska file"));
    else if (Package.IsDirectory)
        Item->setText(Column_Type, tr("Directory"));
    else
        Item->setText(Column_Type, Package.Inputs.size() > 1 ? tr("Files") : tr("File"));
    Item->setText(Column_Status, Package_Status(Package));
    Item->setToolTip(Column_Status, ToolTip_Text(Package_Status_Details(Package)));
    if (Package.IsError || Package.IsNotEncodable)
        Item->setForeground(Column_Status, Error);
    else if (Package.HasWarnings)
        Item->setForeground(Column_Status, Warning);

    // Tracks
    for (size_t j = 0; j < Package.Tracks.size(); j++)
    {
        const gui_track& Track = Package.Tracks[j];
        QTreeWidgetItem* Child = new QTreeWidgetItem(Item, Item_Track);
        Child->setData(Column_Name, Role_Package, (qulonglong)Pos);
        Child->setData(Column_Name, Role_Track, (qulonglong)j);
        Child->setData(Column_Name, Role_Elide, (int)Qt::ElideLeft); // Tracks of a package often differ by the directory nearest to their files
        Child->setText(Column_Name, Track.Name);
        QString Tip = Track.FileName;
        if (!Track.Fields.empty())
            Tip += QLatin1Char('\n') + tr("Double-click to see the metadata");
        Child->setToolTip(Column_Name, ToolTip_Text(Tip));
        Child->setText(Column_Type, Track.Flavor);
        Child->setToolTip(Column_Type, ToolTip_Text(Track.Flavor));
        Child->setText(Column_Files, Track_Files(Track));
        Child->setToolTip(Column_Files, ToolTip_Text(Track_Files(Track)));
        if (Track.Width && Track.Height)
            Child->setText(Column_FrameSize, QStringLiteral("%1x%2").arg(Track.Width).arg(Track.Height));
        Child->setText(Column_FrameRate, Track.FrameRate);
        Child->setText(Column_Duration, Track_Duration(Track.Duration));
        QStringList Texts, Details;
        if (Track.IsNotLicensed)
        {
            // Refused by an encoding, unless the library does not apply the license
            Texts += Package.IsLicenseIgnored ? tr("Not in the license (ignored for the moment)") : tr("Not in the license");
            Details += (Package.IsLicenseIgnored ? tr("%1 is not in the current license. The license is ignored for the moment: the track is encoded.")
                                                 : tr("%1 is not in the current license: the package can not be encoded.")).arg(Track.Flavor);
            Details += tr("Options > License... shows the license and takes a key. Contact info@mediaarea.net for a quote or a temporary key.");
            if (!Package.IsLicenseIgnored)
                Child->setForeground(Column_Status, Error);
        }
        if (Track.NeedsCheck)
        {
            Texts += tr("Full check needed");
            Details += tr("%1 is encoded only with the full check of reversibility after the encoding (preference %2).").arg(Track.Flavor, GUI_Options::Where(QStringLiteral("Check")));
        }
        if (!Track.IsMetadataComplete)
        {
            Texts += tr("The metadata of some files could not be read");
            Details += Texts.last();
        }
        Child->setText(Column_Status, Texts.join(QLatin1String("; ")));
        Child->setToolTip(Column_Status, ToolTip_Text(Details.join(QLatin1Char('\n'))));
    }

    // Attachments
    for (const QString& Attachment : Package.Attachments)
    {
        QTreeWidgetItem* Child = new QTreeWidgetItem(Item, Item_Attachment);
        Child->setData(Column_Name, Role_Package, (qulonglong)Pos);
        Child->setData(Column_Name, Role_Elide, (int)Qt::ElideLeft);
        Child->setText(Column_Name, Attachment);
        Child->setToolTip(Column_Name, ToolTip_Text(Attachment));
        Child->setText(Column_Type, tr("Attachment"));
    }

    // Matroska files
    QList<QTreeWidgetItem*> Sentences;
    for (const gui_compressed& Compressed : Package.Compressed)
    {
        QTreeWidgetItem* Child = new QTreeWidgetItem(Item, Item_Compressed);
        Child->setData(Column_Name, Role_Package, (qulonglong)Pos);
        const QString Name = Compressed.Name.isEmpty() ? Compressed.FileName : Compressed.Name;
        if (Package.Kind == gui_package::Kind_Uncompressed)
        {
            // Among content to encode: an encoding of the package leaves it out
            Child->setData(Column_Name, Role_Elide, (int)Qt::ElideLeft);
            Child->setText(Column_Name, Name);
            Child->setToolTip(Column_Name, ToolTip_Text(Compressed.FileName));
            Child->setText(Column_Type, tr("Matroska file"));
            Child->setText(Column_Status, tr("Not encoded with the package"));
            Child->setToolTip(Column_Status, ToolTip_Text(tr("A Matroska file is not encoded: it will not be in the Matroska file of the package.\nOpened alone, it can be checked and decoded.")));
            Child->setForeground(Column_Status, Warning);
            continue;
        }

        // Content to decode: what is known about it
        QString Text, Details;
        Compressed_Status(Compressed, Name, Text, Details);
        Child->setText(Column_Name, Text);
        Child->setToolTip(Column_Name, ToolTip_Text(Compressed.FileName + QLatin1Char('\n') + Details));
        if (!Compressed.HasReversibilityData)
            Child->setForeground(Column_Name, Error);
        Sentences.append(Child);
    }

    addTopLevelItem(Item);
    for (QTreeWidgetItem* Child : Sentences)
        Child->setFirstColumnSpanned(true); // Not a name: it takes the whole line
    return Item;
}

//***************************************************************************
// Selection
//***************************************************************************

//---------------------------------------------------------------------------
std::vector<size_t> GUI_Main_Files::Selection() const
{
    std::vector<size_t> List;
    for (int i = 0; i < topLevelItemCount(); i++)
    {
        const QTreeWidgetItem* Item = topLevelItem(i);
        bool IsSelected = Item->isSelected();
        for (int j = 0; !IsSelected && j < Item->childCount(); j++)
            IsSelected = Item->child(j)->isSelected();
        const size_t Package = (size_t)Item->data(Column_Name, Role_Package).toULongLong();
        if (IsSelected && Package < Main->Packages.size()) // Not if packages were removed since the last refresh
            List.push_back(Package);
    }
    return List;
}

//***************************************************************************
// Look which the views share
//***************************************************************************

//---------------------------------------------------------------------------
QString GUI_Main_Files::Track_Files(const gui_track& Track)
{
    if (!Track.FileCount)
        return QString();
    const int Count = Count_Get(Track.FileCount);
    QString Text = Count == 1 ? tr("%n file", nullptr, Count) : tr("%n files", nullptr, Count);
    if (!Track.Numbers.isEmpty())
        Text += QLatin1String(" (") + Track.Numbers + QLatin1Char(')');
    return Text;
}

//---------------------------------------------------------------------------
QColor GUI_Main_Files::Color_Error(const QPalette& Palette)
{
    return Palette.color(QPalette::Base).lightness() < 128 ? QColor(255, 110, 110) : QColor(200, 0, 0);
}

//---------------------------------------------------------------------------
QColor GUI_Main_Files::Color_Warning(const QPalette& Palette)
{
    return Palette.color(QPalette::Base).lightness() < 128 ? QColor(255, 180, 80) : QColor(152, 96, 0);
}

//---------------------------------------------------------------------------
void GUI_Main_Files::Hint_Paint(QAbstractScrollArea* View, const QString& Text)
{
    QPainter Painter(View->viewport());
    QFont Font = View->font();
    if (Font.pointSizeF() > 0)
        Font.setPointSizeF(Font.pointSizeF() * 1.25);
    Painter.setFont(Font);
    Painter.setPen(View->palette().color(QPalette::PlaceholderText));
    Painter.drawText(View->viewport()->rect().adjusted(Hint_Margin, Hint_Margin, -Hint_Margin, -Hint_Margin), Qt::AlignCenter | Qt::TextWordWrap, Text);
}

//---------------------------------------------------------------------------
void GUI_Main_Files::FocusFrame_Hide(QStyleOptionViewItem* Option)
{
    if (!(Option->state & QStyle::State_KeyboardFocusChange))
        Option->state &= ~QStyle::State_HasFocus;
}

//***************************************************************************
// Events
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main_Files::changeEvent(QEvent* Event)
{
    QTreeWidget::changeEvent(Event);

    // The colors of the texts are chosen for the background: again for the one of a new theme
    if (Event->type() == QEvent::PaletteChange && topLevelItemCount())
        Refresh();
}

//---------------------------------------------------------------------------
void GUI_Main_Files::keyPressEvent(QKeyEvent* Event)
{
    if (!(Event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
    {
        switch (Event->key())
        {
            case Qt::Key_Return:
            case Qt::Key_Enter:
            {
                size_t Package, Track;
                if (Item_Track_Get(currentItem(), Main, Package, Track))
                {
                    Event->accept();
                    Q_EMIT Track_Activated(Package, Track);
                    return;
                }
                break;
            }
            case Qt::Key_Delete:
            #if defined(Q_OS_MACOS)
            case Qt::Key_Backspace: // The delete key of the keyboards of this platform
            #endif
                if (!Main->IsBusy() && !Selection().empty())
                {
                    Event->accept();
                    Q_EMIT Close_Requested();
                    return;
                }
                break;
            default:;
        }
    }

    QTreeWidget::keyPressEvent(Event);
}

//---------------------------------------------------------------------------
void GUI_Main_Files::paintEvent(QPaintEvent* Event)
{
    QTreeWidget::paintEvent(Event);

    // Nothing opened: what to do is said where the content will be
    if (!topLevelItemCount())
        Hint_Paint(this, tr("Drop a directory or files here, or use File > Open directory... or Open file(s)..."));
}

//---------------------------------------------------------------------------
void GUI_Main_Files::resizeEvent(QResizeEvent* Event)
{
    QTreeWidget::resizeEvent(Event);

    Columns_Fit();
}

//---------------------------------------------------------------------------
void GUI_Main_Files::showEvent(QShowEvent* Event)
{
    QTreeWidget::showEvent(Event);

    Columns_Fit(); // Content received while the view was hidden, its size was not known
}

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
// Width each column needs for its content
void GUI_Main_Files::Columns_Measure()
{
    Columns_Wanted.clear();
    Status_Lines_Wanted = 0;
    if (!topLevelItemCount())
    {
        // As at the start: the widths, the ones of the user too, were for a content which is not there any more
        Columns_User.assign(Column_Max, false);
        Columns_Fitting = true;
        for (int i = 0; i < Column_Status; i++)
            setColumnWidth(i, header()->defaultSectionSize());
        Columns_Fitting = false;
        return;
    }
    for (int i = 0; i < Column_Max; i++)
        Columns_Wanted.push_back(qMax(sizeHintForColumn(i), header()->sectionSizeHint(i)) + Column_Margin);

    // The status of a track or of a file is short and has no other place: it is kept whole if the view can
    QSet<QString> Measured; // The same texts come again and again
    for (int i = 0; i < topLevelItemCount(); i++)
    {
        QTreeWidgetItem* Package = topLevelItem(i);
        if (!Package->isExpanded())
            continue;
        for (int j = 0; j < Package->childCount(); j++)
        {
            QTreeWidgetItem* Child = Package->child(j);
            const QString Text = Child->text(Column_Status);
            if (Text.isEmpty() || Measured.contains(Text))
                continue;
            Measured.insert(Text);
            Status_Lines_Wanted = qMax(Status_Lines_Wanted, sizeHintForIndex(indexFromItem(Child, Column_Status)).width() + Column_Margin);
        }
    }
}

//---------------------------------------------------------------------------
// Widths of the columns for the content and the size of the view, but the widths set by the user
void GUI_Main_Files::Columns_Fit()
{
    if (Columns_Fitting || Columns_Wanted.empty() || !isVisible())
        return;

    // The status is the last column, it gets what remains. If the view is too narrow, the names, the status and the other long texts
    // give place in turn. The limits are in heights of a line of text: they follow the font
    const int Unit = fontMetrics().height();
    const int Status_Lines = qMin(Status_Lines_Wanted, 24 * Unit);
    const int Status_Max = qMax(12 * Unit, Status_Lines);   // A long status is cut first: it starts with what matters
    const int Status_Short = qMax(11 * Unit, Status_Lines); // Place of the short texts (e.g. why a track is not encoded)
    const int Status_Min = 6 * Unit;
    const int Name_Min = qBound(10 * Unit, viewport()->width() / 5, 12 * Unit);
    std::vector<int> Widths = Columns_Wanted;
    for (int i = 0; i < Column_Status; i++)
        if (Columns_User[i])
            Widths[i] = columnWidth(i);
    Widths[Column_Status] = qMin(Widths[Column_Status], Status_Max);
    int Missing = -viewport()->width();
    for (const int Width : Widths)
        Missing += Width;
    const auto Give = [&](int Column, int Width_Min)
    {
        if (Missing <= 0 || Columns_User[Column])
            return;
        const int Given = qMin(Missing, qMax(Widths[Column] - Width_Min, 0));
        Widths[Column] -= Given;
        Missing -= Given;
    };
    Give(Column_Name, qMax(Widths[Column_Name] * 7 / 10, Name_Min)); // A part of a name can go without loss: its end says the most
    Give(Column_Status, Status_Short);
    Give(Column_Name, Name_Min);
    Give(Column_Files, Widths[Column_Files] * 2 / 3); // The count of files comes first, the range of the numbers after it says less
    Give(Column_Status, Status_Min);
    const int Long = (Columns_User[Column_Type] ? 0 : Widths[Column_Type]) + (Columns_User[Column_Files] ? 0 : Widths[Column_Files]);
    if (Missing > 0 && Long > 0)
    {
        const int Long_New = qMax(Long - Missing, Long / 2); // Not less: the view scrolls
        for (const int i : { Column_Type, Column_Files })
            if (!Columns_User[i])
                Widths[i] = Widths[i] * Long_New / Long;
    }

    Columns_Fitting = true;
    for (int i = 0; i < Column_Status; i++)
        if (!Columns_User[i])
            setColumnWidth(i, Widths[i]);

    // The status takes what remains but not less than what was kept for it (the view scrolls then): it is its width without the stretch
    header()->setStretchLastSection(false);
    setColumnWidth(Column_Status, Widths[Column_Status]);
    header()->setStretchLastSection(true);
    Columns_Fitting = false;
}
