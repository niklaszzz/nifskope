#ifndef PORTALEDIT_H
#define PORTALEDIT_H
#include "data/niftypes.h"
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSet>
class NifModel;
struct PortalChange
{
    QPersistentModelIndex block;
    QVector<Vector3> vertices;
};
struct PortalEditPlan
{
    QPointer<NifModel> model;
    QVector<PortalChange> changes;
    QVector<QPair<QPersistentModelIndex, QByteArray>> guards;
    QVector<QPair<Vector3, Vector3>> preview;
    QString description;
};
namespace PortalEditing
{
bool fit(NifModel *model, int portal, int mesh, const QSet<int> &vertices, bool paired, PortalEditPlan &plan,
         QString &error);
bool adjust(NifModel *model, int portal, float moveU, float moveV, float scaleU, float scaleV, bool paired,
            PortalEditPlan &plan, QString &error);
bool apply(NifModel *model, const PortalEditPlan &plan, QString &error);
} // namespace PortalEditing
#endif
