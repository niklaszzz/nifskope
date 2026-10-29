#include "vertexedit.h"
#include "nifmodel.h"
#include "uveditdata.h"
#include <QtEndian>
#include <QBuffer>
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
			if((!field.isValid() && model->get<uint>(stream,"Num Bytes")!=0) || bytes.size()%stride)
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
	if(positions.isEmpty() && !bounds.isValid()) return fail(QObject::tr("Select an editable static mesh in Object mode first."));
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
	if(current.positions.isEmpty()){model->set<Vector3>(current.bounds,"Center",Vector3());model->set<float>(current.bounds,"Radius",0);return;}
	Vector3 lo=current.positions.first(), hi=lo;
	for(auto p:current.positions) for(int a=0;a<3;++a) {lo[a]=qMin(lo[a],p[a]);hi[a]=qMax(hi[a],p[a]);}
	Vector3 center=(lo+hi)*.5f;
	float radius=0; for(auto p:current.positions) radius=qMax(radius,(p-center).length());
	model->set<Vector3>(current.bounds,"Center",center);
	model->set<float>(current.bounds,"Radius",radius);
}

bool FaceEditData::read(NifModel * model,const QModelIndex & block,QString * reason)
{
 triangles.clear();storage=QModelIndex();mesh=block;width=0;
 auto fail=[&](const QString & text){if(reason)*reason=text;triangles.clear();return false;};
 if(!model || block.model()!=model)return fail(QObject::tr("Select a mesh first."));
 if(model->inherits(block,"NiMesh")) {
  if(model->get<uint>(block,"Primitive Type")!=0 || model->get<uint>(block,"Num Submeshes")!=1)return fail(QObject::tr("Face editing supports one triangle-list submesh."));
  bool big=model->getIndex(model->getHeader(),"Endian Type").isValid() && model->get<quint8>(model->getHeader(),"Endian Type")==0;
  auto refs=model->getIndex(block,"Datastreams");
  for(int r=0;r<model->rowCount(refs);++r) {
   auto ref=refs.child(r,0),sem=model->getIndex(ref,"Component Semantics");
   if(model->get<QString>(sem.child(0,0),"Name")!="INDEX")continue;
   if(storage.isValid())return fail(QObject::tr("Multiple index streams are not supported."));
   auto stream=model->getBlock(model->getLink(ref,"Stream"));auto formats=model->getArray<uint>(stream,"Component Formats");
   if(formats.size()!=1 || (formats[0]!=NiMesh::F_UINT16_1 && formats[0]!=NiMesh::F_UINT32_1))return fail(QObject::tr("Unsupported index format."));
   width=formats[0]==NiMesh::F_UINT16_1?2:4;
   auto regions=model->getIndex(stream,"Regions");storage=model->getIndex(stream,"Data").child(0,0);auto bytes=model->get<QByteArray>(storage);
   if(!model->get<uint>(stream,"Num Bytes"))storage=model->getIndex(stream,"Data");
   if(model->get<bool>(ref,"Is Per Instance") || model->rowCount(regions)!=1 || model->get<uint>(regions.child(0,0),"Start Index")!=0 || model->get<uint>(regions.child(0,0),"Num Indices")!=uint(bytes.size()/width) || bytes.size()%(3*width))return fail(QObject::tr("Unsupported index regions."));
   for(int i=0;i<bytes.size();i+=3*width) {
    Triangle t;for(int a=0;a<3;++a){auto ptr=bytes.constData()+i+a*width;
     quint32 index=width==2?(big?qFromBigEndian<quint16>(ptr):qFromLittleEndian<quint16>(ptr)):(big?qFromBigEndian<quint32>(ptr):qFromLittleEndian<quint32>(ptr));
     if(index>65535)return fail(QObject::tr("Face editing supports vertex indices up to 65535."));t[a]=quint16(index);}
    triangles<<t;
   }
  }
 } else {
  auto data=model->getBlock(model->getLink(block,"Data"));
  if(!model->inherits(block,"NiTriShape") || !model->inherits(data,"NiTriShapeData"))return fail(QObject::tr("Face editing currently supports NiMesh triangle lists and NiTriShape meshes."));
  storage=model->getIndex(data,"Triangles");if(storage.isValid())triangles=model->getArray<Triangle>(storage);
 }
 if(!storage.isValid())return fail(QObject::tr("This mesh has no editable triangle list."));
 int source=model->getBlockNumber(storage);
 for(int b=0;b<model->getBlockCount();++b){auto other=model->getBlock(b);
  if(other!=block && (model->inherits(other,"NiMesh") || model->inherits(other,"NiTriBasedGeom")) && model->getChildLinks(b).contains(source))return fail(QObject::tr("Triangle data is shared. Duplicate the branch before editing."));}
 VertexEditData vertices;if(!vertices.read(model,block,reason))return false;
 for(auto t:triangles)for(int a=0;a<3;++a)if(t[a]>=vertices.positions.size())return fail(QObject::tr("Invalid triangle vertex index."));
 if(reason)reason->clear();return true;
}

