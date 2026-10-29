#include "physxcollision.h"
#include "model/nifmodel.h"
#include "model/vertexedit.h"
#include <QDataStream>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <functional>

namespace {
quint32 word(const QByteArray & b,int p) { return qFromLittleEndian<quint32>(b.constData()+p); }
float real(const QByteArray & b,int p) { quint32 bits=word(b,p);float f;std::memcpy(&f,&bits,4);return f; }
bool finite(const Vector3 & v) {return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);}
QMatrix4x4 matrix(const Transform & t) {
	QMatrix4x4 m;
	for(int c=0;c<3;++c) {Vector3 unit;unit[c]=1;auto v=t.rotation*unit*t.scale;for(int r=0;r<3;++r)m(r,c)=v[r];}
	for(int r=0;r<3;++r)m(r,3)=t.translation[r];
	return m;
}
bool worldMatrix(NifModel * model,QModelIndex block,QMatrix4x4 & result) {
	result.setToIdentity();QSet<int> visited;
	while(block.isValid()) {
		int b=model->getBlockNumber(block);if(visited.contains(b))return false;visited.insert(b);
		if(model->inherits(block,"NiAVObject"))result=matrix(Transform(model,block))*result;
		block=model->getBlock(model->getParent(block));
	}
	return true;
}
bool pose(NifModel * model,const QModelIndex & field,QMatrix4x4 & m) {
	if(!field.isValid() || model->rowCount(field)!=12)return false;
	for(int c=0;c<4;++c)for(int r=0;r<3;++r) {
		auto value=model->getIndex(field,QString("m%1%2").arg(r+1).arg(c+1));
		if(!value.isValid())return false;
		float f=model->get<float>(value);if(!std::isfinite(f))return false;
		// Gamebryo serializes the PhysX NxMat33 rows followed by translation.
		// Matrix34's historical XML member order labels that 3x3 part transposed.
		if(c<3)m(c,r)=f;else m(r,c)=f;
	}
	return true;
}
bool sourcePartGeometry(NifModel * model,const PhysXCollision::Mesh & mesh,QVector<Vector3> & vertices,QVector<quint32> & indices,QString & error,const QMatrix4x4 * sourceWorld) {
	VertexEditData data;
	if(!data.read(model,mesh.source,&error))return false;
	if(model->get<uint>(mesh.source,"Primitive Type")!=0 || model->get<uint>(mesh.source,"Num Submeshes")!=1) {
		error=QObject::tr("Collision rebuilding supports a single triangle-list submesh per source mesh.");return false;
	}
	bool big=model->get<quint8>(model->getHeader(),"Endian Type")==0;
	auto refs=model->getIndex(mesh.source,"Datastreams");
	for(int s=0;s<model->rowCount(refs);++s) {
		auto ref=refs.child(s,0),semantics=model->getIndex(ref,"Component Semantics");
		bool index=false;for(int c=0;c<model->rowCount(semantics);++c)if(model->get<QString>(semantics.child(c,0),"Name")=="INDEX")index=true;
		if(!index)continue;
		auto stream=model->getBlock(model->getLink(ref,"Stream"));auto formats=model->getArray<uint>(stream,"Component Formats");
		if(!indices.isEmpty() || formats.size()!=1 || model->get<bool>(ref,"Is Per Instance")
			|| (formats[0]!=NiMesh::F_UINT16_1 && formats[0]!=NiMesh::F_UINT32_1)) {
			error=QObject::tr("Unsupported collision index stream.");return false;
		}
		auto bytes=model->get<QByteArray>(model->getIndex(stream,"Data").child(0,0));
		int width=formats[0]==NiMesh::F_UINT16_1?2:4;
		auto regions=model->getIndex(stream,"Regions");
		if(bytes.isEmpty() || bytes.size()%(3*width) || model->rowCount(regions)!=1
			|| model->get<uint>(regions.child(0,0),"Start Index")!=0
			|| model->get<uint>(regions.child(0,0),"Num Indices")!=uint(bytes.size()/width)) {
			error=QObject::tr("Unsupported collision index regions.");return false;
		}
		for(int p=0;p<bytes.size();p+=width) {
			const char * ptr=bytes.constData()+p;
			quint32 i=width==2?(big?qFromBigEndian<quint16>(ptr):qFromLittleEndian<quint16>(ptr)):(big?qFromBigEndian<quint32>(ptr):qFromLittleEndian<quint32>(ptr));
			if(i>=uint(data.positions.size())){error=QObject::tr("Collision source has an invalid vertex index.");return false;}indices<<i;
		}
	}
	if(indices.isEmpty()){error=QObject::tr("No collision source triangles found.");return false;}
	QMatrix4x4 source;bool invertible=false;
	if(sourceWorld)source=*sourceWorld;
	else if(!worldMatrix(model,mesh.source,source)){error=QObject::tr("Cyclic scene hierarchy.");return false;}
	QMatrix4x4 toCollision=mesh.world.inverted(&invertible)*source;
	if(!invertible){error=QObject::tr("Collision transform is singular.");return false;}
	for(auto v:data.positions) {
		auto p=toCollision.map(QVector3D(v[0],v[1],v[2]));Vector3 point(p.x(),p.y(),p.z());
		if(!finite(point)){error=QObject::tr("Collision source contains invalid coordinates.");return false;}vertices<<point;
	}
	// A mirrored source transform needs winding reversed to retain its front faces.
	if(toCollision.determinant()<0)for(int i=0;i<indices.size();i+=3)qSwap(indices[i+1],indices[i+2]);
	return true;
}

