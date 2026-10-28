#ifndef UVEDITDATA_H
#define UVEDITDATA_H
#include "data/niftypes.h"
#include <QPersistentModelIndex>
class NifModel;

// Editable NiMesh UV components, without rebuilding their interleaved buffers.
class UVEditData {
public:
	static QVector<int> coordinateSets(const NifModel * model,const QModelIndex & mesh);
	bool read(const NifModel * model,const QModelIndex & mesh,int coordinateSet,QString * error=nullptr);
	bool write(NifModel * model,const QVector<Vector2> & coordinates) const;
	QVector<Vector2> coordinates;
	QVector<quint32> indices;
	QPersistentModelIndex mesh,storage;
	QString reason;
private:
	int stride=0,offset=0;
	bool half=false,big=false;
};
#endif
