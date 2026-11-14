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
//---------------------------------------------------------------------------

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
enum
{
    Column_Name,
    Column_Content,
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
    Item_Info,
};
enum
{
    Role_Package = Qt::UserRole,    // Index in the packages
    Role_Track,                     // Tracks: index in the tracks of the package
    Role_Key,                       // Packages: what was opened, same after a rebuild of the tree
};

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

        // Names are paths: when the column is too narrow their end is more useful than their middle
        if (Index.column() == Column_Name)
            Option->textElideMode = Qt::ElideMiddle;

        // Whole lines are selected: a frame around the current cell only says something when the keyboard is used
        if (!(Option->state & QStyle::State_KeyboardFocusChange))
            Option->state &= ~QStyle::State_HasFocus;
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
    const int Lines_Max = 30;
    if (Lines.size() > Lines_Max)
    {
        Lines.erase(Lines.begin() + Lines_Max, Lines.end());
        Lines.append(QStringLiteral("..."));
    }
    return QStringLiteral("<p style='white-space:pre-wrap'>") + Lines.join(QLatin1Char('\n')).toHtmlEscaped() + QStringLiteral("</p>");
}

//---------------------------------------------------------------------------
// Red which can be read on the background of the view
static QColor Color_Error(const QPalette& Palette)
{
    return Palette.color(QPalette::Base).lightness() < 128 ? QColor(255, 110, 110) : QColor(200, 0, 0);
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
    QString Text = Package.Status;
    const size_t Modified = Package.Modified_Count();
    if (Modified)
    {
        if (!Text.isEmpty())
            Text += QLatin1Char(' ');
        Text += QLatin1String("* ");
        Text += Modified == 1 ? GUI_Main_Files::tr("1 modified field") : GUI_Main_Files::tr("%1 modified fields").arg((qulonglong)Modified);
    }
    return Text;
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
// Count of files of a track and, for a sequence, the numbers of the first and last ones
static QString Track_Files(const gui_track& Track)
{
    if (!Track.FileCount)
        return QString();
    QString Text = Track.FileCount == 1 ? GUI_Main_Files::tr("1 file") : GUI_Main_Files::tr("%1 files").arg(Track.FileCount);
    if (!Track.Numbers.isEmpty())
        Text += QLatin1String(" (") + Track.Numbers + QLatin1Char(')');
    return Text;
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
    , Columns_User(Column_Max, false)
{
    // Configuration
    setColumnCount(Column_Max);
    setHeaderLabels(QStringList() << tr("Name") << tr("Content") << tr("Files") << tr("Frame size") << tr("Frame rate") << tr("Duration") << tr("Status"));
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
    const QBrush Error(Color_Error(palette()));
    for (size_t i = 0; i < Main->Packages.size(); i++)
    {
        const gui_package& Package = Main->Packages[i];
        const QString Key = Package.Inputs.join(QLatin1Char('\n'));

        // Package
        QTreeWidgetItem* Item = new QTreeWidgetItem(Item_Package);
        Item->setData(Column_Name, Role_Package, (qulonglong)i);
        Item->setData(Column_Name, Role_Key, Key);
        Item->setText(Column_Name, Package.Name());
        Item->setToolTip(Column_Name, ToolTip_Text(Key));
        Item->setIcon(Column_Name, style()->standardIcon(Package.IsDirectory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon));
        if (Package.Kind == gui_package::Kind_Compressed)
            Item->setText(Column_Content, tr("Matroska"));
        else if (Package.IsDirectory)
            Item->setText(Column_Content, tr("Directory"));
        else
            Item->setText(Column_Content, Package.Inputs.size() > 1 ? tr("Files") : tr("File"));
        Item->setText(Column_Status, Package_Status(Package));
        Item->setToolTip(Column_Status, ToolTip_Text(Package_Status_Details(Package)));
        if (Package.IsError)
            Item->setForeground(Column_Status, Error);

        // Tracks
        for (size_t j = 0; j < Package.Tracks.size(); j++)
        {
            const gui_track& Track = Package.Tracks[j];
            QTreeWidgetItem* Child = new QTreeWidgetItem(Item, Item_Track);
            Child->setData(Column_Name, Role_Package, (qulonglong)i);
            Child->setData(Column_Name, Role_Track, (qulonglong)j);
            Child->setText(Column_Name, Track.Name);
            QString Tip = Track.FileName;
            if (!Track.Fields.empty())
                Tip += QLatin1Char('\n') + tr("Double click to see the metadata");
            Child->setToolTip(Column_Name, ToolTip_Text(Tip));
            Child->setText(Column_Content, Track.Flavor);
            Child->setToolTip(Column_Content, ToolTip_Text(Track.Flavor));
            Child->setText(Column_Files, Track_Files(Track));
            Child->setToolTip(Column_Files, ToolTip_Text(Track_Files(Track)));
            if (Track.Width && Track.Height)
                Child->setText(Column_FrameSize, QStringLiteral("%1x%2").arg(Track.Width).arg(Track.Height));
            Child->setText(Column_FrameRate, Track.FrameRate);
            Child->setText(Column_Duration, Track_Duration(Track.Duration));
            QStringList Problems;
            if (Track.IsNotLicensed)
                Problems += tr("Not supported by the license");
            if (!Track.IsMetadataComplete)
                Problems += tr("Metadata of some files not read");
            Child->setText(Column_Status, Problems.join(QLatin1String("; ")));
            Child->setToolTip(Column_Status, ToolTip_Text(Problems.join(QLatin1Char('\n'))));
            if (Track.IsNotLicensed)
                Child->setForeground(Column_Status, Error); // It can not be encoded
        }

        // Attachments
        for (const QString& Attachment : Package.Attachments)
        {
            QTreeWidgetItem* Child = new QTreeWidgetItem(Item, Item_Attachment);
            Child->setData(Column_Name, Role_Package, (qulonglong)i);
            Child->setText(Column_Name, Attachment);
            Child->setToolTip(Column_Name, ToolTip_Text(Attachment));
            Child->setText(Column_Content, tr("Attachment"));
        }

        // Information about compressed content
        QList<QTreeWidgetItem*> Infos;
        for (const QString& Info : Package.Info)
        {
            QTreeWidgetItem* Child = new QTreeWidgetItem(Item, Item_Info);
            Child->setData(Column_Name, Role_Package, (qulonglong)i);
            Child->setText(Column_Name, Info);
            Child->setToolTip(Column_Name, ToolTip_Text(Info));
            Infos.append(Child);
        }

        addTopLevelItem(Item);
        Item->setExpanded(!Collapsed.contains(Key));
        for (QTreeWidgetItem* Child : Infos)
            Child->setFirstColumnSpanned(true); // A sentence, not a name: it takes the whole line
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

//---------------------------------------------------------------------------
bool GUI_Main_Files::Selection_Track(size_t& Package, size_t& Track) const
{
    return Item_Track_Get(currentItem(), Main, Package, Track);
}

//***************************************************************************
// Events
//***************************************************************************

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
                if (Selection_Track(Package, Track))
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
    if (topLevelItemCount())
        return;
    QPainter Painter(viewport());
    QFont Font = font();
    if (Font.pointSizeF() > 0)
        Font.setPointSizeF(Font.pointSizeF() * 1.25);
    Painter.setFont(Font);
    Painter.setPen(palette().color(QPalette::PlaceholderText));
    Painter.drawText(viewport()->rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap, tr("Drop a directory or files here, or use File > Open"));
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
        Columns_Wanted.push_back(qMax(sizeHintForColumn(i), header()->sectionSizeHint(i)) + 8); // Texts do not touch the next column
}

//---------------------------------------------------------------------------
// Widths of the columns for the content and the size of the view, but the widths set by the user
void GUI_Main_Files::Columns_Fit()
{
    if (Columns_Fitting || Columns_Wanted.empty() || !isVisible())
        return;

    // The status is the last column, it gets what remains.
    // If the view is too narrow for all the content, the names give what is missing first (their start and their end stay visible),
    // then the status but for the place of its short texts (e.g. why a track can not be encoded), then the names again,
    // then the status again (it starts with what matters), then the other long texts. All of them are in full in their tool tip
    const int Unit = fontMetrics().height(); // The limits are for some characters: they follow the font
    std::vector<int> Widths = Columns_Wanted;
    for (int i = 0; i < Column_Status; i++)
        if (Columns_User[i])
            Widths[i] = columnWidth(i);
    Widths[Column_Status] = qMin(Widths[Column_Status], 12 * Unit);
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
    const int Name_Min = qBound(10 * Unit, viewport()->width() / 5, 12 * Unit);
    Give(Column_Name, qMax(Widths[Column_Name] * 7 / 10, Name_Min));
    Give(Column_Status, 11 * Unit);
    Give(Column_Name, Name_Min);
    Give(Column_Status, 6 * Unit);
    const int Long = (Columns_User[Column_Content] ? 0 : Widths[Column_Content]) + (Columns_User[Column_Files] ? 0 : Widths[Column_Files]);
    if (Missing > 0 && Long > 0)
    {
        const int Long_New = qMax(Long - Missing, Long / 2); // Not less: the view scrolls
        for (const int i : { Column_Content, Column_Files })
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
