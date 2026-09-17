// *****************************************************
// L64 - Yet Another C64 Emultator for Linux
// (c) 2026 by Felice Murolo, Salerno, Italia
// email: linuxboy@fel.hotpo.org
// *****************************************************

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "common.h"

#include <chrono>
#include <iostream>
using namespace std;

#include <QCloseEvent>
#include <QFile>
#include <QDir>
#include <QFileDialog>

#include "timebase.h"
timebase *tb=nullptr;
auto startTime = chrono::high_resolution_clock::now();

uint8_t memory[65536];
uint8_t chars[4096];
uint8_t basic[8192];
uint8_t kernal[8192];
uint8_t ioarea[4096];

#include "cpu.h"
CPU *cpu=nullptr;
#include "c64view.h"
C64View *c64view=nullptr;
#include "vic.h"
VIC *vic=nullptr;
#include "cia.h"
CIA1 *cia1=nullptr;
CIA2 *cia2=nullptr;
#include "keyboard.h"
Keyboard *keyb=nullptr;
#include "sid.h"
SID *sid=nullptr;
#include "drive.h"
Drive *drive=nullptr;
#include "d64image.h"
D64Image *d64=nullptr;
DriveStatusChannel drive15;

QString lastdisk;

extern QApplication *app;
extern bool joystick_emulation_enabled;

QString prgname = "L64";
QString version = "1.0a";
QString copyright = "(c) 2026 by Felice Murolo - Salerno - Italia";

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(prgname+" v"+version+" - "+copyright);
    setWindowIcon(QIcon(":/images/logo"));
    connect(app, &QApplication::focusChanged, this, &MainWindow::onFocusChanged);

    ui->actionJoystick->setChecked(true);
    joystick_emulation_enabled = true;

    QFile c(QDir::homePath()+"/.l64.cfg");
    if (c.open(QFile::ReadOnly)){
        while(!c.atEnd()){
            QString s = c.readLine();
            s = s.trimmed();
            QStringList ss = s.split("=");
            if (s.startsWith("LastDisk=")) lastdisk = ss[1];
        }
        c.close();
    }


    QFile f(":/rom/kernal");
    if (f.open(QFile::ReadOnly)){
        f.read(reinterpret_cast<char*>(kernal),8192);
        memcpy(&memory[0xe000],&kernal,8192);
        cout << "Kernal loaded" << endl;
        f.close();
    }

    f.setFileName(":/rom/chars");
    if (f.open(QFile::ReadOnly)){
        f.read(reinterpret_cast<char*>(chars),4096);
        f.close();
        cout << "Chars loaded" << endl;
    }

    f.setFileName(":/rom/basic");
    if (f.open(QFile::ReadOnly)){
        f.read(reinterpret_cast<char*>(basic),8192);
        f.close();
        cout << "Basic loaded" << endl;
    }

    memset(&memory[0x0400],0x20,1000);
    cout << "C64 screen init..." << endl;

    c64view = new C64View();
    ui->vlayout->addWidget(c64view);

    cpu = new CPU(this);
    connect(cpu,SIGNAL(finished()),cpu,SLOT(deleteLater()));
    connect(cpu,SIGNAL(finished()),this,SLOT(cpuerror()));
    cpu->Reset();

    cia1 = new CIA1(this);
    cia2 = new CIA2(this);
    cia1->Reset();
    cia2->Reset();

    vic = new VIC(this);
    vic->Reset();

    drive = new Drive(this);
    d64 = new D64Image(this);
    if (lastdisk.length()) d64->LoadImage(lastdisk.toStdString());

    if (d64->IsLoaded()) ui->lbdiskstatus->setText("Loaded");

    tb = new timebase(this);
    connect(tb,SIGNAL(finished()),tb,SLOT(deleteLater()));
    connect(tb,SIGNAL(frameReady()),c64view,SLOT(updateFrame()));

    sid = new SID(this);

    tb->start();
    startTime = chrono::high_resolution_clock::now();

    keyb = new Keyboard(this);

    cpu->start();
    setFocus();

    tim = new QTimer(this);
    connect(tim,SIGNAL(timeout()),this,SLOT(timeout()));
    tim->start(500);
}

