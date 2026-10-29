#ifndef VERTEXEDIT_H
#define VERTEXEDIT_H

#include "data/niftypes.h"
#include <QPersistentModelIndex>
#include <QSet>

class NifModel;

// Detach shared geometry before editing a duplicated mesh. Owns its undo step.
bool makeMeshGeometryUnique(NifModel * model,const QModelIndex & mesh,QString * reason=nullptr);
bool deleteMeshVertices(NifModel * model,const QModelIndex & mesh,const QSet<int> & selected,QString * reason=nullptr);
bool cubeUnwrapMesh(NifModel * model,const QModelIndex & mesh,int coordinateSet,const QSet<int> & selectedFaces,QString * reason=nullptr);

// Positions and their original storage locations. Editing never rebuilds the
// vertex buffer, so interleaved UVs, colors, normals and padding stay intact.
class VertexEditData {
public:
	bool read( NifModel * model, const QModelIndex & mesh, QString * reason = nullptr );
	bool write( NifModel * model, const QVector<Vector3> & values, const QSet<int> & selected ) const;
	void updateBounds( NifModel * model ) const;
	bool isShared( const NifModel * model ) const;
	QVector<Vector3> positions;
	QPersistentModelIndex block;
private:
	struct Location {
		QPersistentModelIndex field;
		int offset = -1;
		bool half = false;
	};
	QVector<Location> locations;
	QPersistentModelIndex bounds;
	bool bigEndian = false;
};
class FaceEditData {
public:
	bool read(NifModel * model,const QModelIndex & mesh,QString * reason=nullptr);
	bool remove(NifModel * model,const QSet<int> & faces) const;
	bool writeTriangles(NifModel * model,const QVector<Triangle> & values) const;
	QVector<Triangle> triangles;
private:
	QPersistentModelIndex mesh, storage;
	int width=0;
};
#endif
