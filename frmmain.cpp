// This file is a part of "grblControl" application.
// Copyright 2015 Hayrullin Denis Ravilevich

//#define INITTIME //QTime time; time.start();
//#define PRINTTIME(x) //qDebug() << "time elapse" << QString("%1:").arg(x) << time.elapsed(); time.start();

#include <QFileDialog>
#include <QTextStream>
#include <QDebug>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QMessageBox>
#include <QComboBox>
#include <QScrollBar>
#include <QShortcut>
#include <QAction>
#include <QLayout>
#include "frmmain.h"
#include "ui_frmmain.h"

frmMain::frmMain(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::frmMain)
{
    m_status << "Idle" << "Alarm" << "Run" << "Home" << "Hold" << "Queue" << "Check";
    m_statusCaptions << tr("Idle") << tr("Alarm") << tr("Run") << tr("Home") << tr("Hold") << tr("Queue") << tr("Check");
    m_statusBackColors << "palette(button)" << "red" << "lime" << "lime" << "yellow" << "yellow" << "palette(button)";
    m_statusForeColors << "palette(text)" << "white" << "black" << "black" << "black" << "black" << "palette(text)";

    ui->setupUi(this);

#ifdef WINDOWS
    m_taskBarButton = NULL;
    m_taskBarProgress = NULL;
#endif

#ifndef UNIX
    ui->cboCommand->setStyleSheet("QComboBox {padding: 2;} QComboBox::drop-down {width: 0; border-style: none;} QComboBox::down-arrow {image: url(noimg);	border-width: 0;}");
#endif
    ui->scrollArea->updateMinimumWidth();

    m_heightMapMode = false;
    m_lastDrawnLineIndex = 0;
    m_fileProcessedCommandIndex = 0;
    m_cellChanged = false;
    m_programLoading = false;
    m_currentModel = &m_programModel;

    ui->txtJogStep->setLocale(QLocale::C);

    ui->cmdXMinus->setBackColor(QColor(153, 180, 209));
    ui->cmdXPlus->setBackColor(ui->cmdXMinus->backColor());
    ui->cmdYMinus->setBackColor(ui->cmdXMinus->backColor());
    ui->cmdYPlus->setBackColor(ui->cmdXMinus->backColor());

    //    ui->cmdReset->setBackColor(QColor(255, 228, 181));

    ui->cmdFit->setParent(ui->glwVisualizer);
    ui->cmdIsometric->setParent(ui->glwVisualizer);
    ui->cmdTop->setParent(ui->glwVisualizer);
    ui->cmdFront->setParent(ui->glwVisualizer);
    ui->cmdLeft->setParent(ui->glwVisualizer);

    ui->cmdHeightMapBorderAuto->setMinimumHeight(ui->chkHeightMapBorderShow->sizeHint().height());
    ui->cmdHeightMapCreate->setMinimumHeight(ui->cmdFileOpen->sizeHint().height());
    ui->cmdHeightMapLoad->setMinimumHeight(ui->cmdFileOpen->sizeHint().height());
    ui->cmdHeightMapMode->setMinimumHeight(ui->cmdFileOpen->sizeHint().height());

    connect(ui->cboCommand, SIGNAL(returnPressed()), this, SLOT(onCboCommandReturnPressed()));

    m_originDrawer = new OriginDrawer();
    m_codeDrawer = new GcodeDrawer();
    m_codeDrawer->setViewParser(&m_viewParser);
    m_probeDrawer = new GcodeDrawer();
    m_probeDrawer->setViewParser(&m_probeParser);
    m_probeDrawer->setVisible(false);
    m_heightMapGridDrawer.setModel(&m_heightMapModel);
    m_currentDrawer = m_codeDrawer;
    m_toolDrawer.setToolPosition(QVector3D(0, 0, 0));

    QShortcut *insertShortcut = new QShortcut(QKeySequence(Qt::Key_Insert), ui->tblProgram);
    QShortcut *deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), ui->tblProgram);

    connect(insertShortcut, SIGNAL(activated()), this, SLOT(onTableInsertLine()));
    connect(deleteShortcut, SIGNAL(activated()), this, SLOT(onTableDeleteLines()));

    m_tableMenu = new QMenu(this);
    m_tableMenu->addAction(tr("&Insert line"), this, SLOT(onTableInsertLine()), insertShortcut->key());
    m_tableMenu->addAction(tr("&Delete lines"), this, SLOT(onTableDeleteLines()), deleteShortcut->key());

    ui->glwVisualizer->addDrawable(m_originDrawer);
    ui->glwVisualizer->addDrawable(m_codeDrawer);
    ui->glwVisualizer->addDrawable(m_probeDrawer);
    ui->glwVisualizer->addDrawable(&m_toolDrawer);
    ui->glwVisualizer->addDrawable(&m_heightMapBorderDrawer);
    ui->glwVisualizer->addDrawable(&m_heightMapGridDrawer);
    ui->glwVisualizer->addDrawable(&m_heightMapInterpolationDrawer);
    ui->glwVisualizer->fitDrawable();

    connect(ui->glwVisualizer, SIGNAL(rotationChanged()), this, SLOT(onVisualizatorRotationChanged()));
    connect(ui->glwVisualizer, SIGNAL(resized()), this, SLOT(placeVisualizerButtons()));
    connect(&m_programModel, SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(onTableCellChanged(QModelIndex,QModelIndex)));
    connect(&m_programHeightmapModel, SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(onTableCellChanged(QModelIndex,QModelIndex)));
    connect(&m_probeModel, SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(onTableCellChanged(QModelIndex,QModelIndex)));
    connect(&m_heightMapModel, SIGNAL(dataChangedByUserInput()), this, SLOT(updateHeightMapInterpolationDrawer()));

    ui->tblProgram->setModel(&m_programModel);
    ui->tblProgram->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    ui->tblProgram->hideColumn(4);
    ui->tblProgram->hideColumn(5);
//    ui->tblProgram->showColumn(4);
    connect(ui->tblProgram->verticalScrollBar(), SIGNAL(actionTriggered(int)), this, SLOT(onScroolBarAction(int)));
    clearTable();

    // Loading settings
    m_settingsFileName = qApp->applicationDirPath() + "/settings.ini";
    loadSettings();

    // Setup serial port
    m_serialPort.setParity(QSerialPort::NoParity);
    m_serialPort.setDataBits(QSerialPort::Data8);
    m_serialPort.setFlowControl(QSerialPort::NoFlowControl);
    m_serialPort.setStopBits(QSerialPort::OneStop);
    if (m_frmSettings.port() != "") {
        m_serialPort.setPortName(m_frmSettings.port());
        m_serialPort.setBaudRate(m_frmSettings.baud());
    }
    connect(&m_serialPort, SIGNAL(readyRead()), this, SLOT(onSerialPortReadyRead()));
    connect(&m_serialPort, SIGNAL(error(QSerialPort::SerialPortError)), this, SLOT(onSerialPortError(QSerialPort::SerialPortError)));

    // Apply settings    
    foreach (StyledToolButton* button, ui->grpJog->findChildren<StyledToolButton*>(QRegExp("cmdJogStep\\d")))
    {
        connect(button, SIGNAL(clicked(bool)), this, SLOT(onCmdJogStepClicked()));
        button->setChecked(button->text().toDouble() == ui->txtJogStep->value());
    }

    show(); // Visibility bug workaround
    applySettings();
    updateControlsState();

    this->installEventFilter(this);
    ui->tblProgram->installEventFilter(this);

    connect(&m_timerConnection, SIGNAL(timeout()), this, SLOT(onTimerConnection()));
    connect(&m_timerStateQuery, SIGNAL(timeout()), this, SLOT(onTimerStateQuery()));
    m_timerConnection.start(1000);
    m_timerStateQuery.start();
}

frmMain::~frmMain()
{    
    saveSettings();

    delete ui;
}

double frmMain::toolZPosition()
{
    return m_toolDrawer.toolPosition().z();
}

void frmMain::loadSettings()
{
    QSettings set(m_settingsFileName, QSettings::IniFormat);
    set.setIniCodec("UTF-8");

    m_settingsLoading = true;

    m_frmSettings.setPort(set.value("port").toString());
    m_frmSettings.setBaud(set.value("baud").toInt());
    m_frmSettings.setToolDiameter(set.value("toolDiameter", 3).toDouble());
    m_frmSettings.setToolLength(set.value("toolLength", 15).toDouble());
    m_frmSettings.setAntialiasing(set.value("antialiasing", true).toBool());
    m_frmSettings.setMsaa(set.value("msaa", true).toBool());
    m_frmSettings.setZBuffer(set.value("zBuffer", false).toBool());
    m_frmSettings.setSimplify(set.value("simplify", false).toBool());
    m_frmSettings.setSimplifyPrecision(set.value("simplifyPrecision", 0).toDouble());
    ui->txtJogStep->setValue(set.value("jogStep", 1).toDouble());
    m_programSpeed = true;
    ui->sliSpindleSpeed->setValue(set.value("spindleSpeed", 0).toInt());
    m_programSpeed = false;
    m_frmSettings.setLineWidth(set.value("lineWidth", 1).toDouble());
    m_frmSettings.setArcPrecision(set.value("arcPrecision", 0).toDouble());
    m_frmSettings.setShowAllCommands(set.value("showAllCommands", 0).toBool());
    m_frmSettings.setSafeZ(set.value("safeZ", 0).toDouble());
    m_frmSettings.setSpindleSpeedMin(set.value("spindleSpeedMin", 0).toInt());
    m_frmSettings.setSpindleSpeedMax(set.value("spindleSpeedMax", 100).toInt());
    m_frmSettings.setRapidSpeed(set.value("rapidSpeed", 0).toInt());
    m_frmSettings.setHeightmapProbingFeed(set.value("heightmapProbingFeed", 0).toInt());
    m_frmSettings.setAcceleration(set.value("acceleration", 10).toInt());
    m_frmSettings.setToolAngle(set.value("toolAngle", 0).toDouble());
    m_frmSettings.setToolType(set.value("toolType", 0).toInt());
    m_frmSettings.setFps(set.value("fps", 60).toInt());
    m_frmSettings.setQueryStateTime(set.value("queryStateTime", 250).toInt());

    m_frmSettings.setPanelHeightmap(set.value("panelHeightmapVisible", true).toBool());
    m_frmSettings.setPanelSpindle(set.value("panelSpindleVisible", true).toBool());
    m_frmSettings.setPanelFeed(set.value("panelFeedVisible", true).toBool());
    m_frmSettings.setPanelJog(set.value("panelJogVisible", true).toBool());

    m_frmSettings.setFontSize(set.value("fontSize", 8).toInt());

    ui->chkAutoScroll->setChecked(set.value("autoScroll", false).toBool());
    ui->sliSpindleSpeed->setValue(set.value("spindleSpeed", 100).toInt() / 100);
    ui->txtSpindleSpeed->setValue(set.value("spindleSpeed", 100).toInt());
    ui->chkFeedOverride->setChecked(set.value("feedOverride", false).toBool());
    ui->sliFeed->setValue(set.value("feed", 100).toInt());
    m_frmSettings.setUnits(set.value("units", 0).toInt());
    m_storedX = set.value("storedX", 0).toDouble();
    m_storedY = set.value("storedY", 0).toDouble();
    m_storedZ = set.value("storedZ", 0).toDouble();

    ui->cmdReturnXY->setToolTip(QString(tr("Restore XYZ:\n%1, %2, %3")).arg(m_storedX).arg(m_storedY).arg(m_storedZ));

    m_recentFiles = set.value("recentFiles", QStringList()).toStringList();
    m_recentHeightmaps = set.value("recentHeightmaps", QStringList()).toStringList();

    this->restoreGeometry(set.value("formGeometry", QByteArray()).toByteArray());
    m_frmSettings.resize(set.value("formSettingsSize", m_frmSettings.size()).toSize());
    QByteArray splitterState = set.value("splitter", QByteArray()).toByteArray();

    if (splitterState.length() == 0) {
        ui->splitter->setStretchFactor(0, 1);
        ui->splitter->setStretchFactor(1, 1);
    } else ui->splitter->restoreState(splitterState);

    ui->chkAutoScroll->setVisible(ui->splitter->sizes()[1]);
    resizeCheckBoxes();

    ui->grpHeightMap->setChecked(set.value("heightmapPanel", true).toBool());
    ui->grpSpindle->setChecked(set.value("spindlePanel", true).toBool());
    ui->grpFeed->setChecked(set.value("feedPanel", true).toBool());
    ui->grpJog->setChecked(set.value("jogPanel", true).toBool());

    m_storedKeyboardControl = set.value("keyboardControl", false).toBool();

    ui->cboCommand->addItems(set.value("recentCommands", QStringList()).toStringList());
    ui->cboCommand->setCurrentIndex(-1);

    m_frmSettings.setAutoCompletion(set.value("autoCompletion", true).toBool());
    m_frmSettings.setTouchCommand(set.value("touchCommand", "G21G38.2Z-30F100; G92Z0; G0Z25").toString());    

    ui->txtHeightMapBorderX->setValue(set.value("heightmapBorderX", 0).toDouble());
    ui->txtHeightMapBorderY->setValue(set.value("heightmapBorderY", 0).toDouble());
    ui->txtHeightMapBorderWidth->setValue(set.value("heightmapBorderWidth", 1).toDouble());
    ui->txtHeightMapBorderHeight->setValue(set.value("heightmapBorderHeight", 1).toDouble());
    ui->chkHeightMapBorderShow->setChecked(set.value("heightmapBorderShow", false).toBool());

    ui->txtHeightMapGridX->setValue(set.value("heightmapGridX", 1).toDouble());
    ui->txtHeightMapGridY->setValue(set.value("heightmapGridY", 1).toDouble());
    ui->txtHeightMapGridZTop->setValue(set.value("heightmapGridZTop", 1).toDouble());
    ui->txtHeightMapGridZBottom->setValue(set.value("heightmapGridZBottom", -1).toDouble());
    ui->chkHeightMapGridShow->setChecked(set.value("heightmapGridShow", false).toBool());

    ui->txtHeightMapInterpolationStepX->setValue(set.value("heightmapInterpolationStepX", 1).toDouble());
    ui->txtHeightMapInterpolationStepY->setValue(set.value("heightmapInterpolationStepY", 1).toDouble());
    ui->cboHeightMapInterpolationType->setCurrentIndex(set.value("heightmapInterpolationType", 0).toInt());
    ui->chkHeightMapInterpolationShow->setChecked(set.value("heightmapInterpolationShow", false).toBool());

    foreach (ColorPicker* pick, m_frmSettings.colors()) {
        pick->setColor(QColor(set.value(pick->objectName().mid(3), "black").toString()));
    }

    updateRecentFilesMenu();   

    ui->tblProgram->horizontalHeader()->restoreState(set.value("header", QByteArray()).toByteArray());

    m_settingsLoading = false;
}