MainWindow::~MainWindow()
{
    cout << "Deleting MainWindow..." << endl;
    delete ui;
}

void MainWindow::loadprg(QString filename)
{
    uint16_t startaddress=0, endaddress=0,size=0;
    uint8_t s[2];

    QFile f(filename);
    if (f.open(QFile::ReadOnly)){
        size = f.size()-2;
        f.read((char *)s,2);
        startaddress = s[0]+s[1]*256;
        cout << "Loading PRG " << filename.toLocal8Bit().data() << " from location " << (uint) startaddress << " ...";
        endaddress = startaddress + size;
        f.read((char *)&memory[startaddress],size);
        cout << f.size() << " bytes loaded!" << endl;
        f.close();


        if (size>0 && startaddress == 2049){
            // Imposta il puntatore "Inizio delle variabili BASIC / Fine del programma" ($2D-$2E)
            memory[0x002D] = endaddress & 0xFF;
            memory[0x002E] = endaddress >> 8;
            memory[0x00AE] = endaddress & 0xFF;
            memory[0x00AF] = endaddress >> 8;

            // Imposta gli altri puntatori di sicurezza del BASIC ($2F-$30 e $31-$32)
            memory[0x002F] = endaddress & 0xFF;
            memory[0x0030] = endaddress >> 8;
            memory[0x0031] = endaddress & 0xFF;
            memory[0x0032] = endaddress >> 8;

            cout << TC(filename) << " loaded (" << (uint) size+2 << " bytes loaded)." << endl;
            memory[0x0277] = 'R';
            memory[0x0278] = 'U';
            memory[0x0279] = 'N';
            memory[0x027A] = '\r';
            memory[0x00C6] = 4;

        }
    }
}

void MainWindow::saveCfg()
{
    QFile f(QDir::homePath()+"/.l64.cfg");
    if (f.open(QFile::WriteOnly)){
        QString s = "LastDisk="+lastdisk;
        f.write(TC(s));
        f.close();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->accept();
    cout << "Closing MainWindow..." << endl;
    exit();
}

bool cassetteflg=false;
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (!cassetteflg){
        cassetteflg = true;
        memory[0x01] |= 1<<4;   // attiva il bit 4 (nessun tasto premuto sul tape) esclusivamente alla prima pressione di un tasto sul c64
        memory[0x01] |= 1<<5;   // e comunque il bit 5 (motore off) si attiva automaticamente quando il bit 4 viene attivato
    }
    keyb->keyPress(event->key());
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    keyb->keyRelease(event->key());
}

void MainWindow::testclick()
{
    //loadprg("/mnt/nv1/c64/Assembly/aa.prg");
    loadprg(":/test/aa");
}

void MainWindow::ecmclick()
{
    loadprg(":/test/ecm");
}

void MainWindow::bmmclick()
{
    loadprg(":/test/display");
}

void MainWindow::bmm2click()
{
    loadprg(":/test/display2");
}

void MainWindow::mcmclick()
{
    loadprg(":/test/multicolor");
}

void MainWindow::bmmmcmclick()
{
    loadprg(":/test/displaymcm");
}

void MainWindow::rasterintclick()
{
    loadprg(":/test/rasterinterrupt");
}

void MainWindow::rasterint2click()
{
    loadprg(":/test/rasterinterrupt2");
}

void MainWindow::fontclick()
{
    loadprg(":/test/font");
}

void MainWindow::spriteclick()
{
    loadprg(":test/sprite");
}

void MainWindow::chopperclick()
{
    loadprg(":/test/chopperflight");
}

void MainWindow::misuraclockclick()
{
    loadprg(":/test/misuraclock");
}

void MainWindow::exit()
{
    if (cpu) {
        cout << "Requesting interruption for CPU thread..." << endl;
        cpu->requestInterruption();
        cpu->wait();
    }
    if (tb) {
        cout << "Requesting interruption for timebase thread..." << endl;
        tb->requestInterruption();
        tb->wait();
    }
    QApplication::exit();
}

void MainWindow::attach()
{
    QFileDialog *fd = new QFileDialog(this,"Select your .D64 image file...","/mnt/nv1/c64/floppy/mio","d64");
    if (fd->exec()){
        QStringList res = fd->selectedFiles();
        lastdisk = res[0];
        d64->LoadImage(lastdisk.toStdString());
        saveCfg();
    }

}

