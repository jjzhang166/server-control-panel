/*
    WPN-XM Server Control Panel

    WPN-XM SCP is a GUI tool for managing server daemons under Windows.
    It's a fork of Easy WEMP written by Yann Le Moigne and (c) 2010.
    WPN-XM SCP is written by Jens-Andre Koch and (c) 2011 - onwards.

    This file is part of WPN-XM Server Stack for Windows.

    WPN-XM SCP is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    WPN-XM SCP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with WPN-XM SCP. If not, see <http://www.gnu.org/licenses/>.
*/
// Local includes
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "tray.h"
#include "configurationdialog.h"
#include "settings.h"

// Global includes
#include <QApplication>
#include <QMessageBox>
#include <QSharedMemory>
#include <QRegExp>
#include <QDesktopServices>
#include <QUrl>
#include <QDebug>
#include <QTimer>
#include <QDir>
#include <QDialogButtonBox>
#include <QCheckbox>
#include <QDesktopWidget>
#include <QDate>

class QCloseEvent;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // move window to the top
    setFocus();
    setWindowState( windowState() & ( ~Qt::WindowMinimized | Qt::WindowActive | Qt::WindowMaximized ) );

    // disable Maximize functionality
    setWindowFlags( (windowFlags() | Qt::CustomizeWindowHint) & ~Qt::WindowMaximizeButtonHint);
    setFixedWidth(620); // @todo: these values need to be adjusted, when the daemons list is automatically resized
    setFixedHeight(380);

    // overrides the window title defined in mainwindow.ui
    setWindowTitle(APP_NAME_AND_VERSION);

    setDefaultSettings();

    this->servers = new Servers();

    // inital state of status leds is disabled
    ui->label_Nginx_Status->setEnabled(false);
    ui->label_PHP_Status->setEnabled(false);
    ui->label_MariaDb_Status->setEnabled(false);
    ui->label_Memcached_Status->setEnabled(false);
    ui->label_MongoDb_Status->setEnabled(false);

    checkAlreadyActiveDaemons();

    createActions();

    // The tray icon is an instance of the QSystemTrayIcon class.
    // To check whether a system tray is present on the user's desktop,
    // we call the static QSystemTrayIcon::isSystemTrayAvailable() function.
    if ( false == QSystemTrayIcon::isSystemTrayAvailable())
    {
        QMessageBox::critical(0, APP_NAME, tr("You don't have a system tray."));
        QApplication::exit(1);
    }

    createTrayIcon();

    // fetch version numbers from the daemons and set label texts
    ui->label_Nginx_Version->setText( getNginxVersion() );
    ui->label_PHP_Version->setText( getPHPVersion() );
    ui->label_MariaDb_Version->setText( getMariaVersion() );
    ui->label_MongoDB_Version->setText( getMongoVersion() );
    ui->label_Memcached_Version->setText( getMemcachedVersion() );

    // fetch ports and set label texts
    ui->label_Nginx_Port->setText(      settings->get("nginx/port").toString() ); // 80
    ui->label_PHP_Port->setText(        settings->get("php/fastcgi-port").toString() ); // 9100
    ui->label_MariaDb_Port->setText(    settings->get("mariadb/port").toString()); // 3306
    ui->label_MongoDB_Port->setText(    settings->get("mongodb/port").toString()); // 27015
    ui->label_Memcached_Port->setText(  settings->get("memcached/port").toString() ); // 11211

    showPushButtonsOnlyForInstalledTools();
    enableToolsPushButtons(false);

    // daemon autostart
    if(settings->get("global/autostartdaemons").toBool()) {
        autostartDaemons();
    };

    showOnlyInstalledDaemons();
}

MainWindow::~MainWindow()
{
    // stop all daemons, when quitting the tray application
    if(settings->get("global/stopdaemonsonquit").toBool()) {
        qDebug() << "[Daemons] Stopping All Daemons on Quit...";
        stopAllDaemons();
    }

    delete ui;
    delete tray;
}

void MainWindow::createTrayIcon()
{
    // instantiate and attach the tray icon to the system tray
    tray = new Tray(qApp, servers, settings);

    // the following actions point to SLOTS in the tray object
    // therefore connections must be made, after instantiating Tray

    // handle clicks on the icon
    connect(tray, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(iconActivated(QSystemTrayIcon::ActivationReason)));

    // Connect Actions for Status Table - Column Status
    // if process state of a daemon changes, then change the label status in UI::MainWindow too
    connect(servers, SIGNAL(signalSetLabelStatusActive(QString, bool)),
            this, SLOT(setLabelStatusActive(QString, bool)));

    // Connect Action for, if process state of NGINX and PHP changes,
    // then change the disabled/ enabled state of pushButtons too
    connect(servers, SIGNAL(signalEnableToolsPushButtons(bool)),
            this, SLOT(enableToolsPushButtons(bool)));

    // finally: show the tray icon
    tray->show();
}

