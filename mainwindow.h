#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QKeyEvent>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void loadprg(QString filename);
    QTimer *tim=nullptr;
    void saveCfg();

public slots:
    // BUTTON
    void testclick();
    void ecmclick();
    void bmmclick();
    void bmm2click();
    void mcmclick();
    void bmmmcmclick();
    void rasterintclick();
    void rasterint2click();
    void fontclick();
    void spriteclick();
    void chopperclick();
    void misuraclockclick();

    // ACTION
    void reset();
    void exit();
    void attach();
    void loadrun();
    void hardreset();
    void getdirectory();
    void joystick();
    void printer();

    void onFocusChanged();
    void timeout();
    void cpuerror();



private:
    Ui::MainWindow *ui;

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

};
#endif // MAINWINDOW_H
