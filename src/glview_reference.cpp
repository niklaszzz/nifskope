#include "glview.h"
#include "model/nifmodel.h"
#include "gl/gltex.h"
#include "gl/glnode.h"
#include "gl/renderer.h"
#include <QOpenGLFramebufferObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

struct GLView::ReferenceLayer {
	QPointer<NifModel> model;
	Scene * scene = nullptr;
	TexCache * textures = nullptr;
	bool dirty = true;
	QVector<QMetaObject::Connection> connections;
};

void GLView::clearReferences()
{
	if(!referenceLayers.isEmpty())makeCurrent();
	for(const auto & layer:referenceLayers) {
		for(auto connection:layer->connections)disconnect(connection);
		delete layer->scene;delete layer->textures;
	}
	referenceLayers.clear();
	referenceBuffer.reset();
}

void GLView::setReferenceModels(const QList<NifModel *> & models)
{
	bool same=referenceLayers.size()==models.size();
	if(same)for(int i=0;i<models.size();++i)if(referenceLayers[i]->model!=models[i]){same=false;break;}
	if(same){update();return;}
	clearReferences();
	for(auto reference:models) {
		if(!reference)continue;
		auto layer=std::make_shared<ReferenceLayer>();layer->model=reference;
		auto changed=[this,layer](){layer->dirty=true;update();};
		layer->connections<<connect(reference,&NifModel::dataChanged,this,changed);
		layer->connections<<connect(reference,&NifModel::linksChanged,this,changed);
		layer->connections<<connect(reference,&NifModel::modelReset,this,changed);
		layer->connections<<connect(reference,&NifModel::destroyed,this,[this](){update();});
		referenceLayers<<layer;
	}
	update();
}

void GLView::drawReferences()
{
	if(scene->isolatedRoot.isValid())return;
	if(!referencesVisible || referenceOpacity<=0 || referenceLayers.isEmpty() || Node::SELECTING)return;
	QOpenGLFramebufferObjectFormat format;format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);format.setInternalTextureFormat(GL_RGBA8);
	GLint viewport[4],framebuffer=0;glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
	if(viewport[2]<=0 || viewport[3]<=0)return;
	QSize size(viewport[2],viewport[3]);
	if(!referenceBuffer || referenceBuffer->size()!=size)referenceBuffer.reset(new QOpenGLFramebufferObject(size,format));
	auto & buffer=*referenceBuffer;if(!buffer.isValid())return;
	for(const auto & layer:referenceLayers) {
		if(!layer->model || layer->model==model)continue;
		glPushAttrib(GL_ALL_ATTRIB_BITS);glMatrixMode(GL_MODELVIEW);glPushMatrix();glMatrixMode(GL_PROJECTION);glPushMatrix();
		if(!layer->scene){layer->textures=new TexCache(this);layer->scene=new Scene(layer->textures,glContext,glFuncs);layer->scene->animate=false;}
		layer->scene->options=scene->options & ~(Scene::ShowNodes|Scene::ShowMarkers|Scene::ShowCollision);
		layer->scene->detailBrightness=scene->detailBrightness;
		// Collision authoring proxies are often hidden in the game file.
		if(PhysXCollision::hasCollision(layer->model))layer->scene->options|=Scene::ShowHidden;
		layer->scene->lodLevel=scene->lodLevel;
		if(layer->dirty){layer->textures->setNifFolder(layer->model->getFolder());layer->scene->make(layer->model);layer->dirty=false;}
		layer->scene->transform(viewTrans,layer->scene->timeMin());
		scene->renderer->stopProgram();buffer.bind();glViewport(0,0,viewport[2],viewport[3]);
		glClearColor(0,0,0,0);glDepthMask(GL_TRUE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
		glMatrixMode(GL_MODELVIEW);glLoadIdentity();
		glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);layer->scene->drawShapes();layer->scene->renderer->stopProgram();
		glFuncs->glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
		glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_LIGHTING);glDisable(GL_FOG);glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		glFuncs->glActiveTexture(GL_TEXTURE0);glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,buffer.texture());glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
		glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,1,0,1,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glColor4f(1,1,1,referenceOpacity);
		glBegin(GL_QUADS);
		glTexCoord2f(0,0);glVertex2f(0,0);glTexCoord2f(1,0);glVertex2f(1,0);glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(0,1);
		glEnd();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);glPopMatrix();glPopAttrib();
	}
}