void frmMain::saveSettings()
{
    QSettings set(m_settingsFileName, QSettings::IniFormat);
    set.setIniCodec("UTF-8");

    set.setValue("port", m_frmSettings.port());
    set.setValue("baud", m_frmSettings.baud());
    set.setValue("toolDiameter", m_frmSettings.toolDiameter());
    set.setValue("toolLength", m_frmSettings.toolLength());
    set.setValue("antialiasing", m_frmSettings.antialiasing());
    set.setValue("msaa", m_frmSettings.msaa());
    set.setValue("zBuffer", m_frmSettings.zBuffer());
    set.setValue("simplify", m_frmSettings.simplify());
    set.setValue("simplifyPrecision", m_frmSettings.simplifyPrecision());
    set.setValue("jogStep", ui->txtJogStep->value());
    set.setValue("spindleSpeed", ui->txtSpindleSpeed->text());
    set.setValue("lineWidth", m_frmSettings.lineWidth());
    set.setValue("arcPrecision", m_frmSettings.arcPrecision());
    set.setValue("showAllCommands", m_frmSettings.showAllCommands());
    set.setValue("safeZ", m_frmSettings.safeZ());
    set.setValue("spindleSpeedMin", m_frmSettings.spindleSpeedMin());
    set.setValue("spindleSpeedMax", m_frmSettings.spindleSpeedMax());
    set.setValue("rapidSpeed", m_frmSettings.rapidSpeed());
    set.setValue("heightmapProbingFeed", m_frmSettings.heightmapProbingFeed());
    set.setValue("acceleration", m_frmSettings.acceleration());
    set.setValue("toolAngle", m_frmSettings.toolAngle());
    set.setValue("toolType", m_frmSettings.toolType());
    set.setValue("fps", m_frmSettings.fps());
    set.setValue("queryStateTime", m_frmSettings.queryStateTime());
    set.setValue("autoScroll", ui->chkAutoScroll->isChecked());
    set.setValue("header", ui->tblProgram->horizontalHeader()->saveState());
    set.setValue("splitter", ui->splitter->saveState());
    set.setValue("formGeometry", this->saveGeometry());
    set.setValue("formSettingsSize", m_frmSettings.size());
    set.setValue("spindleSpeed", ui->txtSpindleSpeed->value());
    set.setValue("feedOverride", ui->chkFeedOverride->isChecked());
    set.setValue("feed", ui->txtFeed->value());
    set.setValue("heightmapPanel", ui->grpHeightMap->isChecked());
    set.setValue("spindlePanel", ui->grpSpindle->isChecked());
    set.setValue("feedPanel", ui->grpFeed->isChecked());
    set.setValue("jogPanel", ui->grpJog->isChecked());
    set.setValue("keyboardControl", ui->chkKeyboardControl->isChecked());
    set.setValue("autoCompletion", m_frmSettings.autoCompletion());
    set.setValue("units", m_frmSettings.units());
    set.setValue("storedX", m_storedX);
    set.setValue("storedY", m_storedY);
    set.setValue("storedZ", m_storedZ);
    set.setValue("recentFiles", m_recentFiles);
    set.setValue("recentHeightmaps", m_recentHeightmaps);
    set.setValue("touchCommand", m_frmSettings.touchCommand());
    set.setValue("panelHeightmapVisible", m_frmSettings.panelHeightmap());
    set.setValue("panelSpindleVisible", m_frmSettings.panelSpindle());
    set.setValue("panelFeedVisible", m_frmSettings.panelFeed());
    set.setValue("panelJogVisible", m_frmSettings.panelJog());
    set.setValue("fontSize", m_frmSettings.fontSize());

    set.setValue("heightmapBorderX", ui->txtHeightMapBorderX->value());
    set.setValue("heightmapBorderY", ui->txtHeightMapBorderY->value());
    set.setValue("heightmapBorderWidth", ui->txtHeightMapBorderWidth->value());
    set.setValue("heightmapBorderHeight", ui->txtHeightMapBorderHeight->value());
    set.setValue("heightmapBorderShow", ui->chkHeightMapBorderShow->isChecked());

    set.setValue("heightmapGridX", ui->txtHeightMapGridX->value());
    set.setValue("heightmapGridY", ui->txtHeightMapGridY->value());
    set.setValue("heightmapGridZTop", ui->txtHeightMapGridZTop->value());
    set.setValue("heightmapGridZBottom", ui->txtHeightMapGridZBottom->value());
    set.setValue("heightmapGridShow", ui->chkHeightMapGridShow->isChecked());

    set.setValue("heightmapInterpolationStepX", ui->txtHeightMapInterpolationStepX->value());
    set.setValue("heightmapInterpolationStepY", ui->txtHeightMapInterpolationStepY->value());
    set.setValue("heightmapInterpolationType", ui->cboHeightMapInterpolationType->currentIndex());
    set.setValue("heightmapInterpolationShow", ui->chkHeightMapInterpolationShow->isChecked());

    foreach (ColorPicker* pick, m_frmSettings.colors()) {
        set.setValue(pick->objectName().mid(3), pick->color().name());
    }

    QStringList list;

    for (int i = 0; i < ui->cboCommand->count(); i++) list.append(ui->cboCommand->itemText(i));
    set.setValue("recentCommands", list);
}

