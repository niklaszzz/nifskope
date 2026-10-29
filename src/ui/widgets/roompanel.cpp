#include "roompanel.h"
#include "model/nifmodel.h"
#include "model/roomedit.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
RoomPanel::RoomPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("roomPanel");
    setMinimumWidth(330);
    auto outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto content = new QWidget(scroll);
    auto layout = new QVBoxLayout(content);
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto explanation =
        new QLabel(tr("Move meshes normally, then explicitly assign their room here. "
                      "Edit portal openings separately below; portal edits preserve room bounds and links."),
                   this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto overlay = new QCheckBox(tr("Show room bounds and portal openings"), this);
    overlay->setObjectName("showRoomOverlay");
    connect(overlay, &QCheckBox::toggled, this, &RoomPanel::overlayChanged);
    layout->addWidget(overlay);
    objectLabel = new QLabel(this);
    objectLabel->setObjectName("roomObject");
    objectLabel->setWordWrap(true);
    layout->addWidget(objectLabel);
    membershipLabel = new QLabel(this);
    membershipLabel->setObjectName("roomMembership");
    membershipLabel->setWordWrap(true);
    layout->addWidget(membershipLabel);
    suggestionLabel = new QLabel(this);
    suggestionLabel->setObjectName("roomSuggestion");
    suggestionLabel->setWordWrap(true);
    layout->addWidget(suggestionLabel);
    destination = new QComboBox(this);
    destination->setObjectName("roomDestination");
    auto form = new QFormLayout;
    form->addRow(tr("Destination"), destination);
    layout->addLayout(form);
    details = new QLabel(this);
    details->setWordWrap(true);
    layout->addWidget(details);
    useSuggested = new QPushButton(tr("Choose Suggested Room"), this);
    useSuggested->setObjectName("chooseSuggestedRoom");
    layout->addWidget(useSuggested);
    connect(useSuggested, &QPushButton::clicked, this,
            [this]
            {
                if (suggestions.size() == 1)
                    destination->setCurrentIndex(destination->findData(suggestions.first()));
            });
    assign = new QPushButton(tr("Assign Selected Mesh to Room"), this);
    assign->setObjectName("assignMeshRoom");
    layout->addWidget(assign);
    status = new QLabel(this);
    status->setObjectName("roomAssignmentStatus");
    status->setWordWrap(true);
    layout->addWidget(status);
    connect(destination, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]
            {
                if (!model)
                    return;
                int id = destination->currentData().toInt();
                auto room = model->getBlock(id);
                details->setText(tr("%1 wall planes | %2 outgoing / %3 incoming portals | %4 fixtures")
                                     .arg(model->rowCount(model->getIndex(room, "Wall Planes")))
                                     .arg(model->getLinkArray(room, "Out Portals").size())
                                     .arg(model->getLinkArray(room, "In Portals").size())
                                     .arg(model->getLinkArray(room, "Fixtures").size()));
            });
    connect(assign, &QPushButton::clicked, this,
            [this]
            {
                if (!model || destination->currentIndex() < 0)
                    return;
                RoomScene scene(model);
                QString reason;
                applying = true;
                bool ok = scene.assign(model->getBlockNumber(selected), destination->currentData().toInt(),
                                       true, &reason);
                applying = false;
                refresh();
                status->setText(ok ? tr("Room assignment updated. World transform and existing map systems "
                                        "preserved. Ctrl+Z undoes it.")
                                   : reason);
            });
    auto portalBox = new QGroupBox(tr("Portal Openings"), this);
    auto portalLayout = new QVBoxLayout(portalBox);
    auto portalHelp =
        new QLabel(tr("Choose the existing opening. Select boundary vertices/faces in the viewport to fit a "
                      "rectangle, or adjust the opening in its own plane. Preview before applying."),
                   portalBox);
    portalHelp->setWordWrap(true);
    portalLayout->addWidget(portalHelp);
    portals = new QComboBox(portalBox);
    portals->setObjectName("portalChoice");
    portalLayout->addWidget(portals);
    pairedPortals = new QCheckBox(tr("Update matching reverse portal too"), portalBox);
    pairedPortals->setObjectName("pairedPortalEdit");
    pairedPortals->setChecked(true);
    portalLayout->addWidget(pairedPortals);
    portalSelection = new QLabel(tr("Select boundary vertices or faces in the viewport."), portalBox);
    portalSelection->setObjectName("portalSelectionInfo");
    portalSelection->setWordWrap(true);
    portalLayout->addWidget(portalSelection);
    fitPortal = new QPushButton(tr("Preview Fit Rectangle to Selection"), portalBox);
    fitPortal->setObjectName("previewPortalFit");
    portalLayout->addWidget(fitPortal);
    auto portalForm = new QFormLayout;
    const char *labels[] = {"Move U", "Move V", "Scale U", "Scale V"};
    for (int i = 0; i < 4; ++i)
    {
        portalValues[i] = new QDoubleSpinBox(portalBox);
        portalValues[i]->setObjectName(QString("portalValue%1").arg(i));
        portalValues[i]->setDecimals(4);
        portalValues[i]->setRange(i < 2 ? -1000000 : .001, i < 2 ? 1000000 : 1000);
        portalValues[i]->setValue(i < 2 ? 0 : 1);
        portalValues[i]->setToolTip(
            tr("U follows the portal's first usable edge; V is perpendicular within the portal plane. "
               "Offsets are world units; scales are relative to the current opening."));
        portalForm->addRow(tr(labels[i]), portalValues[i]);
        connect(portalValues[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this] { clearPreview(); });
    }
    portalLayout->addLayout(portalForm);
    auto adjust = new QPushButton(tr("Preview Move / Resize"), portalBox);
    adjust->setObjectName("previewPortalAdjust");
    portalLayout->addWidget(adjust);
    portalStatus = new QLabel(portalBox);
    portalStatus->setObjectName("portalEditStatus");
    portalStatus->setWordWrap(true);
    portalLayout->addWidget(portalStatus);
    auto row = new QHBoxLayout;
    applyPortal = new QPushButton(tr("Apply Portal Change"), portalBox);
    applyPortal->setObjectName("applyPortalChange");
    auto cancel = new QPushButton(tr("Cancel Preview"), portalBox);
    cancel->setObjectName("cancelPortalPreview");
    row->addWidget(applyPortal);
    row->addWidget(cancel);
    portalLayout->addLayout(row);
    layout->addWidget(portalBox);
    connect(portals, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]
            {
                clearPreview();
                emit portalHighlightChanged(portals->currentIndex() >= 0 ? portals->currentData().toInt()
                                                                         : -1);
            });
    connect(pairedPortals, &QCheckBox::toggled, this, [this] { clearPreview(); });
    connect(cancel, &QPushButton::clicked, this, &RoomPanel::clearPreview);
    auto preview = [this](bool fit)
    {
        clearPreview();
        if (!model || portals->currentIndex() < 0)
            return;
        QString error;
        bool ok = fit ? PortalEditing::fit(model, portals->currentData().toInt(),
                                           model->getBlockNumber(selectionMesh), selectionVertices,
                                           pairedPortals->isChecked(), portalPlan, error)
                      : PortalEditing::adjust(model, portals->currentData().toInt(), portalValues[0]->value(),
                                              portalValues[1]->value(), portalValues[2]->value(),
                                              portalValues[3]->value(), pairedPortals->isChecked(),
                                              portalPlan, error);
        portalStatus->setText(ok ? portalPlan.description : error);
        applyPortal->setEnabled(ok);
        if (ok)
            emit portalPreviewChanged(portalPlan.preview);
    };
    connect(fitPortal, &QPushButton::clicked, this, [preview] { preview(true); });
    connect(adjust, &QPushButton::clicked, this, [preview] { preview(false); });
    connect(applyPortal, &QPushButton::clicked, this,
            [this]
            {
                QString error;
                applying = true;
                bool ok = PortalEditing::apply(model, portalPlan, error);
                applying = false;
                clearPreview();
                if (ok)
                {
                    for (int i = 0; i < 4; ++i)
                    {
                        QSignalBlocker blocker(portalValues[i]);
                        portalValues[i]->setValue(i < 2 ? 0 : 1);
                    }
                    refresh();
                    portalStatus->setText(
                        tr("Portal opening updated. Ctrl+Z undoes both directions together."));
                }
                else
                    portalStatus->setText(error);
            });
    auto validate = new QPushButton(tr("Validate Room Links (Read Only)"), this);
    validate->setObjectName("validateRooms");
    layout->addWidget(validate);
    issues = new QListWidget(this);
    issues->setObjectName("roomValidation");
    issues->setWordWrap(true);
    layout->addWidget(issues, 1);
    connect(validate, &QPushButton::clicked, this,
            [this]
            {
                issues->clear();
                RoomScene scene(model);
                auto messages = scene.validate();
                if (messages.isEmpty())
                    messages << tr("No structural issues found. Geometry overlap and portal visibility are "
                                   "not tested by this link check.");
                issues->addItems(messages);
            });
    timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(200);
    connect(timer, &QTimer::timeout, this, &RoomPanel::refresh);
    refresh();
}
void RoomPanel::setTarget(NifModel *document, const QModelIndex &target)
{
    if (applying)
        return;
    if (model != document)
    {
        for (auto connection : connections)
            disconnect(connection);
        connections.clear();
        clearPreview();
        selectionMesh = QModelIndex();
        selectionVertices.clear();
        model = document;
        issues->clear();
        status->clear();
        if (model)
        {
            auto schedule = [this]
            {
                if (!applying)
                {
                    clearPreview();
                    if (isVisible())
                        timer->start();
                }
            };
            connections << connect(model, &NifModel::dataChanged, this, schedule)
                        << connect(model, &NifModel::linksChanged, this, schedule)
                        << connect(model, &NifModel::modelReset, this, schedule);
            connections << connect(model, &NifModel::undoAboutToRestore, this,
                                   [this] { restoreSelected = model->getBlockNumber(selected); });
            connections << connect(model, &NifModel::undoRestored, this,
                                   [this]
                                   {
                                       selected = model->getBlock(restoreSelected);
                                       {
                                           clearPreview();
                                           timer->start();
                                       }
                                   });
            connections << connect(model, &NifModel::destroyed, this,
                                   [this]
                                   {
                                       model = nullptr;
                                       selected = QModelIndex();
                                       refresh();
                                   });
        }
    }
    if (selected != target)
    {
        selected = target;
        clearPreview();
        status->clear();
        QSignalBlocker blocker(destination);
        destination->setCurrentIndex(-1);
    }
    if (isVisible())
        refresh();
    else
        timer->start();
}
void RoomPanel::refresh()
{
    if (applying)
        return;
    refreshPortals();
    RoomScene scene(model);
    int id = model ? model->getBlockNumber(selected) : -1;
    int previous = destination->currentIndex() >= 0 ? destination->currentData().toInt() : -1;
    auto members = scene.memberships(id);
    suggestions.clear();
    QString reason;
    bool mesh = id >= 0 && (model->getBlockName(model->getBlock(id)) == "NiMesh" ||
                            model->getBlockName(model->getBlock(id)) == "NiTriShape");
    if (mesh)
        suggestions = scene.containingRooms(id, &reason);
    else
        reason = tr("Select a static mesh to inspect containment and reassign its room.");
    auto label = [this](int block)
    { return QString("%1 (#%2)").arg(model->get<QString>(model->getBlock(block), "Name")).arg(block); };
    objectLabel->setText(id >= 0 ? tr("Selected: %1").arg(label(id))
                                 : tr("Select a mesh in the viewport or Block List."));
    QStringList assigned;
    for (int member : members)
        assigned << label(member);
    membershipLabel->setText(
        tr("Assigned room: %1").arg(assigned.isEmpty() ? tr("None") : assigned.join(", ")));
    if (suggestions.size() == 1)
        suggestionLabel->setText(tr("Entire mesh fits in: %1%2")
                                     .arg(label(suggestions.first()), members.contains(suggestions.first())
                                                                          ? tr(" (already assigned)")
                                                                          : tr(" - reassignment available")));
    else if (suggestions.size() > 1)
        suggestionLabel->setText(
            tr("Several room volumes contain this mesh. Choose the intended room explicitly."));
    else
        suggestionLabel->setText(reason.isEmpty() ? tr("No single room fully contains the mesh. It may cross "
                                                       "a boundary or be outside the rooms.")
                                                  : reason);
    {
        QSignalBlocker blocker(destination);
        destination->clear();
        for (const auto &room : scene.rooms)
        {
            QString group = room.groups.size() == 1
                                ? model->get<QString>(model->getBlock(room.groups.first()), "Name")
                                : tr("ambiguous group");
            destination->addItem(QString("%1 / %2").arg(group, label(room.id)), room.id);
        }
        int index = model && model->getBlockName(selected) == "NiRoom" ? destination->findData(id)
                                                                       : destination->findData(previous);
        if (index < 0 && members.size() == 1)
            index = destination->findData(members.first());
        destination->setCurrentIndex(index < 0 && destination->count() ? 0 : index);
    }
    useSuggested->setEnabled(suggestions.size() == 1);
    assign->setEnabled(mesh && members.size() == 1 && destination->currentIndex() >= 0);
    destination->setEnabled(destination->count() > 0);
    if (destination->currentIndex() >= 0)
    {
        auto room = model->getBlock(destination->currentData().toInt());
        details->setText(tr("%1 wall planes | %2 outgoing / %3 incoming portals | %4 fixtures")
                             .arg(model->rowCount(model->getIndex(room, "Wall Planes")))
                             .arg(model->getLinkArray(room, "Out Portals").size())
                             .arg(model->getLinkArray(room, "In Portals").size())
                             .arg(model->getLinkArray(room, "Fixtures").size()));
    }
    else
        details->clear();
}

