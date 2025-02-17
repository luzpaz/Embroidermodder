#include "embroidermodder.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QStatusBar>
#include <QColor>
#include <QUndoStack>

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsItem>

extern "C" {
#include "embroidery.h"
}
MdiWindow::MdiWindow(const int theIndex, MainWindow* mw, QMdiArea* parent, Qt::WindowFlags wflags) : QMdiSubWindow(parent, wflags)
{
    mainWin = mw;
    mdiArea = parent;

    myIndex = theIndex;

    fileWasLoaded = false;

    setAttribute(Qt::WA_DeleteOnClose);

    QString aName;
    curFile = aName.asprintf("Untitled%d.dst", myIndex);
    this->setWindowTitle(curFile);

    this->setWindowIcon(QIcon("icons/" + mainWin->getSettingsGeneralIconTheme() + "/" + "app" + ".png"));

    gscene = new QGraphicsScene(0,0,0,0, this);
    gview = new View(mainWin, gscene, this);

    setWidget(gview);

    //WARNING: DO NOT SET THE QMDISUBWINDOW (this) FOCUSPROXY TO THE PROMPT
    //WARNING: AS IT WILL CAUSE THE WINDOW MENU TO NOT SWITCH WINDOWS PROPERLY!
    //WARNING: ALTHOUGH IT SEEMS THAT SETTING INTERNAL WIDGETS FOCUSPROXY IS OK.
    gview->setFocusProxy(mainWin->prompt);

    resize(sizeHint());

    promptHistory = "Welcome to Embroidermodder 2!<br/>Open some of our sample files. Many formats are supported.<br/>For help, press F1.";
    mainWin->prompt->setHistory(promptHistory);
    promptInputList << "";
    promptInputNum = 0;

    curLayer = "0";
    curColor = 0; //TODO: color ByLayer
    curLineType = "ByLayer";
    curLineWeight = "ByLayer";

    // Due to strange Qt4.2.3 feature the child window icon is not drawn
    // in the main menu if showMaximized() is called for a non-visible child window
    // Therefore calling show() first...
    show();
    showMaximized();

    setFocusPolicy(Qt::WheelFocus);
    setFocus();

    onWindowActivated();
}

MdiWindow::~MdiWindow()
{
    qDebug("MdiWindow Destructor()");
}

bool MdiWindow::saveFile(const QString &fileName)
{
    SaveObject saveObj(gscene, this);
    return saveObj.save(fileName);
}

