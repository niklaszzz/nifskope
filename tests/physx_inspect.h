#include <QTextStream>

static void inspectPhysx( const QString & path )
{
	NifModel model; check(model.loadFromFile(path),"load PhysX sample");
	QTextStream out(stdout);
	out << "Version=" << model.getVersion() << " blocks=" << model.getBlockCount() << '\n';
	QMap<QString,int> counts;
	for(int b=0;b<model.getBlockCount();++b) ++counts[model.itemName(model.getBlock(b))];
	for(auto it=counts.cbegin();it!=counts.cend();++it) out<<it.key()<<"="<<it.value()<<'\n';
	QSet<QString> printed;
	for(int b=0;b<model.getBlockCount();++b) {
		auto block=model.getBlock(b);auto name=model.itemName(block);
		if(!name.startsWith("NiPhysX") || printed.contains(name)) continue;
		printed.insert(name);out<<"\nBlock "<<b<<" "<<name<<'\n';
		for(int r=0;r<model.rowCount(block);++r) {
			auto field=block.child(r,0);auto value=model.getValue(field);auto type=value.type();
			out<<"  "<<model.itemName(field)<<" ["<<model.itemType(field)<<"] ";
			if(type==NifValue::tByteArray || type==NifValue::tBlob) {
				auto blob=value.get<QByteArray>();out<<"bytes="<<blob.size()<<" head="<<blob.left(80).toHex(' ');
			} else if(model.rowCount(field)>0) {
				out<<"rows="<<model.rowCount(field);
				for(int j=0;j<qMin(3,model.rowCount(field));++j) out<<" {"<<model.getValue(field.child(j,0)).toString()<<"}";
			} else out<<value.toString();
			out<<'\n';
		}
	}
	QMap<uint,int> shapeTypes;
	for(int b=0;b<model.getBlockCount();++b) if(model.itemName(model.getBlock(b))=="NiPhysXShapeDesc") ++shapeTypes[model.get<uint>(model.getBlock(b),"Shape Type")];
	for(auto it=shapeTypes.cbegin();it!=shapeTypes.cend();++it) out<<"Shape type "<<it.key()<<" count="<<it.value()<<'\n';
	for(int b=0;b<model.getBlockCount();++b) {
		auto block=model.getBlock(b);auto type=model.itemName(block);
		if(type=="NiMesh") out<<"Proxy "<<b<<" name="<<model.string(model.getIndex(block,"Name"))<<" transform="<<Transform(&model,block).toString()<<'\n';
		if(type=="NiPhysXActorDesc" || type=="NiPhysXShapeDesc" || type=="NiPhysXMeshDesc") {
			QString label=type=="NiPhysXActorDesc"?"Actor Name":type=="NiPhysXShapeDesc"?"Shape Name":"Mesh Name";
			out<<"Physics "<<b<<" name="<<model.string(model.getIndex(block,label));
			auto pose=type=="NiPhysXActorDesc"?model.getIndex(block,"Poses").child(0,0):model.getIndex(block,"Local Pose");
			if(pose.isValid()) {out<<" pose=";for(int r=0;r<model.rowCount(pose);++r)out<<model.getValue(pose.child(r,0)).toString()<<",";}
			out<<'\n';
		}
	}
}
