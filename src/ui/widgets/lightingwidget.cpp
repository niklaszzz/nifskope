#include "lightingwidget.h"

#include <QAction>
#include <QCheckBox>
#include <QSettings>
#include <QLabel>
#include <QSpinBox>
#include <QHBoxLayout>
#include "ui_lightingwidget.h"

#include "glview.h"


// Slider lambda
auto sld = []( QSlider * slider, int min, int max, int val ) {
	slider->setSizePolicy( QSizePolicy::MinimumExpanding, QSizePolicy::Maximum );
	slider->setRange( min, max );
	slider->setSingleStep( max / 4 );
	slider->setTickInterval( max / 2 );
	slider->setTickPosition( QSlider::TicksBelow );
	slider->setValue( val );
};

LightingWidget::LightingWidget( GLView * ogl, QWidget * parent ) : QWidget(parent),
    ui(new Ui::LightingWidget)
{
    ui->setupUi(this);
	unshaded=new QCheckBox(tr("Unshaded geometry (keep textures)"),this);
	unshaded->setObjectName("cbUnshaded");
	unshaded->setToolTip(tr("Remove geometry lighting, normal-map shading, specular and vertex RGB shading. Ignore material diffuse RGB when a base texture is present. Keep texture layers, lightmaps, decals, glow and transparency. This changes only the preview."));
	ui->verticalLayout->insertWidget(1,unshaded);
	QSettings settings;unshaded->setChecked(settings.value("GLView/Unshaded",false).toBool());
	connect(unshaded,&QCheckBox::toggled,ogl,&GLView::setUnshaded);
	ogl->setUnshaded(unshaded->isChecked());
	auto detailRow=new QWidget(this);auto detailLayout=new QHBoxLayout(detailRow);detailLayout->setContentsMargins(0,0,0,0);
	detailLayout->addWidget(new QLabel(tr("Lightmap/detail brightness"),detailRow));
	auto detailSlider=new QSlider(Qt::Horizontal,detailRow);detailSlider->setObjectName("sldDetailBrightness");detailSlider->setRange(0,200);detailSlider->setPageStep(10);
	auto detailValue=new QSpinBox(detailRow);detailValue->setObjectName("sbDetailBrightness");detailValue->setRange(0,200);detailValue->setSuffix("%");
	detailRow->setToolTip(tr("Preview brightness for the Detail/lightmap slot: 100% is the previous 1x limit; 200% restores the original 2x brightness. Other texture slots and alpha are unchanged. Saved/exported NIF data is unchanged."));
	detailLayout->addWidget(detailSlider,1);detailLayout->addWidget(detailValue);ui->verticalLayout->insertWidget(2,detailRow);
	int detailPercent=qBound(0,settings.value("GLView/DetailBrightness",settings.value("GLView/ClampDetailBrightness",false).toBool()?100:200).toInt(),200);
	detailSlider->setValue(detailPercent);detailValue->setValue(detailPercent);
	connect(detailSlider,&QSlider::valueChanged,detailValue,&QSpinBox::setValue);
	connect(detailValue,QOverload<int>::of(&QSpinBox::valueChanged),detailSlider,&QSlider::setValue);
	connect(detailSlider,&QSlider::valueChanged,ogl,&GLView::setDetailBrightness);ogl->setDetailBrightness(detailPercent);

	setDefaults();

	ui->sldDeclination->setDisabled( ui->btnFrontal->isChecked() );
	ui->sldPlanarAngle->setDisabled( ui->btnFrontal->isChecked() );

	// Disable lighting sliders when Frontal
	connect( ui->btnFrontal, &QToolButton::toggled, ui->sldDeclination, &QSlider::setDisabled );
	connect( ui->btnFrontal, &QToolButton::toggled, ui->sldPlanarAngle, &QSlider::setDisabled );

	// Disable Frontal checkbox (and sliders) when no lighting
	connect( ui->btnLighting, &QToolButton::toggled, ui->btnFrontal, &QToolButton::setEnabled );
	connect( ui->btnLighting, &QToolButton::toggled, [&]( bool checked ) {
		if ( !ui->btnFrontal->isChecked() ) {
			// Don't enable the sliders if Frontal is checked
			ui->sldDeclination->setEnabled( checked );
			ui->sldPlanarAngle->setEnabled( checked );
		}
	} );

	// Inform ogl of changes
	connect( ui->sldDirectional, &QSlider::valueChanged, ogl, &GLView::setBrightness );
	connect( ui->sldAmbient, &QSlider::valueChanged, ogl, &GLView::setAmbient );
	connect( ui->sldDeclination, &QSlider::valueChanged, ogl, &GLView::setDeclination );
	connect( ui->sldPlanarAngle, &QSlider::valueChanged, ogl, &GLView::setPlanarAngle );
	connect( ui->btnFrontal, &QToolButton::toggled, ogl, &GLView::setFrontalLight );
}

LightingWidget::~LightingWidget()
{
}

void LightingWidget::setDefaults()
{
	sld( ui->sldDirectional, DirMin, DirMax, DirDefault );
	sld( ui->sldAmbient, AmbientMin, AmbientMax, AmbientDefault );
	sld( ui->sldDeclination, DeclinationMin, DeclinationMax, DeclinationDefault );
	sld( ui->sldPlanarAngle, PlanarAngleMin, PlanarAngleMax, PlanarAngleDefault );
}

void LightingWidget::setActions( QVector<QAction *> atns )
{
	ui->btnLighting->setDefaultAction( atns.value(0) );
	ui->btnTextures->setDefaultAction( atns.value(1) );
	ui->btnVertexColors->setDefaultAction( atns.value(2) );
	ui->btnSpecular->setDefaultAction( atns.value(3) );
	ui->btnCubemap->setDefaultAction( atns.value(4) );
	ui->btnGlow->setDefaultAction( atns.value(5) );
	ui->btnLightingOnly->setDefaultAction( atns.value(6) );
	ui->btnSilhouette->setDefaultAction( atns.value(7) );

	connect( ui->btnLighting, &QToolButton::toggled, atns.value(3), &QAction::setEnabled );
	auto controls=[this,atns](bool enabled) {
		for(auto button:{ui->btnLighting,ui->btnVertexColors,ui->btnSpecular,ui->btnCubemap,ui->btnLightingOnly,ui->btnSilhouette})button->setEnabled(!enabled);
		ui->btnFrontal->setEnabled(!enabled && ui->btnLighting->isChecked());
		ui->sldDirectional->setEnabled(!enabled);ui->sldAmbient->setEnabled(!enabled);
		ui->sldDeclination->setEnabled(!enabled && ui->btnLighting->isChecked() && !ui->btnFrontal->isChecked());
		ui->sldPlanarAngle->setEnabled(!enabled && ui->btnLighting->isChecked() && !ui->btnFrontal->isChecked());
		ui->btnSpecular->setEnabled(!enabled && ui->btnLighting->isChecked());
		for(int i:{6,7})if(atns.value(i)) {
			atns[i]->setEnabled(!enabled);
		}
	};
	connect(unshaded,&QCheckBox::toggled,this,controls);controls(unshaded->isChecked());
}