bool MdiWindow::loadFile(const QString &fileName)
{
    qDebug("MdiWindow loadFile()");

    QRgb tmpColor = getCurrentColor();

    QFile file(fileName);
    if(!file.open(QFile::ReadOnly | QFile::Text))
    {
        QMessageBox::warning(this, tr("Error reading file"),
                             tr("Cannot read file %1:\n%2.")
                             .arg(fileName)
                             .arg(file.errorString()));
        return false;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    QString ext = fileExtension(fileName);
    qDebug("ext: %s", qPrintable(ext));

    //Read
    EmbPattern* p = embPattern_create();
    if(!p) { printf("Could not allocate memory for embroidery pattern\n"); exit(1); }
    int readSuccessful = 0;
    QString readError;
    EmbReaderWriter* reader = embReaderWriter_getByFileName(qPrintable(fileName));
    if(!reader)
    {
        readSuccessful = 0;
        readError = "Unsupported read file type: " + fileName;
        qDebug("Unsupported read file type: %s\n", qPrintable(fileName));
    }
    else
    {
        readSuccessful = reader->reader(p, qPrintable(fileName));
        if(!readSuccessful)
        {
            readError = "Reading file was unsuccessful: " + fileName;
            qDebug("Reading file was unsuccessful: %s\n", qPrintable(fileName));
        }
    }
    free(reader);
    if(!readSuccessful)
    {
        QMessageBox::warning(this, tr("Error reading pattern"), tr(qPrintable(readError)));
    }

    if(readSuccessful)
    {
        embPattern_moveStitchListToPolylines(p); //TODO: Test more
        int stitchCount = embStitchList_count(p->stitchList);
        QPainterPath path;

        if(p->circles)
        {
            for (int i=0; i<p->circles->count; i++) {
                EmbCircle c = p->circles->circle[i].circle;
                EmbColor thisColor = p->circles->circle[i].color;
                setCurrentColor(qRgb(thisColor.r, thisColor.g, thisColor.b));
                //NOTE: With natives, the Y+ is up and libembroidery Y+ is up, so inverting the Y is NOT needed.
                mainWin->nativeAddCircle(c.centerX, c.centerY, c.radius, false, OBJ_RUBBER_OFF); //TODO: fill
            }
        }
        if(p->ellipses)
        {
            for (int i=0; i<p->ellipses->count; i++) {
                EmbEllipse e = p->ellipses->ellipse[i].ellipse;
                EmbColor thisColor = p->ellipses->ellipse[i].color;
                setCurrentColor(qRgb(thisColor.r, thisColor.g, thisColor.b));
                //NOTE: With natives, the Y+ is up and libembroidery Y+ is up, so inverting the Y is NOT needed.
                mainWin->nativeAddEllipse(e.centerX, e.centerY, e.radiusX, e.radiusY, 0, false, OBJ_RUBBER_OFF); //TODO: rotation and fill
            }
        }
        if(p->lines)
        {
            for (int i=0; i<p->lines->count; i++) {
                EmbLine li = p->lines->line[i].line;
                EmbColor thisColor = p->lines->line[i].color;
                setCurrentColor(qRgb(thisColor.r, thisColor.g, thisColor.b));
                //NOTE: With natives, the Y+ is up and libembroidery Y+ is up, so inverting the Y is NOT needed.
                mainWin->nativeAddLine(li.x1, li.y1, li.x2, li.y2, 0, OBJ_RUBBER_OFF); //TODO: rotation
            }
        }
        if(p->paths)
        {
            //TODO: This is unfinished. It needs more work
            for (int i=0; i<p->paths->count; i++) {
                EmbArray* curPointList = p->paths->path[i]->pointList;
                QPainterPath pathPath;
                EmbColor thisColor = p->paths->path[i]->color;
                if(curPointList->count > 0)
                {
                    EmbPoint pp = curPointList[0].point->point;
                    pathPath.moveTo(pp.x, -pp.y); //NOTE: Qt Y+ is down and libembroidery Y+ is up, so inverting the Y is needed.
                }
                for(int j=1; j<curPointList->count; j++)
                {
                    EmbPoint pp = curPointList[j].point->point;
                    pathPath.lineTo(pp.x, -pp.y); //NOTE: Qt Y+ is down and libembroidery Y+ is up, so inverting the Y is needed.
                }

                QPen loadPen(qRgb(thisColor.r, thisColor.g, thisColor.b));
                loadPen.setWidthF(0.35);
                loadPen.setCapStyle(Qt::RoundCap);
                loadPen.setJoinStyle(Qt::RoundJoin);

                PathObject* obj = new PathObject(0,0, pathPath, loadPen.color().rgb());
                obj->setObjectRubberMode(OBJ_RUBBER_OFF);
                gscene->addItem(obj);
            }
        }
        if(p->points)
        {
            for (int i=0; i<p->points->count; i++) {
                EmbPoint po = p->points->point[i].point;
                EmbColor thisColor = p->points->point[i].color;
                setCurrentColor(qRgb(thisColor.r, thisColor.g, thisColor.b));
                //NOTE: With natives, the Y+ is up and libembroidery Y+ is up, so inverting the Y is NOT needed.
                mainWin->nativeAddPoint(po.x, po.y);
            }
        }
        if(p->polygons)
        {
            for (int i=0; i<p->polygons->count; i++) {
                EmbArray *curPointList = p->polygons->polygon[i]->pointList;
                QPainterPath polygonPath;
                bool firstPoint = false;
                qreal startX = 0, startY = 0;
                qreal x = 0, y = 0;
                EmbColor thisColor = p->polygons->polygon[i]->color;
                setCurrentColor(qRgb(thisColor.r, thisColor.g, thisColor.b));
                for(int j=0; j<curPointList->count; j++)
                {
                    EmbPoint pp = curPointList->point[j].point;
                    x = pp.x;
                    y = -pp.y; //NOTE: Qt Y+ is down and libembroidery Y+ is up, so inverting the Y is needed.

                    if(firstPoint)
                    {
                        polygonPath.lineTo(x,y);
                    }
                    else
                    {
                        polygonPath.moveTo(x,y);
                        firstPoint = true;
                        startX = x;
                        startY = y;
                    }
                }

                polygonPath.translate(-startX, -startY);
                mainWin->nativeAddPolygon(startX, startY, polygonPath, OBJ_RUBBER_OFF);
            }
        }
        /* NOTE: Polylines should only contain NORMAL stitches. */
        if(p->polylines)
        {
            for (int i=0; i<p->polylines->count; i++) {
                EmbArray* curPointList = p->polylines->polyline[i]->pointList;
                QPainterPath polylinePath;
                bool firstPoint = false;
                qreal startX = 0, startY = 0;
                qreal x = 0, y = 0;
                EmbColor thisColor = p->polylines->polyline[i]->color;
                setCurrentColor(qRgb(thisColor.r, thisColor.g, thisColor.b));
                for(int j=0; j<curPointList->count; j++)
                {
                    EmbPoint pp = curPointList->point[j].point;
                    x = pp.x;
                    y = -pp.y; //NOTE: Qt Y+ is down and libembroidery Y+ is up, so inverting the Y is needed.
                    if(firstPoint) { polylinePath.lineTo(x,y); }
                    else           { polylinePath.moveTo(x,y); firstPoint = true; startX = x; startY = y; }
                }

                polylinePath.translate(-startX, -startY);
                mainWin->nativeAddPolyline(startX, startY, polylinePath, OBJ_RUBBER_OFF);
            }
        }
        if(p->rects)
        {
            for (int i=0; i<p->rects->count; i++) {
                EmbRect r = p->rects->rect[i].rect;
                EmbColor thisColor = p->rects->rect[i].color;
                setCurrentColor(qRgb(thisColor.r, thisColor.g, thisColor.b));
                //NOTE: With natives, the Y+ is up and libembroidery Y+ is up, so inverting the Y is NOT needed.
                mainWin->nativeAddRectangle(embRect_x(r), embRect_y(r), embRect_width(r), embRect_height(r), 0, false, OBJ_RUBBER_OFF); //TODO: rotation and fill
            }
        }

        setCurrentFile(fileName);
        mainWin->statusbar->showMessage("File loaded.");
        QString stitches;
        stitches.setNum(stitchCount);

        if(mainWin->getSettingsGridLoadFromFile())
        {
            //TODO: Josh, provide me a hoop size and/or grid spacing from the pattern.
        }

        QApplication::restoreOverrideCursor();
    }
    else
    {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, tr("Error reading pattern"), tr("Cannot read pattern"));
    }
    embPattern_free(p);

    //Clear the undo stack so it is not possible to undo past this point.
    gview->getUndoStack()->clear();

    setCurrentColor(tmpColor);

    fileWasLoaded = true;
    mainWin->setUndoCleanIcon(fileWasLoaded);
    return fileWasLoaded;
}