bool frmMain::saveChanges(bool heightMapMode)
{
    if ((!heightMapMode && m_fileChanged)) {
        int res = QMessageBox::warning(this, this->windowTitle(), tr("G-code program file was changed. Save?"),
                                       QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (res == QMessageBox::Cancel) return false;
        else if (res == QMessageBox::Yes) on_actFileSave_triggered();
        m_fileChanged = false;
    }

    if (m_heightMapChanged) {
        int res = QMessageBox::warning(this, this->windowTitle(), tr("Heightmap file was changed. Save?"),
                                       QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (res == QMessageBox::Cancel) return false;
        else if (res == QMessageBox::Yes) {
            m_heightMapMode = true;
            on_actFileSave_triggered();
            m_heightMapMode = heightMapMode;
            updateRecentFilesMenu(); // Restore g-code files recent menu
        }

        m_fileChanged = false;
    }

    return true;
}

void frmMain::updateControlsState() {    
    bool portOpened = m_serialPort.isOpen();

    ui->grpState->setEnabled(portOpened);
    ui->grpControl->setEnabled(portOpened);
    ui->widgetSpindle->setEnabled(portOpened);
    ui->widgetJog->setEnabled(portOpened && !m_transferringFile);
//    ui->grpConsole->setEnabled(portOpened);
    ui->cboCommand->setEnabled(portOpened && (!ui->chkKeyboardControl->isChecked()));
    ui->cmdCommandSend->setEnabled(portOpened);
//    ui->widgetFeed->setEnabled(!m_transferringFile);

    ui->chkTestMode->setEnabled(portOpened && !m_transferringFile);
    ui->cmdHome->setEnabled(!m_transferringFile);
    ui->cmdTouch->setEnabled(!m_transferringFile);
    ui->cmdZeroXY->setEnabled(!m_transferringFile);
    ui->cmdZeroZ->setEnabled(!m_transferringFile);
    ui->cmdReturnXY->setEnabled(!m_transferringFile);
    ui->cmdTopZ->setEnabled(!m_transferringFile);
    ui->cmdUnlock->setEnabled(!m_transferringFile);
    ui->cmdSpindle->setEnabled(!m_transferringFile);

    ui->actFileNew->setEnabled(!m_transferringFile);
    ui->actFileOpen->setEnabled(!m_transferringFile);
    ui->cmdFileOpen->setEnabled(!m_transferringFile);
    ui->cmdFileReset->setEnabled(!m_transferringFile && m_programModel.rowCount() > 1);
    ui->cmdFileSend->setEnabled(portOpened && !m_transferringFile && m_programModel.rowCount() > 1);
    ui->cmdFilePause->setEnabled(m_transferringFile);
    ui->actFileOpen->setEnabled(!m_transferringFile);
    ui->mnuRecent->setEnabled(!m_transferringFile && ((m_recentFiles.count() > 0 && !m_heightMapMode)
                                                      || (m_recentHeightmaps.count() > 0 && m_heightMapMode)));
    ui->actFileSave->setEnabled(m_programModel.rowCount() > 1);
    ui->actFileSaveAs->setEnabled(m_programModel.rowCount() > 1);

    ui->tblProgram->setEditTriggers(m_transferringFile ? QAbstractItemView::NoEditTriggers :
                                                         QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked
                                                         | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);

    if (!portOpened) {
        ui->txtStatus->setText(tr("Not connected"));
        ui->txtStatus->setStyleSheet(QString("background-color: palette(button); color: palette(text);"));
    }

    this->setWindowTitle(m_programFileName.isEmpty() ? "grblControl"
                                                     : m_programFileName.mid(m_programFileName.lastIndexOf("/") + 1) + " - grblControl");

//    QRegExp rx("([^/]*)$");

    if (!m_transferringFile) ui->chkKeyboardControl->setChecked(m_storedKeyboardControl);

#ifdef WINDOWS
    if (!m_transferringFile && m_taskBarProgress) m_taskBarProgress->hide();
#endif

    style()->unpolish(ui->cmdFileOpen);
    style()->unpolish(ui->cmdFileReset);
    style()->unpolish(ui->cmdFileSend);
    style()->unpolish(ui->cmdFilePause);
    ui->cmdFileOpen->ensurePolished();
    ui->cmdFileReset->ensurePolished();
    ui->cmdFileSend->ensurePolished();
    ui->cmdFilePause->ensurePolished();

    // Heightmap

    m_heightMapBorderDrawer.setVisible(ui->chkHeightMapBorderShow->isChecked() && m_heightMapMode);
    m_heightMapGridDrawer.setVisible(ui->chkHeightMapGridShow->isChecked() && m_heightMapMode);
    m_heightMapInterpolationDrawer.setVisible(ui->chkHeightMapInterpolationShow->isChecked() && m_heightMapMode);

    ui->grpProgram->setTitle(m_heightMapMode ? tr("Heightmap") : tr("G-code program"));
    ui->grpProgram->setProperty("overrided", m_heightMapMode);
    style()->unpolish(ui->grpProgram);
    ui->grpProgram->ensurePolished();

    ui->grpHeightMapSettings->setVisible(m_heightMapMode);
    ui->grpHeightMapSettings->setEnabled(!m_transferringFile);

    ui->chkTestMode->setVisible(!m_heightMapMode);
    ui->chkAutoScroll->setVisible(!m_heightMapMode);

    ui->tblHeightMap->setVisible(m_heightMapMode);
    ui->tblProgram->setVisible(!m_heightMapMode);

    ui->widgetHeightMap->setEnabled(!m_transferringFile && m_programModel.rowCount() > 1);
    ui->cmdHeightMapMode->setEnabled(!ui->txtHeightMap->text().isEmpty());

    ui->cmdFileSend->setText(m_heightMapMode ? tr("Probe") : tr("Send"));

    ui->chkHeightMapUse->setEnabled(!m_heightMapMode && !ui->txtHeightMap->text().isEmpty());
}

void frmMain::openPort()
{
    if (m_serialPort.open(QIODevice::ReadWrite)) {
        ui->txtStatus->setText(tr("Connected"));
        ui->txtStatus->setStyleSheet(QString("background-color: palette(button); color: palette(text);"));
//        updateControlsState();
        grblReset();
    }
}

void frmMain::sendCommand(QString command, int tableIndex)
{
    if (!m_serialPort.isOpen() || !m_resetCompleted) return;

    command = command.toUpper();

    // Commands queue
    if ((bufferLength() + command.length() + 1) > BUFFERLENGTH) {
//        qDebug() << "queue:" << command;

        CommandQueue cq;

        cq.command = command;
        cq.tableIndex = tableIndex;

        m_queue.append(cq);
        return;
    }

    CommandAttributes ca;

    if (!(command == "$G" && tableIndex < -1) && !(command == "$#" && tableIndex < -1)
            && (!m_transferringFile || (m_transferringFile && m_showAllCommands) || tableIndex < 0)) {
        ui->txtConsole->appendPlainText(command);
        ca.consoleIndex = ui->txtConsole->blockCount() - 1;
    } else {
        ca.consoleIndex = -1;
    }

    ca.command = command;
    ca.length = command.length() + 1;
    ca.tableIndex = tableIndex;

    m_commands.append(ca);

    // Processing spindle speed only from g-code program
//    if (m_spindleCommandSpeed) {
        QRegExp s("[Ss]0*(\\d+)");
        if (s.indexIn(command) != -1 && ca.tableIndex > -2) {
            int speed = s.cap(1).toInt();
            if (ui->txtSpindleSpeed->value() != speed) {
                ui->txtSpindleSpeed->setValue(speed);
                m_programSpeed = true;
                ui->sliSpindleSpeed->setValue(speed / 100);
                m_programSpeed = false;
            }
        }
//    }

    m_serialPort.write((command + "\r").toLatin1());
}

void frmMain::grblReset()
{
    m_serialPort.write(QByteArray(1, (char)24));
    m_serialPort.flush();

    m_transferringFile = false;
    m_fileCommandIndex = 0;

//    m_programSpeed = true;
//    ui->cmdSpindle->setChecked(false);
//    m_programSpeed = false;
//    m_timerToolAnimation.stop();

    m_reseting = true;
    m_homing = false;
    m_resetCompleted = false;

    // Drop all remaining commands in buffer
    m_commands.clear();
    m_queue.clear();

    // Prepare reset response catch
    CommandAttributes ca;
    ca.command = "[CTRL+X]";
    ui->txtConsole->appendPlainText(ca.command);
    ca.consoleIndex = ui->txtConsole->blockCount() - 1;
    ca.tableIndex = -1;
    ca.length = ca.command.length() + 1;
    m_commands.append(ca);

    updateControlsState();
}

int frmMain::bufferLength()
{
    int length = 0;

    foreach (CommandAttributes ca, m_commands) {
        length += ca.length;
    }

    return length;
}

void frmMain::onSerialPortReadyRead()
{    
    while (m_serialPort.canReadLine()) {
        QString data = m_serialPort.readLine().trimmed();

        // Filter prereset responses
        if (m_reseting) {
            qDebug() << "reseting filter:" << data;
            if (data[0] == '[' || data[0] == '<' || dataIsEnd(data)) continue;
            else {
                m_reseting = false;
                m_timerStateQuery.setInterval(m_frmSettings.queryStateTime());
            }
        }

        // Status answer
        if (data[0] == '<') {
            int statusIndex = -1;

            // Status
            QRegExp stx("<([^,^>]*)");
            if (stx.indexIn(data) != -1) {

                static int previousStatus = -1;

                statusIndex = m_status.indexOf(stx.cap(1));

                if (statusIndex != previousStatus) {
                    previousStatus = statusIndex;
                    ui->txtStatus->setText(m_statusCaptions[statusIndex]);
                    ui->txtStatus->setStyleSheet(QString("background-color: %1; color: %2;")
                                                 .arg(m_statusBackColors[statusIndex]).arg(m_statusForeColors[statusIndex]));
                }

                // Update controls
                ui->cmdReturnXY->setEnabled(statusIndex == 0);
                ui->cmdTopZ->setEnabled(statusIndex == 0);
                ui->chkTestMode->setChecked(statusIndex == 6);
                ui->cmdFilePause->setChecked(statusIndex == 4 || statusIndex == 5);
#ifdef WINDOWS
                if (m_taskBarProgress) m_taskBarProgress->setPaused(statusIndex == 4 || statusIndex == 5);
#endif

                // Update "elapsed time" timer
                if (m_transferringFile) {
                    QTime time(0, 0, 0);
                    int elapsed = m_startTime.elapsed();
                    ui->glwVisualizer->setSpendTime(time.addMSecs(elapsed));
                }

                // Test for job complete
                if (m_transferCompleted && (statusIndex == 0 || statusIndex == 6) && m_fileCommandIndex == m_currentModel->rowCount() - 1 && m_transferringFile) {
                    GcodeViewParse *parser = m_currentDrawer->viewParser();

                    m_transferringFile = false;
                    m_fileProcessedCommandIndex = 0;
                    m_lastDrawnLineIndex = 0;

                    QList<LineSegment*> list = parser->getLineSegmentList();

                    QList<int> indexes;
                    for (int i = m_lastDrawnLineIndex; i < list.length(); i++) {
                        list[i]->setDrawn(true);
                        indexes.append(i);
                    }
                    m_currentDrawer->update(indexes);

                    updateControlsState();

                    qApp->beep();

                    m_timerStateQuery.stop();
                    m_timerConnection.stop();

                    QMessageBox::information(this, "grblControl", tr("Job done.\nTime elapsed: %1")
                                             .arg(ui->glwVisualizer->spendTime().toString("hh:mm:ss")));

                    m_timerStateQuery.setInterval(m_frmSettings.queryStateTime());
                    m_timerConnection.start();
                    m_timerStateQuery.start();
                }
            }

            QRegExp wpx("WPos:([^,]*),([^,]*),([^,^>]*)");
            if (wpx.indexIn(data) != -1)
            {
                ui->txtWPosX->setText(wpx.cap(1));
                ui->txtWPosY->setText(wpx.cap(2));
                ui->txtWPosZ->setText(wpx.cap(3));

                // Update tool position                
                if (!(statusIndex == 6 && m_fileProcessedCommandIndex < m_currentModel->rowCount() - 1)) {
                    m_toolDrawer.setToolPosition(QVector3D(toMetric(ui->txtWPosX->text().toDouble()),
                                                           toMetric(ui->txtWPosY->text().toDouble()),
                                                           toMetric(ui->txtWPosZ->text().toDouble())));
                }

                // Trajectory shadowing
                if (m_transferringFile && statusIndex != 6) {
                    GcodeViewParse *parser = m_currentDrawer->viewParser();

                    bool toolOnTrajectory = false;

                    QList<int> drawnLines;
                    QList<LineSegment*> list = parser->getLineSegmentList();

                    for (int i = m_lastDrawnLineIndex; i < list.count()
                         && list[i]->getLineNumber()
                         <= (m_currentModel->data(m_currentModel->index(m_fileProcessedCommandIndex, 4)).toInt() + 1); i++) {
                        if (list[i]->contains(m_toolDrawer.toolPosition())) {
                            toolOnTrajectory = true;
                            m_lastDrawnLineIndex = i;
                            break;
                        }
                        drawnLines << i;
                    }

                    if (toolOnTrajectory) {
                        foreach (int i, drawnLines) {
                            list[i]->setDrawn(true);
                        }
                        if (!drawnLines.isEmpty()) m_currentDrawer->update(drawnLines);
                    } else if (m_lastDrawnLineIndex < list.count()) {
                        qDebug() << "tool missed:" << list[m_lastDrawnLineIndex]->getLineNumber()
                                 << m_currentModel->data(m_currentModel->index(m_fileProcessedCommandIndex, 4)).toInt()
                                 << m_fileProcessedCommandIndex;
                    }
                }
            }

            QRegExp mpx("MPos:([^,]*),([^,]*),([^,^>]*)");
            if (mpx.indexIn(data) != -1) {
                ui->txtMPosX->setText(mpx.cap(1));
                ui->txtMPosY->setText(mpx.cap(2));
                ui->txtMPosZ->setText(mpx.cap(3));
            }
        } else if (data.length() > 0) {

            // Processed commands
            if (m_commands.length() > 0 && !dataIsFloating(data)
                    && !(m_commands[0].command != "[CTRL+X]" && data.contains("'$' for help"))) {

                static QString response; // Full response string

                if ((m_commands[0].command != "[CTRL+X]" && dataIsEnd(data))
                        || (m_commands[0].command == "[CTRL+X]" && data.contains("'$' for help"))) {

                    response.append(data);

                    // Take command from buffer
                    CommandAttributes ca = m_commands.takeFirst();
                    QTextBlock tb = ui->txtConsole->document()->findBlockByNumber(ca.consoleIndex);
                    QTextCursor tc(tb);

                    // Restore absolute/relative coordinate system after jog
                    if (ca.command.toUpper() == "$G" && ca.tableIndex == -2) {
                        if (ui->chkKeyboardControl->isChecked()) m_absoluteCoordinates = response.contains("G90");
                        else if (response.contains("G90")) sendCommand("G90");
                    }

                    // Process parser status
                    if (ca.command.toUpper() == "$G" && ca.tableIndex == -3) {
                        // Update status in visualizer window
                        ui->glwVisualizer->setParserStatus(response.left(response.indexOf("; ")));

                        // Process spindle state
                        if (!response.contains("M5")) {
                            m_spindleCW = response.contains("M3");
                            m_timerToolAnimation.start(25, this);
                            m_programSpeed = true;
                            ui->cmdSpindle->setChecked(true);
                            m_programSpeed = false;
                        } else {
                            m_timerToolAnimation.stop();
                            m_programSpeed = true;
                            ui->cmdSpindle->setChecked(false);
                            m_programSpeed = false;
                        }

                        // Spindle speed
                        QRegExp rx(".*S([\\d\\.]+)");
                        if (rx.indexIn(response) != -1) {
                            int speed = toMetric(rx.cap(1).toDouble()); //RPM in imperial?
                            if (ui->txtSpindleSpeed->value() == speed) ui->txtSpindleSpeed->setStyleSheet("color: palette(text);");
                        }
                    }

                    // Store origin
                    if (ca.command == "$#" && ca.tableIndex == -2) {
                        qDebug() << "store origin" << response;
                        QRegExp rx(".*G92:([^,]*),([^,]*),([^]]*)");

                        if (rx.indexIn(response) != -1) {
                            qDebug() << "storing origin:" << rx.capturedTexts();
                            if (m_settingZeroXY) {
                                m_settingZeroXY = false;
                                m_storedX = rx.cap(1).toDouble();
                                m_storedY = rx.cap(2).toDouble();
                            } else if (m_settingZeroZ) {
                                m_settingZeroZ = false;
                                m_storedZ = rx.cap(3).toDouble();
                            }
                            ui->cmdReturnXY->setToolTip(QString(tr("Restore XYZ:\n%1, %2, %3")).arg(m_storedX).arg(m_storedY).arg(m_storedZ));
                        }
                    }

                    // Homing response
                    if ((ca.command.toUpper() == "$H" || ca.command.toUpper() == "$T") && m_homing) m_homing = false;

                    // Reset complete
                    if (ca.command == "[CTRL+X]") m_resetCompleted = true;

                    // Debug
//                    if (ca.command != "$G") qDebug() << "response:" << tb.text() << ca.command << response << ca.consoleIndex;

                    // Clear command buffer on "M2" & "M30" command
                    if ((ca.command.contains("M2") || ca.command.contains("M30")) && response.contains("ok") && !response.contains("[Pgm End]")) {
                        m_commands.clear();
                        m_queue.clear();
                    }

                    // Process probing on heightmap mode only from table commands
                    if (ca.command.contains("G38.2") && m_heightMapMode && ca.tableIndex > -1) {
                        // Get probe Z coordinate
                        // "[PRB:0.000,0.000,0.000:0];ok"
                        QRegExp rx(".*PRB:([^,]*),([^,]*),([^]^:]*)");
                        double z = NAN;
                        if (rx.indexIn(response) != -1) {
                            qDebug() << "probing coordinates:" << rx.cap(1) << rx.cap(2) << rx.cap(3);
                            z = toMetric(rx.cap(3).toDouble());
                        }

                        static double firstZ;
                        if (m_probeIndex == -1) {
                            firstZ = z;
                            z = 0;
                        } else {
                            // Calculate delta Z
                            z -= firstZ;

                            // Calculate table indexes
                            int row = trunc(m_probeIndex / m_heightMapModel.columnCount());
                            int column = m_probeIndex - row * m_heightMapModel.columnCount();
                            if (row % 2) column = m_heightMapModel.columnCount() - 1 - column;

                            // Store Z in table
                            m_heightMapModel.setData(m_heightMapModel.index(row, column), z, Qt::UserRole);
                            ui->tblHeightMap->update(m_heightMapModel.index(m_heightMapModel.rowCount() - 1 - row, column));
                            updateHeightMapInterpolationDrawer();
                        }

                        m_probeIndex++;
                    }

                    // Process check mode
                    if (ca.command.contains(QRegExp("$[cC]"))) {
                        m_timerStateQuery.setInterval(response.contains("Enable") ? 1000 : m_frmSettings.queryStateTime());
                    }

                    // Add answer to console
                    if (tb.isValid() && tb.text() == ca.command) {

                        bool scrolledDown = ui->txtConsole->verticalScrollBar()->value() == ui->txtConsole->verticalScrollBar()->maximum();

                        // Update text block numbers
                        int blocksAdded = response.count("; ");

                        if (blocksAdded > 0) for (int i = 0; i < m_commands.count(); i++) {
                            if (m_commands[i].consoleIndex != -1) m_commands[i].consoleIndex += blocksAdded;
                        }

                        tc.beginEditBlock();
                        tc.movePosition(QTextCursor::EndOfBlock);

                        tc.insertText(" < " + QString(response).replace("; ", "\r\n"));
                        tc.endEditBlock();

                        if (scrolledDown) ui->txtConsole->verticalScrollBar()->setValue(ui->txtConsole->verticalScrollBar()->maximum());
                    }

                    // Check queue
                    if (m_queue.length() > 0) {
                        CommandQueue cq = m_queue.takeFirst();
                        while ((bufferLength() + cq.command.length() + 1) <= BUFFERLENGTH) {
                            sendCommand(cq.command, cq.tableIndex);
                            if (m_queue.isEmpty()) break; else cq = m_queue.takeFirst();
                        }
                    }

                    // Add answer to table, send next program commands
                    if (m_transferringFile) {

                        // Only if command from table
                        if (ca.tableIndex > -1) {
                            m_currentModel->setData(m_currentModel->index(ca.tableIndex, 2), tr("Processed"));
                            m_currentModel->setData(m_currentModel->index(ca.tableIndex, 3), response);

                            m_fileProcessedCommandIndex = ca.tableIndex;

                            if (ui->chkAutoScroll->isChecked() && ca.tableIndex != -1) {
                                ui->tblProgram->scrollTo(m_currentModel->index(ca.tableIndex + 1, 0));
                                ui->tblProgram->setCurrentIndex(m_currentModel->index(ca.tableIndex, 1));
                            }
                        }

                        // Update taskbar progress
#ifdef WINDOWS
                        if (m_taskBarProgress) m_taskBarProgress->setValue(m_fileProcessedCommandIndex);
#endif

                        // Send next program commands
                        if (m_fileCommandIndex < m_currentModel->rowCount()) {
                            sendNextFileCommands();
                        }
                    }

                    // Check transfer complete (last row always blank, last command row = rowcount - 2)
                    if (m_fileProcessedCommandIndex == m_currentModel->rowCount() - 2) m_transferCompleted = true;

                    // Trajectory shadowing on check mode
                    if (m_statusCaptions.indexOf(ui->txtStatus->text()) == 6 && m_fileProcessedCommandIndex < m_currentModel->rowCount() - 1) {
                        GcodeViewParse *parser = m_currentDrawer->viewParser();

                        int i;
                        QList<int> drawnLines;
                        QList<LineSegment*> list = parser->getLineSegmentList();

                        for (i = m_lastDrawnLineIndex; i < list.count()
                             && list[i]->getLineNumber()
                             <= (m_currentModel->data(m_currentModel->index(m_fileProcessedCommandIndex, 4)).toInt() + 1); i++) {
                            drawnLines << i;
                        }

                        if (i < list.count()) {
                            m_lastDrawnLineIndex = i;
                            QVector3D vec = list[i]->getEnd();
                            m_toolDrawer.setToolPosition(vec);
                        }

                        foreach (int i, drawnLines) {
                            list[i]->setDrawn(true);
                        }
                        if (!drawnLines.isEmpty()) m_currentDrawer->update(drawnLines);
                    }
                    response.clear();
                } else {
                    response.append(data + "; ");
                }                

            } else {
                // response without commands in buffer
                ui->txtConsole->appendPlainText(data);
                qDebug() << "floating response:" << data;
            }
        } else {
            // Blank response
//            ui->txtConsole->appendPlainText(data);
        }
    }
}

void frmMain::onSerialPortError(QSerialPort::SerialPortError error)
{
    static QSerialPort::SerialPortError previousError;

    if (error != QSerialPort::NoError && error != previousError) {
        ui->txtConsole->appendPlainText(tr("Serial port error ") + QString::number(error) + ": " + m_serialPort.errorString());
        if (m_serialPort.isOpen()) {
            m_serialPort.close();
            updateControlsState();
        }
        previousError = error;
    }
//    if (error == QSerialPort::ResourceError) m_serialPort.close();
}

void frmMain::onTimerConnection()
{
    if (!m_serialPort.isOpen()) {
        openPort();
    } else if (!m_homing/* && !m_reseting*/ && !ui->cmdFilePause->isChecked()) {

//        qDebug() << "sending $G";
        if (m_updateSpindleSpeed) {
            m_updateSpindleSpeed = false;
            sendCommand(QString("S%1").arg(ui->txtSpindleSpeed->value()), -2);
        }
        if (m_queue.length() == 0) sendCommand("$G", -3);
    }
}

void frmMain::onTimerStateQuery()
{
    if (m_serialPort.isOpen() && m_resetCompleted) {
        m_serialPort.write(QByteArray(1, '?'));
    }

    ui->glwVisualizer->setBufferState(QString(tr("Buffer: %1 / %2")).arg(bufferLength()).arg(m_queue.length()));
}

void frmMain::onCmdJogStepClicked()
{
    ui->txtJogStep->setValue(static_cast<QPushButton*>(sender())->text().toDouble());

    foreach (StyledToolButton* button, ui->grpJog->findChildren<StyledToolButton*>(QRegExp("cmdJogStep\\d")))
    {
        button->setChecked(false);
    }
    static_cast<QPushButton*>(sender())->setChecked(true);
}

void frmMain::onVisualizatorRotationChanged()
{
    ui->cmdIsometric->setChecked(false);
}

void frmMain::onScroolBarAction(int action)
{
    if (m_transferringFile) ui->chkAutoScroll->setChecked(false);
}

void frmMain::onJogTimer()
{
    m_jogBlock = false;
}

void frmMain::placeVisualizerButtons()
{
    ui->cmdIsometric->move(ui->glwVisualizer->width() - ui->cmdIsometric->width() - 8, 8);
    ui->cmdTop->move(ui->cmdIsometric->geometry().left() - ui->cmdTop->width() - 8, 8);
    ui->cmdLeft->move(ui->glwVisualizer->width() - ui->cmdLeft->width() - 8, ui->cmdIsometric->geometry().bottom() + 8);
    ui->cmdFront->move(ui->cmdLeft->geometry().left() - ui->cmdFront->width() - 8, ui->cmdIsometric->geometry().bottom() + 8);
//    ui->cmdFit->move(ui->cmdTop->geometry().left() - ui->cmdFit->width() - 10, 10);
    ui->cmdFit->move(ui->glwVisualizer->width() - ui->cmdFit->width() - 8, ui->cmdLeft->geometry().bottom() + 8);
}

void frmMain::showEvent(QShowEvent *se)
{
    placeVisualizerButtons();

#ifdef WINDOWS
    if (m_taskBarButton == NULL) {
        m_taskBarButton = new QWinTaskbarButton(this);
        m_taskBarButton->setWindow(this->windowHandle());
        m_taskBarProgress = m_taskBarButton->progress();
    }
#endif

    ui->glwVisualizer->setUpdatesEnabled(true);

    resizeCheckBoxes();
}

void frmMain::hideEvent(QHideEvent *he)
{
    ui->glwVisualizer->setUpdatesEnabled(false);
}

void frmMain::resizeEvent(QResizeEvent *re)
{
    placeVisualizerButtons();
    resizeCheckBoxes();
    resizeTableHeightMapSections();

//    ui->scrollArea->setMinimumSize(ui->scrollAreaWidgetContents->sizeHint());
//    qDebug() << "viewport sizeHint:" << ui->scrollArea->viewport()->sizeHint() << ;
}

void frmMain::resizeTableHeightMapSections()
{
    if (ui->tblHeightMap->horizontalHeader()->defaultSectionSize()
            * ui->tblHeightMap->horizontalHeader()->count() < ui->glwVisualizer->width())
        ui->tblHeightMap->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); else {
        ui->tblHeightMap->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    }
}

void frmMain::resizeCheckBoxes()
{
    static int widthCheckMode = ui->chkTestMode->sizeHint().width();
    static int widthAutoScroll = ui->chkAutoScroll->sizeHint().width();

    // Transform checkboxes

    this->setUpdatesEnabled(false);

    updateLayouts();

    if (ui->chkTestMode->sizeHint().width() > ui->chkTestMode->width()) {
        widthCheckMode = ui->chkTestMode->sizeHint().width();
        ui->chkTestMode->setText(tr("Check"));
        ui->chkTestMode->setMinimumWidth(ui->chkTestMode->sizeHint().width());
        updateLayouts();
    }

    if (ui->chkAutoScroll->sizeHint().width() > ui->chkAutoScroll->width()
            && ui->chkTestMode->text() == tr("Check")) {
        widthAutoScroll = ui->chkAutoScroll->sizeHint().width();
        ui->chkAutoScroll->setText(tr("Scroll"));
        ui->chkAutoScroll->setMinimumWidth(ui->chkAutoScroll->sizeHint().width());
        updateLayouts();
    }

    if (ui->spacerBot->geometry().width() + ui->chkAutoScroll->sizeHint().width()
            - ui->spacerBot->sizeHint().width() > widthAutoScroll && ui->chkAutoScroll->text() == tr("Scroll")) {
        ui->chkAutoScroll->setText(tr("Autoscroll"));
        updateLayouts();
    }

    if (ui->spacerBot->geometry().width() + ui->chkTestMode->sizeHint().width()
            - ui->spacerBot->sizeHint().width() > widthCheckMode && ui->chkTestMode->text() == tr("Check")) {
        ui->chkTestMode->setText(tr("Check mode"));
        updateLayouts();
    }

    this->setUpdatesEnabled(true);
    this->repaint();
}

void frmMain::timerEvent(QTimerEvent *te)
{
    if (te->timerId() == m_timerToolAnimation.timerId()) {
        m_toolDrawer.rotate((m_spindleCW ? -40 : 40) * (double)(ui->txtSpindleSpeed->value())
                            / (ui->txtSpindleSpeed->maximum()));
    } else {
        QMainWindow::timerEvent(te);
    }
}

void frmMain::closeEvent(QCloseEvent *ce)
{
    bool mode = m_heightMapMode;
    m_heightMapMode = false;

    if (!saveChanges(m_heightMapMode)) {
        ce->ignore();
        m_heightMapMode = mode;
        return;
    }

    if (m_transferringFile && QMessageBox::warning(this, this->windowTitle(), tr("File sending in progress. Terminate and exit?"),
                                                   QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
        ce->ignore();
        m_heightMapMode = mode;
        return;
    }

    if (m_serialPort.isOpen()) m_serialPort.close();
    if (m_queue.length() > 0) {
        m_commands.clear();
        m_queue.clear();
    }
}

void frmMain::on_actFileExit_triggered()
{
    close();
}

void frmMain::on_cmdFileOpen_clicked()
{
    if (!m_heightMapMode) {
        if (!saveChanges(false)) return;

        QString fileName = QFileDialog::getOpenFileName(this, tr("Open"), "", tr("G-Code files (*.nc *.ncc *.tap);;All files (*.*)"));

        if (fileName != "") {
            addRecentFile(fileName);
            updateRecentFilesMenu();

            loadFile(fileName);
        }
    } else {        
        if (!saveChanges(true)) return;

        QString fileName = QFileDialog::getOpenFileName(this, tr("Open"), "", tr("Heightmap files (*.map)"));

        if (fileName != "") {
            addRecentHeightmap(fileName);
            updateRecentFilesMenu();
            loadHeightMap(fileName);
        }
    }
}

void frmMain::resetHeightmap()
{
    delete m_heightMapInterpolationDrawer.data();
    m_heightMapInterpolationDrawer.setData(NULL);
//    updateHeightMapInterpolationDrawer();

    ui->tblHeightMap->setModel(NULL);
    m_heightMapModel.resize(1, 1);

    ui->txtHeightMap->clear();
    m_heightMapFileName.clear();
    m_heightMapChanged = false;
}

void frmMain::loadFile(QString fileName)
{
    QFile file(fileName);

    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, this->windowTitle(), tr("Can't open file:\n") + fileName);
        return;
    }    

    QTime time;
    time.start();

    // Reset tables
    clearTable();
    m_probeModel.clear();
    m_programHeightmapModel.clear();
    m_currentModel = &m_programModel;

    // Reset parsers
    m_viewParser.reset();
    m_probeParser.reset();

    // Reset code drawer
    m_currentDrawer = m_codeDrawer;

    // Update interface
    ui->chkHeightMapUse->setChecked(false);
    ui->grpHeightMap->setProperty("overrided", false);
    style()->unpolish(ui->grpHeightMap);
    ui->grpHeightMap->ensurePolished();

    // Reset tableview
    QByteArray headerState = ui->tblProgram->horizontalHeader()->saveState();
    ui->tblProgram->setModel(NULL);

    // Set filename
    m_programFileName = fileName;

    // Prepare text stream
    QTextStream textStream(&file);

    // Prepare parser
    GcodeParser gp;
    gp.setTraverseSpeed(m_rapidSpeed);

    qDebug() << "Prepared to load:" << time.elapsed();
    time.start();

    // Block parser updates on table changes
    m_programLoading = true;

//    // Test memory leakage
//    for (int i = 0; i < 1; i++) {

//        textStream.seek(0);

//        clearTable();
//        gp.reset();

        QString command;
        QString stripped;
        QList<QString> args;

        while (!textStream.atEnd())
        {
            command = textStream.readLine();

            // Trim & split command
            stripped = GcodePreprocessorUtils::removeComment(command);
            args = GcodePreprocessorUtils::splitCommand(stripped);

            PointSegment *ps = gp.addCommand(args);
            //Quantum line (if disable pointsegment check some points will have NAN number on raspberry)
            // code alignment?
            if (ps && (std::isnan(ps->point()->x()) || std::isnan(ps->point()->y()) || std::isnan(ps->point()->z())))
                       qDebug() << "nan point segment added:" << *ps->point();

            m_programModel.setData(m_programModel.index(m_programModel.rowCount() - 1, 1), command);
            m_programModel.setData(m_programModel.index(m_programModel.rowCount() - 2, 2), tr("In queue"));
            m_programModel.setData(m_programModel.index(m_programModel.rowCount() - 2, 4), gp.getCommandNumber());
            // Store splitted args to speed up future parser updates
            m_programModel.setData(m_programModel.index(m_programModel.rowCount() - 2, 5), QVariant(args));

            // Test args
//            qDebug() << m_programModel.data(m_programModel.index(m_programModel.rowCount() - 2, 5)).toStringList();
        }

//        m_viewParser.reset();
        updateProgramEstimatedTime(m_viewParser.getLinesFromParser(&gp, m_arcPrecision));
//    }

    qDebug() << "model filled:" << time.elapsed();
    time.start();

    m_programLoading = false;

    // Set table model
    ui->tblProgram->setModel(&m_programModel);
    ui->tblProgram->horizontalHeader()->restoreState(headerState);

    // Update tableview
    connect(ui->tblProgram->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)), this, SLOT(onTableCurrentChanged(QModelIndex,QModelIndex)));
    ui->tblProgram->selectRow(0);

    qDebug() << "view parser filled:" << time.elapsed();

    //  Update code drawer
    m_codeDrawer->update();
    ui->glwVisualizer->fitDrawable(m_codeDrawer);

    resetHeightmap();
    updateControlsState();
}

