#include "vertexedit.h"
#include "nifmodel.h"
#include <QtEndian>
#include <qfloat16.h>
#include <cstring>
#include <cmath>
#include <limits>

namespace {
float readComponent( const char * p, bool half, bool big )
{
	if (half) {
		quint16 bits = big ? qFromBigEndian<quint16>(p) : qFromLittleEndian<quint16>(p);
		qfloat16 value; std::memcpy(&value,&bits,2); return float(value);
	}
	quint32 bits = big ? qFromBigEndian<quint32>(p) : qFromLittleEndian<quint32>(p);
	float value; std::memcpy(&value,&bits,4); return value;
}
void writeComponent( char * p, float value, bool half, bool big )
{
	if (half) {
		qfloat16 h(value); quint16 bits; std::memcpy(&bits,&h,2);
		if (big) qToBigEndian(bits,p); else qToLittleEndian(bits,p);
	} else {
		quint32 bits; std::memcpy(&bits,&value,4);
		if (big) qToBigEndian(bits,p); else qToLittleEndian(bits,p);
	}
}
}

bool VertexEditData::read( NifModel * model, const QModelIndex & mesh, QString * reason )
{
	positions.clear(); locations.clear(); bounds = QModelIndex(); block = mesh;
	auto fail = [&](const QString & text) { if (reason) *reason=text; positions.clear(); locations.clear(); return false; };
	if (!model || !mesh.isValid() || mesh.model()!=model) return fail(QObject::tr("Select a mesh in Object mode first."));
	if (model->getLink(mesh,"Skin Instance") >= 0 || model->getLink(mesh,"Skin") >= 0)
		return fail(QObject::tr("Vertex editing currently supports unskinned meshes."));
	QSet<int> controllers;
	int controller=model->getLink(mesh,"Controller");
	while(controller>=0 && !controllers.contains(controller)) {
		controllers.insert(controller);
		auto item=model->getBlock(controller);
		if(model->inherits(item,"NiGeomMorpherController")) return fail(QObject::tr("Morph-deformed meshes are not editable yet."));
		controller=model->getLink(item,"Next Controller");
	}
	bigEndian = model->getIndex(model->getHeader(),"Endian Type").isValid()
		&& model->get<quint8>(model->getHeader(),"Endian Type") == 0;
	if (model->inherits(mesh,"NiMesh")) {
		if (model->get<uint>(mesh,"Num Modifiers") != 0)
			return fail(QObject::tr("Vertex editing currently supports NiMesh objects without deformation modifiers."));
		auto refs=model->getIndex(mesh,"Datastreams");
		bool found=false;
		for (int s=0;s<model->rowCount(refs);++s) {
			auto ref=refs.child(s,0);
			auto stream=model->getBlock(model->getLink(ref,"Stream"));
			auto semantics=model->getIndex(ref,"Component Semantics");
			auto formats=model->getArray<uint>(stream,"Component Formats");
			quint64 stride=0, offset=0; int component=-1;
			for(int c=0;c<formats.size();++c) {
				auto name=model->get<QString>(semantics.child(c,0),"Name");
				if (name=="POSITION" || name=="POSITION_BP") {
					if(component>=0) return fail(QObject::tr("This mesh has multiple position components."));
					component=c; offset=stride;
				}
				stride+=((formats[c]>>16)&15)*((formats[c]>>8)&15);
			}
			if(component<0) continue;
			if(found || model->get<bool>(ref,"Is Per Instance") || !stride)
				return fail(QObject::tr("This mesh has an unsupported position stream layout."));
			found=true;
			auto format=formats[component];
			if(format!=NiMesh::F_FLOAT32_3 && format!=NiMesh::F_FLOAT16_3)
				return fail(QObject::tr("This position format is not editable yet."));
			bool half=format==NiMesh::F_FLOAT16_3;
			auto field=model->getIndex(stream,"Data").child(0,0);
			auto bytes=model->get<QByteArray>(field);
			if(!field.isValid() || bytes.size()%stride)
				return fail(QObject::tr("The position buffer is incomplete."));
			quint64 count=bytes.size()/stride;
			// Match the renderer's vertex addresses, including region offsets.
			auto regions=model->getIndex(stream,"Regions");
			if(model->rowCount(regions)!=1 || model->get<uint>(regions.child(0,0),"Start Index")!=0
				|| model->get<uint>(regions.child(0,0),"Num Indices")!=count)
				return fail(QObject::tr("Vertex editing currently supports a single contiguous vertex region."));
			for(quint64 i=0;i<count;++i) {
				Location location; location.field=field; location.offset=int(i*stride+offset); location.half=half;
				Vector3 v;
				for(int a=0;a<3;++a) v[a]=readComponent(bytes.constData()+location.offset+a*(half?2:4),half,bigEndian);
				positions << v; locations << location;
			}
		}
		bounds=model->getIndex(mesh,"Bound");
	} else {
		QModelIndex vertices;
		if(model->inherits(mesh,"NiTriBasedGeom")) {
			auto data=model->getBlock(model->getLink(mesh,"Data"));
			vertices=model->getIndex(data,"Vertices"); bounds=data;
		} else if(model->inherits(mesh,"BSTriShape")) {
			vertices=model->getIndex(mesh,model->inherits(mesh,"BSDynamicTriShape")?"Vertices":"Vertex Data");
			bounds=model->getIndex(mesh,"Bounding Sphere");
		}
		if(!vertices.isValid()) return fail(QObject::tr("Select an editable static mesh in Object mode first."));
		for(int i=0;i<model->rowCount(vertices);++i) {
			auto field=vertices.child(i,0);
			if(model->rowCount(field)>0) field=model->getIndex(field,"Vertex");
			auto value=model->getValue(field);
			Vector3 v;
			if(value.type()==NifValue::tVector4) {
				auto v4=value.get<Vector4>(); v=Vector3(v4[0],v4[1],v4[2]);
			} else if(value.type()==NifValue::tVector3 || value.type()==NifValue::tHalfVector3) v=value.get<Vector3>();
			else return fail(QObject::tr("This vertex storage is not editable yet."));
			Location location; location.field=field; location.half=value.type()==NifValue::tHalfVector3; positions<<v; locations<<location;
		}
	}
	if(positions.isEmpty()) return fail(QObject::tr("Select an editable static mesh in Object mode first."));
	if(reason) reason->clear();
	return true;
}

