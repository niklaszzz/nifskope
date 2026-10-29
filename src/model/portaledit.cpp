#include "portaledit.h"
#include "nifmodel.h"
#include "roomedit.h"
#include "vertexedit.h"
#include <QBuffer>
#include <algorithm>
#include <cmath>
namespace
{
float dot(const Vector3 &a, const Vector3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vector3 cross(const Vector3 &a, const Vector3 &b)
{
    return Vector3(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]);
}
bool finite(const Vector3 &p) { return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]); }
bool serialize(NifModel *model, const QModelIndex &block, QByteArray &bytes)
{
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    return block.isValid() && model->saveIndex(buffer, block);
}
struct Polygon
{
    int id = -1, source = -1, destination = -1;
    Transform world;
    QVector<Vector3> points;
    Vector3 center, normal, u, v;
    float tolerance = .01f;
};
bool polygon(NifModel *model, const RoomScene &scene, int id, Polygon &p, QString &error)
{
    auto fail = [&](const QString &text)
    {
        error = text;
        return false;
    };
    auto block = model->getBlock(id);
    if (model->getBlockName(block) != "NiPortal")
        return fail(QObject::tr("Choose an existing NiPortal."));
    if (model->getLink(block, "Collision Object") >= 0 || model->get<bool>(block, "Has Bounding Volume"))
        return fail(QObject::tr(
            "Portals with collision bindings or legacy serialized bounding volumes need manual review."));
    p.id = id;
    for (const auto &room : scene.rooms)
        if (room.outgoing.contains(id))
        {
            if (p.source >= 0 || room.outgoing.count(id) != 1)
                return fail(QObject::tr("The portal has ambiguous outgoing room membership."));
            p.source = room.id;
        }
    if (p.source < 0 || scene.parents.value(id) != QVector<int>{p.source})
        return fail(QObject::tr("The portal must be owned by its one outgoing room."));
    p.destination = model->getLink(block, "Adjoiner");
    const RoomRecord *source = nullptr, *target = nullptr;
    for (const auto &r : scene.rooms)
    {
        if (r.id == p.source)
            source = &r;
        if (r.id == p.destination)
            target = &r;
    }
    if (!source || !target || p.source == p.destination || source->groups.size() != 1 ||
        source->groups != target->groups)
        return fail(QObject::tr("Portal editing requires two rooms in the same unambiguous room group."));
    if (!scene.world(id, p.world, &error))
        return false;
    QSet<int> visited;
    int ancestor = id;
    while (ancestor >= 0)
    {
        if (visited.contains(ancestor))
            return fail(QObject::tr("Cyclic portal hierarchy."));
        visited.insert(ancestor);
        if (model->getLink(model->getBlock(ancestor), "Controller") >= 0)
            return fail(QObject::tr("Animated portal hierarchies are not supported."));
        auto parents = scene.parents.value(ancestor);
        ancestor = parents.isEmpty() ? -1 : parents.first();
    }
    auto points = model->getArray<Vector3>(block, "Vertices");
    if (points.size() < 3 || points.size() > 65535)
        return fail(QObject::tr("The portal has an unsupported polygon."));
    for (auto point : points)
    {
        point = p.world * point;
        if (!finite(point))
            return fail(QObject::tr("The portal contains invalid coordinates."));
        p.points << point;
        p.center += point;
    }
    p.center /= float(p.points.size());
    Vector3 normal;
    float diameter = 0;
    for (int i = 0; i < p.points.size(); ++i)
    {
        normal += cross(p.points[i] - p.center, p.points[(i + 1) % p.points.size()] - p.center);
        diameter = qMax(diameter, (p.points[i] - p.center).length());
    }
    if (!std::isfinite(normal.length()) || normal.length() < 1e-7f)
        return fail(QObject::tr("The portal polygon has no usable area."));
    p.normal = normal / normal.length();
    p.tolerance = qMax(.01f, diameter * 1e-4f);
    for (auto point : p.points)
        if (std::abs(dot(point - p.center, p.normal)) > p.tolerance)
            return fail(QObject::tr("The existing portal is not planar."));
    for (int i = 1; i < p.points.size(); ++i)
    {
        auto edge = p.points[i] - p.points[0];
        edge -= p.normal * dot(edge, p.normal);
        if (edge.length() > p.tolerance)
        {
            p.u = edge / edge.length();
            break;
        }
    }
    if (p.u.length() < .5f)
        return fail(QObject::tr("The portal has no usable edge."));
    p.v = cross(p.normal, p.u);
    p.v /= p.v.length();
    return true;
}
float segmentDistance(Vector3 p, Vector3 a, Vector3 b)
{
    auto d = b - a;
    float length = dot(d, d);
    float t = length > 1e-12f ? qBound(0.f, dot(p - a, d) / length, 1.f) : 0;
    return (p - a - d * t).length();
}
bool sameOpening(const Polygon &a, const Polygon &b)
{
    if (dot(a.normal, b.normal) > -.99f)
        return false;
    float tolerance = qMax(a.tolerance, b.tolerance) * 2;
    auto boundary = [&](const QVector<Vector3> &points, const QVector<Vector3> &edges)
    {
        for (auto point : points)
        {
            float nearest = 1e30f;
            for (int i = 0; i < edges.size(); ++i)
                nearest = qMin(nearest, segmentDistance(point, edges[i], edges[(i + 1) % edges.size()]));
            if (nearest > tolerance)
                return false;
        }
        return true;
    };
    return boundary(a.points, b.points) && boundary(b.points, a.points);
}
bool finish(NifModel *model, const RoomScene &scene, const Polygon &source, const QVector<Vector3> &points,
            bool paired, const QSet<int> &extraGuards, PortalEditPlan &output, QString &error)
{
    output = PortalEditPlan();
    PortalEditPlan plan;
    Vector3 area;
    for (int i = 0; i < points.size(); ++i)
    {
        if (!finite(points[i]))
        {
            error = QObject::tr("The proposed opening contains invalid coordinates.");
            return false;
        }
        area += cross(points[i] - source.center, points[(i + 1) % points.size()] - source.center);
    }
    if (!std::isfinite(area.length()) || area.length() < 1e-7f ||
        dot(area / area.length(), source.normal) < .99f)
    {
        error = QObject::tr("The proposed opening is degenerate or reverses its facing.");
        return false;
    }
    plan.model = model;
    QVector<Polygon> polygons{source};
    QVector<QVector<Vector3>> results{points};
    if (paired)
    {
        QVector<Polygon> candidates;
        for (const auto &room : scene.rooms)
            if (room.id == source.destination)
                for (int id : room.outgoing)
                {
                    if (model->getLink(model->getBlock(id), "Adjoiner") != source.source)
                        continue;
                    Polygon candidate;
                    QString reason;
                    if (polygon(model, scene, id, candidate, reason) && sameOpening(source, candidate))
                        candidates << candidate;
                }
        if (candidates.size() != 1)
        {
            error = QObject::tr("No unique reverse portal matches this opening. Check the chosen portal, or "
                                "explicitly disable paired editing for a one-way portal.");
            return false;
        }
        polygons << candidates.first();
        auto reverse = points;
        std::reverse(reverse.begin(), reverse.end());
        results << reverse;
    }
    QSet<int> guards = extraGuards;
    for (int i = 0; i < polygons.size(); ++i)
    {
        const auto &polygon = polygons[i];
        PortalChange change;
        change.block = model->getBlock(polygon.id);
        for (auto point : results[i])
        {
            auto local =
                polygon.world.rotation.inverted() * (point - polygon.world.translation) / polygon.world.scale;
            if (!finite(local))
            {
                error = QObject::tr("The proposed portal has invalid coordinates.");
                return false;
            }
            change.vertices << local;
        }
        plan.changes << change;
        for (int p = 0; p < results[i].size(); ++p)
            plan.preview << qMakePair(results[i][p], results[i][(p + 1) % results[i].size()]);
        guards.insert(polygon.source);
        guards.insert(polygon.destination);
        int id = polygon.id;
        QSet<int> seen;
        while (id >= 0 && !seen.contains(id))
        {
            seen.insert(id);
            guards.insert(id);
            auto parents = scene.parents.value(id);
            id = parents.isEmpty() ? -1 : parents.first();
        }
    }
    for (int id : guards)
    {
        QByteArray bytes;
        auto block = model->getBlock(id);
        if (!serialize(model, block, bytes))
        {
            error = QObject::tr("Cannot snapshot the portal edit for safe application.");
            return false;
        }
        plan.guards << qMakePair(QPersistentModelIndex(block), bytes);
    }
    plan.description =
        QObject::tr(
            "Preview: %1 portal(s), %2 vertices each. Room bounds and room/portal links stay unchanged.")
            .arg(plan.changes.size())
            .arg(points.size());
    output = plan;
    return true;
}
} // namespace
bool PortalEditing::fit(NifModel *model, int portal, int mesh, const QSet<int> &selected, bool paired,
                        PortalEditPlan &plan, QString &error)
{
    plan = PortalEditPlan();
    if (!model || selected.size() < 3)
    {
        error = QObject::tr(
            "Select at least three boundary vertices, or planar faces, around the desired opening.");
        return false;
    }
    RoomScene scene(model);
    Polygon opening;
    if (!polygon(model, scene, portal, opening, error))
        return false;
    VertexEditData geometry;
    auto block = model->getBlock(mesh);
    if (!geometry.read(model, block, &error))
        return false;
    Transform world;
    if (!scene.world(mesh, world, &error))
        return false;
    float lowU = 1e30f, lowV = 1e30f, highU = -1e30f, highV = -1e30f;
    float lowDistance = 1e30f, highDistance = -1e30f;
    QVector<Vector3> points;
    for (int index : selected)
    {
        if (index < 0 || index >= geometry.positions.size())
        {
            error = QObject::tr("The vertex selection is no longer valid.");
            return false;
        }
        auto p = world * geometry.positions[index];
        if (!finite(p))
        {
            error = QObject::tr("The selection contains invalid coordinates.");
            return false;
        }
        float distance = dot(p - opening.center, opening.normal);
        lowDistance = qMin(lowDistance, distance);
        highDistance = qMax(highDistance, distance);
        points << p;
        float u = dot(p - opening.center, opening.u), v = dot(p - opening.center, opening.v);
        lowU = qMin(lowU, u);
        highU = qMax(highU, u);
        lowV = qMin(lowV, v);
        highV = qMax(highV, v);
    }
    if (highDistance - lowDistance > opening.tolerance * 2)
    {
        error = QObject::tr("Select planar boundary vertices or faces parallel to the chosen portal. Fitting "
                            "projects them onto the existing room divider plane.");
        return false;
    }
    bool area = false;
    for (int i = 1; i < points.size() - 1; ++i)
        if (cross(points[i] - points[0], points[i + 1] - points[0]).length() > 1e-6f)
            area = true;
    if (!area || highU - lowU < 1e-4f || highV - lowV < 1e-4f)
    {
        error = QObject::tr("The selection must span an opening with nonzero width and height.");
        return false;
    }
    QVector<Vector3> rectangle;
    for (auto uv : QVector<Vector2>{Vector2(lowU, lowV), Vector2(highU, lowV), Vector2(highU, highV),
                                    Vector2(lowU, highV)})
        rectangle << opening.center + opening.u * uv[0] + opening.v * uv[1];
    QSet<int> guards{mesh};
    for (int child : model->getChildLinks(mesh))
        guards.insert(child);
    int ancestor = mesh;
    QSet<int> seen;
    while (ancestor >= 0 && !seen.contains(ancestor))
    {
        seen.insert(ancestor);
        guards.insert(ancestor);
        if (model->getLink(model->getBlock(ancestor), "Controller") >= 0)
        {
            error = QObject::tr("Animated source geometry cannot be fitted safely.");
            return false;
        }
        auto owner = scene.parents.value(ancestor);
        ancestor = owner.isEmpty() ? -1 : owner.first();
    }
    bool ok = finish(model, scene, opening, rectangle, paired, guards, plan, error);
    if (ok && std::abs((lowDistance + highDistance) * .5f) > opening.tolerance)
        plan.description +=
            QObject::tr(" Selection projected %1 world units onto the unchanged portal plane.")
                .arg(std::abs((lowDistance + highDistance) * .5f), 0, 'f', 3);
    return ok;
}
bool PortalEditing::adjust(NifModel *model, int portal, float moveU, float moveV, float scaleU, float scaleV,
                           bool paired, PortalEditPlan &plan, QString &error)
{
    plan = PortalEditPlan();
    if (!model || !std::isfinite(moveU) || !std::isfinite(moveV) || !std::isfinite(scaleU) ||
        !std::isfinite(scaleV) || scaleU <= 0 || scaleV <= 0)
    {
        error = QObject::tr("Enter finite offsets and positive scale factors.");
        return false;
    }
    RoomScene scene(model);
    Polygon opening;
    if (!polygon(model, scene, portal, opening, error))
        return false;
    QVector<Vector3> points;
    for (auto p : opening.points)
    {
        auto delta = p - opening.center;
        points << opening.center + opening.u * (dot(delta, opening.u) * scaleU + moveU) +
                      opening.v * (dot(delta, opening.v) * scaleV + moveV);
    }
    return finish(model, scene, opening, points, paired, {}, plan, error);
}
bool PortalEditing::apply(NifModel *model, const PortalEditPlan &plan, QString &error)
{
    if (!model || plan.model != model || plan.changes.isEmpty())
    {
        error = QObject::tr("Preview a portal change first.");
        return false;
    }
    for (auto guard : plan.guards)
    {
        QByteArray bytes;
        if (!serialize(model, guard.first, bytes) || bytes != guard.second)
        {
            error = QObject::tr("The map changed after this preview. Create a new preview before applying.");
            return false;
        }
    }
    for (auto change : plan.changes)
        if (!change.block.isValid() || model->getBlockName(change.block) != "NiPortal" ||
            change.vertices.size() < 3 || change.vertices.size() > 65535)
        {
            error = QObject::tr("The preview is no longer valid.");
            return false;
        }
    model->beginUndoTransaction(QObject::tr("Update portal opening"));
    bool held = model->holdUpdates(true);
    for (auto change : plan.changes)
    {
        model->set<quint16>(change.block, "Num Vertices", change.vertices.size());
        model->updateArray(change.block, "Vertices");
        model->setArray<Vector3>(change.block, "Vertices", change.vertices);
    }
    model->updateHeader();
    model->holdUpdates(held);
    for (auto change : plan.changes)
        emit model->dataChanged(change.block, change.block);
    model->endUndoTransaction();
    error.clear();
    return true;
}
