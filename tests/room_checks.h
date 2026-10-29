#ifndef ROOM_CHECKS_H
#define ROOM_CHECKS_H
#include "model/roomedit.h"
#include "ui/widgets/roompanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
static QByteArray roomBlockBytes(NifModel &model, int block)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    check(model.saveIndex(buffer, model.getBlock(block)),
          "serialize room-related block for preservation check");
    return data;
}
static void roomLinks(NifModel &model, int id, const QString &array, const QString &count,
                      const QVector<qint32> &links)
{
    auto block = model.getBlock(id);
    model.set<uint>(block, count, links.size());
    model.updateArray(block, array);
    model.setLinkArray(block, array, links);
}
static QByteArray protectedRoomBytes(NifModel &model, int room)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    auto block = model.getBlock(room);
    for (int r = 0; r < model.rowCount(block); ++r)
    {
        auto field = block.child(r, 0);
        QString name = model.itemName(field);
        if (name == "Children" || name == "Num Children" || name == "Fixtures" || name == "Num Fixtures")
            continue;
        check(model.saveIndex(buffer, field), "capture protected room fields");
    }
    return data;
}
static void roomTests()
{
    NifModel model;
    VertexEditorTest::fixture(model);
    auto add = [&](const QString &type, const QString &name)
    {
        auto block = model.insertNiBlock(type);
        model.set<QString>(block, "Name", name);
        return model.getBlockNumber(block);
    };
    int group = add("NiRoomGroup", "ExistingGroup"), a = add("NiRoom", "Room A"), b = add("NiRoom", "Room B"),
        pa = add("NiPortal", "A to B"), pb = add("NiPortal", "B to A"),
        occluder = add("NiNode", "Occluder_Keep"), material = add("NiMaterialProperty", "Inherited material");
    roomLinks(model, 0, "Children", "Num Children", {group, occluder});
    roomLinks(model, group, "Children", "Num Children", {a, b});
    roomLinks(model, group, "Rooms", "Num Rooms", {a, b});
    roomLinks(model, a, "Children", "Num Children", {1, 3, pa});
    roomLinks(model, a, "Fixtures", "Num Fixtures", {1, 3});
    roomLinks(model, a, "Out Portals", "Num Out Portals", {pa});
    roomLinks(model, a, "In Portals", "Num In Portals", {pb});
    roomLinks(model, b, "Children", "Num Children", {pb});
    roomLinks(model, b, "Out Portals", "Num Out Portals", {pb});
    roomLinks(model, b, "In Portals", "Num In Portals", {pa});
    roomLinks(model, a, "Properties", "Num Properties", {material});
    model.setLink(model.getBlock(pa), "Adjoiner", b);
    model.setLink(model.getBlock(pb), "Adjoiner", a);
    for (int portal : {pa, pb})
    {
        auto block = model.getBlock(portal);
        model.set<quint16>(block, "Portal Flags", 0x81);
        model.set<quint16>(block, "Num Vertices", 4);
        model.updateArray(block, "Vertices");
        model.setArray<Vector3>(block, "Vertices",
                                QVector<Vector3>{Vector3(0, -10, -10), Vector3(0, 10, -10),
                                                 Vector3(0, 10, 10), Vector3(0, -10, 10)});
    }
    for (int room : {a, b})
    {
        auto block = model.getBlock(room);
        model.set<int>(block, "Num Walls", 6);
        model.updateArray(block, "Wall Planes");
        for (int axis = 0; axis < 3; ++axis)
            for (int sign = 0; sign < 2; ++sign)
            {
                auto plane = model.getIndex(block, "Wall Planes").child(axis * 2 + sign, 0);
                Vector3 normal;
                normal[axis] = sign ? -1 : 1;
                model.set<Vector3>(plane, "Normal", normal);
                model.set<float>(plane, "Constant", -60);
            }
    }
    model.set<Vector3>(model.getBlock(a), "Translation", Vector3(10, 5, 3));
    model.set<Matrix>(model.getBlock(a), "Rotation", Matrix::euler(.1f, .2f, .3f));
    model.set<float>(model.getBlock(a), "Scale", 1.25f);
    model.set<Vector3>(model.getBlock(b), "Translation", Vector3(300, 10, 7));
    model.set<Matrix>(model.getBlock(b), "Rotation", Matrix::euler(.2f, .4f, .7f));
    model.set<float>(model.getBlock(b), "Scale", 2);
    model.set<Matrix>(model.getBlock(0), "Rotation", Matrix::euler(.1f, .3f, .2f));
    model.set<float>(model.getBlock(0), "Scale", 1.5f);
    model.resetUndoHistory();
    auto pristine = bytes(model);
    RoomScene scene(&model);
    check(scene.rooms.size() == 2 && scene.memberships(1) == QVector<int>{a},
          "inspect existing room membership without changing it");
    check(scene.validate().isEmpty() && scene.cage(a).size() == 12,
          "validate room links and reconstruct the twelve box edges");
    check(scene.containingRooms(1) == QVector<int>{a} && bytes(model) == pristine &&
              model.undoStack->count() == 0,
          "room inspection and containment preserve the complete document");
    Transform aw, bw, object;
    scene.world(a, aw);
    scene.world(b, bw);
    auto local = aw.rotation.inverted() * (bw.translation - aw.translation) / aw.scale;
    model.set<Vector3>(model.getBlock(1), "Translation", local);
    model.resetUndoHistory();
    auto moved = bytes(model);
    RoomScene movedScene(&model);
    check(movedScene.memberships(1) == QVector<int>{a} && movedScene.containingRooms(1) == QVector<int>{b},
          "moving a mesh suggests its new room without reassigning it");
    movedScene.world(1, object);
    auto portalA = roomBlockBytes(model, pa), portalB = roomBlockBytes(model, pb),
         groupBefore = roomBlockBytes(model, group), occluderBefore = roomBlockBytes(model, occluder);
    QMap<int, QByteArray> untouched;
    for (int id = 0; id < model.getBlockCount(); ++id)
        if (id != 1 && id != a && id != b)
            untouched[id] = roomBlockBytes(model, id);
    QString reason;
    check(movedScene.assign(1, b, false, &reason) && bytes(model) == moved,
          "room reassignment preflight is read only");
    check(movedScene.assign(1, b, true, &reason), "explicitly reassign a static mesh to another room");
    RoomScene reassigned(&model);
    Transform after;
    reassigned.world(1, after);
    check(reassigned.memberships(1) == QVector<int>{b} &&
              !model.getLinkArray(model.getBlock(a), "Children").contains(1) &&
              model.getLinkArray(model.getBlock(b), "Children").contains(1),
          "room reassignment updates fixture membership and ordinary parent links");
    check((after.translation - object.translation).length() < .001f &&
              std::abs(after.scale - object.scale) < .001f &&
              (after.rotation * Vector3(1, 2, 3) - object.rotation * Vector3(1, 2, 3)).length() < .001f,
          "reassignment preserves the full world transform under rotated scaled parents");
    check(model.getLinkArray(model.getBlock(1), "Properties").contains(material),
          "reassignment preserves inherited material properties on the moved mesh");
    for (auto i = untouched.cbegin(); i != untouched.cend(); ++i)
        check(
            roomBlockBytes(model, i.key()) == i.value(),
            "reassignment preserves every unrelated block including portals groups occluders and mesh data");
    auto assigned = bytes(model);
    check(model.undoStack->count() == 1, "reassignment is a single undo step");
    model.undo();
    check(bytes(model) == moved, "room reassignment undoes to the exact pre-assignment document");
    model.redo();
    check(bytes(model) == assigned, "room reassignment redoes exactly");
    QBuffer saved(&assigned);
    saved.open(QIODevice::ReadOnly);
    NifModel loaded;
    check(loaded.load(saved) && RoomScene(&loaded).memberships(1) == QVector<int>{b},
          "new room membership survives save/reload");
    check(roomBlockBytes(loaded, pa) == portalA && roomBlockBytes(loaded, pb) == portalB &&
              roomBlockBytes(loaded, group) == groupBefore &&
              roomBlockBytes(loaded, occluder) == occluderBefore,
          "save/reload preserves portals room groups and occluders");
    check(RoomScene(&model).assign(1, b, true, &reason) && bytes(model) == assigned &&
              model.undoStack->count() == 1,
          "assigning the current room is a no-op");
    model.undo();
    auto reject = [&](const char *message)
    {
        auto before = bytes(model);
        int history = model.undoStack->count();
        check(!RoomScene(&model).assign(1, b, true, &reason) && !reason.isEmpty() && bytes(model) == before &&
                  model.undoStack->count() == history,
              message);
    };
    model.set<QString>(model.getBlock(1), "Name", "Occluder_Protected");
    reject("occluder-tagged meshes are blocked without changes");
    model.set<QString>(model.getBlock(1), "Name", "Mesh");
    roomLinks(model, b, "Fixtures", "Num Fixtures", {1});
    reject("ambiguous multiple-room membership is blocked without changes");
    roomLinks(model, b, "Fixtures", "Num Fixtures", {});
    int extra = add("NiNode", "Other system");
    roomLinks(model, extra, "Children", "Num Children", {1});
    reject("multiple scene owners are blocked without changes");
    roomLinks(model, extra, "Children", "Num Children", {});
    model.setLink(model.getBlock(pa), "Adjoiner", 1);
    reject("non-room systems referencing the selected mesh are protected");
    model.setLink(model.getBlock(pa), "Adjoiner", b);
    int otherGroup = add("NiRoomGroup", "Other Group");
    roomLinks(model, otherGroup, "Rooms", "Num Rooms", {b});
    reject("cross-group or ambiguous-group reassignment is blocked without changes");
    roomLinks(model, otherGroup, "Rooms", "Num Rooms", {});
    auto originalFlags = model.get<quint16>(model.getBlock(b), "Flags");
    model.set<quint16>(model.getBlock(b), "Flags", originalFlags | 1);
    reject("different inherited visibility is blocked without exposing or hiding the mesh");
    model.set<quint16>(model.getBlock(b), "Flags", originalFlags);
    int destinationProperty = add("NiAlphaProperty", "Destination state");
    roomLinks(model, b, "Properties", "Num Properties", {destinationProperty});
    reject("new inherited destination state is blocked instead of changing appearance");
    roomLinks(model, b, "Properties", "Num Properties", {});
    int controller = add("NiTransformController", "Animated fixture");
    model.setLink(model.getBlock(1), "Controller", controller);
    reject("animated meshes are blocked without changing their controller or hierarchy");
    model.setLink(model.getBlock(1), "Controller", -1);
    roomLinks(model, a, "Out Portals", "Num Out Portals", {pa, pa});
    auto invalid = bytes(model);
    check(!RoomScene(&model).validate().isEmpty() && bytes(model) == invalid,
          "validation reports existing issues without repairing them");
    // A normal fixture subtree can lose one child without disturbing its siblings.
    roomLinks(model, a, "Out Portals", "Num Out Portals", {pa});
    int parent = add("NiNode", "Fixture subtree");
    roomLinks(model, a, "Children", "Num Children", {parent, pa});
    roomLinks(model, a, "Fixtures", "Num Fixtures", {parent});
    roomLinks(model, parent, "Children", "Num Children", {1, 3});
    model.set<Vector3>(model.getBlock(parent), "Translation", Vector3(2, 4, 6));
    RoomScene nested(&model);
    Transform nestedWorld;
    nested.world(1, nestedWorld);
    check(nested.assign(1, b, true, &reason), "reassign a static mesh out of a normal fixture subtree");
    RoomScene nestedAfter(&model);
    nestedAfter.world(1, after);
    check(nestedAfter.memberships(1) == QVector<int>{b} && nestedAfter.memberships(3) == QVector<int>{a} &&
              model.getLinkArray(model.getBlock(a), "Fixtures") == QVector<qint32>{parent} &&
              (after.translation - nestedWorld.translation).length() < .001f,
          "nested reassignment preserves sibling membership and the original fixture group");
    model.setLink(model.getBlock(pa), "Adjoiner", parent);
    auto externallyReferenced = bytes(model);
    check(!RoomScene(&model).assign(3, b, true, &reason) && bytes(model) == externallyReferenced,
          "external visibility references to a fixture ancestor block subtree changes");
    model.setLink(model.getBlock(pa), "Adjoiner", b);
    auto window = new NifSkope;
    auto active = window->findChild<NifModel *>();
    QByteArray source = bytes(model);
    QBuffer sourceBuffer(&source);
    sourceBuffer.open(QIODevice::ReadOnly);
    check(active->load(sourceBuffer), "load room UI fixture");
    window->select(active->getBlock(3));
    window->show();
    QCoreApplication::processEvents();
    window->findChild<QAction *>("aRoomEditor")->trigger();
    QCoreApplication::processEvents();
    auto dock = window->findChild<QDockWidget *>("RoomEditorDock");
    auto panel = dock->findChild<RoomPanel *>();
    check(!dock->isHidden() && panel->findChild<QLabel *>("roomMembership")->text().contains("Room A"),
          "Rooms action opens a sidebar showing the selected mesh membership");
    auto destination = panel->findChild<QComboBox *>("roomDestination");
    auto viewport = window->findChild<GLView *>("OGL1");
    viewport->updateGL();
    auto renderedBefore = viewport->getScene()->getNode(active, active->getBlock(3))->worldTrans();
    destination->setCurrentIndex(destination->findData(b));
    panel->findChild<QPushButton *>("assignMeshRoom")->click();
    check(RoomScene(active).memberships(3) == QVector<int>{b},
          "room sidebar assigns the selected mesh to the explicitly chosen room");
    viewport->updateGL();
    auto renderedAfter = viewport->getScene()->getNode(active, active->getBlock(3))->worldTrans();
    check((renderedBefore.translation-renderedAfter.translation).length()<.001f,
          "viewport rendering keeps the mesh at the same world location after reassignment");
    auto uiBytes = bytes(*active);
    panel->findChild<QCheckBox *>("showRoomOverlay")->setChecked(true);
    viewport->updateGL();
    panel->findChild<QPushButton *>("validateRooms")->click();
    check(bytes(*active) == uiBytes && panel->findChild<QListWidget *>("roomValidation")->count() > 0,
          "overlay and validation do not modify the document");
    panel->grab().save(QCoreApplication::applicationDirPath() + "/room-panel-preview.png");
    delete window;
}
static void realRoomTests()
{
    for (QString filename : {QString("Working.nif"), QString("NotWorking.nif")})
    {
        QString path = "build/qt-check/fixtures/" + filename;
        if (!QFile::exists(path))
            continue;
        NifModel model;
        check(model.loadFromFile(path), "load existing MicroVolts room map");
        RoomScene scene(&model);
        auto before = bytes(model);
        auto protectedBlocks = [&]()
        {
            QMap<int, QByteArray> result;
            for (int id = 0; id < model.getBlockCount(); ++id)
            {
                auto type = model.getBlockName(model.getBlock(id)),
                     name = model.get<QString>(model.getBlock(id), "Name");
                if (type == "NiPortal" || type == "NiRoomGroup" ||
                    name.contains("occlud", Qt::CaseInsensitive))
                    result[id] = roomBlockBytes(model, id);
            }
            return result;
        };
        auto protectedBefore = protectedBlocks();
        QMap<int, QByteArray> protectedRooms;
        for (auto room : scene.rooms)
            protectedRooms[room.id] = protectedRoomBytes(model, room.id);
        scene.validate();
        for (const auto &room : scene.rooms)
            scene.cage(room.id);
        check(bytes(model) == before, "read-only inspection leaves the existing map byte-for-byte unchanged");
        int selected = -1, target = -1;
        QString reason;
        for (int id = 0; id < model.getBlockCount() && selected < 0; ++id)
            if (model.getBlockName(model.getBlock(id)) == "NiMesh")
                for (const auto &room : scene.rooms)
                    if (!scene.memberships(id).contains(room.id) && scene.assign(id, room.id, false, &reason))
                    {
                        selected = id;
                        target = room.id;
                        break;
                    }
        if (selected < 0)
            std::fprintf(stderr, "Blocked real map: %s\n", qPrintable(reason));
        check(selected >= 0, "find a supported static fixture reassignment in the existing map");
        Transform oldWorld;
        scene.world(selected, oldWorld);
        model.resetUndoHistory();
        check(scene.assign(selected, target, true, &reason), "reassign a real MicroVolts NiMesh fixture");
        Transform newWorld;
        RoomScene(&model).world(selected, newWorld);
        check((oldWorld.translation - newWorld.translation).length() < .01f &&
                  protectedBlocks() == protectedBefore,
              "real reassignment preserves world placement and every portal group and occluder block");
        for (auto i = protectedRooms.cbegin(); i != protectedRooms.cend(); ++i)
            check(protectedRoomBytes(model, i.key()) == i.value(),
                  "real assignment preserves room bounds portals flags transforms and other room fields");
        auto edited = bytes(model);
        NifModel reloaded;
        QBuffer input(&edited);
        input.open(QIODevice::ReadOnly);
        check(reloaded.load(input) && RoomScene(&reloaded).memberships(selected) == QVector<int>{target},
              "real reassignment survives save/reload");
        for (auto i = protectedRooms.cbegin(); i != protectedRooms.cend(); ++i)
            check(protectedRoomBytes(reloaded, i.key()) == i.value(),
                  "real save/reload preserves protected room fields");
        for (auto i = protectedBefore.cbegin(); i != protectedBefore.cend(); ++i)
            check(roomBlockBytes(reloaded, i.key()) == i.value(),
                  "real save/reload preserves protected map structures");
        model.undo();
        check(bytes(model) == before, "real room reassignment undo restores the exact existing map");
    }
}
#endif