bool sourceGeometry(NifModel * model,const PhysXCollision::Mesh & mesh,QVector<Vector3> & vertices,QVector<quint32> & indices,QString & error) {
    if(mesh.sourceMeshes.isEmpty() || mesh.sourceWorlds.size()!=mesh.sourceMeshes.size()){error=QObject::tr("No complete source geometry was resolved.");return false;}
    for(int n=0;n<mesh.sourceMeshes.size();++n){
        PhysXCollision::Mesh part;part.source=mesh.sourceMeshes[n];part.world=mesh.world;
        QVector<Vector3> points;QVector<quint32> triangles;
        if(!sourcePartGeometry(model,part,points,triangles,error,&mesh.sourceWorlds[n])){
            error=model->get<QString>(part.source,"Name")+": "+error;return false;
        }
        if(qint64(vertices.size())+points.size()>4000000 || qint64(indices.size())+triangles.size()>12000000){
            error=QObject::tr("Combined collision geometry exceeds supported limits.");return false;
        }
        quint32 offset=vertices.size();vertices+=points;
        for(auto index:triangles)indices<<offset+index;
    }
    return true;
}

bool resolveSources(NifModel * model,const QVector<QModelIndex> & matches,
                    const QMap<int,QVector<int>> & parents,PhysXCollision::Mesh & mesh,QString & error){
    auto fail=[&](const QString & why){error=QObject::tr("Collision '%1': %2").arg(mesh.name,why);return false;};
    if(mesh.name.isEmpty() || matches.isEmpty())return fail(QObject::tr("no matching NiMesh or NiNode source."));
    QSet<int> named;for(auto block:matches)named.insert(model->getBlockNumber(block));
    QVector<QModelIndex> roots;
    for(auto block:matches){int id=model->getBlockNumber(block);QSet<int> seen;bool nested=false;
        while(true){if(seen.contains(id))return fail(QObject::tr("cyclic source hierarchy."));seen.insert(id);
            auto owners=parents.value(id);if(owners.size()>1)return fail(QObject::tr("source has multiple scene parents."));
            if(owners.isEmpty())break;id=owners.first();if(named.contains(id))nested=true;
        }
        if(!nested)roots<<block;
    }
    if(roots.size()!=1)return fail(QObject::tr("multiple unrelated objects have the same name."));
    mesh.source=roots.first();bool group=model->getBlockName(mesh.source)=="NiNode";QSet<int> seen;
    std::function<bool(QModelIndex)> visit=[&](QModelIndex block){
        int id=model->getBlockNumber(block);
        if(!block.isValid() || seen.contains(id)){error=QObject::tr("missing, repeated or cyclic group child.");return false;}
        seen.insert(id);
        auto type=model->getBlockName(block);
        if(type!="NiNode" && type!="NiMesh"){error=QObject::tr("unsupported source child type '%1'.").arg(type);return false;}
        if(group && model->getLink(block,"Controller")>=0){error=QObject::tr("animated source groups are unsupported.");return false;}
        QMatrix4x4 transform;QSet<int> chain;int ancestor=id;
        while(ancestor>=0){
            if(chain.contains(ancestor)){error=QObject::tr("cyclic source hierarchy.");return false;}chain.insert(ancestor);
            auto owners=parents.value(ancestor);if(owners.size()>1){error=QObject::tr("source has multiple scene parents.");return false;}
            auto node=model->getBlock(ancestor);if(!Transform::canConstruct(model,node)){error=QObject::tr("unsupported source transform.");return false;}
            transform=matrix(Transform(model,node))*transform;
            ancestor=owners.isEmpty()?-1:owners.first();
        }
        if(!std::isfinite(transform.determinant()) || transform.determinant()==0){error=QObject::tr("invalid or singular source transform.");return false;}
        if(type=="NiMesh"){mesh.sourceMeshes<<QPersistentModelIndex(block);mesh.sourceWorlds<<transform;return true;}
        for(int child:model->getLinkArray(block,"Children"))if(child>=0 && !visit(model->getBlock(child)))return false;
        return true;
    };
    if(!visit(mesh.source))return fail(error);
    if(mesh.sourceMeshes.isEmpty())return fail(QObject::tr("source group contains no NiMesh geometry."));
    return true;
}
}