void MainWindow::createActions()
{
    // title bar - minimize
    minimizeAction = new QAction(tr("Mi&nimize"), this);
    connect(minimizeAction, SIGNAL(triggered()), this, SLOT(hide()));

    // title bar - restore
    restoreAction = new QAction(tr("&Restore"), this);
    connect(restoreAction, SIGNAL(triggered()), this, SLOT(showNormal()));

    // title bar - close
    // Note that this action is intercepted by MainWindow::closeEvent()
    // Its modified from "quit" to "close to tray" with a msgbox
    quitAction = new QAction(tr("&Quit"), this);
    connect(quitAction, SIGNAL(triggered()), this, SLOT(quitApplication()));

    // Connect Actions for Status Table - Column Action (Start)
    connect(ui->pushButton_StartNginx, SIGNAL(clicked()), servers, SLOT(startNginx()));
    connect(ui->pushButton_StartPHP, SIGNAL(clicked()), servers, SLOT(startPHP()));
    connect(ui->pushButton_StartMariaDb, SIGNAL(clicked()), servers, SLOT(startMariaDb()));
    connect(ui->pushButton_StartMongoDb, SIGNAL(clicked()), servers, SLOT(startMongoDb()));
    connect(ui->pushButton_StartMemcached, SIGNAL(clicked()), servers, SLOT(startMemcached()));

    // Connect Actions for Status Table - Column Action (Stop)
    connect(ui->pushButton_StopNginx, SIGNAL(clicked()), servers, SLOT(stopNginx()));
    connect(ui->pushButton_StopPHP, SIGNAL(clicked()), servers, SLOT(stopPHP()));
    connect(ui->pushButton_StopMariaDb, SIGNAL(clicked()), servers, SLOT(stopMariaDb()));
    connect(ui->pushButton_StopMongoDb, SIGNAL(clicked()), servers, SLOT(stopMongoDb()));
    connect(ui->pushButton_StopMemcached, SIGNAL(clicked()), servers, SLOT(stopMemcached()));

     // Connect Actions for Status Table - AllDaemons Start, Stop
    connect(ui->pushButton_AllDaemons_Start, SIGNAL(clicked()), this, SLOT(startAllDaemons()));
    connect(ui->pushButton_AllDaemons_Stop, SIGNAL(clicked()), this, SLOT(stopAllDaemons()));

    // PushButtons:: Website, Mailinglist, ReportBug, Donate
    connect(ui->pushButton_Website, SIGNAL(clicked()), this, SLOT(goToWebsite()));
    connect(ui->pushButton_GoogleGroup, SIGNAL(clicked()), this, SLOT(goToGoogleGroup()));
    connect(ui->pushButton_ReportBug, SIGNAL(clicked()), this, SLOT(goToReportIssue()));
    connect(ui->pushButton_Donate, SIGNAL(clicked()), this, SLOT(goToDonate()));

    // PushButtons: Configuration, Help, About, Close
    connect(ui->pushButton_Webinterface, SIGNAL(clicked()), this, SLOT(openProjectFolderInBrowser()));
    connect(ui->pushButton_Configuration, SIGNAL(clicked()), this, SLOT(openConfigurationDialog()));
    connect(ui->pushButton_Help, SIGNAL(clicked()), this, SLOT(openHelpDialog()));
    connect(ui->pushButton_About, SIGNAL(clicked()), this, SLOT(openAboutDialog()));
    // clicking Close, does not quit, but closes the window to tray
    connect(ui->pushButton_Close, SIGNAL(clicked()), this, SLOT(hide()));

    // Actions - Tools
    connect(ui->pushButton_tools_phpinfo, SIGNAL(clicked()), this, SLOT(openToolPHPInfo()));
    connect(ui->pushButton_tools_phpmyadmin, SIGNAL(clicked()), this, SLOT(openToolPHPMyAdmin()));
    connect(ui->pushButton_tools_webgrind, SIGNAL(clicked()), this, SLOT(openToolWebgrind()));
    connect(ui->pushButton_tools_adminer, SIGNAL(clicked()), this, SLOT(openToolAdminer()));

    // Actions - Open Projects Folder
    connect(ui->pushButton_OpenProjects_browser, SIGNAL(clicked()), this, SLOT(openProjectFolderInBrowser()));
    connect(ui->pushButton_OpenProjects_Explorer, SIGNAL(clicked()), this, SLOT(openProjectFolderInExplorer()));

    // Actions - Status Table (Config)
    connect(ui->pushButton_ConfigureNginx, SIGNAL(clicked()), this, SLOT(openConfigurationDialogNginx()));
    connect(ui->pushButton_ConfigurePHP, SIGNAL(clicked()), this, SLOT(openConfigurationDialogPHP()));
    connect(ui->pushButton_ConfigureMariaDb, SIGNAL(clicked()), this, SLOT(openConfigurationDialogMariaDb()));

    // Actions - Status Table (Logs)
    connect(ui->pushButton_ShowLog_NginxAccess, SIGNAL(clicked()), this, SLOT(openLog()));
    connect(ui->pushButton_ShowLog_NginxError, SIGNAL(clicked()), this, SLOT(openLog()));
    connect(ui->pushButton_ShowLog_PHP, SIGNAL(clicked()), this, SLOT(openLog()));
    connect(ui->pushButton_ShowLog_MongoDb, SIGNAL(clicked()), this, SLOT(openLog()));
    connect(ui->pushButton_ShowLog_MariaDb, SIGNAL(clicked()), this, SLOT(openLog()));

}

