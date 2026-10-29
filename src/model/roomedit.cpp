#include "roomedit.h"
#include "nifmodel.h"
#include "vertexedit.h"
#include <cmath>
#include <functional>
namespace
{
float dot(const Vector3 &a, const Vector3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vector3 cross(const Vector3 &a, const Vector3 &b)
{
    return Vector3(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]);
}
bool finite(const Transform &t)
{
    if (!std::isfinite(t.scale) || std::abs(t.scale) < 1e-8f)
        return false;
    Vector3 axes[3];
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(t.translation[i]))
            return false;
        axes[i] = t.rotation * Vector3(i == 0, i == 1, i == 2);
        for (int j = 0; j < 3; ++j)
            if (!std::isfinite(axes[i][j]))
                return false;
        if (std::abs(axes[i].length() - 1) > 1e-3f)
            return false;
    }
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            if (std::abs(dot(axes[i], axes[j])) > 1e-3f)
                return false;
    return true;
}
} // namespace
RoomScene::RoomScene(NifModel *m) : model(m)
{
    if (!model)
        return;
    for (int b = 0; b < model->getBlockCount(); ++b)
    {
        auto block = model->getBlock(b);
        if (model->inherits(block, "NiNode"))
            for (int child : model->getLinkArray(block, "Children"))
                if (child >= 0)
                    parents[child] << b;
        if (model->getBlockName(block) == "NiRoom")
        {
            RoomRecord r;
            r.id = b;
            r.fixtures = model->getLinkArray(block, "Fixtures");
            r.incoming = model->getLinkArray(block, "In Portals");
            r.outgoing = model->getLinkArray(block, "Out Portals");
            rooms << r;
        }
    }
    for (int b = 0; b < model->getBlockCount(); ++b)
        if (model->getBlockName(model->getBlock(b)) == "NiRoomGroup")
            for (int id : model->getLinkArray(model->getBlock(b), "Rooms"))
                for (auto &r : rooms)
                    if (r.id == id)
                        r.groups << b;
}
const RoomRecord *RoomScene::room(int id) const
{
    for (const auto &r : rooms)
        if (r.id == id)
            return &r;
    return nullptr;
}
QVector<int> RoomScene::ancestry(int id) const
{
    QVector<int> chain;
    while (id >= 0 && !chain.contains(id))
    {
        chain << id;
        auto p = parents.value(id);
        if (p.size() != 1)
            break;
        id = p.first();
    }
    return chain;
}
bool RoomScene::world(int id, Transform &result, QString *reason) const
{
    result = Transform();
    QSet<int> visited;
    while (id >= 0)
    {
        if (visited.contains(id))
        {
            if (reason)
                *reason = QObject::tr("The hierarchy contains a cycle.");
            return false;
        }
        visited.insert(id);
        auto block = model->getBlock(id);
        if (!Transform::canConstruct(model, block))
        {
            if (reason)
                *reason = QObject::tr("The hierarchy contains an unsupported transform.");
            return false;
        }
        auto local = Transform(model, block);
        if (!finite(local))
        {
            if (reason)
                *reason = QObject::tr("The hierarchy has an invalid or zero-scale transform.");
            return false;
        }
        result = local * result;
        auto p = parents.value(id);
        if (p.size() > 1)
        {
            if (reason)
                *reason = QObject::tr("The object has multiple scene parents.");
            return false;
        }
        id = p.isEmpty() ? -1 : p.first();
    }
    return finite(result);
}
QVector<int> RoomScene::memberships(int id) const
{
    auto chain = ancestry(id);
    QVector<int> result;
    for (const auto &r : rooms)
        for (int fixture : r.fixtures)
            if (chain.contains(fixture))
            {
                result << r.id;
                break;
            }
    return result;
}
QVector<int> RoomScene::containingRooms(int mesh, QString *reason) const
{
    QVector<int> result;
    VertexEditData data;
    if (!model || !data.read(model, model->getBlock(mesh), reason))
        return result;
    for (auto point : data.positions)
        for (int a = 0; a < 3; ++a)
            if (!std::isfinite(point[a]))
            {
                if (reason)
                    *reason = QObject::tr("The mesh contains non-finite coordinates.");
                return result;
            }
    Transform meshWorld;
    if (!world(mesh, meshWorld, reason))
        return result;
    for (const auto &r : rooms)
    {
        Transform transform;
        if (!world(r.id, transform))
            continue;
        auto planes = model->getIndex(model->getBlock(r.id), "Wall Planes");
        if (!model->rowCount(planes))
            continue;
        bool inside = true;
        for (int p = 0; p < model->rowCount(planes) && inside; ++p)
        {
            auto plane = planes.child(p, 0);
            auto n = model->get<Vector3>(plane, "Normal");
            float c = model->get<float>(plane, "Constant"), length = n.length();
            if (!std::isfinite(length) || length < 1e-8f || !std::isfinite(c))
            {
                inside = false;
                break;
            }
            for (auto point : data.positions)
            {
                auto local = transform.rotation.inverted() * (meshWorld * point - transform.translation) /
                             transform.scale;
                if (dot(n, local) - c < -.001f * length)
                {
                    inside = false;
                    break;
                }
            }
        }
        if (inside)
            result << r.id;
    }
    return result;
}
QVector<QPair<Vector3, Vector3>> RoomScene::cage(int id) const
{
    QVector<QPair<Vector3, Vector3>> edges;
    Transform transform;
    if (!model || !world(id, transform))
        return edges;
    auto planes = model->getIndex(model->getBlock(id), "Wall Planes");
    int count = model->rowCount(planes);
    if (count > 32)
        return edges;
    QVector<Vector3> normals;
    QVector<float> constants;
    for (int i = 0; i < count; ++i)
    {
        auto n = model->get<Vector3>(planes.child(i, 0), "Normal");
        float length = n.length();
        if (!std::isfinite(length) || length < 1e-8f)
            return edges;
        normals << n / length;
        float constant = model->get<float>(planes.child(i, 0), "Constant");
        if (!std::isfinite(constant))
            return edges;
        constants << constant / length;
    }
    QVector<Vector3> vertices;
    for (int a = 0; a < count; ++a)
        for (int b = a + 1; b < count; ++b)
            for (int c = b + 1; c < count; ++c)
            {
                float determinant = dot(normals[a], cross(normals[b], normals[c]));
                if (std::abs(determinant) < 1e-6f)
                    continue;
                auto p = (cross(normals[b], normals[c]) * constants[a] +
                          cross(normals[c], normals[a]) * constants[b] +
                          cross(normals[a], normals[b]) * constants[c]) /
                         determinant;
                bool inside = true;
                for (int i = 0; i < count; ++i)
                    if (dot(normals[i], p) - constants[i] < -.01f)
                        inside = false;
                for (auto old : vertices)
                    if ((old - p).length() < .01f)
                        inside = false;
                if (inside)
                    vertices << p;
            }
    for (int a = 0; a < vertices.size(); ++a)
        for (int b = a + 1; b < vertices.size(); ++b)
        {
            int shared = 0;
            for (int p = 0; p < count; ++p)
                if (std::abs(dot(normals[p], vertices[a]) - constants[p]) < .01f &&
                    std::abs(dot(normals[p], vertices[b]) - constants[p]) < .01f)
                    ++shared;
            if (shared >= 2)
                edges << qMakePair(transform * vertices[a], transform * vertices[b]);
        }
    return edges;
}
bool RoomScene::assign(int selected, int destination, bool apply, QString *reason)
{
    auto fail = [&](const QString &text)
    {
        if (reason)
            *reason = text;
        return false;
    };
    if (!model)
        return fail(QObject::tr("No document."));
    auto mesh = model->getBlock(selected);
    if (!(model->getBlockName(mesh) == "NiMesh" || model->getBlockName(mesh) == "NiTriShape"))
        return fail(QObject::tr(
            "Select a static NiMesh or NiTriShape mesh. Room/group/portal nodes are not reassigned."));
    VertexEditData geometry;
    if (!geometry.read(model, mesh, reason))
        return false;
    auto sourceRooms = memberships(selected);
    if (sourceRooms.size() != 1)
        return fail(QObject::tr("Reassignment requires exactly one existing room membership; shared or "
                                "unassigned objects need manual review."));
    int source = sourceRooms.first();
    auto src = room(source), dst = room(destination);
    if (!dst)
        return fail(QObject::tr("Choose an existing room."));
    if (src->groups.size() != 1 || dst->groups != src->groups)
        return fail(QObject::tr("Both rooms must belong to the same single room group."));
    auto oldParents = parents.value(selected);
    if (oldParents.size() != 1)
        return fail(QObject::tr("The mesh must have exactly one ordinary scene parent."));
    int oldParent = oldParents.first();
    auto oldChain = ancestry(selected), newChain = ancestry(destination);
    if (!oldChain.contains(source))
        return fail(QObject::tr("Fixture membership and scene ownership disagree; no changes were made."));
    if (parents.value(source) != QVector<int>{src->groups.first()} ||
        parents.value(destination) != QVector<int>{src->groups.first()})
        return fail(
            QObject::tr("Room-group membership and ordinary ownership disagree; no changes were made."));
    for (int id : oldChain + newChain)
    {
        auto block = model->getBlock(id);
        if (model->get<QString>(block, "Name").contains("occlud", Qt::CaseInsensitive))
            return fail(QObject::tr("Occluder-tagged objects and hierarchies are protected."));
        if (model->getLink(block, "Controller") >= 0)
            return fail(QObject::tr("Animated hierarchies require manual review before reparenting."));
    }
    for (int id : oldChain + newChain)
    {
        auto block = model->getBlock(id);
        auto metadata = model->getLinkArray(block, "Extra Data List");
        int first = model->getLink(block, "Extra Data");
        if (first >= 0)
            metadata << first;
        QSet<int> seen;
        while (!metadata.isEmpty())
        {
            int extra = metadata.takeLast();
            if (extra < 0 || seen.contains(extra))
                continue;
            seen.insert(extra);
            auto data = model->getBlock(extra);
            QString tags = model->get<QString>(data, "Name") + model->get<QString>(data, "String Data");
            if (tags.contains("occlud", Qt::CaseInsensitive))
                return fail(QObject::tr("Occluder metadata is protected; no changes were made."));
            int next = model->getLink(data, "Next Extra Data");
            if (next >= 0)
                metadata << next;
        }
    }
    if (model->getBlockName(model->getBlock(oldParent)) != "NiNode" && oldParent != source)
        return fail(
            QObject::tr("The mesh has a special parent; its behavior cannot be preserved automatically."));
    for (int id : oldChain)
    {
        if (id == source)
            break;
        if (id == selected)
            continue;
        auto block = model->getBlock(id);
        if (model->getBlockName(block) != "NiNode" ||
            !model->getLinkArray(block, "Extra Data List").isEmpty() ||
            model->getLink(block, "Extra Data") >= 0)
            return fail(QObject::tr("A tagged or specialized fixture ancestor requires manual review."));
    }
    Transform oldWorld, parentWorld;
    if (!world(selected, oldWorld, reason) || !world(destination, parentWorld, reason))
        return false;
    // Moving a nested fixture also changes its old ancestors' contents. Protect
    // external references to that entire path, not only to the selected mesh.
    QSet<int> affected;
    for (int id : oldChain)
    {
        if (id == source)
            break;
        affected.insert(id);
    }
    bool external = false;
    std::function<void(QModelIndex, int)> refs = [&](QModelIndex parent, int owner)
    {
        for (int r = 0; r < model->rowCount(parent); ++r)
        {
            auto field = parent.child(r, 0);
            if (model->isLink(field) && affected.contains(model->getLink(field)))
            {
                int referenced = model->getLink(field);
                bool child =
                    parents.value(referenced) == QVector<int>{owner} && model->itemName(parent) == "Children";
                bool fixture = owner == source && model->itemName(parent) == "Fixtures";
                if (!child && !fixture)
                    external = true;
            }
            refs(field, owner);
        }
    };
    for (int b = 0; b < model->getBlockCount() && !external; ++b)
        refs(model->getBlock(b), b);
    if (external)
        return fail(QObject::tr(
            "Other systems reference this mesh. Reassignment is blocked to preserve those links."));
    auto properties = [&](const QVector<int> &chain)
    {
        QMap<QString, int> result;
        for (int id : chain)
            for (int prop : model->getLinkArray(model->getBlock(id), "Properties"))
            {
                auto block = model->getBlock(prop);
                auto type = model->getBlockName(block);
                if (block.isValid() && !result.contains(type))
                    result[type] = prop;
            }
        return result;
    };
    auto oldProps = properties(oldChain), targetProps = properties(newChain);
    for (auto i = targetProps.cbegin(); i != targetProps.cend(); ++i)
        if (!oldProps.contains(i.key()))
            return fail(QObject::tr(
                "The destination adds inherited render properties that cannot be neutralized safely."));
    auto effects = [&](const QVector<int> &chain)
    {
        QSet<int> result;
        for (int id : chain)
            for (int effect : model->getLinkArray(model->getBlock(id), "Effects"))
                if (effect >= 0)
                    result.insert(effect);
        return result;
    };
    if (effects(oldChain) != effects(newChain))
        return fail(QObject::tr(
            "The destination has different inherited effects or lights; automatic reassignment is unsafe."));
    auto hidden = [&](const QVector<int> &chain)
    {
        for (int id : chain)
            if (model->get<quint16>(model->getBlock(id), "Flags") & 1)
                return true;
        return false;
    };
    if (hidden(oldChain) != (hidden(newChain) || (model->get<quint16>(mesh, "Flags") & 1)))
        return fail(QObject::tr("The destination changes inherited visibility; reassignment needs manual review."));
    if (source == destination)
    {
        if (reason)
            reason->clear();
        return true;
    }
    auto children = model->getLinkArray(model->getBlock(oldParent), "Children"),
         fixtures = model->getLinkArray(model->getBlock(source), "Fixtures"),
         newChildren = model->getLinkArray(model->getBlock(destination), "Children"),
         newFixtures = model->getLinkArray(model->getBlock(destination), "Fixtures");
    if (children.count(selected) != 1 || fixtures.count(selected) > 1 || newChildren.contains(selected) ||
        newFixtures.contains(selected))
        return fail(QObject::tr("Duplicate or inconsistent membership links require manual review."));
    children.removeAll(selected);
    fixtures.removeAll(selected);
    newChildren << selected;
    newFixtures << selected;
    Transform local;
    local.scale = oldWorld.scale / parentWorld.scale;
    local.rotation = parentWorld.rotation.inverted() * oldWorld.rotation;
    local.translation = parentWorld.rotation.inverted() * (oldWorld.translation - parentWorld.translation) /
                        parentWorld.scale;
    if (!finite(local))
        return fail(QObject::tr("Cannot preserve the world transform."));
    auto meshProps = model->getLinkArray(mesh, "Properties");
    for (int prop : oldProps)
        if (!meshProps.contains(prop))
            meshProps << prop;
    if (meshProps != model->getLinkArray(mesh, "Properties") &&
        !model->getIndex(mesh, "Properties").isValid())
        return fail(QObject::tr(
            "This mesh cannot store the inherited properties needed to preserve its appearance."));
    if (!apply)
    {
        if (reason)
            reason->clear();
        return true;
    }
    model->beginUndoTransaction(QObject::tr("Reassign mesh to room"));
    bool held = model->holdUpdates(true);
    auto links = [&](int block, const QString &array, const QString &count, const QVector<qint32> &values)
    {
        auto index = model->getBlock(block);
        model->set<uint>(index, count, values.size());
        model->updateArray(index, array);
        model->setLinkArray(index, array, values);
    };
    links(oldParent, "Children", "Num Children", children);
    if (src->fixtures.contains(selected))
        links(source, "Fixtures", "Num Fixtures", fixtures);
    links(destination, "Children", "Num Children", newChildren);
    links(destination, "Fixtures", "Num Fixtures", newFixtures);
    if (meshProps != model->getLinkArray(mesh, "Properties"))
        links(selected, "Properties", "Num Properties", meshProps);
    local.writeBack(model, mesh);
    model->updateHeader();
    model->holdUpdates(held);
    emit model->dataChanged(mesh, mesh);
    model->endUndoTransaction();
    if (reason)
        reason->clear();
    return true;
}
QStringList RoomScene::validate() const
{
    QStringList issues;
    if (!model)
        return issues;
    for (const auto &r : rooms)
    {
        auto block = model->getBlock(r.id);
        QString label = QString("#%1 %2").arg(r.id).arg(model->get<QString>(block, "Name"));
        if (r.groups.size() != 1)
            issues << label + QObject::tr(": not a member of exactly one room group.");
        Transform transform;
        if (!world(r.id, transform))
            issues << label + QObject::tr(": invalid/shared/cyclic transform hierarchy.");
        auto planes = model->getIndex(block, "Wall Planes");
        if (!model->rowCount(planes))
            issues << label + QObject::tr(": no wall planes; containment is ambiguous.");
        for (int p = 0; p < model->rowCount(planes); ++p)
        {
            auto plane = planes.child(p, 0);
            auto n = model->get<Vector3>(plane, "Normal");
            if (!std::isfinite(n.length()) || n.length() < 1e-8f ||
                !std::isfinite(model->get<float>(plane, "Constant")))
                issues << label + QObject::tr(": invalid wall plane.");
        }
        QSet<int> seen;
        for (int portal : r.outgoing)
        {
            if (seen.contains(portal))
                issues << label + QObject::tr(": duplicate outgoing portal.");
            seen.insert(portal);
            auto target = model->getBlock(portal);
            if (model->getBlockName(target) != "NiPortal")
            {
                issues << label + QObject::tr(": invalid outgoing portal reference.");
                continue;
            }
            if (!model->getLinkArray(block, "Children").contains(portal))
                issues << label + QObject::tr(": outgoing portal is not a room child.");
            auto destination = room(model->getLink(target, "Adjoiner"));
            if (!destination)
                issues << label +
                              QObject::tr(": portal has a non-room or missing target (may be intentional).");
            else if (!destination->incoming.contains(portal))
                issues << label + QObject::tr(": destination lacks optional incoming portal reference.");
            if (model->rowCount(model->getIndex(target, "Vertices")) < 3)
                issues << label + QObject::tr(": portal has fewer than three vertices.");
        }
        seen.clear();
        for (int fixture : r.fixtures)
        {
            if (seen.contains(fixture))
                issues << label + QObject::tr(": duplicate fixture.");
            seen.insert(fixture);
            if (!model->inherits(model->getBlock(fixture), "NiAVObject"))
                issues << label + QObject::tr(": missing or invalid fixture.");
        }
    }
    return issues;
}