void MdiWindow::print()
{
    QPrintDialog dialog(&printer, this);
    if(dialog.exec() == QDialog::Accepted)
    {
        QPainter painter(&printer);
        if(mainWin->getSettingsPrintingDisableBG())
        {
            //Save current bg
            QBrush brush = gview->backgroundBrush();
            //Save ink by not printing the bg at all
            gview->setBackgroundBrush(Qt::NoBrush);
            //Print, fitting the viewport contents into a full page
            gview->render(&painter);
            //Restore the bg
            gview->setBackgroundBrush(brush);
        }
        else
        {
            //Print, fitting the viewport contents into a full page
            gview->render(&painter);
        }
    }
}

//TODO: Save a Brother PEL image (An 8bpp, 130x113 pixel monochromatic? bitmap image) Why 8bpp when only 1bpp is needed?

//TODO: Should BMC be limited to ~32KB or is this a mix up with Bitmap Cache?
//TODO: Is there/should there be other embedded data in the bitmap besides the image itself?
//NOTE: Can save a Singer BMC image (An 8bpp, 130x113 pixel colored bitmap image)
void MdiWindow::saveBMC()
{
    //TODO: figure out how to center the image, right now it just plops it to the left side.
    QImage img(150, 150, QImage::Format_ARGB32_Premultiplied);
    img.fill(qRgb(255,255,255));
    QRectF extents = gscene->itemsBoundingRect();

    QPainter painter(&img);
    QRectF targetRect(0,0,150,150);
    if(mainWin->getSettingsPrintingDisableBG()) //TODO: Make BMC background into it's own setting?
    {
        QBrush brush = gscene->backgroundBrush();
        gscene->setBackgroundBrush(Qt::NoBrush);
        gscene->update();
        gscene->render(&painter, targetRect, extents, Qt::KeepAspectRatio);
        gscene->setBackgroundBrush(brush);
    }
    else
    {
        gscene->update();
        gscene->render(&painter, targetRect, extents, Qt::KeepAspectRatio);
    }

    img.convertToFormat(QImage::Format_Indexed8, Qt::ThresholdDither|Qt::AvoidDither).save("test.bmc", "BMP");
}

void MdiWindow::setCurrentFile(const QString &fileName)
{
    curFile = QFileInfo(fileName).canonicalFilePath();
    setWindowModified(false);
    setWindowTitle(getShortCurrentFile());
}