bool FaceEditData::remove(NifModel * model,const QSet<int> & faces) const
{
 FaceEditData current;if(!current.read(model,mesh) || current.triangles!=triangles || faces.isEmpty())return false;
 for(int f:faces)if(f<0 || f>=triangles.size())return false;
 QVector<Triangle> kept;for(int i=0;i<triangles.size();++i)if(!faces.contains(i))kept<<triangles[i];
 return writeTriangles(model,kept);
}

bool FaceEditData::writeTriangles(NifModel * model,const QVector<Triangle> & kept) const
{
 if(!model || !mesh.isValid() || mesh.model()!=model || !storage.isValid())return false;
 VertexEditData vertices;if(!vertices.read(model,mesh) || vertices.isShared(model))return false;
 for(auto t:kept)for(int a=0;a<3;++a)if(t[a]>=vertices.positions.size())return false;
 int source=model->getBlockNumber(storage);
 for(int b=0;b<model->getBlockCount();++b)if(model->getBlock(b)!=mesh && model->inherits(model->getBlock(b),"NiMesh") && model->getChildLinks(b).contains(source))return false;
 auto data=model->getBlock(storage);
 if(width) {
  QByteArray bytes(kept.size()*3*width,0);
  bool big=model->getIndex(model->getHeader(),"Endian Type").isValid() && model->get<quint8>(model->getHeader(),"Endian Type")==0;
  for(int i=0;i<kept.size();++i)for(int a=0;a<3;++a){char * ptr=bytes.data()+(i*3+a)*width;
   if(width==2){if(big)qToBigEndian<quint16>(kept[i][a],ptr);else qToLittleEndian<quint16>(kept[i][a],ptr);}
   else {if(big)qToBigEndian<quint32>(kept[i][a],ptr);else qToLittleEndian<quint32>(kept[i][a],ptr);}}
  model->set<uint>(data,"Num Bytes",bytes.size());model->updateArray(data,"Data");
  if(!bytes.isEmpty())model->set<QByteArray>(model->getIndex(data,"Data").child(0,0),bytes);
  model->set<uint>(model->getIndex(data,"Regions").child(0,0),"Num Indices",kept.size()*3);
 } else {
  model->set<uint>(data,"Num Triangles",kept.size());
  if(model->getIndex(data,"Num Triangle Points").isValid())model->set<uint>(data,"Num Triangle Points",kept.size()*3);
  model->updateArray(data,"Triangles");model->setArray<Triangle>(data,"Triangles",kept);
 }
 model->updateHeader();emit model->dataChanged(data,data);emit model->dataChanged(mesh,mesh);return true;
}