void MainWindow::loadrun()
{
    QFileDialog *fd = new QFileDialog(this,"Select your .PRG file...","/mnt/nv1/c64/Assembly","prg");
    if (fd->exec()){
        QStringList res = fd->selectedFiles();
        loadprg(res[0]);
    }
}

void MainWindow::reset()
{
    cout << "Software reset..." << endl;
    if (cia1) cia1->Reset();
    if (cia2) cia2->Reset();
    if (vic) vic->Reset();
    if (cpu) cpu->Reset();
}

void MainWindow::hardreset()
{
    cout << "Hardware reset..." << endl;
    if (tb) {
        cout << "Requesting interruption for timebase thread..." << endl;
        tb->requestInterruption();
        tb->wait();
    }
    memset(&memory,0,65536);
    memcpy(&memory[0xe000],&kernal,8192);
    memset(&ioarea,0,4096);

    tb = new timebase(this);
    connect(tb,SIGNAL(finished()),tb,SLOT(deleteLater()));
    connect(tb,SIGNAL(frameReady()),c64view,SLOT(updateFrame()));
    tb->start();

    if (cia1) cia1->Reset();
    if (cia2) cia2->Reset();
    if (vic) vic->Reset();
    if (cpu) cpu->Reset();
}

void MainWindow::getdirectory()
{
    vector<uint8_t> dir_bytes = d64->LoadDirectory();
    if (dir_bytes.size()>0){
        uint16_t start_addr = 0x0801; // Indirizzo standard BASIC
        uint16_t end_addr = start_addr + dir_bytes.size();
        for (size_t i = 0; i < dir_bytes.size(); i++) {
            cpu->MemoryWrite(start_addr + i, dir_bytes[i]);
        }
        // Imposta il puntatore "Inizio delle variabili BASIC / Fine del programma" ($2D-$2E)
        memory[0x002D] = end_addr & 0xFF;
        memory[0x002E] = end_addr >> 8;
        memory[0x00AE] = end_addr & 0xFF;
        memory[0x00AF] = end_addr >> 8;
        // Imposta gli altri puntatori di sicurezza del BASIC ($2F-$30 e $31-$32)
        memory[0x002F] = end_addr & 0xFF;
        memory[0x0030] = end_addr >> 8;
        memory[0x0031] = end_addr & 0xFF;
        memory[0x0032] = end_addr >> 8;
        // scrive L+shift I (LIST) nel buffer di tastiera
        memory[0x0277] = 'L';
        memory[0x0278] = 73+128;    // Load
        memory[0x0279] = '\r';
        memory[0x00C6] = 3;
    }
}

void MainWindow::joystick()
{
    if (ui->actionJoystick->isChecked()) joystick_emulation_enabled = true;
    else joystick_emulation_enabled = false;
}

void MainWindow::onFocusChanged()
{
    setFocus();
}

void MainWindow::timeout()
{
    QString s = d64->IsLoaded() ? "Loaded" : "Detached";
    if (d64->IsLoaded()) s = s + " " + drive15.error_buffer.data();
    ui->lbdiskstatus->setText(s);
    s  = QString::number((cpu->MemoryRead(0xD011) & 0x20)?1:0)+",";
    s += QString::number((cpu->MemoryRead(0xD011) & 0x40)?1:0)+",";
    s += QString::number((cpu->MemoryRead(0xD016) & 0x10)?1:0);
    ui->graphics->setText(s);
    uint8_t a = cpu->MemoryRead(0xDD00)&0x03;
    if (a == 0) ui->vicbank->setText("3");
    else if (a == 1) ui->vicbank->setText("2");
    else if (a == 2) ui->vicbank->setText("1");
    else if (a == 3)  ui->vicbank->setText("0");
    s = QString::number((cpu->MemoryRead(0xD018) & 0x0E) << 10);
    ui->charbase->setText(s);
    s = QString("%1").arg(cpu->MemoryRead(0x01), 8, 2, QChar('0'));
    ui->reg01->setText(s);
}

void MainWindow::cpuerror()
{
    cpu = nullptr;
    cout << "Exiting on cpu error..." << endl;
    exit();
}

