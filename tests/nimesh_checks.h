#include "gl/glmesh.h"
#include "gl/glscene.h"
#include <QFile>
#include <QTextStream>
#include <QCryptographicHash>
#include "gl/gltex.h"
#include <QtEndian>
#include <cstring>

class NiMeshReadback : public Mesh {
public:
	using Mesh::Mesh;
	using Shape::verts;
	using Shape::norms;
	using Shape::coords;
	using Shape::colors;
	using Shape::triangles;
	using Shape::indices;
};

static bool verifyNiMeshData( const NifModel & model, const QModelIndex & block, const NiMeshReadback & mesh )
{
	// Compare the decoded attributes with the original bytes, independently of
	// datastream order, to detect misplaced components or index truncation.
	auto streams=model.getIndex(block,"Datastreams");
	QVector<quint16> expectedIndices;
	for(int s=0;s<model.rowCount(streams);++s) {
		auto reference=streams.child(s,0);
		auto stream=model.getBlock(model.getLink(reference,"Stream"));
		auto formats=model.getArray<uint>(stream,"Component Formats");
		auto semantics=model.getIndex(reference,"Component Semantics");
		auto data=model.get<QByteArray>(model.getIndex(stream,"Data").child(0,0));
		int stride=0;
		for(auto f:formats) stride+=((f>>16)&15)*((f>>8)&15);
		int componentOffset=0;
		for(int c=0;c<formats.size();++c) {
			auto format=formats[c];
			int length=(format>>16)&15, size=(format>>8)&15;
			auto name=model.get<QString>(semantics.child(c,0),"Name");
			uint uv=model.get<uint>(semantics.child(c,0),"Index");
			auto regions=model.getIndex(stream,"Regions");
			for(int r=0;r<model.rowCount(regions);++r) {
				uint start=model.get<uint>(regions.child(r,0),"Start Index");
				uint count=model.get<uint>(regions.child(r,0),"Num Indices");
				for(uint j=0;j<count;++j) {
					uint vertex=start+j;
					quint64 offset=quint64(vertex)*stride+componentOffset;
					if(offset+length*size>quint64(data.size())) return false;
					const auto p=reinterpret_cast<const uchar*>(data.constData()+offset);
					if(name=="INDEX") {
						quint32 index=format==NiMesh::F_UINT32_1?qFromLittleEndian<quint32>(p):qFromLittleEndian<quint16>(p);
						if(quint64(index)+start>0xffff) return false;
						expectedIndices << quint16(index+start);
					} else if(format==NiMesh::F_FLOAT32_2 || format==NiMesh::F_FLOAT32_3 || format==NiMesh::F_FLOAT32_4) {
						for(int k=0;k<length;++k) {
							quint32 bits=qFromLittleEndian<quint32>(p+k*4);
							float value; std::memcpy(&value,&bits,4);
							float actual=value;
							if(name=="POSITION" || name=="POSITION_BP") {
								if(vertex>=uint(mesh.verts.size())) return false;
								actual=mesh.verts[vertex][k];
							} else if(name=="NORMAL" || name=="NORMAL_BP") {
								if(vertex>=uint(mesh.norms.size())) return false;
								actual=mesh.norms[vertex][k];
							} else if(name=="COLOR") {
								if(vertex>=uint(mesh.colors.size())) return false;
								actual=mesh.colors[vertex][k];
							} else if(name=="TEXCOORD") {
								if(uv>=uint(mesh.coords.size()) || vertex>=uint(mesh.coords[uv].size())) return false;
								actual=mesh.coords[uv][vertex][k];
							}
							if(std::abs(actual-value)>1e-6f) return false;
						}
					}
				}
			}
			componentOffset+=length*size;
		}
	}
	return expectedIndices==mesh.indices;
}