bool makeMeshGeometryUnique(NifModel * model,const QModelIndex & mesh,QString * reason)
{
 VertexEditData vertices;if(!vertices.read(model,mesh,reason))return false;
 QVector<QPersistentModelIndex> links;
 if(model->inherits(mesh,"NiMesh")) {
  auto refs=model->getIndex(mesh,"Datastreams");
  for(int r=0;r<model->rowCount(refs);++r)links<<model->getIndex(refs.child(r,0),"Stream");
 } else if(model->inherits(mesh,"NiTriBasedGeom"))links<<model->getIndex(mesh,"Data");
 QSet<int> shared;
 for(auto link:links) {
  int id=model->getLink(link);
  for(int b=0;b<model->getBlockCount();++b) {
   auto other=model->getBlock(b);
   if(other!=mesh && (model->inherits(other,"NiMesh") || model->inherits(other,"NiTriBasedGeom")) && model->getChildLinks(b).contains(id)){shared.insert(id);break;}
  }
 }
 if(shared.isEmpty())return true;
 QMap<int,QByteArray> saved;
 for(int id:shared){QBuffer buffer(&saved[id]);buffer.open(QIODevice::WriteOnly);if(!model->saveIndex(buffer,model->getBlock(id)))return false;}
 int initialBlocks=model->getBlockCount();
 model->beginUndoTransaction(QObject::tr("Make mesh geometry unique"));
 bool held=model->holdUpdates(true);
 QMap<int,int> copies;
 for(int id:shared) {
  auto source=model->getBlock(id);auto copy=model->insertNiBlock(model->getBlockName(source));copies[id]=model->getBlockNumber(copy);
  QBuffer buffer(&saved[id]);buffer.open(QIODevice::ReadOnly);
  if(!model->loadIndex(buffer,copy)) {
   while(model->getBlockCount()>initialBlocks)model->removeNiBlock(model->getBlockCount()-1);
   model->holdUpdates(held);model->endUndoTransaction();return false;
  }
  if(model->inherits(source,"NiDataStream")) {
   model->set<uint>(copy,"Usage",model->get<uint>(source,"Usage"));
   model->set<uint>(copy,"Access",model->get<uint>(source,"Access"));
  }
 }
 for(auto link:links){int id=model->getLink(link);if(copies.contains(id))model->setLink(link,copies[id]);}
 model->updateHeader();model->holdUpdates(held);model->endUndoTransaction();
 return true;
}



