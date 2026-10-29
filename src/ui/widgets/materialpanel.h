#ifndef MATERIALPANEL_H
#define MATERIALPANEL_H
#include <QWidget>
#include <QPointer>
#include <QPersistentModelIndex>
#include <QColor>
class NifModel;
class QComboBox;
class QLabel;
class QLineEdit;
class QDoubleSpinBox;
class QPushButton;
class QTimer;

class MaterialPanel final : public QWidget {
 Q_OBJECT
public:
 explicit MaterialPanel(QWidget * parent=nullptr);
 void setTarget(NifModel * model,const QModelIndex & mesh);
 static int assignedMaterial(const NifModel * model,const QModelIndex & mesh);
 static int createMaterial(NifModel * model,const QString & name=QString());
 static bool assignMaterial(NifModel * model,const QModelIndex & mesh,int material,QString * reason=nullptr);
private:
 void refresh();
 void readFields();
 void apply();
 void readTextures();
 void applyTextures();
 QPointer<NifModel> model;
 QPersistentModelIndex mesh;
 QVector<QMetaObject::Connection> connections;
 QComboBox * materials;
 QLabel * targetLabel,* usageLabel,* textureLabel;
 QLineEdit * name;
 QLineEdit * texturePaths[12];
 QString originalPaths[12];
 QPushButton * textureBrowse[12];
 QDoubleSpinBox * opacity,* glossiness;
 QPushButton * colors[4],* assign,* create,* unique,* save;
 QColor values[4];
 QTimer * refreshTimer;
 int restoreMesh=-1,preferred=-1,lastAssigned=-1;
 bool applying=false;
};
#endif