static QByteArray checkNiMeshFile( const QString & path, QOpenGLContext * context, QOpenGLFunctions * functions )
{
	NifModel model;
	check(model.loadFromFile(path), "load NiMesh regression fixture");
	TexCache textures;
	Scene scene(&textures, context, functions);
	scene.animate = false;
	QByteArray geometry;
	QDataStream output(&geometry, QIODevice::WriteOnly);
	int meshes = 0, empty = 0, mismatches = 0;
	for (int b=0; b<model.getBlockCount(); ++b) {
		auto block=model.getBlock(b);
		if (!model.inherits(block,"NiMesh")) continue;
		NiMeshReadback mesh(&scene,block);
		mesh.update(&model,block);
		mesh.transform();
		++meshes;
		if (mesh.verts.isEmpty() || mesh.triangles.isEmpty()) ++empty;
		if(!verifyNiMeshData(model,block,mesh)) {
			std::fprintf(stderr,"Mesh %d does not match source data\n",b);
			++mismatches;
		}
		output << mesh.verts.size() << mesh.norms.size() << mesh.colors.size() << mesh.coords.size() << mesh.triangles.size();
		for (auto v : mesh.verts) for(int c=0;c<3;++c) output << v[c];
		for (auto v : mesh.norms) for(int c=0;c<3;++c) output << v[c];
		for (auto v : mesh.colors) for(int c=0;c<4;++c) output << v[c];
		for (auto uv : mesh.coords) { output << uv.size(); for(auto v : uv) output << v[0] << v[1]; }
		for (auto t : mesh.triangles) output << t[0] << t[1] << t[2];
	}
	auto hash=QCryptographicHash::hash(geometry,QCryptographicHash::Sha256).toHex();
	QTextStream(stdout) << path << " meshes=" << meshes << " empty=" << empty << " geometry-sha256=" << hash << '\n';
	check(meshes>0 && empty==0,"all sample NiMeshes decode into geometry");
	check(mismatches==0,"decoded positions, normals, colors, UVs and indices match source streams");
	return hash;
}

static void inspectNiMesh( const QString & path )
{
	NifModel model;
	check(model.loadFromFile(path), "load sample NIF");
	QTextStream out(stdout);
	out << path << " version=" << model.getVersion() << " user=" << model.getUserVersion()
		<< " blocks=" << model.getBlockCount() << '\n';
	int meshes = 0;
	for ( int b = 0; b < model.getBlockCount(); ++b ) {
		auto block = model.getBlock(b);
		if ( !model.inherits(block, "NiMesh") ) continue;
		++meshes;
		if ( meshes > 3 ) continue;
		auto streams = model.getIndex(block, "Datastreams");
		out << "mesh " << b << " streams=" << model.rowCount(streams) << '\n';
		for ( int i = 0; i < model.rowCount(streams); ++i ) {
			auto reference = streams.child(i,0);
			int stream = model.getLink(reference, "Stream");
			auto data = model.getBlock(stream);
			out << "  slot=" << i << " block=" << stream << " usage=" << model.get<uint>(data,"Usage")
				<< " access=" << model.get<uint>(data,"Access") << " bytes=" << model.get<uint>(data,"Num Bytes");
			auto semantics = model.getIndex(reference, "Component Semantics");
			for ( int j=0; j<model.rowCount(semantics); ++j )
				out << " " << model.get<QString>(semantics.child(j,0),"Name") << ":"
					<< model.get<uint>(semantics.child(j,0),"Index");
			out << " formats=";
			for ( auto format : model.getArray<uint>(data,"Component Formats") ) out << QString::number(format,16) << ",";
			out << " regions=";
			auto regions = model.getIndex(data,"Regions");
			for(int j=0;j<model.rowCount(regions);++j)
				out << model.get<uint>(regions.child(j,0),"Start Index") << ":" << model.get<uint>(regions.child(j,0),"Num Indices") << ",";
			out << '\n';
		}
	}
	out << "NiMesh count=" << meshes << '\n';
}