bool deleteMeshVertices(NifModel * model,const QModelIndex & mesh,const QSet<int> & selected,QString * reason)
{
 auto fail=[&](const QString & text){if(reason)*reason=text;return false;};
 VertexEditData vertices;FaceEditData faces;
 if(!vertices.read(model,mesh,reason) || !faces.read(model,mesh,reason))return false;
 if(vertices.isShared(model))return fail(QObject::tr("Make the mesh geometry unique before deleting vertices."));
 if(selected.isEmpty())return false;
 int count=vertices.positions.size();QVector<int> remap(count,-1);int remaining=0;
 for(int i:selected)if(i<0 || i>=count)return fail(QObject::tr("Invalid vertex selection."));
 for(int i=0;i<count;++i)if(!selected.contains(i))remap[i]=remaining++;
 QVector<Triangle> triangles;
 for(auto t:faces.triangles)if(remap[t[0]]>=0 && remap[t[1]]>=0 && remap[t[2]]>=0)
  triangles<<Triangle(remap[t[0]],remap[t[1]],remap[t[2]]);
 struct Stream {QPersistentModelIndex block;QByteArray bytes;int stride;};
 QVector<Stream> streams;
 QMap<QPersistentModelIndex,QVector<NifValue>> arrays;
 auto data=model->getBlock(model->getLink(mesh,"Data"));
 auto capture=[&](QModelIndex array){
  if(!array.isValid() || !model->rowCount(array))return true;
  if(model->rowCount(array)!=count)return false;
  QVector<NifValue> values;
  for(int i=0;i<count;++i) {
   if(model->rowCount(array.child(i,0)))return false;
   if(remap[i]>=0)values<<model->getValue(array.child(i,0));
  }
  arrays.insert(array,values);return true;
 };
 if(model->inherits(mesh,"NiMesh")) {
  auto refs=model->getIndex(mesh,"Datastreams");QSet<int> visited;
  for(int r=0;r<model->rowCount(refs);++r) {
   auto ref=refs.child(r,0),stream=model->getBlock(model->getLink(ref,"Stream"));
   auto sem=model->getIndex(ref,"Component Semantics");bool index=false;
   for(int a=0;a<model->rowCount(sem);++a)if(model->get<QString>(sem.child(a,0),"Name")=="INDEX")index=true;
   if(index)continue;
   int id=model->getBlockNumber(stream);if(visited.contains(id))continue;visited.insert(id);
   if(model->get<bool>(ref,"Is Per Instance"))return fail(QObject::tr("Cannot delete vertices from an instanced stream layout."));
   int stride=0;for(uint format:model->getArray<uint>(stream,"Component Formats"))stride+=((format>>16)&15)*((format>>8)&15);
   auto bytes=model->get<QByteArray>(model->getIndex(stream,"Data").child(0,0));auto regions=model->getIndex(stream,"Regions");
   if(stride<=0 || qint64(count)*stride!=bytes.size() || model->rowCount(regions)!=1 || model->get<uint>(regions.child(0,0),"Start Index")!=0 || model->get<uint>(regions.child(0,0),"Num Indices")!=uint(count))return fail(QObject::tr("Vertex deletion requires matching, contiguous vertex streams."));
   for(int b=0;b<model->getBlockCount();++b)if(model->getBlock(b)!=mesh && model->inherits(model->getBlock(b),"NiMesh") && model->getChildLinks(b).contains(id))return fail(QObject::tr("A vertex stream is still shared with another mesh."));
   QByteArray kept;kept.reserve(remaining*stride);for(int i=0;i<count;++i)if(remap[i]>=0)kept.append(bytes.constData()+i*stride,stride);
   streams<<Stream{stream,kept,stride};
  }
 } else {
  if(model->getLink(data,"Additional Data")>=0)return fail(QObject::tr("Vertex deletion does not support additional geometry data."));
  for(const char * name:{"Vertices","Normals","Tangents","Bitangents","Vertex Colors"})if(!capture(model->getIndex(data,name)))return fail(QObject::tr("A vertex attribute array has an unsupported layout."));
  auto uvs=model->getIndex(data,"UV Sets");for(int set=0;set<model->rowCount(uvs);++set)if(!capture(uvs.child(set,0)))return fail(QObject::tr("A UV array has an unsupported layout."));
 }
 model->beginUndoTransaction(QObject::tr("Delete vertices"));
 for(const auto & stream:streams) {
  model->set<uint>(stream.block,"Num Bytes",stream.bytes.size());model->updateArray(stream.block,"Data");
  if(!stream.bytes.isEmpty())model->set<QByteArray>(model->getIndex(stream.block,"Data").child(0,0),stream.bytes);
  model->set<uint>(model->getIndex(stream.block,"Regions").child(0,0),"Num Indices",remaining);
 }
 if(data.isValid()) {
  auto groups=model->getIndex(data,"Match Groups");
  for(int g=0;g<model->rowCount(groups);++g){auto group=groups.child(g,0);QVector<quint16> kept;
   for(auto i:model->getArray<quint16>(group,"Vertex Indices"))if(i<remap.size() && remap[i]>=0)kept<<quint16(remap[i]);
   model->set<quint16>(group,"Num Vertices",kept.size());model->updateArray(group,"Vertex Indices");model->setArray<quint16>(group,"Vertex Indices",kept);
  }
  model->set<quint16>(data,"Num Vertices",remaining);
  for(auto it=arrays.cbegin();it!=arrays.cend();++it){model->updateArray(it.key());for(int r=0;r<it.value().size();++r)model->setValue(it.key().child(r,0),it.value()[r]);}
 }
 bool ok=faces.writeTriangles(model,triangles);
 vertices.updateBounds(model);model->updateHeader();
 for(auto stream:streams)emit model->dataChanged(stream.block,stream.block);
 if(data.isValid())emit model->dataChanged(data,data);
 emit model->dataChanged(mesh,mesh);model->endUndoTransaction();
 if(reason)reason->clear();return ok;
}