bool PhysXCollision::hasCollision(const NifModel * model) {
	if(model)for(int b=0;b<model->getBlockCount();++b)if(model->inherits(model->getBlock(b),"NiPhysXProp"))return true;
	return false;
}
bool PhysXCollision::decode(const QByteArray & b,QVector<Vector3> & vertices,QVector<quint32> & indices) {
	vertices.clear();indices.clear();
	if(b.size()<36 || word(b,0)!=0x0153584e || word(b,4)!=0x4853454d || word(b,8)!=1)return false;
	quint32 flags=word(b,12),nv=word(b,28),nt=word(b,32);
	int width=(flags&8)?1:(flags&16)?2:4;
	if((flags&~quint32(31)) || !nv || !nt || nv>4000000 || nt>4000000 || quint64(36)+quint64(nv)*12+quint64(nt)*3*width>quint64(b.size()))return false;
	for(quint32 i=0;i<nv;++i){Vector3 v(real(b,36+i*12),real(b,40+i*12),real(b,44+i*12));if(!finite(v)){vertices.clear();return false;}vertices<<v;}
	int offset=36+nv*12;
	for(quint32 i=0;i<nt*3;++i) {
		const char * p=b.constData()+offset+i*width;
		quint32 index=width==1?quint8(*p):width==2?qFromLittleEndian<quint16>(p):qFromLittleEndian<quint32>(p);
		if(index>=nv){vertices.clear();indices.clear();return false;}indices<<index;
	}
	return true;
}