QTime frmMain::updateProgramEstimatedTime(QList<LineSegment*> lines)
{
    double time = 0;

    foreach (LineSegment *ls, lines) {
        double length = (ls->getEnd() - ls->getStart()).length();

        if (!std::isnan(length) && !std::isnan(ls->getSpeed()) && ls->getSpeed() != 0) time +=
                length / ((ui->chkFeedOverride->isChecked() && !ls->isFastTraverse())
                          ? (ls->getSpeed() * ui->txtFeed->value() / 100) : ls->getSpeed());

//        qDebug() << "length/time:" << length << ((ui->chkFeedOverride->isChecked() && !ls->isFastTraverse())
//                                                 ? (ls->getSpeed() * ui->txtFeed->value() / 100) : ls->getSpeed())
//                 << time;

        if (std::isnan(length)) qDebug() << "length nan:" << ls->getStart() << ls->getEnd();
        if (std::isnan(ls->getSpeed())) qDebug() << "speed nan:" << ls->getSpeed();
    }

    time *= 60;

    QTime t;

    t.setHMS(0, 0, 0);
    t = t.addSecs(time);

    ui->glwVisualizer->setSpendTime(QTime(0, 0, 0));
    ui->glwVisualizer->setEstimatedTime(t);

    return t;
}

void frmMain::clearTable()
{
    m_programModel.clear();
    m_programModel.insertRow(0);
}

void frmMain::on_cmdFit_clicked()
{
    ui->glwVisualizer->fitDrawable(m_currentDrawer);
}

void frmMain::on_cmdFileSend_clicked()
{    
    if (m_currentModel->rowCount() == 1) return;

    on_cmdFileReset_clicked();

    m_startTime.start();

    m_transferCompleted = false;
    m_transferringFile = true;
    m_storedKeyboardControl = ui->chkKeyboardControl->isChecked();
    ui->chkKeyboardControl->setChecked(false);

#ifdef WINDOWS
    if (m_taskBarProgress) {
        m_taskBarProgress->setMaximum(m_currentModel->rowCount() - 2);
        m_taskBarProgress->setValue(0);
        m_taskBarProgress->show();
    }
#endif

    updateControlsState();
    sendNextFileCommands();
}

void frmMain::sendNextFileCommands() {

    if (m_queue.length() > 0) return;

    QString command = feedOverride(m_currentModel->data(m_currentModel->index(m_fileCommandIndex, 1)).toString());

    while ((bufferLength() + command.length() + 1) <= BUFFERLENGTH && m_fileCommandIndex < m_currentModel->rowCount() - 1) {
        m_currentModel->setData(m_currentModel->index(m_fileCommandIndex, 2), tr("Sended"));
        sendCommand(command, m_fileCommandIndex);
        m_fileCommandIndex++;
        command = feedOverride(m_currentModel->data(m_currentModel->index(m_fileCommandIndex, 1)).toString());
    }
}

