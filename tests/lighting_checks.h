#include "ui/widgets/lightingwidget.h"
#include <QCheckBox>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>

class LightingPreviewTest {
public:
    static void falloff() {
        NifModel model;VertexEditorTest::fixture(model);
        auto point=model.getBlock(5),root=model.getBlock(0);
        model.set<uint>(model.getBlock(1),"Flags",1);model.set<uint>(model.getBlock(3),"Flags",1);
        model.set<Vector3>(root,"Translation",Vector3(-8,12,0));model.set<float>(root,"Scale",3);
        model.set<Matrix>(root,"Rotation",Matrix::euler(0,0,1.570796327f));
        model.set<Vector3>(point,"Translation",Vector3(20,30,10));
        model.set<float>(point,"Constant Attenuation",0);model.set<float>(point,"Linear Attenuation",0);
        model.set<float>(point,"Quadratic Attenuation",.000016f);
        QGLFormat format;format.setVersion(2,1);GLView view(format,nullptr);view.timer->stop();
        view.cfg.fov=45;view.cfg.upAxis=GLView::ZAxis;view.cfg.background=QColor(25,25,25);view.setNif(&model);
        view.scene->options=Scene::ShowNodes;view.scene->animate=false;view.animState=GLView::AnimDisabled;
        view.setProjection(false);view.setRotation(0,0,0);view.setPosition(0,0,0);view.setDistance(6000);view.view=GLView::ViewUser;
        view.resize(800,600);view.show();QCoreApplication::processEvents();view.setCurrentIndex(point);view.updateGL();view.updateGL();
        double ten=view.lightAttenuationRadius(point,.1),one=view.lightAttenuationRadius(point,.01);
        check(std::abs(ten-790.569415)<.001 && std::abs(one-2500)<.001,
            "screenshot attenuation values reach 10 percent at 790.57 and 1 percent at 2500 world units");
        auto world=view.scene->getNode(&model,point)->worldTrans();auto cage=view.lightFalloffLines(point,world,.01);
        check(cage.size()==384 && (world.translation-Vector3(-98,72,30)).length()<.001f,
            "point falloff cage is centered at the light's transformed world position");
        bool worldRadius=true;
        for(const auto & edge:cage)worldRadius=worldRadius && std::abs((edge.first-world.translation).length()-2500)<.002;
        check(worldRadius,"falloff distance stays in world units under a scaled parent");
        auto before=bytes(model);model.resetUndoHistory();
        auto orangeNear=[&](const QImage & frame,const QPointF & p) {
            for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x){auto q=p.toPoint()+QPoint(x,y);if(!frame.rect().contains(q))continue;
                auto c=frame.pixelColor(q);if(c.red()>220 && c.green()>110 && c.green()<200 && c.blue()<100)return true;}
            return false;
        };
        QPointF sample;check(view.projectGizmo(world.translation+Vector3(ten/std::sqrt(2.),ten/std::sqrt(2.),0),sample)
            && orangeNear(view.grabFrameBuffer(),sample),"selected light renders its world-sized falloff guide");
        view.grabFrameBuffer().save(QCoreApplication::applicationDirPath()+"/light-falloff-preview.png");
        view.setCurrentIndex(root);view.updateGL();view.updateGL();
        check(!orangeNear(view.grabFrameBuffer(),sample),"falloff guides appear only for the selected light");
        view.setCurrentIndex(point);view.updateGL();view.updateGL();
        check(bytes(model)==before && model.undoStack->isClean() && model.undoStack->count()==0,
            "falloff visualization and selection do not change NIF data or history");
        model.beginUndoTransaction("Adjust quadratic attenuation");model.set<float>(point,"Quadratic Attenuation",.000064f);model.endUndoTransaction();
        view.updateGL();view.updateGL();
        check(std::abs(view.lightAttenuationRadius(point,.01)-1250)<.001 && !orangeNear(view.grabFrameBuffer(),sample),
            "increasing quadratic attenuation immediately shrinks the guide");
        model.undo();view.updateGL();view.updateGL();
        check(orangeNear(view.grabFrameBuffer(),sample) && bytes(model)==before,"undo restores attenuation and the displayed radius");
        point=model.getBlock(5);
        model.set<float>(point,"Constant Attenuation",2);model.set<float>(point,"Linear Attenuation",.5f);model.set<float>(point,"Quadratic Attenuation",.01f);
        double radius=view.lightAttenuationRadius(point,.01);
        check(std::abs(2+.5*radius+.01*radius*radius-100)<.0001,"radius calculation includes constant, linear and quadratic terms");
        model.set<float>(point,"Quadratic Attenuation",0);
        check(std::abs(view.lightAttenuationRadius(point,.01)-196)<.0001,"linear-only falloff has a finite contour");
        model.set<float>(point,"Linear Attenuation",0);
        check(view.lightAttenuationRadius(point,.01)<0 && view.lightFalloffLines(point,world,.01).isEmpty(),
            "constant-only lights have no finite falloff contour");
        model.set<float>(point,"Constant Attenuation",101);
        check(view.lightAttenuationRadius(point,.01)==0,"light already below the threshold has no positive contour");
        model.set<float>(point,"Constant Attenuation",0);model.set<float>(point,"Quadratic Attenuation",-1);
        check(view.lightAttenuationRadius(point,.01)<0,"negative attenuation is not presented as a reliable radius");
        model.set<float>(point,"Quadratic Attenuation",std::numeric_limits<float>::quiet_NaN());
        check(view.lightAttenuationRadius(point,.01)<0,"nonfinite attenuation cannot generate invalid guide vertices");
        auto spot=model.insertNiBlock("NiSpotLight");model.set<float>(spot,"Constant Attenuation",0);
        model.set<float>(spot,"Linear Attenuation",0);model.set<float>(spot,"Quadratic Attenuation",.000016f);model.set<float>(spot,"Outer Spot Angle",30);
        auto cone=view.lightFalloffLines(spot,world,.01);auto forward=world.rotation*Vector3(1,0,0);
        bool bounded=!cone.isEmpty();
        for(const auto & edge:cone)for(auto p:{edge.first,edge.second}) {
            auto delta=p-world.translation;double length=delta.length();
            bounded=bounded && length<=2500.002 && (length<.001 || Vector3::dotproduct(delta,forward)/length>=std::cos(3.141592654/6)-.00001);
        }
        check(bounded,"spot falloff contours remain within the outer angle and attenuation distance");
        auto directional=model.insertNiBlock("NiDirectionalLight");
        check(view.lightAttenuationRadius(directional,.01)<0 && view.lightFalloffLines(directional,world,.01).isEmpty(),
            "directional lights do not acquire a fictitious falloff radius");
    }
	static void indicators() {
        QSettings settings;const QString versionKey="Settings/NIF/Startup Defaults/Version";
        auto previousVersion=settings.value(versionKey);settings.setValue(versionKey,"20.6.0.0");
        NifModel model;
        if(previousVersion.isValid())settings.setValue(versionKey,previousVersion);else settings.remove(versionKey);
        VertexEditorTest::fixture(model);
        model.set<uint>(model.getBlock(1),"Flags",1);model.set<uint>(model.getBlock(3),"Flags",1);
        auto root=model.getBlock(0),point=model.getBlock(5);
        auto directional=model.insertNiBlock("NiDirectionalLight");int directionalId=model.getBlockNumber(directional);
        auto spot=model.insertNiBlock("NiSpotLight");int spotId=model.getBlockNumber(spot);
        model.set<uint>(root,"Num Children",5);auto children=model.getIndex(root,"Children");model.updateArray(children);
        model.setLink(children.child(3,0),directionalId);model.setLink(children.child(4,0),spotId);
        model.set<Vector3>(point,"Translation",Vector3());
        model.set<Vector3>(directional,"Translation",Vector3(0,50,0));
        model.set<Vector3>(spot,"Translation",Vector3(-35,0,0));
        model.set<Vector3>(root,"Translation",Vector3(-8,12,0));model.set<float>(root,"Scale",2);
        model.set<Matrix>(root,"Rotation",Matrix::euler(0,0,1.570796327f));
        model.set<Matrix>(directional,"Rotation",Matrix::euler(0,0,1.570796327f));
        model.set<float>(spot,"Outer Spot Angle",30);model.set<float>(spot,"Inner Spot Angle",15);
        auto before=bytes(model);model.resetUndoHistory();
        QGLFormat format;format.setVersion(2,1);GLView view(format,nullptr);view.timer->stop();
        view.cfg.fov=45;view.cfg.upAxis=GLView::ZAxis;view.cfg.background=QColor(25,25,25);view.setNif(&model);
        view.scene->options=Scene::ShowNodes;view.scene->animate=false;view.animState=GLView::AnimDisabled;
        view.setProjection(false);view.setRotation(0,0,0);view.setPosition(0,0,0);view.setDistance(400);view.view=GLView::ViewUser;
        view.resize(800,600);view.show();QCoreApplication::processEvents();view.updateGL();view.updateGL();
        auto directionalWorld=view.scene->getNode(&model,directional)->worldTrans();
        auto arrow=view.lightIndicatorLines(directional,directionalWorld,80);
        check(arrow.size()==5 && (arrow.first().first-Vector3(-108,12,0)).length()<.001f
            && (arrow.first().second-arrow.first().first-Vector3(-80,0,0)).length()<.001f,
            "directional arrow follows Gamebryo +X through parent and light rotations, without scaling its display length");
        auto spotWorld=view.scene->getNode(&model,spot)->worldTrans();auto cone=view.lightIndicatorLines(spot,spotWorld,80);
        check(cone.size()==(model.checkVersion(0x14020005,0)?73:41)
            && (cone.first().second-cone.first().first-Vector3(0,80,0)).length()<.001f,
            "spotlight follows inherited +X and shows cone angles supported by the NIF version");
        auto ray=cone[37].second-cone[37].first;auto direction=cone.first().second-cone.first().first;
        check(std::abs(Vector3::dotproduct(ray,direction)/(ray.length()*direction.length())-std::cos(3.141592654/6))<.0001,
            "spot cone uses the stored outer half-angle in degrees");
        auto globe=view.lightIndicatorLines(point,view.scene->getNode(&model,point)->worldTrans(),80);
        auto center=view.scene->getNode(&model,point)->worldTrans().translation;
        Vector3 sum;for(const auto & line:globe){sum+=line.first-center;sum+=line.second-center;}
        check(globe.size()==102 && sum.length()<.001f,"point-light globe emits symmetrically in every direction");
        check(view.lightMarkers.size()==3,"all three visible light types have viewport indicators");
        for(const auto & marker:view.lightMarkers)check(view.lightAt(marker.second.toPoint())==marker.first,
            "light centers remain independently pickable");
        auto image=view.grabFrameBuffer();
        auto yellowNear=[&](const QImage & frame,const QPointF & p) {
            for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x){QPoint q=p.toPoint()+QPoint(x,y);if(!frame.rect().contains(q))continue;
                auto c=frame.pixelColor(q);if(c.red()>220 && c.green()>170 && c.blue()<100)return true;}
            return false;
        };
        QPointF sample;check(view.projectGizmo(directionalWorld.translation+Vector3(-35,0,0),sample)
            && yellowNear(image,sample),"directional arrow renders along the transformed direction");
        view.setCurrentIndex(directional);view.updateGL();view.updateGL();
        view.grabFrameBuffer().save(QCoreApplication::applicationDirPath()+"/light-indicators-preview.png");
        view.setIsolateSelection(true);view.updateGL();view.updateGL();
        check(view.lightMarkers.size()==1 && view.lightMarkers.first().first==directional,"isolation hides unrelated light indicators");
        view.setIsolateSelection(false);view.updateGL();view.updateGL();
        view.setProjection(true);view.updateGL();view.updateGL();check(view.lightMarkers.size()==3,"light indicators also render in perspective");
        view.scene->options&=~Scene::ShowNodes;view.updateGL();view.updateGL();
        check(view.lightMarkers.isEmpty() && !view.lightAt(sample.toPoint()).isValid(),"Show Nodes hides indicators and disables light picking");
        check(bytes(model)==before && model.undoStack->isClean() && model.undoStack->count()==0,
            "light indicators, selection and camera changes leave all NIF data and undo history unchanged");
    }
	static void inspect(const QString & path) {
		NifModel model;check(model.loadFromFile(path),"load alpha inspection NIF");
		QGLFormat format;format.setVersion(2,1);GLView view(format,nullptr);view.timer->stop();
		TexCache textures;Scene scene(&textures,view.glContext,view.glFuncs);scene.animate=false;
		for(int b=0;b<model.getBlockCount();++b) {
			auto block=model.getBlock(b);if(!model.inherits(block,"NiMesh"))continue;
			auto props=model.getIndex(block,"Properties");QModelIndex alpha,material,vertexColor,texture;
			for(int i=0;i<model.rowCount(props);++i) {auto p=model.getBlock(model.getLink(props.child(i,0)));auto type=model.itemName(p);
				if(type=="NiAlphaProperty")alpha=p;else if(type=="NiMaterialProperty")material=p;else if(type=="NiVertexColorProperty")vertexColor=p;else if(type=="NiTexturingProperty")texture=p;}
			if(!alpha.isValid() || !(model.get<uint>(alpha,"Flags")&512))continue;
			NiMeshReadback mesh(&scene,block);mesh.update(&model,block);mesh.transform();float low=1,high=0;
			for(auto c:mesh.colors){low=qMin(low,c.alpha());high=qMax(high,c.alpha());}
			auto source=model.getBlock(model.getLink(model.getIndex(texture,"Base Texture"),"Source"));
			std::printf("mesh=%d name=%s alphaFlags=%u threshold=%u materialAlpha=%g vertexProperty=%d vertexFlags=%u colorCount=%d vertexAlpha=%g..%g base=%s\n",b,qPrintable(model.get<QString>(block,"Name")),model.get<uint>(alpha,"Flags"),model.get<uint>(alpha,"Threshold"),model.get<float>(material,"Alpha"),model.getBlockNumber(vertexColor),model.get<uint>(vertexColor,"Flags"),mesh.colors.size(),low,high,qPrintable(model.get<QString>(source,"File Name")));
		}
	}
	static void realAlpha(const QString & path) {
		NifModel model;check(model.loadFromFile(path),"load supplied alpha-test map read-only");auto before=bytes(model);model.resetUndoHistory();
		QGLFormat format;format.setVersion(2,1);GLView view(format,nullptr);view.timer->stop();
		QColor background(13,247,179);view.cfg.background=background;view.cfg.upAxis=GLView::ZAxis;view.cfg.fov=45;view.setNif(&model);
		view.scene->options=Scene::ShowHidden|Scene::DoTexturing|Scene::DoLighting|Scene::DoVertexColors|Scene::DoBlending|Scene::DoGlow;
		view.scene->animate=false;view.animState=GLView::AnimDisabled;view.setProjection(false);view.setRotation(0,0,0);view.setPosition(0,0,0);view.setDistance(100);view.view=GLView::ViewUser;
		view.resize(320,240);view.show();QCoreApplication::processEvents();view.updateGL();view.updateGL();view.doCenter=false;
		auto roots=view.scene->roots;int tested=0,ignoredZeroAlpha=0;
		for(int b=0;b<model.getBlockCount();++b) {
			auto block=model.getBlock(b);if(!model.inherits(block,"NiMesh"))continue;
			auto node=view.scene->nodes.get(block);if(!node)continue;auto alpha=node->findProperty<AlphaProperty>();
			if(!alpha || !(model.get<uint>(alpha->index(),"Flags")&512))continue;
			auto vertexColor=node->findProperty<VertexColorProperty>();
			if(!vertexColor || !vertexColor->usesVertexAlpha())++ignoredZeroAlpha;
			auto sphere=node->bounds();view.scene->roots.clear();view.scene->roots.add(node);view.setPosition(-sphere.center[0],-sphere.center[1],-sphere.center[2]);view.setDistance(qMax(10.f,sphere.radius*1.5f));
			bool visible=false;
			for(auto rotation:{Vector3(0,0,0),Vector3(90,0,0),Vector3(-90,0,0),Vector3(0,0,90),Vector3(0,0,-90),Vector3(180,0,0)}) {
				view.setRotation(rotation[0],rotation[1],rotation[2]);view.scene->options&=~(Scene::Unshaded|Scene::DoVertexColors);view.updateGL();view.updateGL();auto normal=view.grabFrameBuffer();
				view.scene->options|=Scene::Unshaded|Scene::DoVertexColors;view.updateGL();view.updateGL();auto unshaded=view.grabFrameBuffer();int covered=0,missing=0;
				for(int y=1;y<normal.height();y+=2)for(int x=1;x<normal.width();x+=2)if(normal.pixelColor(x,y)!=background){++covered;if(unshaded.pixelColor(x,y)==background)++missing;}
				if(covered<10)continue;visible=true;
				std::printf("Alpha mesh %d: normal pixels=%d missing in unshaded=%d\n",b,covered,missing);
				check(missing==0,"supplied map alpha-tested geometry keeps its visible pixels in unshaded mode");break;
			}
			if(visible)++tested;
			else std::printf("Alpha mesh %d: no visible reference pixels to compare\n",b);
		}
		std::printf("Verified %d alpha-tested meshes, %d with ignored/emissive vertex colors\n",tested,ignoredZeroAlpha);
		check(tested>0 && ignoredZeroAlpha>0,"real map exercises alpha testing with ignored vertex alpha");
		view.scene->roots=roots;view.scene->options&=~Scene::Unshaded;
		check(bytes(model)==before && model.undoStack->isClean(),"real-map rendering leaves NIF bytes and undo history unchanged");
	}
	static void run() {
		QTemporaryDir directory;QString texturePath=directory.filePath("baked.bmp"),glowPath=directory.filePath("glow.bmp"),lightmapPath=directory.filePath("lightmap.bmp"),decalPath=directory.filePath("decal.tga");
		QImage texture(16,16,QImage::Format_RGB32);texture.fill(QColor(180,120,60));check(texture.save(texturePath),"prepare a baked base-color texture");
		texture.fill(QColor(10,20,30));check(texture.save(glowPath),"prepare a glow texture layer");
		texture.fill(QColor(128,128,128));for(int y=0;y<16;++y)for(int x=8;x<16;++x)texture.setPixelColor(x,y,QColor(64,64,64));
		check(texture.save(lightmapPath),"prepare a lightmap with distinct UV regions");
		QByteArray tga(18,0);tga[2]=2;tga[12]=4;tga[14]=4;tga[16]=32;tga[17]=0x28;
		for(int i=0;i<16;++i){tga.append(char(220));tga.append(char(160));tga.append(char(40));tga.append(char(128));}
		QFile decalFile(decalPath);check(decalFile.open(QIODevice::WriteOnly) && decalFile.write(tga)==tga.size(),"prepare a translucent decal texture");decalFile.close();
		NifModel model;VertexEditorTest::fixture(model);model.refreshFileInfo(directory.filePath("lighting.nif"));
		auto mesh=model.getBlock(1),data=model.getBlock(2);model.set<uint>(model.getBlock(3),"Flags",1);
		model.set<bool>(data,"Has Normals",true);model.updateArray(model.getIndex(data,"Normals"));
		model.setArray<Vector3>(model.getIndex(data,"Normals"),{Vector3(0,0,1),Vector3(1,0,0),Vector3(0,1,0),Vector3(0,0,-1)});
		model.set<bool>(data,"Has Vertex Colors",true);model.updateArray(model.getIndex(data,"Vertex Colors"));
		model.setArray<Color4>(model.getIndex(data,"Vertex Colors"),QVector<Color4>(4,Color4(.1f,.2f,.1f,1)));
		model.set<quint16>(data,"Vector Flags",2);model.set<quint16>(data,"BS Vector Flags",0);
		auto uvSets=model.getIndex(data,"UV Sets");model.updateArray(uvSets);model.updateArray(uvSets.child(0,0));
		model.setArray<Vector2>(uvSets.child(0,0),{Vector2(0,0),Vector2(1,0),Vector2(0,1),Vector2(1,1)});
		model.updateArray(uvSets.child(1,0));model.setArray<Vector2>(uvSets.child(1,0),QVector<Vector2>(4,Vector2(.25f,.5f)));
		auto material=model.insertNiBlock("NiMaterialProperty");int materialId=model.getBlockNumber(material);
		model.set<Color3>(material,"Diffuse Color",Color3(0,0,0));model.set<Color3>(material,"Emissive Color",Color3(.8f,.8f,.8f));model.set<float>(material,"Alpha",1);
		auto property=model.insertNiBlock("NiTexturingProperty");int propertyId=model.getBlockNumber(property);
		auto source=model.insertNiBlock("NiSourceTexture");int sourceId=model.getBlockNumber(source);
		model.set<quint8>(source,"Use External",1);model.set<QString>(source,"File Name",texturePath);
		auto glow=model.insertNiBlock("NiSourceTexture");int glowId=model.getBlockNumber(glow);
		model.set<quint8>(glow,"Use External",1);model.set<QString>(glow,"File Name",glowPath);
		model.set<bool>(property,"Has Base Texture",true);model.setLink(model.getIndex(property,"Base Texture"),"Source",sourceId);
		model.set<bool>(property,"Has Glow Texture",true);model.setLink(model.getIndex(property,"Glow Texture"),"Source",glowId);
		auto lightmap=model.insertNiBlock("NiSourceTexture");model.set<quint8>(lightmap,"Use External",1);model.set<QString>(lightmap,"File Name",lightmapPath);
		model.set<bool>(property,"Has Dark Texture",true);auto dark=model.getIndex(property,"Dark Texture");model.setLink(dark,"Source",model.getBlockNumber(lightmap));model.set<uint>(dark,"UV Set",1);
		auto decal=model.insertNiBlock("NiSourceTexture");model.set<quint8>(decal,"Use External",1);model.set<QString>(decal,"File Name",decalPath);
		model.set<uint>(property,"Texture Count",7);model.set<bool>(property,"Has Decal 0 Texture",true);model.setLink(model.getIndex(property,"Decal 0 Texture"),"Source",model.getBlockNumber(decal));
		model.set<uint>(mesh,"Num Properties",2);auto props=model.getIndex(mesh,"Properties");model.updateArray(props);
		model.setLink(props.child(0,0),materialId);model.setLink(props.child(1,0),propertyId);
		auto before=bytes(model);model.resetUndoHistory();
		QGLFormat format;format.setVersion(2,1);auto view=new GLView(format,nullptr);view->timer->stop();
		view->cfg.fov=45;view->cfg.upAxis=GLView::ZAxis;view->cfg.background=QColor(25,25,25);view->setNif(&model);
		view->scene->options=Scene::ShowHidden|Scene::DoTexturing|Scene::DoLighting|Scene::DoGlow|Scene::DoSpecular|Scene::DoVertexColors|Scene::DoBlending;
		view->scene->animate=false;view->animState=GLView::AnimDisabled;view->setProjection(false);view->setRotation(0,0,0);view->setPosition(0,0,0);view->setDistance(80);view->view=GLView::ViewUser;
		view->resize(640,480);view->show();QCoreApplication::processEvents();
		auto panel=new LightingWidget(view);auto checkbox=panel->findChild<QCheckBox *>("cbUnshaded");check(checkbox!=nullptr,"lighting settings expose the Unshaded checkbox");
		checkbox->setChecked(false);view->updateGL();view->updateGL();auto normalBefore=view->grabFrameBuffer();
		auto normalOptions=view->scene->options;auto normalVisMode=view->scene->visMode;
		checkbox->setChecked(true);QCoreApplication::processEvents();view->updateGL();view->updateGL();
		check(view->scene->options & Scene::Unshaded,"checkbox enables unshaded rendering on the viewport");
		auto sample=[view](const QImage & image,Vector3 position){QPointF point;check(view->projectGizmo(position,point),"project an unshaded texture sample");return image.pixelColor(point.toPoint());};
		auto image=view->grabFrameBuffer();auto color=sample(image,Vector3(-45,-4,0));
		std::printf("Unshaded RGB: %d,%d,%d\n",color.red(),color.green(),color.blue());
		check(std::abs(color.red()-75)<=3 && std::abs(color.green()-130)<=3 && std::abs(color.blue()-155)<=3,"black diffuse cannot blacken a base-textured mesh or its lightmap, translucent decal and glow layers in unshaded mode");
		for(int i=0;i<3;++i) {
			checkbox->setChecked(false);view->updateGL();view->updateGL();
			check(view->scene->options==normalOptions && view->scene->visMode==normalVisMode && view->grabFrameBuffer()==normalBefore,"turning unshaded off restores the exact normal image and preview flags");
			checkbox->setChecked(true);view->updateGL();view->updateGL();
		}
		view->setFrontalLight(true);view->setBrightness(1200);view->setAmbient(0);view->updateGL();view->updateGL();
		check(sample(view->grabFrameBuffer(),Vector3(-45,-4,0))==color,"unshaded color is unaffected by directional or ambient lighting strength");
		check(bytes(model)==before && model.undoStack->isClean(),"unshaded preview changes no NIF data or undo history");
		auto clamp=panel->findChild<QSlider *>("sldDetailBrightness");auto detailValue=panel->findChild<QSpinBox *>("sbDetailBrightness");
		check(clamp && detailValue && clamp->value()==200,"detail/lightmap brightness slider defaults to the original 200 percent");
		QString detailPath=directory.filePath("detail.bmp");texture.fill(QColor(255,255,255));check(texture.save(detailPath),"prepare a white detail lightmap");
		auto detailSource=model.insertNiBlock("NiSourceTexture");model.set<quint8>(detailSource,"Use External",1);model.set<QString>(detailSource,"File Name",detailPath);
		model.set<bool>(property,"Has Detail Texture",true);auto detail=model.getIndex(property,"Detail Texture");model.setLink(detail,"Source",model.getBlockNumber(detailSource));
		// Keep this fixture within four fixed-function texture units.
		model.setLink(model.getIndex(property,"Glow Texture"),"Source",-1);model.set<bool>(property,"Has Glow Texture",false);
		auto detailBefore=bytes(model);model.resetUndoHistory();view->updateGL();view->updateGL();auto boostedImage=view->grabFrameBuffer();auto boosted=sample(boostedImage,Vector3(-45,-4,0));
		std::printf("Detail 2x RGB: %d,%d,%d\n",boosted.red(),boosted.green(),boosted.blue());
		check(std::abs(boosted.red()-110)<=3 && std::abs(boosted.green()-140)<=3 && std::abs(boosted.blue()-140)<=3,"default detail compositing keeps its original 2x brightness");
		clamp->setValue(100);view->updateGL();view->updateGL();
		auto limited=sample(view->grabFrameBuffer(),Vector3(-45,-4,0));
		check(view->scene->detailBrightness==1.0f && std::abs(limited.red()-65)<=3 && std::abs(limited.green()-110)<=3 && std::abs(limited.blue()-125)<=3,"1x detail limit removes the white lightmap boost while preserving base, dark and decal layers");
		for(auto entry:{QPair<int,QColor>(150,QColor(88,125,133)),QPair<int,QColor>(50,QColor(43,95,118)),QPair<int,QColor>(0,QColor(20,80,110))}) {
			detailValue->setValue(entry.first);view->updateGL();view->updateGL();auto actual=sample(view->grabFrameBuffer(),Vector3(-45,-4,0));
			std::printf("Detail %d%% RGB: %d,%d,%d\n",entry.first,actual.red(),actual.green(),actual.blue());
			check(clamp->value()==entry.first && std::abs(actual.red()-entry.second.red())<=3 && std::abs(actual.green()-entry.second.green())<=3 && std::abs(actual.blue()-entry.second.blue())<=3,"fractional and zero detail brightness preserve the base, dark and decal texture stages");
		}
		clamp->setValue(200);view->updateGL();view->updateGL();
		check(view->grabFrameBuffer()==boostedImage,"disabling the lightmap limit restores the exact original detail rendering");
		check(bytes(model)==detailBefore && model.undoStack->isClean(),"lightmap brightness preview changes no NIF data or undo history");
		model.setLink(detail,"Source",-1);model.set<bool>(property,"Has Detail Texture",false);
		model.set<bool>(property,"Has Glow Texture",true);model.setLink(model.getIndex(property,"Glow Texture"),"Source",glowId);
		model.setArray<Vector2>(uvSets.child(1,0),QVector<Vector2>(4,Vector2(.75f,.5f)));view->updateGL();view->updateGL();
		auto darker=sample(view->grabFrameBuffer(),Vector3(-45,-4,0));
		check(std::abs(darker.red()-53)<=3 && std::abs(darker.green()-115)<=3 && std::abs(darker.blue()-148)<=3,"lightmap keeps its independent UV set in unshaded mode");
		auto alpha=model.insertNiBlock("NiAlphaProperty");model.set<quint16>(alpha,"Flags",237);
		model.set<uint>(mesh,"Num Properties",3);model.updateArray(props);model.setLink(props.child(2,0),model.getBlockNumber(alpha));
		model.setArray<Color4>(model.getIndex(data,"Vertex Colors"),QVector<Color4>(4,Color4(.1f,.2f,.1f,.5f)));view->updateGL();view->updateGL();
		auto transparent=sample(view->grabFrameBuffer(),Vector3(-45,-4,0));
		std::printf("Unshaded transparent RGB: %d,%d,%d; opaque: %d,%d,%d\n",transparent.red(),transparent.green(),transparent.blue(),darker.red(),darker.green(),darker.blue());
		check(std::abs(transparent.red()-(darker.red()+25)/2)<=3 && std::abs(transparent.green()-(darker.green()+25)/2)<=3 && std::abs(transparent.blue()-(darker.blue()+25)/2)<=3,"unshaded vertex RGB removal preserves vertex alpha transparency");
		view->scene->options&=~Scene::DoVertexColors;
		model.set<float>(material,"Alpha",.5f);view->updateGL();view->updateGL();
		auto materialAlpha=sample(view->grabFrameBuffer(),Vector3(-45,-4,0));
		check(std::abs(materialAlpha.red()-(darker.red()+25)/2)<=3 && std::abs(materialAlpha.green()-(darker.green()+25)/2)<=3 && std::abs(materialAlpha.blue()-(darker.blue()+25)/2)<=3,"base-textured meshes without vertex colors preserve material opacity independently of diffuse RGB");
		model.set<QString>(source,"File Name",decalPath);
		model.set<bool>(property,"Has Dark Texture",false);model.set<bool>(property,"Has Glow Texture",false);model.set<bool>(property,"Has Decal 0 Texture",false);view->updateGL();view->updateGL();
		auto decalColor=sample(view->grabFrameBuffer(),Vector3(-45,-4,0));
		check(std::abs(decalColor.red()-29)<=3 && std::abs(decalColor.green()-59)<=3 && std::abs(decalColor.blue()-74)<=3,"a decal mesh with black diffuse keeps its base texture RGB, texture alpha and material opacity");
		model.set<float>(material,"Alpha",1);model.set<Color3>(material,"Diffuse Color",Color3(.2f,.4f,.6f));model.setLink(props.child(1,0),-1);view->updateGL();view->updateGL();
		auto untextured=sample(view->grabFrameBuffer(),Vector3(-45,-4,0));
		std::printf("Unshaded untextured RGB: %d,%d,%d\n",untextured.red(),untextured.green(),untextured.blue());
		check(std::abs(untextured.red()-51)<=2 && std::abs(untextured.green()-102)<=2 && std::abs(untextured.blue()-153)<=2,"an untextured mesh still uses its own diffuse color in unshaded mode");
		QByteArray cutout=tga;for(int i=0;i<16;++i)cutout[18+i*4+3]=(i%4<2)?char(255):char(0);
		QString cutoutPath=directory.filePath("cutout.tga");QFile cutoutFile(cutoutPath);
		check(cutoutFile.open(QIODevice::WriteOnly) && cutoutFile.write(cutout)==cutout.size(),"prepare an alpha-tested texture with opaque and cutout regions");cutoutFile.close();
		auto cutoutSource=model.insertNiBlock("NiSourceTexture");model.set<quint8>(cutoutSource,"Use External",1);model.set<QString>(cutoutSource,"File Name",cutoutPath);
		auto cutoutProperty=model.insertNiBlock("NiTexturingProperty");model.set<bool>(cutoutProperty,"Has Base Texture",true);model.setLink(model.getIndex(cutoutProperty,"Base Texture"),"Source",model.getBlockNumber(cutoutSource));model.setLink(props.child(1,0),model.getBlockNumber(cutoutProperty));
		auto vertexColor=model.insertNiBlock("NiVertexColorProperty");model.set<uint>(mesh,"Num Properties",4);model.updateArray(props);model.setLink(props.child(3,0),model.getBlockNumber(vertexColor));
		model.set<quint16>(alpha,"Flags",4844);model.set<quint8>(alpha,"Threshold",5);
		model.setArray<Color4>(model.getIndex(data,"Vertex Colors"),QVector<Color4>(4,Color4(.1f,.2f,.1f,0)));view->scene->options|=Scene::DoVertexColors;
		auto setVertexMode=[&](uint mode) {
			if(model.checkVersion(0,0x14010001))model.set<uint>(vertexColor,"Vertex Mode",mode);
			else model.set<quint16>(vertexColor,"Flags",quint16(mode<<4));
		};
		for(uint mode:{0,1}) {
			setVertexMode(mode);view->updateGL();view->updateGL();auto masked=view->grabFrameBuffer();
			auto solid=sample(masked,Vector3(-50,-4,0)),hole=sample(masked,Vector3(-30,-4,0));
			check(std::abs(solid.red()-40)<=2 && std::abs(solid.green()-160)<=2 && std::abs(solid.blue()-220)<=2,"ignored or emissive zero vertex alpha cannot hide an alpha-tested mesh");
			check(hole==QColor(25,25,25),"unshaded preserves transparent texture cutouts and the original alpha threshold");
			checkbox->setChecked(false);view->updateGL();view->updateGL();auto normalMasked=view->grabFrameBuffer();
			check(sample(normalMasked,Vector3(-50,-4,0))!=QColor(25,25,25) && sample(normalMasked,Vector3(-30,-4,0))==QColor(25,25,25),"normal rendering has the same opaque and cutout coverage");checkbox->setChecked(true);
		}
		setVertexMode(2);view->updateGL();view->updateGL();
		check(sample(view->grabFrameBuffer(),Vector3(-50,-4,0))==QColor(25,25,25),"vertex alpha still controls cutouts when the material explicitly uses ambient/diffuse vertex colors");
		model.setLink(props.child(3,0),-1);view->updateGL();view->updateGL();auto defaultCutout=view->grabFrameBuffer();
		check(sample(defaultCutout,Vector3(-50,-4,0))!=QColor(25,25,25) && sample(defaultCutout,Vector3(-30,-4,0))==QColor(25,25,25),"a cutout-only material without vertex-color rules uses texture alpha instead of unused zero vertex alpha");
		model.set<bool>(cutoutProperty,"Has Detail Texture",true);model.setLink(model.getIndex(cutoutProperty,"Detail Texture"),"Source",model.getBlockNumber(detailSource));
		clamp->setValue(150);view->updateGL();view->updateGL();auto fractionalMask=view->grabFrameBuffer();auto fractionalSolid=sample(fractionalMask,Vector3(-50,-4,0));
		check(std::abs(fractionalSolid.red()-60)<=2 && std::abs(fractionalSolid.green()-240)<=2 && fractionalSolid.blue()==255 && sample(fractionalMask,Vector3(-30,-4,0))==QColor(25,25,25),"fractional detail brightness preserves alpha testing and transparent texture cutouts");
		model.setLink(props.child(3,0),model.getBlockNumber(vertexColor));setVertexMode(0);
		checkbox->setChecked(false);clamp->setValue(200);view->updateGL();view->updateGL();auto litOriginal=view->grabFrameBuffer();
		clamp->setValue(150);view->updateGL();view->updateGL();auto litFractional=view->grabFrameBuffer();
		check(sample(litFractional,Vector3(-50,-4,0))!=QColor(25,25,25) && sample(litFractional,Vector3(-30,-4,0))==QColor(25,25,25),"fractional brightness also preserves material lighting and alpha coverage in normal mode");
		clamp->setValue(200);view->updateGL();view->updateGL();check(view->grabFrameBuffer()==litOriginal,"200 percent restores the exact normal lit rendering after fractional adjustment");
		view->grabFrameBuffer().save(QCoreApplication::applicationDirPath()+"/unshaded-preview.png");
		checkbox->setChecked(false);check(!(view->scene->options & Scene::Unshaded),"turning Unshaded off restores the normal rendering path");
		delete panel;delete view;
		auto window=new NifSkope;auto option=window->findChild<QCheckBox *>("cbUnshaded");
		check(option!=nullptr,"main-window lighting popover contains the unshaded option");
		option->setChecked(true);check(window->findChild<GLView *>("OGL1")->getScene()->options & Scene::Unshaded,"main-window checkbox enables the unshaded texture preview");
		option->setChecked(false);
		for(const char * name:{"aVisNormals","aSilhouette"}) {
			auto action=window->findChild<QAction *>(name);check(action!=nullptr,"find existing preview action");action->setChecked(true);
			auto viewport=window->findChild<GLView *>("OGL1");auto savedOptions=viewport->getScene()->options;auto savedVisMode=viewport->getScene()->visMode;
			option->setChecked(true);option->setChecked(false);
			check(action->isChecked() && viewport->getScene()->options==savedOptions && viewport->getScene()->visMode==savedVisMode,"unshaded toggle preserves pre-existing lighting-only and silhouette settings");action->setChecked(false);
		}
		delete window;
		QSettings migration;migration.remove("GLView/DetailBrightness");migration.setValue("GLView/ClampDetailBrightness",true);
		auto migrated=new NifSkope;check(migrated->findChild<QSlider *>("sldDetailBrightness")->value()==100,"slider carries over an enabled legacy brightness-limit checkbox");delete migrated;
		migration.setValue("GLView/DetailBrightness",200);migration.setValue("GLView/ClampDetailBrightness",false);
	}
};