bool PhysXCollision::collect(NifModel * model,QVector<Mesh> & meshes,QString & error,bool requireSources) {
	meshes.clear();error.clear();
	auto fail=[&](const QString & message){error=message;meshes.clear();return false;};
	if(!hasCollision(model))return fail(QObject::tr("This file contains no NiPhysXProp collision."));
	QMap<QString,QVector<QModelIndex>> sources;QMap<int,QVector<int>> parents;
	for(int b=0;b<model->getBlockCount();++b){auto block=model->getBlock(b);auto type=model->getBlockName(block);
		if(type=="NiMesh" || type=="NiNode")sources[model->string(model->getIndex(block,"Name"))]<<block;
		if(model->inherits(block,"NiNode"))for(int child:model->getLinkArray(block,"Children"))if(child>=0)parents[child]<<b;
	}
	QSet<int> targets;
	for(int b=0;b<model->getBlockCount();++b) {
		auto prop=model->getBlock(b);if(!model->inherits(prop,"NiPhysXProp"))continue;
		if(model->get<uint>(prop,"Num Sources") || model->get<int>(prop,"Num Dests") || model->get<uint>(prop,"Num Modified Meshes"))
			return fail(QObject::tr("PhysX source/destination bindings and modified meshes are not supported yet."));
		auto desc=model->getBlock(model->getLink(prop,"Prop Description"));
		if(!desc.isValid() || model->get<uint>(desc,"Num States")!=1 || model->get<uint>(desc,"Num Clothes") || model->get<uint>(desc,"Num Joints"))
			return fail(QObject::tr("Rebuilding supports static PhysX triangle collision with one state, without cloth or joints."));
		float scale=model->get<float>(prop,"PhysX to World Scale");
		if(!std::isfinite(scale) || scale<=0)return fail(QObject::tr("Invalid PhysX world scale."));
		QMatrix4x4 root;if(!worldMatrix(model,prop,root))return fail(QObject::tr("Cyclic collision hierarchy."));root.scale(scale);
		auto actors=model->getIndex(desc,"Actors");
		for(int a=0;a<model->rowCount(actors);++a) {
			auto actor=model->getBlock(model->getLink(actors.child(a,0)));
			if(!actor.isValid() || model->getLink(actor,"Body Desc")>=0 || model->getLink(actor,"Actor Parent")>=0
				|| model->getLink(actor,"Source")>=0 || model->getLink(actor,"Dest")>=0 || model->get<uint>(actor,"Num Poses")!=1)
				return fail(QObject::tr("Dynamic actors and parented or animated collision are not supported yet."));
			QMatrix4x4 actorPose;
			if(!pose(model,model->getIndex(actor,"Poses").child(0,0),actorPose))return fail(QObject::tr("Unsupported actor pose."));
			auto shapes=model->getIndex(actor,"Shape Descriptions");
			for(int s=0;s<model->rowCount(shapes);++s) {
				auto shape=model->getBlock(model->getLink(shapes.child(s,0)));
				if(model->get<uint>(shape,"Shape Type")!=6)return fail(QObject::tr("Only PhysX triangle-mesh shapes can be rebuilt. Convex and primitive shapes are not supported yet."));
				auto target=model->getBlock(model->getLink(shape,"Mesh"));
				int id=model->getBlockNumber(target);
				if(id<0 || targets.contains(id))return fail(QObject::tr("Missing or shared PhysX mesh description."));targets.insert(id);
				Mesh mesh;mesh.name=model->string(model->getIndex(target,"Mesh Name"));
				QString sourceError;
				if(!resolveSources(model,sources.value(mesh.name),parents,mesh,sourceError)){
					if(requireSources)return fail(sourceError);
					mesh.source=QModelIndex();mesh.sourceMeshes.clear();mesh.sourceWorlds.clear();
				}
				mesh.data=model->getIndex(target,"Mesh Data");
				if(model->getValue(mesh.data).type()!=NifValue::tByteArray || !decode(model->get<QByteArray>(mesh.data),mesh.vertices,mesh.indices))return fail(QObject::tr("Unsupported cooked collision data for '%1'.").arg(mesh.name));
				QMatrix4x4 local;
				if(!pose(model,model->getIndex(shape,"Local Pose"),local))return fail(QObject::tr("Unsupported collision shape pose."));
				mesh.world=root*actorPose*local;meshes<<mesh;
			}
		}
	}
	if(meshes.isEmpty())return fail(QObject::tr("No supported triangle collision found."));
	return true;
}