void frmMain::onTableCellChanged(QModelIndex i1, QModelIndex i2)
{
    GCodeTableModel *model = (GCodeTableModel*)sender();

    if (i1.column() != 1) return;
    // Inserting new line at end
    if (i1.row() == (model->rowCount() - 1) && model->data(model->index(i1.row(), 1)).toString() != "") {
        model->setData(model->index(model->rowCount() - 1, 2), tr("In queue"));
        model->insertRow(model->rowCount());
        if (!m_programLoading) ui->tblProgram->setCurrentIndex(model->index(i1.row() + 1, 1));
    // Remove last line
    } /*else if (i1.row() != (model->rowCount() - 1) && model->data(model->index(i1.row(), 1)).toString() == "") {
        ui->tblProgram->setCurrentIndex(model->index(i1.row() + 1, 1));
        m_tableModel.removeRow(i1.row());
    }*/   

    if (!m_programLoading) {

        // Clear cached args
        model->setData(model->index(i1.row(), 5), QVariant());

        // Drop heightmap cache
        if (m_currentModel == &m_programModel) m_programHeightmapModel.clear();

        // Update visualizer
        updateParser();

        // Hightlight w/o current cell changed event (double hightlight on current cell changed)
        QList<LineSegment*> list = m_viewParser.getLineSegmentList();
        for (int i = 0; i < list.count() && list[i]->getLineNumber() <= m_currentModel->data(m_currentModel->index(i1.row(), 4)).toInt(); i++) {
            list[i]->setIsHightlight(true);
        }
    }
}

void frmMain::onTableCurrentChanged(QModelIndex idx1, QModelIndex idx2)
{
    // Update toolpath hightlighting
    if (idx1.row() > m_currentModel->rowCount() - 2) idx1 = m_currentModel->index(m_currentModel->rowCount() - 2, 0);
    if (idx2.row() > m_currentModel->rowCount() - 2) idx2 = m_currentModel->index(m_currentModel->rowCount() - 2, 0);

    // Update linesegments on cell changed
    if (!m_currentDrawer->geometryUpdated()) {
        QList<LineSegment*> list = m_viewParser.getLineSegmentList();
        for (int i = 0; i < list.count(); i++) {
            list[i]->setIsHightlight(list[i]->getLineNumber() <= m_currentModel->data(m_currentModel->index(idx1.row(), 4)).toInt());
        }
    // Update vertices on current cell changed
    } else {
        GcodeViewParse *parser = m_currentDrawer->viewParser();
        QList<LineSegment*> list = parser->getLineSegmentList();
        int segmentLine;
        int modelLine = m_currentModel->data(m_currentModel->index(idx1.row(), 4)).toInt();
        int modelLinePrevious = m_currentModel->data(m_currentModel->index(idx2.row(), 4)).toInt();
        QList<int> indexes;

        for (int i = 0; i < list.count(); i++) {
            segmentLine = list[i]->getLineNumber();
            // Highlight
            if (idx1.row() > idx2.row()) {
                if (segmentLine > modelLinePrevious && segmentLine <= modelLine) {
                    list[i]->setIsHightlight(true);
                    indexes.append(i);
                }
            // Reset
            } else {
                if (segmentLine <= modelLinePrevious && segmentLine > modelLine) {
                    list[i]->setIsHightlight(false);
                    indexes.append(i);
                }
            }
        }

        if (!indexes.isEmpty()) m_currentDrawer->update(indexes);
    }
}

void frmMain::onTableInsertLine()
{
    if (ui->tblProgram->selectionModel()->selectedRows().count() == 0 || m_transferringFile) return;

    int row = ui->tblProgram->selectionModel()->selectedRows()[0].row();

    m_currentModel->insertRow(row);
    m_currentModel->setData(m_currentModel->index(row, 2), tr("In queue"));

    updateParser();
    m_cellChanged = true;
    ui->tblProgram->selectRow(row);
}