bool VertexEditData::write( NifModel * model, const QVector<Vector3> & values, const QSet<int> & selected ) const
{
	if(!model || !block.isValid() || block.model()!=model || values.size()!=locations.size() || isShared(model)) return false;
	QMap<QPersistentModelIndex,QByteArray> buffers;
	// Validate every destination before modifying any data.
	for(int i:selected) {
		if(i<0 || i>=locations.size() || !locations[i].field.isValid()) return false;
		for(int a=0;a<3;++a) if(!std::isfinite(values[i][a]) || (locations[i].half && std::abs(values[i][a])>65504)) return false;
		const auto & location=locations[i];
		if(location.offset<0) continue;
		if(!buffers.contains(location.field)) buffers.insert(location.field,model->get<QByteArray>(location.field));
		auto & buffer=buffers[location.field];
		if(location.offset+3*(location.half?2:4)>buffer.size()) return false;
		for(int a=0;a<3;++a) writeComponent(buffer.data()+location.offset+a*(location.half?2:4),values[i][a],location.half,bigEndian);
	}
	QSet<QPersistentModelIndex> changedBlocks;
	model->setState(BaseModel::Processing);
	for(int i:selected) {
		const auto & location=locations[i];
		changedBlocks.insert(model->getBlock(location.field));
		if(location.offset>=0) continue;
		auto value=model->getValue(location.field);
		if(value.type()==NifValue::tVector4) {
			auto v=value.get<Vector4>(); for(int a=0;a<3;++a) v[a]=values[i][a]; value.set<Vector4>(v);
		} else value.set<Vector3>(values[i]);
		model->setData(location.field.sibling(location.field.row(),NifModel::ValueCol),value.toVariant(),Qt::EditRole);
	}
	for(auto it=buffers.cbegin();it!=buffers.cend();++it) model->set<QByteArray>(it.key(),it.value());
	model->restoreState();
	// Refresh storage blocks and the owning mesh once, rather than updating the
	// entire scene separately for every vertex in a large selection.
	changedBlocks.insert(block);
	for(const auto & changed:changedBlocks) if(changed.isValid()) emit model->dataChanged(changed,changed);
	return true;
}

bool VertexEditData::isShared( const NifModel * model ) const
{
	if(!model || !block.isValid() || block.model()!=model) return true;
	QSet<int> sources;
	for(const auto & location:locations) {
		int source=model->getBlockNumber(location.field);
		if(source>=0 && model->getBlock(source)!=block) sources.insert(source);
	}
	if(sources.isEmpty()) return false;
	for(int b=0;b<model->getBlockCount();++b) {
		auto candidate=model->getBlock(b);
		if(candidate==block) continue;
		auto links=model->getChildLinks(b);
		bool referencesSource=false;
		for(int source:sources) if(links.contains(source)) {referencesSource=true;break;}
		if(referencesSource && (model->inherits(candidate,"NiMesh") || model->inherits(candidate,"NiTriBasedGeom")
			|| model->inherits(candidate,"BSTriShape"))) return true;
	}
	return false;
}

void VertexEditData::updateBounds( NifModel * model ) const
{
	VertexEditData current;
	if(!current.read(model,block) || !current.bounds.isValid()) return;
	Vector3 lo=current.positions.first(), hi=lo;
	for(auto p:current.positions) for(int a=0;a<3;++a) {lo[a]=qMin(lo[a],p[a]);hi[a]=qMax(hi[a],p[a]);}
	Vector3 center=(lo+hi)*.5f;
	float radius=0; for(auto p:current.positions) radius=qMax(radius,(p-center).length());
	model->set<Vector3>(current.bounds,"Center",center);
	model->set<float>(current.bounds,"Radius",radius);
}