QString MdiWindow::getShortCurrentFile()
{
    return QFileInfo(curFile).fileName();
}

QString MdiWindow::fileExtension(const QString& fileName)
{
    return QFileInfo(fileName).suffix().toLower();
}

void MdiWindow::closeEvent(QCloseEvent* /*e*/)
{
    qDebug("MdiWindow closeEvent()");
    emit sendCloseMdiWin(this);
}

void MdiWindow::onWindowActivated()
{
    qDebug("MdiWindow onWindowActivated()");
    gview->getUndoStack()->setActive(true);
    mainWin->setUndoCleanIcon(fileWasLoaded);
    mainWin->statusbar->statusBarSnapButton->setChecked(gscene->property(ENABLE_SNAP).toBool());
    mainWin->statusbar->statusBarGridButton->setChecked(gscene->property(ENABLE_GRID).toBool());
    mainWin->statusbar->statusBarRulerButton->setChecked(gscene->property(ENABLE_RULER).toBool());
    mainWin->statusbar->statusBarOrthoButton->setChecked(gscene->property(ENABLE_ORTHO).toBool());
    mainWin->statusbar->statusBarPolarButton->setChecked(gscene->property(ENABLE_POLAR).toBool());
    mainWin->statusbar->statusBarQSnapButton->setChecked(gscene->property(ENABLE_QSNAP).toBool());
    mainWin->statusbar->statusBarQTrackButton->setChecked(gscene->property(ENABLE_QTRACK).toBool());
    mainWin->statusbar->statusBarLwtButton->setChecked(gscene->property(ENABLE_LWT).toBool());
    mainWin->prompt->setHistory(promptHistory);
}

QSize MdiWindow::sizeHint() const
{
    qDebug("MdiWindow sizeHint()");
    return QSize(450, 300);
}

void MdiWindow::currentLayerChanged(const QString& layer)
{
    curLayer = layer;
}

void MdiWindow::currentColorChanged(const QRgb& color)
{
    curColor = color;
}

void MdiWindow::currentLinetypeChanged(const QString& type)
{
    curLineType = type;
}

void MdiWindow::currentLineweightChanged(const QString& weight)
{
    curLineWeight = weight;
}

void MdiWindow::updateColorLinetypeLineweight()
{
}

void MdiWindow::deletePressed()
{
    gview->deletePressed();
}

void MdiWindow::escapePressed()
{
    gview->escapePressed();
}

void MdiWindow::showViewScrollBars(bool val)
{
    gview->showScrollBars(val);
}

void MdiWindow::setViewCrossHairColor(QRgb color)
{
    gview->setCrossHairColor(color);
}

void MdiWindow::setViewBackgroundColor(QRgb color)
{
    gview->setBackgroundColor(color);
}

void MdiWindow::setViewSelectBoxColors(QRgb colorL, QRgb fillL, QRgb colorR, QRgb fillR, int alpha)
{
    gview->setSelectBoxColors(colorL, fillL, colorR, fillR, alpha);
}

void MdiWindow::setViewGridColor(QRgb color)
{
    gview->setGridColor(color);
}

void MdiWindow::setViewRulerColor(QRgb color)
{
    gview->setRulerColor(color);
}

void MdiWindow::promptHistoryAppended(const QString& txt)
{
    promptHistory.append("<br/>" + txt);
}

void MdiWindow::logPromptInput(const QString& txt)
{
    promptInputList << txt;
    promptInputNum = promptInputList.size();
}

void MdiWindow::promptInputPrevious()
{
    promptInputPrevNext(true);
}

void MdiWindow::promptInputNext()
{
    promptInputPrevNext(false);
}

void MdiWindow::promptInputPrevNext(bool prev)
{
    if(promptInputList.isEmpty())
    {
        if(prev) QMessageBox::critical(this, tr("Prompt Previous Error"), tr("The prompt input is empty! Please report this as a bug!"));
        else     QMessageBox::critical(this, tr("Prompt Next Error"),     tr("The prompt input is empty! Please report this as a bug!"));
        qDebug("The prompt input is empty! Please report this as a bug!");
    }
    else
    {
        if(prev) promptInputNum--;
        else     promptInputNum++;
        int maxNum = promptInputList.size();
        if     (promptInputNum < 0)       { promptInputNum = 0;      mainWin->prompt->setCurrentText(""); }
        else if(promptInputNum >= maxNum) { promptInputNum = maxNum; mainWin->prompt->setCurrentText(""); }
        else                              { mainWin->prompt->setCurrentText(promptInputList.at(promptInputNum)); }
    }
}