void frmMain::onTableDeleteLines()
{
    if (ui->tblProgram->selectionModel()->selectedRows().count() == 0 || m_transferringFile ||
            QMessageBox::warning(this, this->windowTitle(), tr("Delete lines?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) return;

    QModelIndex firstRow = ui->tblProgram->selectionModel()->selectedRows()[0];
    int rowsCount = ui->tblProgram->selectionModel()->selectedRows().count();

    for (int i = 0; i < rowsCount && firstRow.row() != m_currentModel->rowCount() - 1; i++) {
        m_currentModel->removeRow(firstRow.row());
    }

    // Drop heightmap cache
    if (m_currentModel == &m_programModel) m_programHeightmapModel.clear();

    updateParser();
    m_cellChanged = true;
    ui->tblProgram->selectRow(firstRow.row());
}

void frmMain::on_actServiceSettings_triggered()
{
    if (m_frmSettings.exec()) {
        qDebug() << "Applying settings";
        qDebug() << "Port:" << m_frmSettings.port() << "Baud:" << m_frmSettings.baud();

        if (m_frmSettings.port() != "" && (m_frmSettings.port() != m_serialPort.portName() ||
                                           m_frmSettings.baud() != m_serialPort.baudRate())) {
            if (m_serialPort.isOpen()) m_serialPort.close();
            m_serialPort.setPortName(m_frmSettings.port());
            m_serialPort.setBaudRate(m_frmSettings.baud());
            openPort();
        }

        updateControlsState();
        applySettings();
    } else {
        m_frmSettings.undo();
    }
}

bool buttonLessThan(StyledToolButton *b1, StyledToolButton *b2)
{
    return b1->text().toDouble() < b2->text().toDouble();
}

void frmMain::applySettings() {
    m_originDrawer->setLineWidth(m_frmSettings.lineWidth());
    m_toolDrawer.setToolDiameter(m_frmSettings.toolDiameter());
    m_toolDrawer.setToolLength(m_frmSettings.toolLength());
    m_toolDrawer.setLineWidth(m_frmSettings.lineWidth());
    m_codeDrawer->setLineWidth(m_frmSettings.lineWidth());
    m_heightMapBorderDrawer.setLineWidth(m_frmSettings.lineWidth());
    m_heightMapGridDrawer.setLineWidth(0.1);
    m_heightMapInterpolationDrawer.setLineWidth(m_frmSettings.lineWidth());
    m_arcPrecision = m_frmSettings.arcPrecision();
    ui->glwVisualizer->setLineWidth(m_frmSettings.lineWidth());
    m_showAllCommands = m_frmSettings.showAllCommands();
    m_safeZ = m_frmSettings.safeZ();
    m_rapidSpeed = m_frmSettings.rapidSpeed();
    m_timerStateQuery.setInterval(m_frmSettings.queryStateTime());

    m_toolDrawer.setToolAngle(m_frmSettings.toolType() == 0 ? 180 : m_frmSettings.toolAngle());
    m_toolDrawer.setColor(m_frmSettings.colors("Tool"));
    m_toolDrawer.update();

    ui->glwVisualizer->setAntialiasing(m_frmSettings.antialiasing());
    ui->glwVisualizer->setMsaa(m_frmSettings.msaa());
    ui->glwVisualizer->setZBuffer(m_frmSettings.zBuffer());
    ui->glwVisualizer->setFps(m_frmSettings.fps());
    ui->glwVisualizer->setColorBackground(m_frmSettings.colors("VisualizerBackground"));
    ui->glwVisualizer->setColorText(m_frmSettings.colors("VisualizerText"));

    ui->txtSpindleSpeed->setMinimum(m_frmSettings.spindleSpeedMin());
    ui->txtSpindleSpeed->setMaximum(m_frmSettings.spindleSpeedMax());
    ui->sliSpindleSpeed->setMinimum(ui->txtSpindleSpeed->minimum() / 100);
    ui->sliSpindleSpeed->setMaximum(ui->txtSpindleSpeed->maximum() / 100);

    ui->grpHeightMap->setVisible(m_frmSettings.panelHeightmap());
    ui->grpSpindle->setVisible(m_frmSettings.panelSpindle());
    ui->grpFeed->setVisible(m_frmSettings.panelFeed());
    ui->grpJog->setVisible(m_frmSettings.panelJog());

    ui->scrollArea->setVisible(m_frmSettings.panelHeightmap() || m_frmSettings.panelFeed()
                               || m_frmSettings.panelJog() || m_frmSettings.panelSpindle());

    ui->cboCommand->setAutoCompletion(m_frmSettings.autoCompletion());

    m_codeDrawer->setSimplify(m_frmSettings.simplify());
    m_codeDrawer->setSimplifyPrecision(m_frmSettings.simplifyPrecision());
    m_codeDrawer->setColorNormal(m_frmSettings.colors("ToolpathNormal"));
    m_codeDrawer->setColorDrawn(m_frmSettings.colors("ToolpathDrawn"));
    m_codeDrawer->setColorHighlight(m_frmSettings.colors("ToolpathHighlight"));
    m_codeDrawer->setColorZMovement(m_frmSettings.colors("ToolpathZMovement"));
    m_codeDrawer->setColorStart(m_frmSettings.colors("ToolpathStart"));
    m_codeDrawer->setColorEnd(m_frmSettings.colors("ToolpathEnd"));

    m_codeDrawer->update();
}

void frmMain::updateParser()
{       
    QTime time;

    qDebug() << "updating parser:" << m_currentModel << m_currentDrawer;
    time.start();

    GcodeViewParse *parser = m_currentDrawer->viewParser();

    GcodeParser gp;
    gp.setTraverseSpeed(m_rapidSpeed);

    ui->tblProgram->setUpdatesEnabled(false);

    QString stripped;
    QList<QString> args;

    for (int i = 0; i < m_currentModel->rowCount() - 1; i++) {
        // Get stored args
        args = m_currentModel->data(m_currentModel->index(i, 5)).toStringList();

        // Store args if none
        if (args.isEmpty()) {
//                qDebug() << "updating args";
            stripped = GcodePreprocessorUtils::removeComment(m_currentModel->data(m_currentModel->index(i, 1)).toString());
            args = GcodePreprocessorUtils::splitCommand(stripped);
            m_currentModel->setData(m_currentModel->index(i, 5), QVariant(args));
        }

        // Add command to parser
        gp.addCommand(args);

        // Update table model
        m_currentModel->setData(m_currentModel->index(i, 2), tr("In queue"));
        m_currentModel->setData(m_currentModel->index(i, 3), "");
        m_currentModel->setData(m_currentModel->index(i, 4), gp.getCommandNumber());
    }

    ui->tblProgram->setUpdatesEnabled(true);

    parser->reset();

    updateProgramEstimatedTime(parser->getLinesFromParser(&gp, m_arcPrecision));
    m_currentDrawer->update();
    ui->glwVisualizer->updateExtremes(m_currentDrawer);
    updateControlsState();

    if (m_currentModel == &m_programModel) m_fileChanged = true;

    qDebug() << "Update parser time: " << time.elapsed();
}

void frmMain::on_cmdCommandSend_clicked()
{
    QString command = ui->cboCommand->currentText();
    if (command.isEmpty()) return;

    ui->cboCommand->storeText();
    ui->cboCommand->setCurrentText("");
    sendCommand(command, -1);
}

void frmMain::on_actFileOpen_triggered()
{
    on_cmdFileOpen_clicked();
}

void frmMain::on_cmdHome_clicked()
{
    m_homing = true;
    sendCommand("$H");
}

void frmMain::on_cmdTouch_clicked()
{
//    m_homing = true;

    QStringList list = m_frmSettings.touchCommand().split(";");

    foreach (QString cmd, list) {
        sendCommand(cmd.trimmed());
    }
}

void frmMain::on_cmdZeroXY_clicked()
{
    m_settingZeroXY = true;
    sendCommand("G92X0Y0");
    sendCommand("$#", -2);
}

void frmMain::on_cmdZeroZ_clicked()
{
    m_settingZeroZ = true;
    sendCommand("G92Z0");
    sendCommand("$#", -2);
}

void frmMain::on_cmdReturnXY_clicked()
{    
    sendCommand(QString("G21"));
    sendCommand(QString("G53G90G0X%1Y%2").arg(m_storedX).arg(m_storedY));
    sendCommand(QString("G92X0Y0Z%1").arg(ui->txtMPosZ->text().toDouble() - m_storedZ));
}

void frmMain::on_cmdReset_clicked()
{
    grblReset();
}

void frmMain::on_cmdUnlock_clicked()
{
    sendCommand("$X");
}

void frmMain::on_cmdTopZ_clicked()
{

    sendCommand(QString("G21"));
    sendCommand(QString("G53G90G0Z%1").arg(m_safeZ));
}

void frmMain::on_cmdSpindle_toggled(bool checked)
{    
    if (!m_programSpeed) sendCommand(checked ? QString("M3 S%1").arg(ui->txtSpindleSpeed->text()) : "M5");
    ui->grpSpindle->setProperty("overrided", checked);
    style()->unpolish(ui->grpSpindle);
    ui->grpSpindle->ensurePolished();

    if (checked) {
        if (!ui->grpSpindle->isChecked()) ui->grpSpindle->setTitle(tr("Spindle") + QString(tr(" (%1)")).arg(ui->txtSpindleSpeed->text()));
    } else {
        ui->grpSpindle->setTitle(tr("Spindle"));
    }
}

void frmMain::on_txtSpindleSpeed_valueChanged(const QString &arg1)
{
}

void frmMain::on_txtSpindleSpeed_editingFinished()
{
    ui->txtSpindleSpeed->setStyleSheet("color: red;");
    m_programSpeed = true;
    ui->sliSpindleSpeed->setValue(ui->txtSpindleSpeed->value() / 100);
    m_programSpeed = false;
    m_updateSpindleSpeed = true;
}

void frmMain::on_sliSpindleSpeed_valueChanged(int value)
{
    if (!m_programSpeed) {
        ui->txtSpindleSpeed->setValue(ui->sliSpindleSpeed->value() * 100);
//        sendCommand(QString("S%1").arg(ui->sliSpindleSpeed->value() * 100), -2);
        m_updateSpindleSpeed = true;
    }

    ui->txtSpindleSpeed->setStyleSheet("color: red;");

    if (!ui->grpSpindle->isChecked() && ui->cmdSpindle->isChecked())
        ui->grpSpindle->setTitle(tr("Spindle") + QString(tr(" (%1)")).arg(ui->txtSpindleSpeed->text()));
}

void frmMain::on_sliSpindleSpeed_sliderReleased()
{
}

void frmMain::on_cmdYPlus_clicked()
{
    // Query parser state to restore coordinate system, hide from table and console
    sendCommand("$G", -2);
    sendCommand("G91G0Y" + ui->txtJogStep->text());
}

void frmMain::on_cmdYMinus_clicked()
{
    sendCommand("$G", -2);
    sendCommand("G91G0Y-" + ui->txtJogStep->text());
}

void frmMain::on_cmdXPlus_clicked()
{
    sendCommand("$G", -2);
    sendCommand("G91G0X" + ui->txtJogStep->text());
}

void frmMain::on_cmdXMinus_clicked()
{    
    sendCommand("$G", -2);
    sendCommand("G91G0X-" + ui->txtJogStep->text());
}

void frmMain::on_cmdZPlus_clicked()
{
    sendCommand("$G", -2);
    sendCommand("G91G0Z" + ui->txtJogStep->text());
}

void frmMain::on_cmdZMinus_clicked()
{
    sendCommand("$G", -2);
    sendCommand("G91G0Z-" + ui->txtJogStep->text());
}

void frmMain::on_chkTestMode_clicked(bool checked)
{
    sendCommand("$C");
}

void frmMain::on_cmdFilePause_clicked(bool checked)
{
    m_serialPort.write(checked ? "!" : "~");
}

void frmMain::on_cmdFileReset_clicked()
{
    m_fileCommandIndex = 0;
    m_fileProcessedCommandIndex = 0;
    m_lastDrawnLineIndex = 0;
    m_probeIndex = -1;

    if (!m_heightMapMode) {
        QTime time;

        time.start();

        QList<LineSegment*> list = m_viewParser.getLineSegmentList();

        QList<int> indexes;
        for (int i = m_lastDrawnLineIndex; i < list.count(); i++) {
            list[i]->setDrawn(false);
            indexes.append(i);
        }
        m_codeDrawer->update(indexes);

        qDebug() << "drawn false:" << time.elapsed();

        time.start();

        ui->tblProgram->setUpdatesEnabled(false);
        for (int i = 0; i < m_currentModel->rowCount() - 1; i++) {
            m_currentModel->setData(m_currentModel->index(i, 2), tr("In queue"));
            m_currentModel->setData(m_currentModel->index(i, 3), "");
        }
        ui->tblProgram->setUpdatesEnabled(true);

        qDebug() << "table updated:" << time.elapsed();

        ui->tblProgram->scrollTo(m_currentModel->index(0, 0));
        ui->tblProgram->clearSelection();
        ui->tblProgram->selectRow(0);

        ui->glwVisualizer->setSpendTime(QTime(0, 0, 0));
    } else {
        ui->txtHeightMapGridX->setEnabled(true);
        ui->txtHeightMapGridY->setEnabled(true);
        ui->txtHeightMapGridZBottom->setEnabled(true);
        ui->txtHeightMapGridZTop->setEnabled(true);

        delete m_heightMapInterpolationDrawer.data();
        m_heightMapInterpolationDrawer.setData(NULL);

        m_heightMapModel.clear();
        updateHeightMapGrid();
    }
}

void frmMain::on_actFileNew_triggered()
{    
    qDebug() << "changes:" << m_fileChanged << m_heightMapChanged;

    if (!saveChanges(m_heightMapMode)) return;

    if (!m_heightMapMode) {
        // Reset tables
        clearTable();
        m_probeModel.clear();
        m_programHeightmapModel.clear();
        m_currentModel = &m_programModel;

        // Reset parsers
        m_viewParser.reset();
        m_probeParser.reset();

        // Reset code drawer
        m_codeDrawer->update();
        m_currentDrawer = m_codeDrawer;
        ui->glwVisualizer->fitDrawable();
        ui->glwVisualizer->setSpendTime(QTime(0, 0, 0));
        ui->glwVisualizer->setEstimatedTime(QTime(0, 0, 0));

        m_programFileName = "";
        ui->chkHeightMapUse->setChecked(false);
        ui->grpHeightMap->setProperty("overrided", false);
        style()->unpolish(ui->grpHeightMap);
        ui->grpHeightMap->ensurePolished();

        // Reset tableview
        QByteArray headerState = ui->tblProgram->horizontalHeader()->saveState();
        ui->tblProgram->setModel(NULL);

        // Set table model
        ui->tblProgram->setModel(&m_programModel);
        ui->tblProgram->horizontalHeader()->restoreState(headerState);

        // Update tableview
        connect(ui->tblProgram->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)), this, SLOT(onTableCurrentChanged(QModelIndex,QModelIndex)));
        ui->tblProgram->selectRow(0);

        resetHeightmap();
    } else {
        m_heightMapModel.clear();
        on_cmdFileReset_clicked();
        ui->txtHeightMap->setText(tr("Untitled"));
        m_heightMapFileName.clear();

        updateHeightMapBorderDrawer();
        updateHeightMapGrid();

        m_heightMapChanged = false;
    }

    updateControlsState();
}

void frmMain::on_cmdClearConsole_clicked()
{
    ui->txtConsole->clear();
}

bool frmMain::saveProgramToFile(QString fileName)
{
    QFile file(fileName);
    QDir dir;

    qDebug() << "Saving program";

    m_fileChanged = false;

    if (file.exists()) dir.remove(file.fileName());

    if (!file.open(QIODevice::WriteOnly)) return false;

    QTextStream textStream(&file);

    for (int i = 0; i < m_programModel.rowCount() - 1; i++) {
        textStream << m_programModel.data(m_programModel.index(i, 1)).toString() << "\r\n";
    }

    return true;
}

void frmMain::on_actFileSaveAs_triggered()
{
    if (!m_heightMapMode) {

        QString fileName = (QFileDialog::getSaveFileName(this, tr("Save file as"), "", tr("G-Code files (*.nc;*.ncc;*.tap)")));

        if (!fileName.isEmpty()) if (saveProgramToFile(fileName)) {
            m_programFileName = fileName;

            addRecentFile(fileName);
            updateRecentFilesMenu();

            updateControlsState();
        }
    } else {
        QString fileName = (QFileDialog::getSaveFileName(this, tr("Save file as"), "", tr("Heightmap files (*.map)")));

        if (!fileName.isEmpty()) if (saveHeightMap(fileName)) {
            ui->txtHeightMap->setText(fileName.mid(fileName.lastIndexOf("/") + 1));
            m_heightMapFileName = fileName;
            m_heightMapChanged = false;

            addRecentHeightmap(fileName);
            updateRecentFilesMenu();

//            addRecentFile(fileName);
//            updateRecentFilesMenu();

            updateControlsState();
        }
    }
}

void frmMain::on_actFileSave_triggered()
{
    if (!m_heightMapMode) {
        // G-code saving
        if (m_programFileName.isEmpty()) on_actFileSaveAs_triggered(); else saveProgramToFile(m_programFileName);
    } else {
        // Height map saving
        if (m_heightMapFileName.isEmpty()) on_actFileSaveAs_triggered(); else saveHeightMap(m_heightMapFileName);
    }
}

void frmMain::on_cmdTop_clicked()
{
    ui->glwVisualizer->setTopView();
}

void frmMain::on_cmdFront_clicked()
{
    ui->glwVisualizer->setFrontView();
}

void frmMain::on_cmdLeft_clicked()
{
    ui->glwVisualizer->setLeftView();
}

void frmMain::on_cmdIsometric_clicked()
{
    ui->glwVisualizer->setIsometricView();
}

void frmMain::on_actAbout_triggered()
{
    m_frmAbout.exec();
}

bool frmMain::dataIsEnd(QString data) {
    QStringList ends;

    ends << "ok";
    ends << "error";
//    ends << "Reset to continue";
//    ends << "'$' for help";
//    ends << "'$H'|'$X' to unlock";
//    ends << "Caution: Unlocked";
//    ends << "Enabled";
//    ends << "Disabled";
//    ends << "Check Door";
//    ends << "Pgm End";

    foreach (QString str, ends) {
        if (data.contains(str)) return true;
    }

    return false;
}

bool frmMain::dataIsFloating(QString data) {
    QStringList ends;

    ends << "Reset to continue";
    ends << "'$H'|'$X' to unlock";
    ends << "ALARM: Hard limit. MPos?";

    foreach (QString str, ends) {
        if (data.contains(str)) return true;
    }

    return false;
}

QString frmMain::feedOverride(QString command)
{
    // Feed override
    if (ui->chkFeedOverride->isChecked()) command = GcodePreprocessorUtils::overrideSpeed(command, ui->txtFeed->value());

    return command;
}

void frmMain::on_txtFeed_editingFinished()
{
    ui->sliFeed->setValue(ui->txtFeed->value());
}

void frmMain::on_sliFeed_valueChanged(int value)
{
    ui->txtFeed->setValue(value);
    updateProgramEstimatedTime(m_currentDrawer->viewParser()->getLineSegmentList());
}

void frmMain::on_chkFeedOverride_toggled(bool checked)
{
    ui->grpFeed->setProperty("overrided", checked);
    style()->unpolish(ui->grpFeed);
    ui->grpFeed->ensurePolished();
    updateProgramEstimatedTime(m_currentDrawer->viewParser()->getLineSegmentList());
}

void frmMain::on_grpFeed_toggled(bool checked)
{
    if (checked) {
        ui->grpFeed->setTitle(tr("Feed"));
    } else if (ui->chkFeedOverride->isChecked()) {
        ui->grpFeed->setTitle(tr("Feed") + QString(tr(" (%1)")).arg(ui->txtFeed->text()));
    }
    updateLayouts();

    ui->widgetFeed->setVisible(checked);
}

void frmMain::on_grpSpindle_toggled(bool checked)
{
    if (checked) {
        ui->grpSpindle->setTitle(tr("Spindle"));
    } else if (ui->cmdSpindle->isChecked()) {
        ui->grpSpindle->setTitle(tr("Spindle") + QString(tr(" (%1)")).arg(ui->txtSpindleSpeed->text()));
    }
    updateLayouts();

    ui->widgetSpindle->setVisible(checked);
}

void frmMain::on_grpJog_toggled(bool checked)
{
    if (checked) {
        ui->grpJog->setTitle(tr("Jog"));
    } else if (ui->chkKeyboardControl->isChecked()) {
        ui->grpJog->setTitle(tr("Jog") + QString(tr(" (%1)")).arg(ui->txtJogStep->text()));
    }
    updateLayouts();

    ui->widgetJog->setVisible(checked);
}

void frmMain::blockJogForRapidMovement() {
    m_jogBlock = true;

    const double acc = m_frmSettings.acceleration();    // Acceleration mm/sec^2
    double v = m_frmSettings.rapidSpeed() / 60;         // Rapid speed mm/sec
    double at = v / acc;                                // Acceleration time
    double s = acc * at * at / 2;                       // Distance on acceleration
    double time;
    double step = ui->txtJogStep->text().toDouble();

    if (2 * s > step) {
        time = sqrt(step / acc);
    } else {
        time = (step - 2 * s) / v + 2 * at;
    }

    qDebug() << QString("acc: %1; v: %2; at: %3; s: %4; time: %5").arg(acc).arg(v).arg(at).arg(s).arg(time);
    QTimer::singleShot(time * 1000, Qt::PreciseTimer, this, SLOT(onJogTimer()));
}

bool frmMain::eventFilter(QObject *obj, QEvent *event)
{    
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

        if (!m_transferringFile && keyEvent->key() == Qt::Key_ScrollLock && obj == this) {
            ui->chkKeyboardControl->toggle();
        }

        if (!m_transferringFile && ui->chkKeyboardControl->isChecked()) {
            // Block only autorepeated keypresses
            if (keyIsMovement(keyEvent->key()) && !(m_jogBlock && keyEvent->isAutoRepeat())) {
                blockJogForRapidMovement();

                switch (keyEvent->key()) {
                case Qt::Key_4:
                    sendCommand("G91G0X-" + ui->txtJogStep->text());
                    break;
                case Qt::Key_6:
                    sendCommand("G91G0X" + ui->txtJogStep->text());
                    break;
                case Qt::Key_8:
                    sendCommand("G91G0Y" + ui->txtJogStep->text());
                    break;
                case Qt::Key_2:
                    sendCommand("G91G0Y-" + ui->txtJogStep->text());
                    break;
                case Qt::Key_9:
                    sendCommand("G91G0Z" + ui->txtJogStep->text());
                    break;
                case Qt::Key_3:
                    sendCommand("G91G0Z-" + ui->txtJogStep->text());
                    break;
                }
            }
            else if (keyEvent->key() == Qt::Key_5 || keyEvent->key() == Qt::Key_Period) {
                QList<StyledToolButton*> stepButtons = ui->grpJog->findChildren<StyledToolButton*>(QRegExp("cmdJogStep\\d"));
                std::sort(stepButtons.begin(), stepButtons.end(), buttonLessThan);

                for (int i = 0; i < stepButtons.count(); i++) {
                    if (stepButtons[i]->isChecked()) {

                        StyledToolButton *button = stepButtons[keyEvent->key() == Qt::Key_5
                                ? (i == stepButtons.length() - 1 ? 0 : i + 1)
                                : (i == 0 ? stepButtons.length() - 1 : i - 1)];

                        ui->txtJogStep->setValue(button->text().toDouble());
                        foreach (StyledToolButton* button, ui->grpJog->findChildren<StyledToolButton*>(QRegExp("cmdJogStep\\d")))
                        {
                            button->setChecked(false);
                        }
                        button->setChecked(true);

                        if (!ui->grpJog->isChecked()) {
                            ui->grpJog->setTitle(tr("Jog") + QString(tr(" (%1)")).arg(ui->txtJogStep->text()));
                        }
                        break;
                    }
                }
            } else if (keyEvent->key() == Qt::Key_0) {
                ui->cmdSpindle->toggle();
            } else if (keyEvent->key() == Qt::Key_7) {
                ui->sliSpindleSpeed->setValue(ui->sliSpindleSpeed->value() + 1);
            } else if (keyEvent->key() == Qt::Key_1) {
                ui->sliSpindleSpeed->setValue(ui->sliSpindleSpeed->value() - 1);
            }
        }

        if (obj == ui->tblProgram && m_transferringFile) {
            if (keyEvent->key() == Qt::Key_PageDown || keyEvent->key() == Qt::Key_PageUp
                        || keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up) {
                ui->chkAutoScroll->setChecked(false);
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

bool frmMain::keyIsMovement(int key)
{
    return key == Qt::Key_4 || key == Qt::Key_6 || key == Qt::Key_8 || key == Qt::Key_2 || key == Qt::Key_9 || key == Qt::Key_3;
}

void frmMain::on_chkKeyboardControl_toggled(bool checked)
{
    ui->grpJog->setProperty("overrided", checked);
    style()->unpolish(ui->grpJog);
    ui->grpJog->ensurePolished();

    // Store/restore coordinate system
    if (checked) {
        sendCommand("$G", -2);
        if (!ui->grpJog->isChecked()) ui->grpJog->setTitle(tr("Jog") + QString(tr(" (%1)")).arg(ui->txtJogStep->text()));
    } else {
        if (m_absoluteCoordinates) sendCommand("G90");
        ui->grpJog->setTitle(tr("Jog"));
    }

    if (!m_transferringFile) m_storedKeyboardControl = checked;

    updateControlsState();
}

void frmMain::on_tblProgram_customContextMenuRequested(const QPoint &pos)
{
    if (m_transferringFile) return;

    if (ui->tblProgram->selectionModel()->selectedRows().count() > 0) {
        m_tableMenu->actions().at(0)->setEnabled(true);
        m_tableMenu->actions().at(1)->setEnabled(ui->tblProgram->selectionModel()->selectedRows()[0].row() != m_currentModel->rowCount() - 1);
    } else {
        m_tableMenu->actions().at(0)->setEnabled(false);
        m_tableMenu->actions().at(1)->setEnabled(false);
    }
    m_tableMenu->popup(ui->tblProgram->viewport()->mapToGlobal(pos));
}

void frmMain::on_splitter_splitterMoved(int pos, int index)
{
    static bool tableCollapsed = ui->splitter->sizes()[1] == 0;

    if ((ui->splitter->sizes()[1] == 0) != tableCollapsed) {
        this->setUpdatesEnabled(false);
        ui->chkAutoScroll->setVisible(ui->splitter->sizes()[1] && !m_heightMapMode);
        updateLayouts();
        resizeCheckBoxes();

        this->setUpdatesEnabled(true);
        ui->chkAutoScroll->repaint();

        // Store collapsed state
        tableCollapsed = ui->splitter->sizes()[1] == 0;
    }
}

void frmMain::updateLayouts()
{
    this->update();
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
}

void frmMain::addRecentFile(QString fileName)
{
    m_recentFiles.removeAll(fileName);
    m_recentFiles.append(fileName);
    if (m_recentFiles.count() > 5) m_recentFiles.takeFirst();
}

void frmMain::addRecentHeightmap(QString fileName)
{
    m_recentHeightmaps.removeAll(fileName);
    m_recentHeightmaps.append(fileName);
    if (m_recentHeightmaps.count() > 5) m_recentHeightmaps.takeFirst();
}

void frmMain::onActRecentFileTriggered()
{
    QAction *action = static_cast<QAction*>(sender());

    if (action != NULL) {
        if (!saveChanges(m_heightMapMode)) return;
        if (!m_heightMapMode) loadFile(action->text()); else loadHeightMap(action->text());
    }
}

void frmMain::onCboCommandReturnPressed()
{
    QString command = ui->cboCommand->currentText();
    if (command.isEmpty()) return;

    ui->cboCommand->setCurrentText("");
    sendCommand(command, -1);
}

void frmMain::updateRecentFilesMenu()
{
    foreach (QAction * action, ui->mnuRecent->actions()) {
        if (action->text() == "") break; else {
            ui->mnuRecent->removeAction(action);
            delete action;
        }
    }

    foreach (QString file, !m_heightMapMode ? m_recentFiles : m_recentHeightmaps) {
        QAction *action = new QAction(file, this);
        connect(action, SIGNAL(triggered()), this, SLOT(onActRecentFileTriggered()));
        ui->mnuRecent->insertAction(ui->mnuRecent->actions()[0], action);
    }

    updateControlsState();
}

void frmMain::on_actRecentClear_triggered()
{
    if (!m_heightMapMode) m_recentFiles.clear(); else m_recentHeightmaps.clear();
    updateRecentFilesMenu();
}

double frmMain::toMetric(double value)
{
    return m_frmSettings.units() == 0 ? value : value * 25.4;
}

void frmMain::on_grpHeightMap_toggled(bool arg1)
{
    ui->widgetHeightMap->setVisible(arg1);
}

QRectF frmMain::borderRectFromTextboxes()
{
    QRectF rect;

    rect.setX(ui->txtHeightMapBorderX->value());
    rect.setY(ui->txtHeightMapBorderY->value());
    rect.setWidth(ui->txtHeightMapBorderWidth->value());
    rect.setHeight(ui->txtHeightMapBorderHeight->value());

    return rect;
}

QRectF frmMain::borderRectFromExtremes()
{
    QRectF rect;

    rect.setX(m_codeDrawer->getMinimumExtremes().x());
    rect.setY(m_codeDrawer->getMinimumExtremes().y());
    rect.setWidth(m_codeDrawer->getSizes().x());
    rect.setHeight(m_codeDrawer->getSizes().y());

    return rect;
}

void frmMain::updateHeightMapBorderDrawer()
{
    if (m_settingsLoading) return;

    qDebug() << "updating border drawer";

    m_heightMapBorderDrawer.setBorderRect(borderRectFromTextboxes());
}

void frmMain::updateHeightMapGrid(double arg1)
{
    if (sender()->property("previousValue").toDouble() != arg1 && !updateHeightMapGrid())
        static_cast<QDoubleSpinBox*>(sender())->setValue(sender()->property("previousValue").toDouble());
    else sender()->setProperty("previousValue", arg1);
}

bool frmMain::updateHeightMapGrid()
{    
    if (m_settingsLoading) return true;

    qDebug() << "updating heightmap grid drawer";

    // Grid map changing warning
    bool nan = true;
    for (int i = 0; i < m_heightMapModel.rowCount(); i++)
        for (int j = 0; j < m_heightMapModel.columnCount(); j++)
            if (!std::isnan(m_heightMapModel.data(m_heightMapModel.index(i, j), Qt::UserRole).toDouble())) {
                nan = false;
                break;
            }
    if (!nan && QMessageBox::warning(this, this->windowTitle(), tr("Changing grid settings will reset probe data. Continue?"),
                                                           QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) return false;

    // Update grid drawer
    QRectF borderRect = borderRectFromTextboxes();
    m_heightMapGridDrawer.setBorderRect(borderRect);
    m_heightMapGridDrawer.setGridSize(QPointF(ui->txtHeightMapGridX->value(), ui->txtHeightMapGridY->value()));
    m_heightMapGridDrawer.setZBottom(ui->txtHeightMapGridZBottom->value());
    m_heightMapGridDrawer.setZTop(ui->txtHeightMapGridZTop->value());

    // Reset model
//    int gridPointsX = trunc(borderRect.width() / ui->txtHeightMapGridX->value()) + 1;
//    int gridPointsY = trunc(borderRect.height() / ui->txtHeightMapGridY->value()) + 1;
    int gridPointsX = ui->txtHeightMapGridX->value();
    int gridPointsY = ui->txtHeightMapGridY->value();

    m_heightMapModel.resize(gridPointsX, gridPointsY);
    ui->tblHeightMap->setModel(NULL);
    ui->tblHeightMap->setModel(&m_heightMapModel);
    resizeTableHeightMapSections();

    // Update interpolation
    updateHeightMapInterpolationDrawer(true);

    // Generate probe program
    double gridStepX = gridPointsX > 1 ? borderRect.width() / (gridPointsX - 1) : 0;
    double gridStepY = gridPointsY > 1 ? borderRect.height() / (gridPointsY - 1) : 0;

    qDebug() << "generating probe program";

    m_programLoading = true;
    m_probeModel.clear();
    m_probeModel.insertRow(0);

    m_probeModel.setData(m_probeModel.index(m_probeModel.rowCount() - 1, 1), QString("G21G90F%1G0Z%2").
                         arg(m_frmSettings.heightmapProbingFeed()).arg(ui->txtHeightMapGridZTop->value()));
    m_probeModel.setData(m_probeModel.index(m_probeModel.rowCount() - 1, 1), QString("G0X0Y0"));
//                         .arg(ui->txtHeightMapGridZTop->value()));
    m_probeModel.setData(m_probeModel.index(m_probeModel.rowCount() - 1, 1), QString("G38.2Z%1")
                         .arg(ui->txtHeightMapGridZBottom->value()));
    m_probeModel.setData(m_probeModel.index(m_probeModel.rowCount() - 1, 1), QString("G0Z%1")
                         .arg(ui->txtHeightMapGridZTop->value()));

    double x, y;

    for (int i = 0; i < gridPointsY; i++) {
        y = borderRect.top() + gridStepY * i;
        for (int j = 0; j < gridPointsX; j++) {
            x = borderRect.left() + gridStepX * (i % 2 ? gridPointsX - 1 - j : j);
            m_probeModel.setData(m_probeModel.index(m_probeModel.rowCount() - 1, 1), QString("G0X%1Y%2")
                                 .arg(x, 0, 'f', 3).arg(y, 0, 'f', 3));
            m_probeModel.setData(m_probeModel.index(m_probeModel.rowCount() - 1, 1), QString("G38.2Z%1")
                                 .arg(ui->txtHeightMapGridZBottom->value()));
            m_probeModel.setData(m_probeModel.index(m_probeModel.rowCount() - 1, 1), QString("G0Z%1")
                                 .arg(ui->txtHeightMapGridZTop->value()));
        }
    }

    m_programLoading = false;

    if (m_currentDrawer == m_probeDrawer) updateParser();

    m_heightMapChanged = true;
    return true;
}

void frmMain::updateHeightMapInterpolationDrawer(bool reset)
{
    if (m_settingsLoading) return;    

    qDebug() << "Updating interpolation";

    QRectF borderRect = borderRectFromTextboxes();
    m_heightMapInterpolationDrawer.setBorderRect(borderRect);

    QVector<QVector<double>> *interpolationData = new QVector<QVector<double>>;

    int interpolationPointsX = ui->txtHeightMapInterpolationStepX->value();// * (ui->txtHeightMapGridX->value() - 1) + 1;
    int interpolationPointsY = ui->txtHeightMapInterpolationStepY->value();// * (ui->txtHeightMapGridY->value() - 1) + 1;

    double interpolationStepX = interpolationPointsX > 1 ? borderRect.width() / (interpolationPointsX - 1) : 0;
    double interpolationStepY = interpolationPointsY > 1 ? borderRect.height() / (interpolationPointsY - 1) : 0;

    for (int i = 0; i < interpolationPointsY; i++) {
        QVector<double> row;
        for (int j = 0; j < interpolationPointsX; j++) {

            double x = interpolationStepX * j + borderRect.x();
            double y = interpolationStepY * i + borderRect.y();

            row.append(reset ? NAN : Interpolation::bicubicInterpolate(borderRect, &m_heightMapModel, x, y));
        }
        interpolationData->append(row);
    }   

    if (m_heightMapInterpolationDrawer.data() != NULL) {
        delete m_heightMapInterpolationDrawer.data();
    }
    m_heightMapInterpolationDrawer.setData(interpolationData);

    // Update grid drawer
    m_heightMapGridDrawer.update();

    // Heightmap changed by table user input
    if (sender() == &m_heightMapModel) m_heightMapChanged = true;

    // Reset heightmapped program model
    m_programHeightmapModel.clear();
}

void frmMain::on_chkHeightMapBorderShow_toggled(bool checked)
{
    updateControlsState();
}

void frmMain::on_txtHeightMapBorderX_valueChanged(double arg1)
{
    updateHeightMapBorderDrawer();
    updateHeightMapGrid(arg1);
}

void frmMain::on_txtHeightMapBorderWidth_valueChanged(double arg1)
{
    updateHeightMapBorderDrawer();
    updateHeightMapGrid(arg1);
}

void frmMain::on_txtHeightMapBorderY_valueChanged(double arg1)
{
    updateHeightMapBorderDrawer();
    updateHeightMapGrid(arg1);
}

void frmMain::on_txtHeightMapBorderHeight_valueChanged(double arg1)
{
    updateHeightMapBorderDrawer();
    updateHeightMapGrid(arg1);
}

void frmMain::on_chkHeightMapGridShow_toggled(bool checked)
{
    updateControlsState();
}

void frmMain::on_txtHeightMapGridX_valueChanged(double arg1)
{    
    updateHeightMapGrid(arg1);
}

void frmMain::on_txtHeightMapGridY_valueChanged(double arg1)
{
    updateHeightMapGrid(arg1);
}

void frmMain::on_txtHeightMapGridZBottom_valueChanged(double arg1)
{
    updateHeightMapGrid(arg1);
}

void frmMain::on_txtHeightMapGridZTop_valueChanged(double arg1)
{
    updateHeightMapGrid(arg1);
}

void frmMain::on_cmdHeightMapMode_toggled(bool checked)
{
    // Update flag
    m_heightMapMode = checked;

    // Reset file progress
    m_fileCommandIndex = 0;
    m_fileProcessedCommandIndex = 0;
    m_lastDrawnLineIndex = 0;

    // Reset/restore g-code program modification on edit mode enter/exit
    if (ui->chkHeightMapUse->isChecked()) {
        on_chkHeightMapUse_clicked(!checked); // Update gcode program parser
//        m_codeDrawer->updateData(); // Force update data to properly shadowing
    }

    if (checked) {
        ui->tblProgram->setModel(&m_probeModel);
        resizeTableHeightMapSections();
        m_currentModel = &m_probeModel;
        m_currentDrawer = m_probeDrawer;
        updateParser();  // Update probe program parser
    } else {
        m_probeParser.reset();
        if (!ui->chkHeightMapUse->isChecked()) {
            ui->tblProgram->setModel(&m_programModel);
            connect(ui->tblProgram->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)), this, SLOT(onTableCurrentChanged(QModelIndex,QModelIndex)));
            ui->tblProgram->selectRow(0);

            resizeTableHeightMapSections();
            m_currentModel = &m_programModel;
            m_currentDrawer = m_codeDrawer;

            if (!ui->chkHeightMapUse->isChecked()) updateProgramEstimatedTime(m_currentDrawer->viewParser()->getLineSegmentList());
        }
    }

    // Shadow trajectory
    QList<LineSegment*> list = m_viewParser.getLineSegmentList();
    QList<int> indexes;
    for (int i = m_lastDrawnLineIndex; i < list.count(); i++) {
        list[i]->setDrawn(checked);
        list[i]->setIsHightlight(false);
        indexes.append(i);
    }   
    // Update only vertex color.
    // If chkHeightMapUse was checked codeDrawer updated via updateParser
    if (!ui->chkHeightMapUse->isChecked()) m_codeDrawer->update(indexes);

    updateRecentFilesMenu();
    updateControlsState();
}

bool frmMain::saveHeightMap(QString fileName)
{
    QFile file(fileName);
    QDir dir;

    if (file.exists()) dir.remove(file.fileName());
    if (!file.open(QIODevice::WriteOnly)) return false;

    QTextStream textStream(&file);
    textStream << ui->txtHeightMapBorderX->text() << ";"
               << ui->txtHeightMapBorderY->text() << ";"
               << ui->txtHeightMapBorderWidth->text() << ";"
               << ui->txtHeightMapBorderHeight->text() << "\r\n";
    textStream << ui->txtHeightMapGridX->text() << ";"
               << ui->txtHeightMapGridY->text() << ";"
               << ui->txtHeightMapGridZBottom->text() << ";"
               << ui->txtHeightMapGridZTop->text() << "\r\n";
    textStream << ui->cboHeightMapInterpolationType->currentIndex() << ";"
               << ui->txtHeightMapInterpolationStepX->text() << ";"
                << ui->txtHeightMapInterpolationStepY->text() << "\r\n";

    for (int i = 0; i < m_heightMapModel.rowCount(); i++) {
        for (int j = 0; j < m_heightMapModel.columnCount(); j++) {
            textStream << m_heightMapModel.data(m_heightMapModel.index(i, j), Qt::UserRole).toString() << ((j == m_heightMapModel.columnCount() - 1) ? "" : ";");
        }
        textStream << "\r\n";
    }

    file.close();

    m_heightMapChanged = false;

    return true;
}

void frmMain::loadHeightMap(QString fileName)
{
    QFile file(fileName);

    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, this->windowTitle(), tr("Can't open file:\n") + fileName);
        return;
    }
    QTextStream textStream(&file);

    m_settingsLoading = true;

    // Storing previous values
    ui->txtHeightMapBorderX->setValue(NAN);
    ui->txtHeightMapBorderY->setValue(NAN);
    ui->txtHeightMapBorderWidth->setValue(NAN);
    ui->txtHeightMapBorderHeight->setValue(NAN);

    ui->txtHeightMapGridX->setValue(NAN);
    ui->txtHeightMapGridY->setValue(NAN);
    ui->txtHeightMapGridZBottom->setValue(NAN);
    ui->txtHeightMapGridZTop->setValue(NAN);

    QList<QString> list = textStream.readLine().split(";");
    ui->txtHeightMapBorderX->setValue(list[0].toDouble());
    ui->txtHeightMapBorderY->setValue(list[1].toDouble());
    ui->txtHeightMapBorderWidth->setValue(list[2].toDouble());
    ui->txtHeightMapBorderHeight->setValue(list[3].toDouble());    

    list = textStream.readLine().split(";");
    ui->txtHeightMapGridX->setValue(list[0].toDouble());
    ui->txtHeightMapGridY->setValue(list[1].toDouble());
    ui->txtHeightMapGridZBottom->setValue(list[2].toDouble());
    ui->txtHeightMapGridZTop->setValue(list[3].toDouble());

    m_settingsLoading = false;

    updateHeightMapBorderDrawer();

    m_heightMapModel.clear();   // To avoid probe data wipe message
    updateHeightMapGrid();

    list = textStream.readLine().split(";");

//    ui->chkHeightMapBorderAuto->setChecked(false);
//    ui->chkHeightMapBorderAuto->setEnabled(false);
//    ui->txtHeightMapBorderX->setEnabled(false);
//    ui->txtHeightMapBorderY->setEnabled(false);
//    ui->txtHeightMapBorderWidth->setEnabled(false);
//    ui->txtHeightMapBorderHeight->setEnabled(false);

//    ui->txtHeightMapGridX->setEnabled(false);
//    ui->txtHeightMapGridY->setEnabled(false);
//    ui->txtHeightMapGridZBottom->setEnabled(false);
//    ui->txtHeightMapGridZTop->setEnabled(false);

    for (int i = 0; i < m_heightMapModel.rowCount(); i++) {
        QList<QString> row = textStream.readLine().split(";");
        for (int j = 0; j < m_heightMapModel.columnCount(); j++) {
            m_heightMapModel.setData(m_heightMapModel.index(i, j), row[j].toDouble(), Qt::UserRole);
        }
    }

    file.close();

    ui->txtHeightMap->setText(fileName.mid(fileName.lastIndexOf("/") + 1));
    m_heightMapFileName = fileName;
    m_heightMapChanged = false;

    ui->cboHeightMapInterpolationType->setCurrentIndex(list[0].toInt());
    ui->txtHeightMapInterpolationStepX->setValue(list[1].toDouble());
    ui->txtHeightMapInterpolationStepY->setValue(list[2].toDouble());

    updateHeightMapInterpolationDrawer();
}

void frmMain::on_chkHeightMapInterpolationShow_toggled(bool checked)
{
    updateControlsState();    
}

void frmMain::on_cmdHeightMapLoad_clicked()
{
    if (!saveChanges(true)) {
        return;
    }

    QString fileName = QFileDialog::getOpenFileName(this, tr("Open"), "", tr("Heightmap files (*.map)"));

    if (fileName != "") {
        addRecentHeightmap(fileName);
        loadHeightMap(fileName);

        // If using heightmap
        if (ui->chkHeightMapUse->isChecked() && !m_heightMapMode) {
            // Restore original file
            on_chkHeightMapUse_clicked(false);
            // Apply heightmap
            on_chkHeightMapUse_clicked(true);
        }

        updateRecentFilesMenu();
        updateControlsState(); // Enable 'cmdHeightMapMode' button
    }
}

void frmMain::on_txtHeightMapInterpolationStepX_valueChanged(double arg1)
{
    updateHeightMapInterpolationDrawer();
}

void frmMain::on_txtHeightMapInterpolationStepY_valueChanged(double arg1)
{
    updateHeightMapInterpolationDrawer();
}

void frmMain::on_chkHeightMapUse_clicked(bool checked)
{
//    static bool fileChanged;

    ui->grpHeightMap->setProperty("overrided", checked);
    style()->unpolish(ui->grpHeightMap);
    ui->grpHeightMap->ensurePolished();

    if (checked) {

        // Performance test
        QTime time;

        // Store fileChanged state
//        fileChanged = m_fileChanged;

        // Reset table view
        QByteArray headerState = ui->tblProgram->horizontalHeader()->saveState();
        ui->tblProgram->setModel(NULL);

        // Set current model to prevent reseting heightmap cache
        m_currentModel = &m_programHeightmapModel;

        // Update heightmap-modificated program if not cached
        if (m_programHeightmapModel.rowCount() == 0) {

            // Modifying linesegments
            QList<LineSegment*> *list = m_viewParser.getLines();
            QRectF borderRect = borderRectFromTextboxes();
            double x, y, z;

            time.start();

            for (int i = 0; i < list->count(); i++) {
                if (!list->at(i)->isZMovement()) {
                    QList<LineSegment*> subSegments = subdivideSegment(list->at(i));

                    if (subSegments.count() > 0) {
                        delete list->at(i);
                        list->removeAt(i);
                        foreach (LineSegment* subSegment, subSegments) list->insert(i++, subSegment);
                        i--;
                    }
                }
            }

            qDebug() << "Subdivide time: " << time.elapsed();

            time.start();

            for (int i = 0; i < list->count(); i++) {
                x = list->at(i)->getStart().x();
                y = list->at(i)->getStart().y();
                z = list->at(i)->getStart().z() + Interpolation::bicubicInterpolate(borderRect, &m_heightMapModel, x, y);
                list->at(i)->setStart(QVector3D(x, y, z));

                x = list->at(i)->getEnd().x();
                y = list->at(i)->getEnd().y();
                z = list->at(i)->getEnd().z() + Interpolation::bicubicInterpolate(borderRect, &m_heightMapModel, x, y);
                list->at(i)->setEnd(QVector3D(x, y, z));
            }

            qDebug() << "Z update time (interpolation): " << time.elapsed();

            time.start();

            // Modifying g-code program
            int lastSegmentIndex = 0;
            int commandIndex;
            int lastCommandIndex = -1;
            QString command;
            QString newCommandPrefix;
            QStringList codes;

            m_programLoading = true;
        //        m_programHeightmapModel.clear();
            m_programHeightmapModel.insertRow(0);

            for (int i = 0; i < m_programModel.rowCount(); i++) {
                commandIndex = m_programModel.data(m_programModel.index(i, 4)).toInt();
                command = m_programModel.data(m_programModel.index(i, 1)).toString();

                if (commandIndex < 0 || commandIndex == lastCommandIndex) {
                    m_programHeightmapModel.setData(m_programHeightmapModel.index(m_programHeightmapModel.rowCount() - 1, 1), command);
                } else {
                    // Get command codes
//                    codes = GcodePreprocessorUtils::splitCommand(command);
//                    qDebug() << "codes from split:" << codes;
                    // from cache
                    codes = m_programModel.data(m_programModel.index(i, 5)).toStringList();
//                    qDebug() << "codes from cache:" << codes;
                    newCommandPrefix.clear();

                    // Remove coordinates codes
                    foreach (QString code, codes) if (code.contains(QRegExp("[XxYyZzIiJjKkRr]+"))) codes.removeOne(code);
                    else newCommandPrefix.append(code);

                    // Replace arcs with lines
                    newCommandPrefix.replace(QRegExp("[Gg]0*2|[Gg]0*3"), "G1");

                    // Last motion code
                    static QString lastCode;

                    // Find first linesegment by command index
                    for (int j = lastSegmentIndex; j < list->count(); j++) {
                        if (list->at(j)->getLineNumber() == commandIndex) {

        //                        qDebug() << "Updating line:" << commandIndex << list->at(j)->getEnd().length()
        //                                 << newCommandPrefix << lastCode;

                            // If command is G0 or G1
                            if (!std::isnan(list->at(j)->getEnd().length())
                                    && (newCommandPrefix.contains(QRegExp("[Gg]0+|[Gg]0*1"))
                                        || (!newCommandPrefix.contains(QRegExp("[Gg]|[Mm]"))
                                            && lastCode.contains(QRegExp("[Gg]0+|[Gg]0*1"))))) {

                                // Store motion code
                                if (newCommandPrefix.contains(QRegExp("[Gg]0+"))) lastCode = "G0";
                                else if (newCommandPrefix.contains(QRegExp("[Gg]0*1"))) lastCode = "G1";

                                // Create new commands for each linesegment with given command index
                                while ((j < list->count()) && (list->at(j)->getLineNumber() == commandIndex)) {
                                    x = list->at(j)->getEnd().x();
                                    y = list->at(j)->getEnd().y();
                                    z = list->at(j)->getEnd().z();

                                    if (!list->at(j)->isAbsolute()) {
                                        x -= list->at(j)->getStart().x();
                                        y -= list->at(j)->getStart().y();
                                        z -= list->at(j)->getStart().z();
                                    }

                                    if (!list->at(j)->isMetric()) {
                                        x /= 25.4;
                                        y /= 25.4;
                                        z /= 25.4;
                                    }

                                    m_programHeightmapModel.setData(m_programHeightmapModel.index(m_programHeightmapModel.rowCount() - 1, 1)
                                                           , newCommandPrefix + QString("X%1Y%2Z%3")
                                                           .arg(x, 0, 'f', 3)
                                                           .arg(y, 0, 'f', 3)
                                                           .arg(z, 0, 'f', 3));
                                    if (!newCommandPrefix.isEmpty()) newCommandPrefix.clear();
                                    j++;
                                }
                            // Copy original command if not G0 or G1
                            } else {
                                m_programHeightmapModel.setData(m_programHeightmapModel.index(m_programHeightmapModel.rowCount() - 1, 1), command);
                            }

                            lastSegmentIndex = j;
                            break;
                        }
                    }
                }
                lastCommandIndex = commandIndex;
            }
        }
        qDebug() << "Model modification time: " << time.elapsed();

        ui->tblProgram->setModel(&m_programHeightmapModel);        
        ui->tblProgram->horizontalHeader()->restoreState(headerState);

        connect(ui->tblProgram->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)), this, SLOT(onTableCurrentChanged(QModelIndex,QModelIndex)));
        ui->tblProgram->selectRow(0);

        m_programLoading = false;        

        // Update parser
        m_currentDrawer = m_codeDrawer;
        updateParser();

    } else {
        QByteArray headerState = ui->tblProgram->horizontalHeader()->saveState();
        ui->tblProgram->setModel(NULL);
//        m_programHeightmapModel.clear();

        m_currentModel = &m_programModel;

        ui->tblProgram->setModel(&m_programModel);
        ui->tblProgram->horizontalHeader()->restoreState(headerState);

        connect(ui->tblProgram->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)), this, SLOT(onTableCurrentChanged(QModelIndex,QModelIndex)));
        ui->tblProgram->selectRow(0);

        bool fileChanged = m_fileChanged;
        updateParser();
        m_fileChanged = fileChanged;
    }
}