void MainWindow::changeEvent(QEvent *event)
{
    if(0 != event)
    {
        switch (event->type())
        {
            case QEvent::WindowStateChange:
            {
                // minimize to tray (do not minimize to taskbar)
                if (this->windowState() & Qt::WindowMinimized)
                {
                    QTimer::singleShot(0, this, SLOT(hide()));
                }

                break;
            }
            default:
                break;
        }
    }

    QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (tray->isVisible()) {

        bool doNotAskAgainCloseToTray = settings->get("global/donotaskagainclosetotray").toBool();

        if(!doNotAskAgainCloseToTray)
        {
            QCheckBox *checkbox = new QCheckBox(tr("Do not show this message again."), this);
            checkbox->setChecked(doNotAskAgainCloseToTray);

            QMessageBox msgbox(this);
            msgbox.setWindowTitle(qApp->applicationName());
            msgbox.setIconPixmap(QMessageBox::standardIcon(QMessageBox::Information));
            msgbox.setText(
               tr("This program will keep running in the system tray.<br>"
                  "To terminate the program, choose <b>Quit</b> in the context menu of the system tray.")
            );
            msgbox.setCheckBox(checkbox);
            msgbox.exec();

            settings->set("global/donotaskagainclosetotray", int(msgbox.checkBox()->isChecked()));
        }

        // hide mainwindow
        hide();

        // do not propagate the event to the base class
        event->ignore();
    }
    event->accept();
}

void MainWindow::setVisible(bool visible)
{
    minimizeAction->setEnabled(visible);
    restoreAction->setEnabled(isMaximized() || !visible);
    QMainWindow::setVisible(visible);
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason) {
        // Double click toggles dialog display
        case QSystemTrayIcon::DoubleClick:
            if( isVisible() )
                // clicking the tray icon, when the main window is shown, hides it
                setVisible(false);
            else {
                // clicking the tray icon, when the main window is hidden, shows the main window
                show();
                setFocus();
                setWindowState( windowState() & ( ~Qt::WindowMinimized | Qt::WindowActive | Qt::WindowMaximized ) );
            }
            break;
        default:
            break;
    }
}

void MainWindow::enableToolsPushButtons(bool enabled)
{
    // get all PushButtons from the Tools GroupBox of MainWindow::UI
    QList<QPushButton *> allPushButtonsButtons = ui->ToolsGroupBox->findChildren<QPushButton *>();

    // set all PushButtons enabled/disabled
    for(int i = 0; i < allPushButtonsButtons.size(); ++i)
    {
        allPushButtonsButtons[i]->setEnabled(enabled);
    }

    // the following two buttons provide the same functionality
    // they open up the webinterface in the browser
    // change state of "Open Projects Folder" >> "Browser" button
    ui->pushButton_OpenProjects_browser->setEnabled(enabled);
    // change state of "Rightside Toolbar" >> "Webinterface" button
    ui->pushButton_Webinterface->setEnabled(enabled);

    // webinterface configuration is only available, when nginx+php running
    ui->pushButton_ConfigureMariaDb->setEnabled(enabled);
    ui->pushButton_ConfigureMongoDb->setEnabled(enabled);
    ui->pushButton_ConfigureNginx->setEnabled(enabled);
    ui->pushButton_ConfigurePHP->setEnabled(enabled);
}

