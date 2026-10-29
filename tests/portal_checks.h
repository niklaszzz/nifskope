#ifndef PORTAL_CHECKS_H
#define PORTAL_CHECKS_H
#include "model/portaledit.h"
#include <QDoubleSpinBox>
#include <QTemporaryDir>
struct PortalFixture
{
    int group, a, b, forward, reverse, occluder;
    QVector<Vector3> desired;
};
static PortalFixture portalFixture(NifModel &model)
{
    VertexEditorTest::fixture(model);
    PortalFixture f;
    auto add = [&](QString type, QString name)
    {
        auto block = model.insertNiBlock(type);
        model.set<QString>(block, "Name", name);
        return model.getBlockNumber(block);
    };
    f.group = add("NiRoomGroup", "Group");
    f.a = add("NiRoom", "A");
    f.b = add("NiRoom", "B");
    f.forward = add("NiPortal", "Window A to B");
    f.reverse = add("NiPortal", "Window B to A");
    f.occluder = add("NiNode", "Occluder_Keep");
    roomLinks(model, 0, "Children", "Num Children", {f.group, f.occluder});
    roomLinks(model, f.group, "Children", "Num Children", {f.a, f.b});
    roomLinks(model, f.group, "Rooms", "Num Rooms", {f.a, f.b});
    roomLinks(model, f.a, "Children", "Num Children", {1, f.forward});
    roomLinks(model, f.a, "Fixtures", "Num Fixtures", {1});
    roomLinks(model, f.a, "Out Portals", "Num Out Portals", {f.forward});
    roomLinks(model, f.a, "In Portals", "Num In Portals", {f.reverse});
    roomLinks(model, f.b, "Children", "Num Children", {3, f.reverse});
    roomLinks(model, f.b, "Fixtures", "Num Fixtures", {3});
    roomLinks(model, f.b, "Out Portals", "Num Out Portals", {f.reverse});
    roomLinks(model, f.b, "In Portals", "Num In Portals", {f.forward});
    model.setLink(model.getBlock(f.forward), "Adjoiner", f.b);
    model.setLink(model.getBlock(f.reverse), "Adjoiner", f.a);

    for (int room : {f.a, f.b})
    {
        model.set<int>(model.getBlock(room), "Num Walls", 6);
        model.updateArray(model.getBlock(room), "Wall Planes");
        for (int p = 0; p < 6; ++p)
        {
            auto plane = model.getIndex(model.getBlock(room), "Wall Planes").child(p, 0);
            Vector3 normal;
            normal[p / 2] = p % 2 ? -1 : 1;
            model.set<Vector3>(plane, "Normal", normal);
            model.set<float>(plane, "Constant", -1000);
        }
    }
    auto transform = [&](int id, Vector3 translation, Vector3 angles, float scale)
    {
        model.set<Vector3>(model.getBlock(id), "Translation", translation);
        model.set<Matrix>(model.getBlock(id), "Rotation", Matrix::euler(angles[0], angles[1], angles[2]));
        model.set<float>(model.getBlock(id), "Scale", scale);
    };
    transform(0, Vector3(20, 30, 40), Vector3(.2, .1, .3), 1.3f);
    transform(f.a, Vector3(-100, 20, 10), Vector3(.1, .3, .4), 1.2f);
    transform(f.b, Vector3(100, -50, 30), Vector3(-.2, .4, -.1), .8f);
    transform(f.forward, Vector3(10, 5, 2), Vector3(.15, .2, .3), 1.5f);
    transform(f.reverse, Vector3(-2, 9, 8), Vector3(-.3, .4, -.2), -.75f);
    for (int id : {f.forward, f.reverse})
    {
        model.set<quint16>(model.getBlock(id), "Num Vertices", 4);
        model.updateArray(model.getBlock(id), "Vertices");
        model.set<quint16>(model.getBlock(id), "Portal Flags", id == f.forward ? 0x81 : 0x42);
        model.set<quint16>(model.getBlock(id), "Plane Count", 7);
    }
    QVector<Vector3> old = {Vector3(-4, -3, 0), Vector3(4, -3, 0), Vector3(4, 3, 0), Vector3(-4, 3, 0)};
    model.setArray<Vector3>(model.getBlock(f.forward), "Vertices", old);
    RoomScene scene(&model);
    Transform forward, reverse, mesh;
    scene.world(f.forward, forward);
    scene.world(f.reverse, reverse);
    scene.world(1, mesh);
    QVector<Vector3> other;
    for (int i = old.size() - 1; i >= 0; --i)
        other << reverse.rotation.inverted() * (forward * old[i] - reverse.translation) / reverse.scale;
    model.setArray<Vector3>(model.getBlock(f.reverse), "Vertices", other);
    QVector<Vector3> desiredLocal = {Vector3(-2, -1, 0), Vector3(8, -1, 0), Vector3(8, 4, 0),
                                     Vector3(-2, 4, 0)},
                     positions;
    for (auto p : desiredLocal)
    {
        auto world = forward * p;
        f.desired << world;
        positions << mesh.rotation.inverted() * (world - mesh.translation) / mesh.scale;
    }
    model.setArray<Vector3>(model.getBlock(2), "Vertices", positions);
    model.updateHeader();
    model.resetUndoHistory();
    return f;
}
static QVector<Vector3> worldPortal(NifModel &model, int id)
{
    RoomScene scene(&model);
    Transform world;
    check(scene.world(id, world), "resolve portal world transform");
    QVector<Vector3> result;
    for (auto point : model.getArray<Vector3>(model.getBlock(id), "Vertices"))
        result << world * point;
    return result;
}
static void verifyPortalPair(NifModel &model, int forward, int reverse)
{
    auto a = worldPortal(model, forward), b = worldPortal(model, reverse);
    check(a.size() == b.size(), "paired portals have matching polygon sizes");
    for (int i = 0; i < a.size(); ++i)
        check((a[i] - b[b.size() - 1 - i]).length() < .002f,
              "paired portal vertices share world positions with reversed winding");
}
static void portalEditingChecks()
{
    NifModel model;
    auto f = portalFixture(model);
    auto before = bytes(model);
    PortalEditPlan plan;
    QString error;
    check(PortalEditing::fit(&model, f.forward, 1, QSet<int>({0, 1, 2, 3}), true, plan, error),
          "prepare a paired rectangular fit from selected boundary vertices");
    check(plan.changes.size() == 2 && plan.preview.size() == 8 && bytes(model) == before &&
              model.undoStack->count() == 0,
          "portal preview changes no document data or undo history");
    QMap<int, QByteArray> preserved;
    for (int id = 0; id < model.getBlockCount(); ++id)
        if (id != f.forward && id != f.reverse)
            preserved[id] = roomBlockBytes(model, id);
    check(PortalEditing::apply(&model, plan, error), "apply the paired portal update");
    auto current = worldPortal(model, f.forward);
    for (int i = 0; i < 4; ++i)
        check((current[i] - f.desired[i]).length() < .002f,
              "fitted portal matches selected opening through rotated scaled mesh and room parents");
    verifyPortalPair(model, f.forward, f.reverse);
    for (auto i = preserved.cbegin(); i != preserved.cend(); ++i)
        check(roomBlockBytes(model, i.key()) == i.value(),
              "portal update preserves rooms bounds groups meshes occluders and unrelated blocks");
    NifModel expected;
    QBuffer baseline(&before);
    baseline.open(QIODevice::ReadOnly);
    check(expected.load(baseline), "load exact portal preservation baseline");
    for (auto change : plan.changes)
    {
        int id = model.getBlockNumber(change.block);
        expected.set<quint16>(expected.getBlock(id), "Num Vertices", change.vertices.size());
        expected.updateArray(expected.getBlock(id), "Vertices");
        expected.setArray<Vector3>(expected.getBlock(id), "Vertices", change.vertices);
    }
    auto after = bytes(model);
    check(bytes(expected) == after && model.undoStack->count() == 1,
          "only polygon vertices and their counts change in one undo step");
    model.undo();
    check(bytes(model) == before, "portal update undo restores the exact map");
    model.redo();
    check(bytes(model) == after, "portal update redo restores the exact paired update");
    QTemporaryDir directory;
    QString saved = directory.filePath("portal.nif");
    check(model.saveToFile(saved), "save updated portal NIF");
    NifModel loaded;
    check(loaded.loadFromFile(saved), "reload updated portal NIF");
    verifyPortalPair(loaded, f.forward, f.reverse);
    check(loaded.get<quint16>(loaded.getBlock(f.forward), "Portal Flags") == 0x81 &&
              loaded.get<quint16>(loaded.getBlock(f.reverse), "Portal Flags") == 0x42 &&
              loaded.get<quint16>(loaded.getBlock(f.forward), "Plane Count") == 7,
          "portal flags and unrelated portal fields survive editing and save/reload");
    model.undo();
    check(PortalEditing::adjust(&model, f.forward, 2, -1, 1.2f, .8f, true, plan, error),
          "preview direct portal movement and resize");
    check(bytes(model) == before, "direct portal movement preview is read only");
    check(PortalEditing::apply(&model, plan, error), "apply direct portal movement and resize");
    verifyPortalPair(model, f.forward, f.reverse);
    model.undo();
    check(bytes(model) == before, "direct paired adjustment is exactly undoable");
    auto originalTranslation = model.get<Vector3>(model.getBlock(1), "Translation");
    check(PortalEditing::fit(&model, f.forward, 1, {0, 1, 2, 3}, true, plan, error),
          "prepare a guarded preview");
    model.set<Vector3>(model.getBlock(1), "Translation", Vector3(1, 0, 0));
    auto changed = bytes(model);
    check(!PortalEditing::apply(&model, plan, error) && bytes(model) == changed,
          "stale preview is rejected after source transform changes");
    model.set<Vector3>(model.getBlock(1), "Translation", originalTranslation);
    check(PortalEditing::fit(&model, f.forward, 1, {0, 1, 2, 3}, true, plan, error), "refresh guarded fit");
    model.set<quint16>(model.getBlock(f.forward), "Portal Flags", 9);
    changed = bytes(model);
    check(!PortalEditing::apply(&model, plan, error) && bytes(model) == changed,
          "stale preview is rejected after portal state changes");
    model.set<quint16>(model.getBlock(f.forward), "Portal Flags", 0x81);
    model.setLink(model.getBlock(f.reverse), "Adjoiner", f.b);
    changed = bytes(model);
    check(!PortalEditing::fit(&model, f.forward, 1, {0, 1, 2, 3}, true, plan, error) &&
              bytes(model) == changed,
          "missing reverse portal blocks paired editing without changes");
    check(PortalEditing::fit(&model, f.forward, 1, {0, 1, 2, 3}, false, plan, error) &&
              plan.changes.size() == 1,
          "one-way portal editing requires an explicit unpaired request");
    model.setLink(model.getBlock(f.reverse), "Adjoiner", f.a);
    QByteArray reverseBytes;
    QBuffer reverseOutput(&reverseBytes);
    reverseOutput.open(QIODevice::WriteOnly);
    check(model.saveIndex(reverseOutput, model.getBlock(f.reverse)),
          "snapshot reverse portal for ambiguity test");
    int duplicate = model.getBlockNumber(model.insertNiBlock("NiPortal"));
    QBuffer reverseInput(&reverseBytes);
    reverseInput.open(QIODevice::ReadOnly);
    check(model.loadIndex(reverseInput, model.getBlock(duplicate)), "create another matching reverse portal");
    roomLinks(model, f.b, "Children", "Num Children", {3, f.reverse, duplicate});
    roomLinks(model, f.b, "Out Portals", "Num Out Portals", {f.reverse, duplicate});
    model.updateHeader();
    changed = bytes(model);
    check(!PortalEditing::fit(&model, f.forward, 1, {0, 1, 2, 3}, true, plan, error) &&
              bytes(model) == changed,
          "ambiguous reverse portals are rejected instead of updating an arbitrary portal");
    roomLinks(model, f.b, "Children", "Num Children", {3, f.reverse});
    roomLinks(model, f.b, "Out Portals", "Num Out Portals", {f.reverse});
    RoomScene geometryScene(&model);
    Transform meshWorld, portalWorld;
    geometryScene.world(1, meshWorld);
    geometryScene.world(f.forward, portalWorld);
    auto originalPositions = model.getArray<Vector3>(model.getIndex(model.getBlock(2), "Vertices"));
    auto normal = portalWorld.rotation * Vector3(0, 0, 1);
    auto localOffset = meshWorld.rotation.inverted() * (normal * 5.f) / meshWorld.scale;
    auto offsetPositions = originalPositions;
    for (auto &point : offsetPositions)
        point += localOffset;
    model.setArray<Vector3>(model.getBlock(2), "Vertices", offsetPositions);
    auto projectedBefore = bytes(model);
    check(PortalEditing::fit(&model, f.forward, 1, {0, 1, 2, 3}, true, plan, error) &&
              bytes(model) == projectedBefore,
          "parallel wall-face selection can be projected without changing the portal divider plane");
    check(PortalEditing::apply(&model, plan, error), "apply a projected opening fit");
    auto projected = worldPortal(model, f.forward);
    for (int i = 0; i < 4; ++i)
        check((projected[i] - f.desired[i]).length() < .002f,
              "projected fitting keeps the existing portal plane and selected in-plane bounds");
    model.undo();
    check(bytes(model) == projectedBefore, "projected portal fit undoes exactly");
    model.setArray<Vector3>(model.getBlock(2), "Vertices", originalPositions);
    auto positions = model.getArray<Vector3>(model.getIndex(model.getBlock(2), "Vertices"));
    positions[0] += Vector3(10, 20, 30);
    model.setArray<Vector3>(model.getBlock(2), "Vertices", positions);
    changed = bytes(model);
    check(!PortalEditing::fit(&model, f.forward, 1, {0, 1, 2, 3}, true, plan, error) &&
              bytes(model) == changed,
          "nonplanar or wrong-plane source selection is refused without editing the map");
    check(!PortalEditing::fit(&model, f.forward, 1, {0, 1}, false, plan, error),
          "fitting requires a meaningful boundary selection");
    check(!PortalEditing::adjust(&model, f.forward, 0, 0, -1, 1, true, plan, error),
          "negative portal scales are refused");
    auto window = new NifSkope;
    auto active = window->findChild<NifModel *>();
    auto ui = portalFixture(*active);
    window->findChild<QAction *>("aSelectObject")->trigger();
    window->select(active->getBlock(1));
    window->show();
    QCoreApplication::processEvents();
    window->findChild<QAction *>("aRoomEditor")->trigger();
    auto panel = window->findChild<RoomPanel *>("roomPanel");
    auto choice = panel->findChild<QComboBox *>("portalChoice");
    choice->setCurrentIndex(choice->findData(ui.forward));
    auto viewport = window->findChild<GLView *>("OGL1");
    window->findChild<QAction *>("aSelectVertex")->trigger();
    QKeyEvent all(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    QApplication::sendEvent(viewport, &all);
    QCoreApplication::processEvents();
    auto pristine = bytes(*active);
    panel->findChild<QPushButton *>("previewPortalFit")->click();
    check(panel->findChild<QPushButton *>("applyPortalChange")->isEnabled() && bytes(*active) == pristine,
          "Rooms sidebar previews the current viewport selection without writing");
    panel->findChild<QPushButton *>("cancelPortalPreview")->click();
    check(!panel->findChild<QPushButton *>("applyPortalChange")->isEnabled() && bytes(*active) == pristine,
          "canceling portal preview preserves the complete map");
    panel->findChild<QPushButton *>("previewPortalFit")->click();
    panel->findChild<QPushButton *>("applyPortalChange")->click();
    check(bytes(*active) != pristine, "Rooms sidebar applies its explicit paired portal update");
    verifyPortalPair(*active, ui.forward, ui.reverse);
    active->undo();
    QCoreApplication::processEvents();
    check(bytes(*active) == pristine, "sidebar portal edit undoes exactly");
    panel->grab().save(QCoreApplication::applicationDirPath() + "/portal-panel-preview.png");
    delete window;
}
static void realPortalEditingChecks()
{
    for (QString file : {QString("Working.nif"), QString("NotWorking.nif")})
    {
        QString path = "build/qt-check/fixtures/" + file;
        if (!QFile::exists(path))
            continue;
        NifModel model;
        check(model.loadFromFile(path), "load existing MicroVolts portal map");
        RoomScene scene(&model);
        PortalEditPlan plan;
        QString error;
        bool found = false;
        int forward = -1;
        for (auto room : scene.rooms)
        {
            for (int portal : room.outgoing)
                if (PortalEditing::adjust(&model, portal, 5, -3, 1.1f, .9f, true, plan, error))
                {
                    found = true;
                    forward = portal;
                    break;
                }
            if (found)
                break;
        }
        if (!found)
            std::fprintf(stderr, "No portal fit: %s\n", qPrintable(error));
        check(found, "identify an unambiguous paired portal in a real map");
        model.resetUndoHistory();
        auto before = bytes(model);
        QMap<int, QByteArray> preserved;
        QSet<int> edited;
        for (auto change : plan.changes)
            edited.insert(model.getBlockNumber(change.block));
        for (int id = 0; id < model.getBlockCount(); ++id)
            if (!edited.contains(id))
                preserved[id] = roomBlockBytes(model, id);
        check(PortalEditing::apply(&model, plan, error),
              "apply a paired portal edit on the existing MicroVolts map");
        bool unchanged = true;
        for (auto i = preserved.cbegin(); i != preserved.cend(); ++i)
            unchanged = unchanged && roomBlockBytes(model, i.key()) == i.value();
        check(
            unchanged,
            "real portal editing preserves every room group wall plane fixture material and unrelated block");
        int reverse = -1;
        for (auto id : edited)
            if (id != forward)
                reverse = id;
        verifyPortalPair(model, forward, reverse);
        auto after = bytes(model);
        model.undo();
        check(bytes(model) == before, "real portal editing has exact one-step undo");
        model.redo();
        check(bytes(model) == after, "real portal editing has exact redo");
        QTemporaryDir directory;
        QString output = directory.filePath("portal.nif");
        check(model.saveToFile(output), "save real updated portal map to a temporary file");
        NifModel loaded;
        check(loaded.loadFromFile(output), "reload real updated portal map");
        verifyPortalPair(loaded, forward, reverse);
    }
}
#endif