QList<LineSegment*> frmMain::subdivideSegment(LineSegment* segment)
{
    QList<LineSegment*> list;

    QRectF borderRect = borderRectFromTextboxes();

    double interpolationStepX = borderRect.width() / (ui->txtHeightMapInterpolationStepX->value() - 1);
    double interpolationStepY = borderRect.height() / (ui->txtHeightMapInterpolationStepY->value() - 1);

    double length;

    QVector3D vec = segment->getEnd() - segment->getStart();

    if (std::isnan(vec.length())) return QList<LineSegment*>();

    if (fabs(vec.x()) / fabs(vec.y()) < interpolationStepX / interpolationStepY) length = interpolationStepY / (vec.y() / vec.length());
    else length = interpolationStepX / (vec.x() / vec.length());

    length = fabs(length);

    if (std::isnan(length)) {
        qDebug() << "ERROR length:" << segment->getStart() << segment->getEnd();
        return QList<LineSegment*>();
    }

    QVector3D seg = vec.normalized() * length;
    int count = trunc(vec.length() / length);

    if (count == 0) return QList<LineSegment*>();

    for (int i = 0; i < count; i++) {
        LineSegment* line = new LineSegment(segment);
        line->setStart(i == 0 ? segment->getStart() : list[i - 1]->getEnd());
        line->setEnd(line->getStart() + seg);
        list.append(line);
    }

    if (list.count() > 0 && list.last()->getEnd() != segment->getEnd()) {
        LineSegment* line = new LineSegment(segment);
        line->setStart(list.last()->getEnd());
        line->setEnd(segment->getEnd());
        list.append(line);
    }

    return list;
}

void frmMain::on_cmdHeightMapCreate_clicked()
{
    ui->cmdHeightMapMode->setChecked(true);
    on_actFileNew_triggered();
}

void frmMain::on_cmdHeightMapMode_clicked(bool checked)
{
}

void frmMain::on_cmdHeightMapBorderAuto_clicked()
{
    QRectF rect = borderRectFromExtremes();

    if (!std::isnan(rect.width()) && !std::isnan(rect.height())) {
        ui->txtHeightMapBorderX->setValue(rect.x());
        ui->txtHeightMapBorderY->setValue(rect.y());
        ui->txtHeightMapBorderWidth->setValue(rect.width());
        ui->txtHeightMapBorderHeight->setValue(rect.height());
    }
}