void MainWindow::showPushButtonsOnlyForInstalledTools()
{
    // get all PushButtons from the Tools GroupBox of MainWindow::UI
    QList<QPushButton *> allPushButtonsButtons = ui->ToolsGroupBox->findChildren<QPushButton *>();

    // set all PushButtons invisible
    for(int i = 0; i < allPushButtonsButtons.size(); ++i)
    {
       allPushButtonsButtons[i]->setDisabled(true);
    }

    // if tool directory exists, show pushButtons in the Tools Groupbox
    if(QDir(getProjectFolder() + "/webinterface").exists()) { ui->pushButton_tools_phpinfo->setDisabled(false); }
    if(QDir(getProjectFolder() + "/phpmyadmin").exists())   { ui->pushButton_tools_phpmyadmin->setDisabled(false); }
    if(QDir(getProjectFolder() + "/adminer").exists())      { ui->pushButton_tools_adminer->setDisabled(false); }
    if(QDir(getProjectFolder() + "/webgrind").exists())     { ui->pushButton_tools_webgrind->setDisabled(false); }
}

void MainWindow::setLabelStatusActive(QString label, bool enabled)
{
    label = label.toLower();
    if(label == "nginx")     { ui->label_Nginx_Status->setEnabled(enabled); }
    if(label == "php")       { ui->label_PHP_Status->setEnabled(enabled); }
    if(label == "mariadb")   { ui->label_MariaDb_Status->setEnabled(enabled); }
    if(label == "mongodb")   { ui->label_MongoDb_Status->setEnabled(enabled); }
    if(label == "memcached") { ui->label_Memcached_Status->setEnabled(enabled); }
}

void MainWindow::quitApplication()
{
    if(settings->get("global/stopdaemonsonquit").toBool()) {
        qDebug() << "[Daemons] Stopping on Quit...\n";
        stopAllDaemons();
    }
    qApp->quit();
}

QString MainWindow::getNginxVersion()
{
    QProcess processNginx;
    processNginx.setProcessChannelMode(QProcess::MergedChannels);
    processNginx.start("./bin/nginx/nginx.exe -v");

    if (!processNginx.waitForFinished()) {
        qDebug() << "[Nginx] Version failed:" << processNginx.errorString();
        return "";
    }

    QByteArray p_stdout = processNginx.readAll();

    // string for regexp testing
    //QString p_stdout = "nginx version: nginx/1.2.1";

    qDebug() << "[Nginx] Version: \n" << p_stdout;

    return parseVersionNumber(p_stdout);
}

QString MainWindow::getMariaVersion()
{
    QProcess processMaria;
    processMaria.setProcessChannelMode(QProcess::MergedChannels);
    processMaria.start("./bin/mariadb/bin/mysqld.exe -V"); // upper-case V

    if (!processMaria.waitForFinished()) {
        qDebug() << "[MariaDb] Version failed:" << processMaria.errorString();
        return "";
    }

    QByteArray p_stdout = processMaria.readAll();

    // string for regexp testing
    //QString p_stdout = "mysql  Ver 15.1 Distrib 5.5.24-MariaDB, for Win32 (x86)";

    qDebug() << "[MariaDb] Version: \n" << p_stdout;

    return parseVersionNumber(p_stdout.mid(15));
}

QString MainWindow::getPHPVersion()
{
    QProcess processPhp;
    processPhp.setProcessChannelMode(QProcess::MergedChannels);
    processPhp.start("./bin/php/php.exe -v");

    if (!processPhp.waitForFinished()) {
        qDebug() << "[PHP] Version failed:" << processPhp.errorString();
        return "";
    }

    QByteArray p_stdout = processPhp.readAll();

    // remove everything before "PHP" (e.g. warnings from false php.ini settings, etc)
    p_stdout.remove(0, p_stdout.indexOf("PHP"));

    // string for regexp testing
    //QString p_stdout = "PHP 5.4.3 (cli) (built: Feb 29 2012 19:06:50)";

    qDebug() << "[PHP] Version: \n" << p_stdout;

    return parseVersionNumber(p_stdout);
}

QString MainWindow::getMongoVersion()
{
    QProcess processMongoDB;
    processMongoDB.start("./bin/mongodb/bin/mongod --version");

    if (!processMongoDB.waitForFinished()) {
        qDebug() << "[MongoDB] Version failed:" << processMongoDB.errorString();
        return "";
    }

    QByteArray p_stdout = processMongoDB.readAll();

    // string for regexp testing
    //QString p_stdout = "----";

    qDebug() << "[MongoDb] Version: \n" << p_stdout;

    return parseVersionNumber(p_stdout.mid(3)); //21
}

QString MainWindow::getMemcachedVersion()
{
    QProcess processMemcached;
    processMemcached.start("./bin/memcached/memcached.exe -h");

    if (!processMemcached.waitForFinished()) {
        qDebug() << "[Memcached] Version failed:" << processMemcached.errorString();
        return "";
    }

    QByteArray p_stdout = processMemcached.readAll();

    qDebug() << "[Memcached] Version: \n" << p_stdout;

    return parseVersionNumber(p_stdout.mid(2)); //10
}

