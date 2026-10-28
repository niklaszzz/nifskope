#ifndef VERTEXEDIT_H
#define VERTEXEDIT_H

#include "data/niftypes.h"
#include <QPersistentModelIndex>
#include <QSet>

class NifModel;

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
#endif
