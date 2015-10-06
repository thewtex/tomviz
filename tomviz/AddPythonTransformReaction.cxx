/******************************************************************************

  This source file is part of the tomviz project.

  Copyright Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/
#include "AddPythonTransformReaction.h"

#include "ActiveObjects.h"
#include "DataSource.h"
#include "OperatorPython.h"
#include "pqCoreUtilities.h"
#include "EditOperatorDialog.h"

#include <vtkImageData.h>
#include <vtkSMSourceProxy.h>
#include <vtkTrivialProducer.h>

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>

namespace
{
class SelectSliceRangeWidget : public QWidget
{
  Q_OBJECT
  typedef QWidget Superclass;
public:
  SelectSliceRangeWidget(int *ext, bool showAxisSelector = true, QWidget* p = NULL)
    : Superclass(p),
      firstSlice(new QSpinBox(this)),
      lastSlice(new QSpinBox(this)),
      axisSelect(new QComboBox(this))
  {
    for (int i = 0; i < 6; ++i)
    {
      this->Extent[i] = ext[i];
    }
    QHBoxLayout *rangeLayout =  new QHBoxLayout;
    QLabel *startLabel = new QLabel("Start:", this);
    QLabel *endLabel = new QLabel("End:", this);

    this->firstSlice->setRange(0, ext[5] - ext[4]);
    this->firstSlice->setValue(0);
    this->lastSlice->setRange(0, ext[5] - ext[4]);
    this->lastSlice->setValue(0);

    rangeLayout->addWidget(startLabel);
    rangeLayout->addWidget(this->firstSlice);
    rangeLayout->addWidget(endLabel);
    rangeLayout->addWidget(this->lastSlice);

    QHBoxLayout *axisSelectLayout = new QHBoxLayout;
    QLabel *axisLabel = new QLabel("Axis:", this);
    this->axisSelect->addItem("X");
    this->axisSelect->addItem("Y");
    this->axisSelect->addItem("Z");
    this->axisSelect->setCurrentIndex(2);

    axisSelectLayout->addWidget(axisLabel);
    axisSelectLayout->addWidget(this->axisSelect);

    QVBoxLayout *widgetLayout = new QVBoxLayout;
    widgetLayout->addLayout(rangeLayout);
    widgetLayout->addLayout(axisSelectLayout);

    if (!showAxisSelector)
    {
      axisLabel->hide();
      this->axisSelect->hide();
    }

    this->setLayout(widgetLayout);

    this->connect(this->firstSlice, SIGNAL(valueChanged(int)),
                  SLOT(onMinimumChanged(int)));
    this->connect(this->lastSlice, SIGNAL(valueChanged(int)),
                  SLOT(onMaximumChanged(int)));
    this->connect(axisSelect, SIGNAL(currentIndexChanged(int)),
                  SLOT(onAxisChanged(int)));
  }
  ~SelectSliceRangeWidget() {}

  int startSlice() const
  {
    return this->firstSlice->value();
  }
  int endSlice() const
  {
    return this->lastSlice->value();
  }
  int axis() const
  {
    return this->axisSelect->currentIndex();
  }

public slots:
  void onMinimumChanged(int val)
  {
    if (this->lastSlice->value() < val)
    {
      this->lastSlice->setValue(val);
    }
  }

  void onMaximumChanged(int val)
  {
    if (this->firstSlice->value() > val)
    {
      this->firstSlice->setValue(val);
    }
  }

  void onAxisChanged(int newAxis)
  {
    int newSliceMax = this->Extent[2 * newAxis + 1] - this->Extent[2 * newAxis];
    this->firstSlice->setMaximum(newSliceMax);
    this->lastSlice->setMaximum(newSliceMax);
  }

private:
  Q_DISABLE_COPY(SelectSliceRangeWidget)
  QSpinBox *firstSlice; // delete slices starting at this slice index
  QSpinBox *lastSlice; // delete slices ending at this slice index
  QComboBox *axisSelect;
  int Extent[6];
};
}

#include "AddPythonTransformReaction.moc"

namespace tomviz
{
//-----------------------------------------------------------------------------
AddPythonTransformReaction::AddPythonTransformReaction(QAction* parentObject,
                                               const QString &l,
                                               const QString &s,
                                               bool rts,
                                               bool rv)
  : Superclass(parentObject), scriptLabel(l), scriptSource(s),
    interactive(false), requiresTiltSeries(rts), requiresVolume(rv)
{
  connect(&ActiveObjects::instance(), SIGNAL(dataSourceChanged(DataSource*)),
          SLOT(updateEnableState()));
  updateEnableState();
}

//-----------------------------------------------------------------------------
AddPythonTransformReaction::~AddPythonTransformReaction()
{
}

//-----------------------------------------------------------------------------
void AddPythonTransformReaction::updateEnableState()
{
  bool enable = ActiveObjects::instance().activeDataSource() != NULL;
  if (enable && this->requiresTiltSeries)
  {
    enable = ActiveObjects::instance().activeDataSource()->type() == DataSource::TiltSeries;
  }
  if (enable && this->requiresVolume)
  {
    enable = ActiveObjects::instance().activeDataSource()->type() == DataSource::Volume;
  }
  parentAction()->setEnabled(enable);
}


//-----------------------------------------------------------------------------
OperatorPython* AddPythonTransformReaction::addExpression(DataSource* source)
{
  source = source ? source : ActiveObjects::instance().activeDataSource();
  if (!source)
  {
    return NULL;
  }

  OperatorPython *opPython = new OperatorPython();
  QSharedPointer<Operator> op(opPython);
  opPython->setLabel(scriptLabel);
  opPython->setScript(scriptSource);

  // Shift uniformly, crop, both have custom gui
  if (scriptLabel == "Shift Uniformly")
  {
    vtkTrivialProducer *t = vtkTrivialProducer::SafeDownCast(
      source->producer()->GetClientSideObject());
    vtkImageData *data = vtkImageData::SafeDownCast(t->GetOutputDataObject(0));
    int *extent = data->GetExtent();

    QDialog dialog(pqCoreUtilities::mainWidget());
    QHBoxLayout *layout = new QHBoxLayout;
    QLabel *label = new QLabel("Shift to apply:", &dialog);
    layout->addWidget(label);
    QSpinBox *spinx = new QSpinBox(&dialog);
    spinx->setRange(-(extent[1]-extent[0]), extent[1]-extent[0]);
    spinx->setValue(0);
    QSpinBox *spiny = new QSpinBox(&dialog);
    spiny->setRange(-(extent[3]-extent[2]), extent[3]-extent[2]);
    spiny->setValue(0);
    QSpinBox *spinz = new QSpinBox(&dialog);
    spinz->setRange(-(extent[5]-extent[4]), extent[5]-extent[4]);
    spinz->setValue(0);
    layout->addWidget(spinx);
    layout->addWidget(spiny);
    layout->addWidget(spinz);
    QVBoxLayout *v = new QVBoxLayout;
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                     | QDialogButtonBox::Cancel,
                                                     Qt::Horizontal,
                                                     &dialog);
    connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
    v->addLayout(layout);
    v->addWidget(buttons);
    dialog.setLayout(v);

    if (dialog.exec() == QDialog::Accepted)
    {
      QString shiftScript = scriptSource;
      shiftScript.replace("###SHIFT###",
                          QString("SHIFT = [%1, %2, %3]").arg(spinx->value())
                          .arg(spiny->value()).arg(spinz->value()));
      opPython->setScript(shiftScript);
      source->addOperator(op);
    }
  }
  else if (scriptLabel == "Crop")
  {
    vtkTrivialProducer *t = vtkTrivialProducer::SafeDownCast(
      source->producer()->GetClientSideObject());
    vtkImageData *data = vtkImageData::SafeDownCast(t->GetOutputDataObject(0));
    int *extent = data->GetExtent();

    QDialog dialog(pqCoreUtilities::mainWidget());
    QHBoxLayout *layout1 = new QHBoxLayout;
    QLabel *label = new QLabel("Crop data start:", &dialog);
    layout1->addWidget(label);
    QSpinBox *spinx = new QSpinBox(&dialog);
    spinx->setRange(extent[0], extent[1]);
    spinx->setValue(extent[0]);
    QSpinBox *spiny = new QSpinBox(&dialog);
    spiny->setRange(extent[2], extent[3]);
    spiny->setValue(extent[2]);
    QSpinBox *spinz = new QSpinBox(&dialog);
    spinz->setRange(extent[4], extent[5]);
    spinz->setValue(extent[4]);
    layout1->addWidget(label);
    layout1->addWidget(spinx);
    layout1->addWidget(spiny);
    layout1->addWidget(spinz);
    QHBoxLayout *layout2 = new QHBoxLayout;
    label = new QLabel("Crop data end:", &dialog);
    layout2->addWidget(label);
    QSpinBox *spinxx = new QSpinBox(&dialog);
    spinxx->setRange(extent[0], extent[1]);
    spinxx->setValue(extent[1]);
    QSpinBox *spinyy = new QSpinBox(&dialog);
    spinyy->setRange(extent[2], extent[3]);
    spinyy->setValue(extent[3]);
    QSpinBox *spinzz = new QSpinBox(&dialog);
    spinzz->setRange(extent[4], extent[5]);
    spinzz->setValue(extent[5]);
    layout2->addWidget(label);
    layout2->addWidget(spinxx);
    layout2->addWidget(spinyy);
    layout2->addWidget(spinzz);
    QVBoxLayout *v = new QVBoxLayout;
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                     | QDialogButtonBox::Cancel,
                                                     Qt::Horizontal,
                                                     &dialog);
    connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
    v->addLayout(layout1);
    v->addLayout(layout2);
    v->addWidget(buttons);
    dialog.setLayout(v);

    if (dialog.exec() == QDialog::Accepted)
    {
      QString cropScript = scriptSource;
      cropScript.replace("###START_CROP###",
                          QString("START_CROP = [%1, %2, %3]").arg(spinx->value())
                          .arg(spiny->value()).arg(spinz->value()));
      cropScript.replace("###END_CROP###",
                          QString("END_CROP = [%1, %2, %3]").arg(spinxx->value())
                          .arg(spinyy->value()).arg(spinzz->value()));
      opPython->setScript(cropScript);
      source->addOperator(op);
    }
  }
  else if (scriptLabel == "Rotate")
  {
    QDialog dialog(pqCoreUtilities::mainWidget());
    QHBoxLayout *layout1 = new QHBoxLayout;
    QLabel *label = new QLabel("Rotate Angle:", &dialog);
    layout1->addWidget(label);
    QDoubleSpinBox *angle = new QDoubleSpinBox(&dialog);
    angle->setRange(0, 360);
    angle->setValue(90);
    layout1->addWidget(label);
    layout1->addWidget(angle);
    QHBoxLayout *layout2 = new QHBoxLayout;
    label = new QLabel("Rotate Axis:", &dialog);
    layout2->addWidget(label);
    QComboBox *axis = new QComboBox(&dialog);
    axis->addItem("X");
    axis->addItem("Y");
    axis->addItem("Z");
    axis->setCurrentIndex(2);
    layout2->addWidget(label);
    layout2->addWidget(axis);
    QVBoxLayout *v = new QVBoxLayout;
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                     | QDialogButtonBox::Cancel,
                                                     Qt::Horizontal,
                                                     &dialog);
    connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
    v->addLayout(layout1);
    v->addLayout(layout2);
    v->addWidget(buttons);
    dialog.setLayout(v);
    
    if (dialog.exec() == QDialog::Accepted)
    {
      QString cropScript = scriptSource;
      cropScript.replace("###ROT_AXIS###",
                         QString("ROT_AXIS = %1").arg(axis->currentIndex()) );
      cropScript.replace("###ROT_ANGLE###",
                         QString("ROT_ANGLE = %1").arg(angle->value()) );
      opPython->setScript(cropScript);
      source->addOperator(op);
    }
  }
    
  else if (scriptLabel == "Delete Slices")
  {
    vtkTrivialProducer *t = vtkTrivialProducer::SafeDownCast(
      source->producer()->GetClientSideObject());
    vtkImageData *data = vtkImageData::SafeDownCast(t->GetOutputDataObject(0));
    int *shape = data->GetExtent();

    QDialog dialog(pqCoreUtilities::mainWidget());

    SelectSliceRangeWidget *sliceRange = new SelectSliceRangeWidget(shape, true, &dialog);
    
    QVBoxLayout *v = new QVBoxLayout;
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                     | QDialogButtonBox::Cancel,
                                                     Qt::Horizontal,
                                                     &dialog);
    connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
    v->addWidget(sliceRange);
    v->addWidget(buttons);
    dialog.setLayout(v);
    dialog.setWindowTitle("Delete Slices");
    dialog.layout()->setSizeConstraint(QLayout::SetFixedSize); //Make the UI non-resizeable
    
    if (dialog.exec() == QDialog::Accepted)
    {
      QString deleteSlicesScript = scriptSource;
      deleteSlicesScript.replace("###firstSlice###",
                         QString("firstSlice = %1").arg(sliceRange->startSlice()) );
      deleteSlicesScript.replace("###lastSlice###",
                         QString("lastSlice = %1").arg(sliceRange->endSlice()) );
      deleteSlicesScript.replace("###axis###",
                         QString("axis = %1").arg(sliceRange->axis()) );

      opPython->setScript(deleteSlicesScript);
      source->addOperator(op);
    }
  }

  else if (scriptLabel == "Sobel Filter") //UI for Sobel Filter
  {
    QDialog dialog(pqCoreUtilities::mainWidget());
    QHBoxLayout *layout = new QHBoxLayout;
    QLabel *label = new QLabel("Axis:", &dialog);
    layout->addWidget(label);
    QComboBox *axis = new QComboBox(&dialog);
    axis->addItem("X");
    axis->addItem("Y");
    axis->addItem("Z");
    axis->setCurrentIndex(2);
    layout->addWidget(label);
    layout->addWidget(axis);
    QVBoxLayout *v = new QVBoxLayout;
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                     | QDialogButtonBox::Cancel,
                                                     Qt::Horizontal,
                                                     &dialog);
    connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
    v->addLayout(layout);
    v->addWidget(buttons);
    dialog.setLayout(v);
    
    if (dialog.exec() == QDialog::Accepted)
    {
      QString cropScript = scriptSource;
      cropScript.replace("###Filter_AXIS###",
                         QString("Filter_AXIS = %1").arg(axis->currentIndex()) );
      opPython->setScript(cropScript);
      source->addOperator(op);
    }
  }
    
  else if (scriptLabel == "Resample")
  {
    QDialog dialog(pqCoreUtilities::mainWidget());
    QGridLayout *layout = new QGridLayout;
    //Add labels
    QLabel *labelx = new QLabel("x:");
    layout->addWidget(labelx,0,1,1,1,Qt::AlignCenter);
    QLabel *labely = new QLabel("y:");
    layout->addWidget(labely,0,2,1,1,Qt::AlignCenter);
    QLabel *labelz = new QLabel("z:");
    layout->addWidget(labelz,0,3,1,1,Qt::AlignCenter);
    QLabel *label = new QLabel("Scale Factors:");
    layout->addWidget(label,1,0,1,1);
      
    QDoubleSpinBox *spinx = new QDoubleSpinBox;
    spinx->setSingleStep(0.5);
    spinx->setValue(1);
    
    QDoubleSpinBox *spiny = new QDoubleSpinBox;
    spiny->setSingleStep(0.5);
    spiny->setValue(1);
    
    QDoubleSpinBox *spinz = new QDoubleSpinBox;
    spinz->setSingleStep(0.5);
    spinz->setValue(1);
    
    layout->addWidget(spinx,1,1,1,1);
    layout->addWidget(spiny,1,2,1,1);
    layout->addWidget(spinz,1,3,1,1);
    
    QVBoxLayout *v = new QVBoxLayout;
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                     | QDialogButtonBox::Cancel);
    connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
    v->addLayout(layout);
    v->addWidget(buttons);
    dialog.setLayout(v);
    
    if (dialog.exec() == QDialog::Accepted)
    {
      QString shiftScript = scriptSource;
      shiftScript.replace("###resampingFactor###",
                          QString("resampingFactor = [%1, %2, %3]").arg(spinx->value())
                          .arg(spiny->value()).arg(spinz->value()));
      opPython->setScript(shiftScript);
      source->addOperator(op);
    }
  }
  else if (scriptLabel == "Generate Tilt Series")
  {
      
      QDialog dialog(pqCoreUtilities::mainWidget());
      dialog.setWindowTitle("Generate Tilt Series");
      
      QGridLayout *layout = new QGridLayout;
      
      QLabel *label = new QLabel("Generate electron tomography tilt series from volume dataset:");
      label->setWordWrap(true);
      layout->addWidget(label,0,0,1,2);
      
      label = new QLabel("Start Angle:");
      layout->addWidget(label,1,0,1,1);
      
      QDoubleSpinBox *startAngle = new QDoubleSpinBox;
      startAngle->setSingleStep(1);
      startAngle->setValue(0);
      startAngle->setRange(-180,180);
      layout->addWidget(startAngle,1,1,1,1);

      label = new QLabel("Angle Increment:");
      layout->addWidget(label,2,0,1,1);
      
      QDoubleSpinBox *angleIncrement = new QDoubleSpinBox;
      angleIncrement->setSingleStep(0.5);
      angleIncrement->setValue(6);
      angleIncrement->setRange(-180,180);
      layout->addWidget(angleIncrement,2,1,1,1);

      label = new QLabel("Number of Tilts:");
      layout->addWidget(label,3,0,1,1);
      
      QSpinBox *numberOfTilts = new QSpinBox;
      numberOfTilts->setSingleStep(1);
      numberOfTilts->setValue(30);
      layout->addWidget(numberOfTilts,3,1,1,1);

      QVBoxLayout *v = new QVBoxLayout;
      QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                       | QDialogButtonBox::Cancel);
      connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
      connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
      
      v->addLayout(layout);
      v->addWidget(buttons);
      dialog.setLayout(v);
      dialog.layout()->setSizeConstraint(QLayout::SetFixedSize); //Make the UI non-resizeable
      if (dialog.exec() == QDialog::Accepted)
      {
          QString pythonScript = scriptSource;
          pythonScript.replace("###startAngle###",
                              QString("startAngle = %1").arg(startAngle->value()));
          pythonScript.replace("###angleIncrement###",
                               QString("angleIncrement = %1").arg(angleIncrement->value()));
          pythonScript.replace("###Nproj###",
                               QString("Nproj = %1").arg(numberOfTilts->value()));
          opPython->setScript(pythonScript);
          source->addOperator(op);
      }
      

  }
  else if (interactive)
  {
    // Create a non-modal dialog, delete it once it has been closed.
    EditOperatorDialog *dialog =
        new EditOperatorDialog(op, source, pqCoreUtilities::mainWidget());
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->show();
  }
  else
  {
    source->addOperator(op);
  }
  return NULL;
}

}