QString MainWindow::parseVersionNumber(QString stringWithVersion)
{
    //qDebug() << stringWithVersion;

    // This RegExp matches version numbers: (\d+\.)?(\d+\.)?(\d+\.)?(\*|\d+)
    // This is the same, but escaped:
    QRegExp regex("(\\d+\\.)?(\\d+\\.)?(\\d+\\.)?(\\*|\\d+)");

    // match
    regex.indexIn(stringWithVersion);

    //qDebug() << regex.cap(0);
    QString cap = regex.cap(0);
    return cap;

// Leave this for debugging reasons
//    int pos = 0;
//    while((pos = regex.indexIn(stringWithVersion, pos)) != -1)
//    {
//        qDebug() << "Match at pos " << pos
//                 << " with length " << regex.matchedLength()
//                 << ", captured = " << regex.capturedTexts().at(0).toLatin1().data()
//                 << ".\n";
//        pos += regex.matchedLength();
//    }
}

//*
//* Action slots
//*
void MainWindow::startAllDaemons()
{
    servers->startNginx();
    servers->startPHP();
    servers->startMariaDb();
    servers->startMongoDb();
    servers->startMemcached();
}

void MainWindow::stopAllDaemons()
{
    servers->stopMariaDb();
    servers->stopPHP();
    servers->stopNginx();
    servers->stopMongoDb();
    servers->stopMemcached();
}

void MainWindow::goToWebsite()
{
    QDesktopServices::openUrl(QUrl("http://wpn-xm.org/"));
}

void MainWindow::goToGoogleGroup()
{
    QDesktopServices::openUrl(QUrl("http://groups.google.com/group/wpn-xm/"));
}

void MainWindow::goToReportIssue()
{
    QDesktopServices::openUrl(QUrl("https://github.com/WPN-XM/WPN-XM/issues/"));
}

void MainWindow::goToDonate()
{
    QDesktopServices::openUrl(QUrl("http://wpn-xm.org/#donate"));
}

void MainWindow::openToolPHPInfo()
{
    QDesktopServices::openUrl(QUrl("http://localhost/webinterface/index.php?page=phpinfo"));
}

void MainWindow::openToolPHPMyAdmin()
{
    QDesktopServices::openUrl(QUrl("http://localhost/phpmyadmin/"));
}

void MainWindow::openToolWebgrind()
{
    QDesktopServices::openUrl(QUrl("http://localhost/webgrind/"));
}

void MainWindow::openToolAdminer()
{
    QDesktopServices::openUrl(QUrl("http://localhost/adminer/"));
}

void MainWindow::openWebinterface()
{
    QDesktopServices::openUrl(QUrl("http://localhost/webinterface/"));
}

void MainWindow::openProjectFolderInBrowser()
{
    // @todo open only, when Nginx and PHP are running...
    QDesktopServices::openUrl(QUrl("http://localhost"));
}

void MainWindow::openProjectFolderInExplorer()
{
    if(QDir(getProjectFolder()).exists())
    {
        // exec explorer with path to projects
        QDesktopServices::openUrl(QUrl("file:///" + getProjectFolder(), QUrl::TolerantMode));
    }
    else
    {
        QMessageBox::warning(this, tr("Warning"), tr("The projects folder was not found."));
    }
}

QString MainWindow::getProjectFolder() const
{
    return QDir::toNativeSeparators(QApplication::applicationDirPath() + "/www");
}

void MainWindow::openConfigurationDialog()
{
    ConfigurationDialog cfgDlg;
    cfgDlg.setWindowTitle("WPN-XM Server Control Panel - Configuration");
    cfgDlg.exec();
}

void MainWindow::openConfigurationDialogNginx()
{
    // Open Configuration Dialog - Tab for Nginx
    QDesktopServices::openUrl(QUrl("http://localhost/webinterface/index.php?page=config#nginx"));
}

void MainWindow::openConfigurationDialogPHP()
{
    // Open Configuration Dialog - Tab for PHP
    QDesktopServices::openUrl(QUrl("http://localhost/webinterface/index.php?page=config#php"));
}

void MainWindow::openConfigurationDialogMariaDb()
{
    // Open Configuration Dialog - Tab for MariaDb
    QDesktopServices::openUrl(QUrl("http://localhost/webinterface/index.php?page=config#mariadb"));
}

void MainWindow::openConfigurationDialogMongoDb()
{
    // Open Configuration Dialog - Tab for MongoDb
    QDesktopServices::openUrl(QUrl("http://localhost/webinterface/index.php?page=config#mongodb"));
}

