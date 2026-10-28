#include "uveditdata.h"
#include "nifmodel.h"
#include <QtEndian>
#include <qfloat16.h>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {
float component(const char * p,bool half,bool big) {
	if(half){quint16 bits=big?qFromBigEndian<quint16>(p):qFromLittleEndian<quint16>(p);qfloat16 f;std::memcpy(&f,&bits,2);return float(f);}
	quint32 bits=big?qFromBigEndian<quint32>(p):qFromLittleEndian<quint32>(p);float f;std::memcpy(&f,&bits,4);return f;
}
void put(char * p,float f,bool half,bool big) {
	if(half){qfloat16 h(f);quint16 bits;std::memcpy(&bits,&h,2);if(big)qToBigEndian(bits,p);else qToLittleEndian(bits,p);}
	else {quint32 bits;std::memcpy(&bits,&f,4);if(big)qToBigEndian(bits,p);else qToLittleEndian(bits,p);}
}
bool region(const NifModel * model,const QModelIndex & stream,int count) {
	auto regions=model->getIndex(stream,"Regions");
	return model->rowCount(regions)==1 && model->get<uint>(regions.child(0,0),"Start Index")==0
		&& model->get<uint>(regions.child(0,0),"Num Indices")==uint(count);
}
}

QVector<int> UVEditData::coordinateSets(const NifModel * model,const QModelIndex & mesh) {
	QVector<int> sets;if(!model || !model->inherits(mesh,"NiMesh"))return sets;
	auto refs=model->getIndex(mesh,"Datastreams");
	for(int s=0;s<model->rowCount(refs);++s) {
		auto semantics=model->getIndex(refs.child(s,0),"Component Semantics");
		for(int c=0;c<model->rowCount(semantics);++c) {
			auto sem=semantics.child(c,0);
			if(model->get<QString>(sem,"Name")=="TEXCOORD") {int set=model->get<uint>(sem,"Index");if(!sets.contains(set))sets<<set;}
		}
	}
	std::sort(sets.begin(),sets.end());return sets;
}

bool UVEditData::read(const NifModel * model,const QModelIndex & block,int coordinateSet,QString * error) {
	coordinates.clear();indices.clear();storage=QModelIndex();mesh=block;reason.clear();stride=offset=0;
	auto fail=[&](const QString & text){reason=text;if(error)*error=text;coordinates.clear();indices.clear();return false;};
	if(!model || block.model()!=model || !model->inherits(block,"NiMesh"))return fail(QObject::tr("Select a NiMesh object."));
	if(model->get<uint>(block,"Primitive Type")!=0 || model->get<uint>(block,"Num Submeshes")!=1)
		return fail(QObject::tr("NiMesh UV editing currently supports one triangle-list submesh."));
	big=model->getIndex(model->getHeader(),"Endian Type").isValid() && model->get<quint8>(model->getHeader(),"Endian Type")==0;
	auto refs=model->getIndex(block,"Datastreams");
	for(int s=0;s<model->rowCount(refs);++s) {
		auto ref=refs.child(s,0),stream=model->getBlock(model->getLink(ref,"Stream"));
		auto semantics=model->getIndex(ref,"Component Semantics");auto formats=model->getArray<uint>(stream,"Component Formats");
		quint64 step=0,uvOffset=0;int uv=-1;bool index=false;
		for(int c=0;c<formats.size();++c) {
			auto sem=semantics.child(c,0);auto name=model->get<QString>(sem,"Name");
			if(name=="TEXCOORD" && model->get<uint>(sem,"Index")==uint(coordinateSet)){if(uv>=0)return fail(QObject::tr("Duplicate UV components."));uv=c;uvOffset=step;}
			if(name=="INDEX")index=true;
			step+=((formats[c]>>16)&15)*((formats[c]>>8)&15);
		}
		if(uv<0 && !index)continue;
		if(model->get<bool>(ref,"Is Per Instance") || !step || step>1048576)return fail(QObject::tr("Unsupported UV stream layout."));
		auto field=model->getIndex(stream,"Data").child(0,0);auto bytes=model->get<QByteArray>(field);
		if(bytes.isEmpty() || quint64(bytes.size())%step || !region(model,stream,bytes.size()/step))return fail(QObject::tr("UV editing requires one complete, contiguous stream region."));
		if(uv>=0) {
			if(storage.isValid() || (formats[uv]!=NiMesh::F_FLOAT32_2 && formats[uv]!=NiMesh::F_FLOAT16_2))return fail(QObject::tr("Unsupported or duplicate UV coordinate format."));
			storage=field;stride=int(step);offset=int(uvOffset);half=formats[uv]==NiMesh::F_FLOAT16_2;
			if(offset+2*(half?2:4)>stride)return fail(QObject::tr("Incomplete UV component."));
			for(int p=0;p<bytes.size();p+=stride) {
				Vector2 v(component(bytes.constData()+p+offset,half,big),component(bytes.constData()+p+offset+(half?2:4),half,big));
				if(!std::isfinite(v[0]) || !std::isfinite(v[1]))return fail(QObject::tr("The mesh contains invalid UV coordinates."));coordinates<<v;
			}
		}
		if(index) {
			if(!indices.isEmpty() || formats.size()!=1 || (formats[0]!=NiMesh::F_UINT16_1 && formats[0]!=NiMesh::F_UINT32_1))return fail(QObject::tr("Unsupported UV index stream."));
			int width=formats[0]==NiMesh::F_UINT16_1?2:4;
			if(bytes.size()%(3*width))return fail(QObject::tr("Incomplete UV triangles."));
			for(int p=0;p<bytes.size();p+=width){const char * ptr=bytes.constData()+p;indices<< (width==2?(big?qFromBigEndian<quint16>(ptr):qFromLittleEndian<quint16>(ptr)):(big?qFromBigEndian<quint32>(ptr):qFromLittleEndian<quint32>(ptr)));}
		}
	}
	if(coordinates.isEmpty() || indices.isEmpty())return fail(QObject::tr("This mesh has no editable UV set %1.").arg(coordinateSet));
	for(auto i:indices)if(i>=uint(coordinates.size()))return fail(QObject::tr("A triangle references a missing UV coordinate."));
	// Shared storage would silently change another object's mapping.
	int id=model->getBlockNumber(storage);
	for(int b=0;b<model->getBlockCount();++b) {
		auto other=model->getBlock(b);if(other!=block && model->inherits(other,"NiMesh") && model->getChildLinks(b).contains(id))
			return fail(QObject::tr("This UV buffer is shared with another mesh. Shared-buffer UV editing is not supported yet."));
	}
	if(error)error->clear();return true;
}

bool UVEditData::write(NifModel * model,const QVector<Vector2> & values) const {
	if(!model || mesh.model()!=model || !mesh.isValid() || !storage.isValid() || values.size()!=coordinates.size() || !stride)return false;
	auto bytes=model->get<QByteArray>(storage);if(qint64(values.size())*stride!=bytes.size())return false;
	for(auto v:values)for(int a=0;a<2;++a)if(!std::isfinite(v[a]) || (half && std::abs(v[a])>65504))return false;
	for(int i=0;i<values.size();++i)for(int a=0;a<2;++a)put(bytes.data()+i*stride+offset+a*(half?2:4),values[i][a],half,big);
	return model->set<QByteArray>(storage,bytes);
}
