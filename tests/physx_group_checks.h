#ifndef PHYSX_GROUP_CHECKS_H
#define PHYSX_GROUP_CHECKS_H
#include "physx/physxcollision.h"
#include <QTemporaryDir>
#include <QtEndian>
static QVector<quint32> collisionSourceIndices(NifModel &model, const QModelIndex &mesh)
{
    QVector<quint32> indices;
    auto refs = model.getIndex(mesh, "Datastreams");
    bool big = model.get<quint8>(model.getHeader(), "Endian Type") == 0;
    for (int r = 0; r < model.rowCount(refs); ++r)
    {
        auto ref = refs.child(r, 0), sem = model.getIndex(ref, "Component Semantics");
        bool index = false;
        for (int c = 0; c < model.rowCount(sem); ++c)
            if (model.get<QString>(sem.child(c, 0), "Name") == "INDEX")
                index = true;
        if (!index)
            continue;
        auto stream = model.getBlock(model.getLink(ref, "Stream"));
        auto formats = model.getArray<uint>(stream, "Component Formats");
        int width = formats.first() == NiMesh::F_UINT16_1 ? 2 : 4;
        auto bytes = model.get<QByteArray>(model.getIndex(stream, "Data").child(0, 0));
        for (int p = 0; p < bytes.size(); p += width)
            indices << (width == 2 ? (big ? qFromBigEndian<quint16>(bytes.constData() + p)
                                          : qFromLittleEndian<quint16>(bytes.constData() + p))
                                   : (big ? qFromBigEndian<quint32>(bytes.constData() + p)
                                          : qFromLittleEndian<quint32>(bytes.constData() + p)));
    }
    return indices;
}
static PhysXCollision::Mesh findCollision(const QVector<PhysXCollision::Mesh> &meshes, const QString &name)
{
    for (auto mesh : meshes)
        if (mesh.name == name)
            return mesh;
    return PhysXCollision::Mesh();
}
static void verifyGroupGeometry(NifModel &model, const PhysXCollision::Mesh &mesh)
{
    QVector<Vector3> expected;
    QVector<quint32> triangles;
    for (int part = 0; part < mesh.sourceMeshes.size(); ++part)
    {
        VertexEditData data;
        check(data.read(&model, mesh.sourceMeshes[part]), "read contributor after grouped rebuild");
        auto transform = mesh.world.inverted() * mesh.sourceWorlds[part];
        quint32 offset = expected.size();
        for (auto v : data.positions)
        {
            auto p = transform.map(QVector3D(v[0], v[1], v[2]));
            expected << Vector3(p.x(), p.y(), p.z());
        }
        auto indices = collisionSourceIndices(model, mesh.sourceMeshes[part]);
        if (transform.determinant() < 0)
            for (int i = 0; i < indices.size(); i += 3)
                qSwap(indices[i + 1], indices[i + 2]);
        for (auto i : indices)
            triangles << offset + i;
    }
    QVector<int> canonical;
    for (int i = 0; i < expected.size(); ++i)
    {
        int root = i;
        for (int j = 0; j < i; ++j)
            if ((expected[i] - expected[j]).length() < .02f)
            {
                root = canonical[j];
                break;
            }
        canonical << root;
    }
    QVector<int> mapping;
    bool close = true;
    for (auto v : mesh.vertices)
    {
        int index = -1;
        float nearest = 1e30f;
        for (int i = 0; i < expected.size(); ++i)
        {
            float d = (v - expected[i]).length();
            if (d < nearest)
            {
                nearest = d;
                index = i;
            }
        }
        close = close && nearest < .03f;
        mapping << index;
    }
    check(close, "saved grouped collider has the physical positions requested by every child transform");
    auto key = [](int a, int b, int c)
    {
        if (b < a && b < c)
        {
            int old = a;
            a = b;
            b = c;
            c = old;
        }
        else if (c < a && c < b)
        {
            int old = a;
            a = c;
            c = b;
            b = old;
        }
        return QString("%1,%2,%3").arg(a).arg(b).arg(c);
    };
    QMap<QString, int> wanted, actual;
    for (int i = 0; i < triangles.size(); i += 3)
        ++wanted[key(canonical[triangles[i]], canonical[triangles[i + 1]], canonical[triangles[i + 2]])];
    for (int i = 0; i < mesh.indices.size(); i += 3)
        ++actual[key(canonical[mapping[mesh.indices[i]]], canonical[mapping[mesh.indices[i + 1]]],
                     canonical[mapping[mesh.indices[i + 2]]])];
    check(wanted == actual, "grouped collider includes all child triangles with the correct winding");
}
static void groupedCollisionChecks(const QString &path, const QString &dll)
{
    NifModel model;
    check(model.loadFromFile(path), "load grouped collision fixture without modifying the source file");
    QVector<PhysXCollision::Mesh> colliders;
    QString error;
    bool ok = PhysXCollision::collect(&model, colliders, error);
    if (!ok)
        std::fprintf(stderr, "Collect: %s\n", qPrintable(error));
    check(ok, "resolve all collision sources including named NiNode groups");
    PhysXCollision::Mesh grouped;
    for (auto collider : colliders)
        if (collider.sourceMeshes.size() > 1)
        {
            grouped = collider;
            if (collider.name == "coc_sand")
                break;
        }
    check(grouped.source.isValid() && grouped.sourceMeshes.size() >= 2,
          "find the actual multi-mesh source group");
    QString name = grouped.name;
    int groupId = model.getBlockNumber(grouped.source),
        partId = model.getBlockNumber(grouped.sourceMeshes.last());
    auto pristine = bytes(model);
    model.resetUndoHistory();
    check(PhysXCollision::collect(&model, colliders, error) && bytes(model) == pristine &&
              model.undoStack->count() == 0,
          "collecting grouped sources does not change the map");
    check(makeMeshGeometryUnique(&model, model.getBlock(partId), &error),
          "prepare an independent child for a vertex-edit regression");
    VertexEditData edit;
    check(edit.read(&model, model.getBlock(partId)), "read child vertex geometry");
    auto indices = collisionSourceIndices(model, model.getBlock(partId));
    check(!indices.isEmpty(), "find a referenced child vertex");
    int vertex = indices.first();
    auto changed = edit.positions;
    changed[vertex] += Vector3(125, -70, 35);
    check(edit.write(&model, changed, QSet<int>({vertex})),
          "edit a referenced vertex of only one child mesh");
    edit.updateBounds(&model);
    auto parent = Transform(&model, model.getBlock(groupId));
    parent.translation += Vector3(100, 20, -15);
    parent.writeBack(&model, model.getBlock(groupId));
    auto child = Transform(&model, model.getBlock(partId));
    child.scale = -child.scale;
    child.translation += Vector3(40, -10, 25);
    child.writeBack(&model, model.getBlock(partId));
    model.updateHeader();
    model.resetUndoHistory();
    auto before = bytes(model);
    QString helper = QCoreApplication::applicationDirPath() + "/PhysxCooker.exe";
    int count = 0;
    ok = PhysXCollision::rebuild(&model, dll, helper, error, &count);
    if (!ok)
        std::fprintf(stderr, "Rebuild: %s\n", qPrintable(error));
    check(ok && count == colliders.size(), "rebuild all colliders after grouped parent and child edits");
    check(PhysXCollision::collect(&model, colliders, error), "refresh rebuilt collision");
    grouped = findCollision(colliders, name);
    verifyGroupGeometry(model, grouped);
    auto after = bytes(model);
    check(model.undoStack->count() == 1, "grouped rebuild is one undo step");
    NifModel expected;
    QBuffer baseline(&before);
    baseline.open(QIODevice::ReadOnly);
    check(expected.load(baseline), "load preservation baseline");
    for (auto mesh : colliders)
    {
        int id = model.getBlockNumber(mesh.data);
        expected.set<QByteArray>(expected.getBlock(id), "Mesh Data", model.get<QByteArray>(mesh.data));
    }
    check(bytes(expected) == after, "only cooked collision payloads change; hierarchy transforms materials "
                                    "rooms and metadata are preserved");
    QTemporaryDir directory;
    QString savedPath = directory.filePath("World_colp.nif");
    check(model.saveToFile(savedPath), "write grouped collision to a real disk file");
    NifModel disk;
    check(disk.loadFromFile(savedPath) && PhysXCollision::collect(&disk, colliders, error),
          "reopen saved grouped collision file");
    auto savedGroup = findCollision(colliders, name);
    verifyGroupGeometry(disk, savedGroup);
    check(disk.get<QByteArray>(savedGroup.data) == model.get<QByteArray>(grouped.data),
          "saved disk NIF retains the exact new collision payload");
    // Save sanitization uses the same path as the editor's standard Save command.
    SpellBook::sanitize(&disk);
    check(disk.saveToFile(directory.filePath("sanitized.nif")),
          "save grouped collision with normal Save sanitization");
    NifModel sanitized;
    check(sanitized.loadFromFile(directory.filePath("sanitized.nif")) &&
              PhysXCollision::collect(&sanitized, colliders, error),
          "reopen normally sanitized collision NIF");
    verifyGroupGeometry(sanitized, findCollision(colliders, name));
    QFile validation(QCoreApplication::applicationDirPath() + "/grouped-collision-runtime.nxs");
    check(validation.open(QIODevice::WriteOnly), "save workspace-only runtime-validation payload");
    validation.write(model.get<QByteArray>(grouped.data));
    validation.close();
    model.undo();
    check(bytes(model) == before, "undo restores the exact pre-rebuild collision map");
    model.redo();
    check(bytes(model) == after, "redo restores the exact grouped collision map");
    check(PhysXCollision::collect(&model, colliders, error), "refresh group references after undo/redo");
    grouped = findCollision(colliders, name);
    auto oldPayload = model.get<QByteArray>(grouped.data);
    auto next = Transform(&model, model.getBlock(groupId));
    next.translation += Vector3(75, -20, 10);
    next.writeBack(&model, model.getBlock(groupId));
    check(PhysXCollision::rebuild(&model, dll, helper, error) &&
              PhysXCollision::collect(&model, colliders, error),
          "rebuild again after a subsequent group move");
    grouped = findCollision(colliders, name);
    check(model.get<QByteArray>(grouped.data) != oldPayload,
          "subsequent group movement changes the actual cooked payload");
    verifyGroupGeometry(model, grouped);
    // A child can share its source node's name; unrelated duplicates stay ambiguous.
    model.set<QString>(model.getBlock(partId), "Name", name);
    check(PhysXCollision::collect(&model, colliders, error) &&
              findCollision(colliders, name).source == model.getBlock(groupId),
          "same-named children resolve to their unique enclosing group");
    auto duplicate = model.insertNiBlock("NiNode");
    model.set<QString>(duplicate, "Name", name);
    model.updateHeader();
    auto ambiguous = bytes(model);
    int history = model.undoStack->count();
    check(!PhysXCollision::rebuild(&model, dll, helper, error) && bytes(model) == ambiguous &&
              model.undoStack->count() == history,
          "ambiguous source groups abort without altering any collision data");
    model.set<QString>(duplicate, "Name", "unrelated test node");
    model.set<uint>(model.getBlock(partId), "Primitive Type", 1);
    auto unsupported = bytes(model);
    history = model.undoStack->count();
    check(!PhysXCollision::rebuild(&model, dll, helper, error) && bytes(model) == unsupported &&
              model.undoStack->count() == history,
          "unsupported child geometry aborts the entire rebuild before writing");
    check(PhysXCollision::collect(&model, colliders, error, false),
          "existing collision preview remains readable when source rebuilding is unsupported");
}
#endif