void MainWindow::openLog()
{
    // we have a incoming SIGNAL object to this SLOT
    // lets determine the object name, e.g. pushButton_ShowLog_NginxAccess
    // and then reduce to the log filename

    QPushButton *button = (QPushButton *)sender();
    //qDebug() << "Sender : " << button->objectName();
    QString name = button->objectName();
    name.replace("pushButton_ShowLog_", "");

    QString logs = settings->get("paths/logs").toString();

    QString logfile = "";

    if(name == "NginxAccess"){ logfile = logs + "/access.log";}
    if(name == "NginxError") { logfile = logs + "/error.log";}
    if(name == "PHP")        { logfile = logs + "/php_error.log";}
    if(name == "MariaDb")    { logfile = logs + "/mariadb_error.log";}
    if(name == "MongoDb")    { logfile = logs + "/mongodb.log";}

    if(!QFile().exists(logfile)) {
        QMessageBox::warning(this, tr("Warning"), tr("Log file not found: \n") + logfile, QMessageBox::Yes);
    } else {
       QDesktopServices::openUrl(QUrl::fromLocalFile(logfile));
    }
}

void MainWindow::openHelpDialog()
{
    QDesktopServices::openUrl(QUrl("https://github.com/WPN-XM/WPN-XM/wiki/Using-the-Server-Control-Panel"));
}

void MainWindow::openAboutDialog()
{
    QString year = QDate::currentDate().toString("yyyy");

    QMessageBox about(this);
    about.setWindowTitle(tr("About"));
    about.setText(
        tr(
            "<table border=0>"
            "<tr><td colspan=2><img src=\":/wpnxm-logo-dark-transparent\"></img>&nbsp;"
            "<span style=\"display: inline-block; vertical-align: super; top: -20px; font-weight: bold; font-size: 16px;\">v" APP_VERSION "</span>"
            "</td></tr>"
            "<tr><td colspan=2>&nbsp;&nbsp;</td></tr>"
            "<tr><td align=center><b>Website</b></td><td><a href=\"http://wpn-xm.org/\">http://wpn-xm.org/</a><br></td></tr>"
            "<tr><td align=center><b>License</b></td><td>GNU/GPL version 3, or any later version.<br></td></tr>"
            "<tr><td align=center><b>Author(s)</b></td><td>Jens-André Koch (C) 2011 - ").append(year).append(", <br>Yann Le Moigne (C) 2010.<br></td></tr>"
            "<tr><td align=center><b>Github</b></td><td>WPN-XM is developed on Github.<br><a href=\"https://github.com/WPN-XM/WPN-XM/\">https://github.com/WPN-XM/WPN-XM/</a><br></td></tr>"
            "<tr><td align=center><b>Icons</b></td><td>We are using Yusukue Kamiyamane's Fugue Icon Set.<br><a href=\"http://p.yusukekamiyamane.com/\">http://p.yusukekamiyamane.com/</a><br></td></tr>"
            "<tr><td align=center><b>+1?</b></td><td>If you like using WPN-XM, consider supporting it:<br><a href=\"http://wpn-xm.org/#donate\">http://wpn-xm.org/#donate</a><br></td></tr>"
            "</td></tr></table>"
            "<br><br>The program is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.<br>"
    ));
    about.setParent(this);
    about.setAutoFillBackground(true);
    about.exec();
}

void MainWindow::autostartDaemons()
{
    qDebug() << "[Daemons] Autostart...";
    if(settings->get("autostart/nginx").toBool()) servers->startNginx();
    if(settings->get("autostart/php").toBool()) servers->startPHP();
    if(settings->get("autostart/mariadb").toBool()) servers->startMariaDb();
    if(settings->get("autostart/mongodb").toBool()) servers->startMongoDb();
    if(settings->get("autostart/memcached").toBool()) servers->startMemcached();
}