void RoomPanel::clearPreview()
{
    portalPlan = PortalEditPlan();
    if (applyPortal)
        applyPortal->setEnabled(false);
    if (portalStatus)
        portalStatus->clear();
    emit portalPreviewChanged({});
}
void RoomPanel::setMeshSelection(NifModel *document, const QModelIndex &mesh, const QSet<int> &vertices)
{
    if (document != model)
        return;
    if (selectionMesh != mesh || selectionVertices != vertices)
    {
        selectionMesh = mesh;
        selectionVertices = vertices;
        clearPreview();
    }
    portalSelection->setText(selectionMesh.isValid()
                                 ? tr("%1 selected vertices on %2 (#%3). Select only the desired opening "
                                      "boundary or planar faces.")
                                       .arg(selectionVertices.size())
                                       .arg(model->get<QString>(selectionMesh, "Name"))
                                       .arg(model->getBlockNumber(selectionMesh))
                                 : tr("Select boundary vertices or faces in the viewport."));
    fitPortal->setEnabled(model && selectionMesh.isValid() && selectionVertices.size() >= 3 &&
                          portals->count() > 0);
}
void RoomPanel::refreshPortals()
{
    int previous = portals->currentIndex() >= 0 ? portals->currentData().toInt() : -1;
    QSignalBlocker blocker(portals);
    portals->clear();
    if (model)
    {
        RoomScene scene(model);
        for (const auto &room : scene.rooms)
            for (int id : room.outgoing)
            {
                auto block = model->getBlock(id);
                if (model->getBlockName(block) != "NiPortal" || portals->findData(id) >= 0)
                    continue;
                auto destination = model->getBlock(model->getLink(block, "Adjoiner"));
                QString from = model->get<QString>(model->getBlock(room.id), "Name"),
                        to = model->get<QString>(destination, "Name");
                portals->addItem(
                    QString("%1 to %2: %3 (#%4)").arg(from, to, model->get<QString>(block, "Name")).arg(id),
                    id);
            }
        int selectedId = model->getBlockNumber(selected);
        if (model->getBlockName(selected) == "NiPortal")
            previous = selectedId;
    }
    int index = portals->findData(previous);
    portals->setCurrentIndex(index >= 0 ? index : (portals->count() ? 0 : -1));
    fitPortal->setEnabled(selectionMesh.isValid() && selectionVertices.size() >= 3 && portals->count() > 0);
    emit portalHighlightChanged(isVisible() && portals->currentIndex() >= 0 ? portals->currentData().toInt()
                                                                            : -1);
}
void RoomPanel::hideEvent(QHideEvent *event)
{
    clearPreview();
    emit portalHighlightChanged(-1);
    QWidget::hideEvent(event);
}
