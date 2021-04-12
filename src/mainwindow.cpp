#include <QMainWindow>
#include <QPushButton>

#include "mainwindow.h"
#include "osdpretty.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui_(new Ui_MainWindow), osdpretty_(new OSDPretty(OSDPretty::Mode_Popup)) {

  ui_->setupUi(this);

  show();

  connect(ui_->button_show, &QPushButton::clicked, this, &MainWindow::Show);

  osdpretty_->set_popup_duration(5000);
  osdpretty_->ReloadSettings();

}

MainWindow::~MainWindow() { delete ui_; }

void MainWindow::Show() {

  osdpretty_->ShowMessage("Test", "Test", QImage(":/something-on-me.jpg"));

}
