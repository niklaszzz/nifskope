#ifndef PHYSXCOLLISION_H
#define PHYSXCOLLISION_H
#include "data/niftypes.h"
#include <QMatrix4x4>
#include <QPersistentModelIndex>
class NifModel;

namespace PhysXCollision {
struct Mesh {
	QPersistentModelIndex source, data;
	QVector<QPersistentModelIndex> sourceMeshes;
	QVector<QMatrix4x4> sourceWorlds;
	QString name;
	QMatrix4x4 world;
	QVector<Vector3> vertices;
	QVector<quint32> indices;
};
bool hasCollision(const NifModel * model);
// Only the supported little-endian PhysX 2.8 triangle format is interpreted.
bool decode(const QByteArray & bytes, QVector<Vector3> & vertices, QVector<quint32> & indices);
bool collect(NifModel * model, QVector<Mesh> & meshes, QString & error, bool requireSources = true);
// Runs cooking out of process; applies all results as one undo step only on success.
bool rebuild(NifModel * model, const QString & cookingDll, const QString & helper, QString & error, int * count = nullptr);
}
#endif