void MainWindow::checkAlreadyActiveDaemons()
{
    // Check list of active processes for
    // apache, nginx, mysql, php-cgi, memcached
    // and report, if processes are already running.
    // We do this to avoid collisions.

    // Provide a modal dialog with checkboxes for all running processes
    // The user might then select the processes to Leave Running or Shutdown.

    // a) fetch processes via tasklist stdout
    QProcess process;
    process.setReadChannel(QProcess::StandardOutput);
    process.setReadChannelMode(QProcess::MergedChannels);
    process.start("cmd", QStringList() << "/c tasklist.exe");
    process.waitForFinished();

    // processList contains the tasklist output
    QByteArray processList = process.readAll();
    //qDebug() << "Read" << processList.length() << "bytes";
    //qDebug() << processList;

    // b) define processes to look for
    QStringList processesToSearch;
    processesToSearch << "nginx"
                      << "apache"
                      << "memcached"
                      << "mysqld"
                      << "php-cgi"
                      << "mongod"
                      << "pg_ctl"; // postgresql

    // c) init a list for found processes
    QStringList processesFoundList;

    // d) foreach processesToSearch take a look in the processList
    for (int i = 0; i < processesToSearch.size(); ++i)
    {
        //qDebug() << "Searching for process: " << processesToSearch.at(i).toLocal8Bit().constData() << endl;

        if(processList.contains( processesToSearch.at(i).toLatin1().constData() ))
        {
            // process found
            processesFoundList << processesToSearch.at(i).toLatin1().constData();
        }
    }

    //qDebug() << "Already running Processes found : " << processesFoundList;

    // only show the "process shutdown" dialog, when there are processes to shutdown
    if(false == processesFoundList.isEmpty())
    {
        QLabel *labelA = new QLabel(tr("The following processes are already running:"));
        QGroupBox *groupBox = new QGroupBox(tr("Running Processes"));
        QVBoxLayout *vbox = new QVBoxLayout;

        // iterate over proccesFoundList and draw a "process shutdown" checkbox for each one
        int c = processesFoundList.size();
        for(int i = 0; i < c; ++i) {
           // create checkbox
           QCheckBox *checkbox = new QCheckBox(processesFoundList.at(i));
           checkbox->setChecked(true);
           checkbox->setCheckable(true);
           // add checkbox to view
           vbox->addWidget(checkbox);
        }

        groupBox->setLayout(vbox);

        QLabel *labelB = new QLabel(tr("Please select the processes you wish to shutdown.<br><br>"
                                       "Click Shutdown to shut the selected processes down and continue using the server control panel.<br>"
                                       "To proceed without shuting processes down, click Continue.<br>"));

        QPushButton *ShutdownButton = new QPushButton(tr("Shutdown"));
        QPushButton *ContinueButton = new QPushButton(tr("Continue"));
        ShutdownButton->setDefault(true);

        QDialogButtonBox *buttonBox = new QDialogButtonBox(Qt::Horizontal);
        buttonBox->addButton(ShutdownButton, QDialogButtonBox::ActionRole);
        buttonBox->addButton(ContinueButton, QDialogButtonBox::ActionRole);

        // e) build dialog to inform user about running processes
        QGridLayout *grid = new QGridLayout;
        grid->addWidget(labelA);
        grid->addWidget(groupBox);
        grid->addWidget(labelB);
        grid->addWidget(buttonBox);

        QDialog *dlg = new QDialog(this);
        dlg->setWindowModality(Qt::WindowModal);
        dlg->setLayout(grid);
        dlg->resize(250, 100);
        dlg->setWindowTitle(tr(APP_NAME));
        dlg->setWindowFlags(dlg->windowFlags() | Qt::WindowStaysOnTopHint);

        // Set signal and slot for "Buttons"
        connect(ShutdownButton, SIGNAL(clicked()), dlg, SLOT(accept()));
        connect(ContinueButton, SIGNAL(clicked()), dlg, SLOT(reject()));

        // fire modal window event loop and wait for button clicks
        // if shutdown was clicked (accept), execute shutdowns
        // if continue was clicked (reject), do nothing and proceed to mainwindow
        if(dlg->exec() == QDialog::Accepted)
        {
            // fetch all checkboxes
            QList<QCheckBox *> allCheckBoxes = dlg->findChildren<QCheckBox *>();

            // iterate checkbox values
            c = allCheckBoxes.size();
            for(int i = 0; i < c; ++i) {
               QCheckBox *cb = allCheckBoxes.at(i);
               if(cb->isChecked())
               {
                   //qDebug() << "Shutting down :" << cb->text();

                   QProcess::startDetached("cmd.exe",
                    // taskkill parameters:
                    // /f = force shutdown, /t = structure shutdown, /im = the name of the process
                    // nginx and mariadb need a forced shutdown !
                    QStringList() << "/c" << "taskkill /f /t /im " + cb->text() + ".exe"
                   );
               }
               delete cb;
            }
        }
        delete vbox;
        delete labelA;
        delete labelB;
        delete ShutdownButton;
        delete ContinueButton;
        delete buttonBox;
        delete groupBox;
        delete grid;
        delete dlg;
    }
}