bool PhysXCollision::rebuild(NifModel * model,const QString & dll,const QString & helper,QString & error,int * count) {
	if(count)*count=0;
	QVector<Mesh> meshes;if(!collect(model,meshes,error))return false;
	QTemporaryDir directory;if(!directory.isValid()){error=QObject::tr("Cannot create collision workspace.");return false;}
	QString input=directory.filePath("input.bin"),output=directory.filePath("output.bin");
	QFile file(input);if(!file.open(QIODevice::WriteOnly)){error=file.errorString();return false;}
	QDataStream request(&file);request.setByteOrder(QDataStream::LittleEndian);request.setFloatingPointPrecision(QDataStream::SinglePrecision);
	request<<quint32(0x31435850)<<qint32(meshes.size());
	for(const auto & mesh:meshes) {
		QVector<Vector3> vertices;QVector<quint32> indices;
		if(!sourceGeometry(model,mesh,vertices,indices,error)){error=mesh.name+": "+error;return false;}
		auto blob=model->get<QByteArray>(mesh.data);request<<qint32(blob.size());request.writeRawData(blob.constData(),blob.size());
		request<<qint32(vertices.size())<<qint32(indices.size()/3);
		for(auto v:vertices)for(int a=0;a<3;++a)request<<v[a];
		for(auto i:indices)request<<i;
	}
	if(request.status()!=QDataStream::Ok || !file.flush()){error=QObject::tr("Cannot write collision workspace.");return false;}file.close();
	QProcess process;
#ifdef Q_OS_WIN
	process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments * args){args->flags|=0x08000000;});
#endif
	process.setProgram(helper);process.setArguments({"--cook",dll,input,output});process.start();
	if(!process.waitForStarted(5000)){error=QObject::tr("Cannot start PhysxCooker.exe. Rebuild the Windows helper alongside NifSkope.");return false;}
	if(!process.waitForFinished(55000)){process.kill();process.waitForFinished();error=QObject::tr("Collision rebuild timed out. No collision data was changed.");return false;}
	if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0){error=QString::fromLocal8Bit(process.readAllStandardError()).trimmed();if(error.isEmpty())error=QObject::tr("PhysX rejected the collision rebuild. No collision data was changed.");return false;}
	QFile result(output);if(!result.open(QIODevice::ReadOnly) || result.size()>536870912){error=QObject::tr("Invalid collision helper output.");return false;}
	QDataStream response(&result);response.setByteOrder(QDataStream::LittleEndian);
	quint32 magic; qint32 size;response>>magic>>size;
	if(magic!=0x31435850 || size!=meshes.size()){error=QObject::tr("Invalid collision helper response.");return false;}
	QVector<QByteArray> blobs;
	for(int n=0;n<meshes.size();++n) {
		response>>size;if(size<36 || size>134217728 || size>result.bytesAvailable()){error=QObject::tr("Incomplete collision helper response.");return false;}
		QByteArray blob(size,Qt::Uninitialized);response.readRawData(blob.data(),size);
		QVector<Vector3> vertices;QVector<quint32> indices;
		if(response.status()!=QDataStream::Ok || !decode(blob,vertices,indices)){error=QObject::tr("Invalid rebuilt collision geometry.");return false;}blobs<<blob;
	}
	if(!result.atEnd()){error=QObject::tr("Unexpected collision helper data.");return false;}
	model->beginUndoTransaction(QObject::tr("Rebuild collision"));
	for(int i=0;i<meshes.size();++i)model->set<QByteArray>(meshes[i].data,blobs[i]);
	// Cooked byte lengths change. Include derived sizes in the same undo step,
	// otherwise a subsequent Save would appear as another document edit.
	model->updateHeader();
	model->endUndoTransaction();if(count)*count=meshes.size();return true;
}