bool cubeUnwrapMesh(NifModel * model,const QModelIndex & mesh,int coordinateSet,const QSet<int> & selectedFaces,QString * reason)
{
 auto fail=[&](const QString & text){if(reason)*reason=text;return false;};
 VertexEditData vertices;FaceEditData faces;
 if(!vertices.read(model,mesh,reason) || !faces.read(model,mesh,reason))return false;
 if(vertices.isShared(model))return fail(QObject::tr("Enter Vertex or Face mode to make shared geometry independent first."));
 const int count=vertices.positions.size();if(!count || faces.triangles.isEmpty())return fail(QObject::tr("Select a mesh with triangles."));
 auto data=model->getBlock(model->getLink(mesh,"Data"));QVector<Vector2> originalUV;
 if(model->inherits(mesh,"NiMesh")){UVEditData uv;if(!uv.read(model,mesh,coordinateSet,reason))return false;originalUV=uv.coordinates;}
 else originalUV=model->getArray<Vector2>(model->getIndex(data,"UV Sets").child(coordinateSet,0));
 if(originalUV.size()!=count)return fail(QObject::tr("The active UV channel must match the vertex count."));
 for(int f:selectedFaces)if(f<0 || f>=faces.triangles.size())return fail(QObject::tr("Invalid face selection."));
 QSet<int> targets=selectedFaces;if(targets.isEmpty())for(int i=0;i<faces.triangles.size();++i)targets.insert(i);
 Vector3 low,high;bool first=true;for(int f:targets)for(int a=0;a<3;++a){auto p=vertices.positions[faces.triangles[f][a]];if(first){low=high=p;first=false;}for(int axis=0;axis<3;++axis){low[axis]=qMin(low[axis],p[axis]);high[axis]=qMax(high[axis],p[axis]);}}
 float extent=qMax(high[0]-low[0],qMax(high[1]-low[1],high[2]-low[2]));if(extent<1e-8f)return fail(QObject::tr("The selection has no surface area to unwrap."));auto center=(low+high)*.5f;
 QVector<int> sources;QVector<Vector2> coordinates;QVector<Triangle> triangles;QHash<quint64,int> indices;
 for(int f=0;f<faces.triangles.size();++f){auto t=faces.triangles[f];int side=6;
  if(targets.contains(f)){auto a=vertices.positions[t[1]]-vertices.positions[t[0]],b=vertices.positions[t[2]]-vertices.positions[t[0]];Vector3 n(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]);int axis=0;for(int i=1;i<3;++i)if(std::abs(n[i])>std::abs(n[axis]))axis=i;side=axis*2+(n[axis]<0?1:0);}
  Triangle mapped;for(int a=0;a<3;++a){int old=t[a];quint64 key=quint64(old)*7+side;if(!indices.contains(key)){int next=sources.size();if(next>=65535)return fail(QObject::tr("Cube seams would exceed the 65535 vertex limit."));indices.insert(key,next);sources<<old;Vector2 uv=originalUV[old];
    if(side<6){auto p=(vertices.positions[old]-center)/extent;float u=0,v=0;switch(side){case 0:u=-p[2];v=p[1];break;case 1:u=p[2];v=p[1];break;case 2:u=p[0];v=-p[2];break;case 3:u=p[0];v=p[2];break;case 4:u=p[0];v=p[1];break;case 5:u=-p[0];v=p[1];break;}uv=Vector2((side%3+.5f+u*.94f)/3.f,(side/3+.5f+v*.94f)/3.f+1.f/6.f);}
    coordinates<<uv;}mapped[a]=quint16(indices.value(key));}triangles<<mapped;
 }
 struct Stream {QPersistentModelIndex block;QByteArray bytes;};QVector<Stream> streams;QMap<QPersistentModelIndex,QVector<NifValue>> arrays;
 auto capture=[&](QModelIndex array){if(!array.isValid() || !model->rowCount(array))return true;if(model->rowCount(array)!=count)return false;QVector<NifValue> values;for(int source:sources){if(model->rowCount(array.child(source,0)))return false;values<<model->getValue(array.child(source,0));}arrays.insert(array,values);return true;};
 if(model->inherits(mesh,"NiMesh")){auto refs=model->getIndex(mesh,"Datastreams");QSet<int> visited;for(int r=0;r<model->rowCount(refs);++r){auto ref=refs.child(r,0),stream=model->getBlock(model->getLink(ref,"Stream")),sem=model->getIndex(ref,"Component Semantics");bool index=false;for(int i=0;i<model->rowCount(sem);++i)if(model->get<QString>(sem.child(i,0),"Name")=="INDEX")index=true;if(index)continue;int id=model->getBlockNumber(stream);if(visited.contains(id))continue;visited.insert(id);
   if(model->get<bool>(ref,"Is Per Instance"))return fail(QObject::tr("Cube unwrap does not support instanced streams."));int stride=0;for(uint format:model->getArray<uint>(stream,"Component Formats"))stride+=((format>>16)&15)*((format>>8)&15);auto bytes=model->get<QByteArray>(model->getIndex(stream,"Data").child(0,0));auto regions=model->getIndex(stream,"Regions");
   if(stride<=0 || qint64(count)*stride!=bytes.size() || model->rowCount(regions)!=1 || model->get<uint>(regions.child(0,0),"Start Index")!=0 || model->get<uint>(regions.child(0,0),"Num Indices")!=uint(count))return fail(QObject::tr("Cube unwrap requires matching contiguous vertex streams."));
   for(int b=0;b<model->getBlockCount();++b)if(model->getBlock(b)!=mesh && model->inherits(model->getBlock(b),"NiMesh") && model->getChildLinks(b).contains(id))return fail(QObject::tr("A vertex stream is shared; enter edit mode first."));
   QByteArray copied;copied.reserve(sources.size()*stride);for(int source:sources)copied.append(bytes.constData()+source*stride,stride);streams<<Stream{stream,copied};
  }}else{
  if(model->getLink(data,"Additional Data")>=0 || model->rowCount(model->getIndex(data,"Match Groups")))return fail(QObject::tr("Cube unwrap does not support additional geometry data or match groups."));
  for(const char * field:{"Vertices","Normals","Tangents","Bitangents","Vertex Colors"})if(!capture(model->getIndex(data,field)))return fail(QObject::tr("Unsupported vertex attributes."));auto sets=model->getIndex(data,"UV Sets");for(int i=0;i<model->rowCount(sets);++i)if(!capture(sets.child(i,0)))return fail(QObject::tr("Unsupported UV array."));
 }
 model->beginUndoTransaction(QObject::tr("Cube unwrap"));bool held=model->holdUpdates(true);
 for(auto stream:streams){model->set<uint>(stream.block,"Num Bytes",stream.bytes.size());model->updateArray(stream.block,"Data");model->set<QByteArray>(model->getIndex(stream.block,"Data").child(0,0),stream.bytes);model->set<uint>(model->getIndex(stream.block,"Regions").child(0,0),"Num Indices",sources.size());}
 if(data.isValid()){model->set<quint16>(data,"Num Vertices",sources.size());for(auto it=arrays.cbegin();it!=arrays.cend();++it){model->updateArray(it.key());for(int i=0;i<it.value().size();++i)model->setValue(it.key().child(i,0),it.value()[i]);}}
 bool ok=faces.writeTriangles(model,triangles);
 if(model->inherits(mesh,"NiMesh")){UVEditData uv;ok=ok && uv.read(model,mesh,coordinateSet,reason) && uv.write(model,coordinates);}else if(ok)model->setArray<Vector2>(model->getIndex(data,"UV Sets").child(coordinateSet,0),coordinates);
 vertices.updateBounds(model);model->updateHeader();model->holdUpdates(held);for(auto stream:streams)emit model->dataChanged(stream.block,stream.block);if(data.isValid())emit model->dataChanged(data,data);emit model->dataChanged(mesh,mesh);model->endUndoTransaction();if(ok && reason)reason->clear();return ok;
}