void MainWindow::setDefaultSettings()
{
    // if the INI is not existing yet, set defaults, they will be written to file
    // if the INI exists, do not set the defaults but read them from file
    if(false == QFile(settings->file()).exists()) {

    // The MainWindow position. Default value is screen center.
    //QPoint center = QApplication::desktop()->screenGeometry().center();
    //settings->set("mainwindow/position", center);

    //QMap<QString, QVariant> languages;
    //languages[str::sLanguageEnglishTitle] = str::sLanguageEnglishKey;
    //languages[str::sLanguageRussianTitle] = str::sLanguageRussianKey;
    //m_defaultManager.addProperty(str::sDefLanguages, languages, languages);

    settings->set("global/runonstartup",      0);
    settings->set("global/autostartdaemons",  0);
    settings->set("global/stopdaemonsonquit", 1);
    settings->set("global/clearlogsonstart",  0);
    settings->set("global/donotaskagainclosetotray", 0);

    settings->set("paths/logs",             "./logs");
    settings->set("paths/php",              "./bin/php");
    settings->set("paths/mongodb",          "./bin/mongodb/bin");
    settings->set("paths/memcached",        "./bin/memcached");
    settings->set("paths/mariadb",          "./bin/mariadb/bin");
    settings->set("paths/nginx",            "./bin/nginx");

    settings->set("autostart/nginx",        1);
    settings->set("autostart/php",          1);
    settings->set("autostart/mariadb",      1);
    settings->set("autostart/mongodb",      0);
    settings->set("autostart/memcached",    0);

    settings->set("php/config",             "./bin/php/php.ini");
    settings->set("php/fastcgi-host",       "localhost");
    settings->set("php/fastcgi-port",       9100);

    settings->set("nginx/config",           "./bin/nginx/conf/nginx.conf");
    settings->set("nginx/sites",            "./www");
    settings->set("nginx/port",             80);

    settings->set("mariadb/config",         "./bin/mariadb/my.ini");
    settings->set("mariadb/port",           3306);
    settings->set("mariadb/password",       "");

    settings->set("memcached/port",         11211);

    settings->set("mongodb/config",         "./bin/mongodb/mongodb.conf");
    settings->set("mongodb/port",           27015);

    qDebug() << "[Settings] Loaded Defaults...\n";
    }
}

void MainWindow::showOnlyInstalledDaemons()
{
    removeRow(ui->DaemonsGridLayout, ui->DaemonsGridLayout->rowCount(), true);

}

/**
 * Helper function. Deletes all child widgets of the given layout @a item.
 */
void MainWindow::deleteChildWidgets(QLayoutItem *item) {
    if (item->layout()) {
        // Process all child items recursively.
        for (int i = 0; i < item->layout()->count(); i++) {
            deleteChildWidgets(item->layout()->itemAt(i));
        }
    }
    delete item->widget();
}

/**
 * Helper function. Removes all layout items within the given @a layout
 * which either span the given @a row or @a column. If @a deleteWidgets
 * is true, all concerned child widgets become not only removed from the
 * layout, but also deleted.
 */
void MainWindow::remove(QGridLayout *layout, int row, int column, bool deleteWidgets) {
    // We avoid usage of QGridLayout::itemAtPosition() here to improve performance.
    for (int i = layout->count() - 1; i >= 0; i--) {
        int r, c, rs, cs;
        layout->getItemPosition(i, &r, &c, &rs, &cs);
        if ((r <= row && r + rs - 1 >= row) || (c <= column && c + cs - 1 >= column)) {
            // This layout item is subject to deletion.
            QLayoutItem *item = layout->takeAt(i);
            if (deleteWidgets) {
                deleteChildWidgets(item);
            }
            delete item;
        }
    }
}


/**
 * Removes all layout items on the given @a row from the given grid
 * @a layout. If @a deleteWidgets is true, all concerned child widgets
 * become not only removed from the layout, but also deleted. Note that
 * this function doesn't actually remove the row itself from the grid
 * layout, as this isn't possible (i.e. the rowCount() and row indices
 * will stay the same after this function has been called).
 */
void MainWindow::removeRow(QGridLayout *layout, int row, bool deleteWidgets) {
    remove(layout, row, -1, deleteWidgets);
    layout->setRowMinimumHeight(row, 0);
    layout->setRowStretch(row, 0);
}

/**
 * Removes all layout items on the given @a column from the given grid
 * @a layout. If @a deleteWidgets is true, all concerned child widgets
 * become not only removed from the layout, but also deleted. Note that
 * this function doesn't actually remove the column itself from the grid
 * layout, as this isn't possible (i.e. the columnCount() and column
 * indices will stay the same after this function has been called).
 */
/*void MainWindow::removeColumn(QGridLayout *layout, int column, bool deleteWidgets) {
    remove(layout, -1, column, deleteWidgets);
    layout->setColumnMinimumWidth(column, 0);
    layout->setColumnStretch(column, 0);
}*/
