#include <QDebug>
#include <qglobal.h>
#include <QTextCodec>

#include "main.h"
#include "mainwindow.h"

//#include <QTime>
//#include <QDebug>
//QTime tTimer;


int main(int argc, char *argv[])
{

 TApplication a(argc, argv);

#if QT_VERSION < 0x050000
 QTextCodec *codec = QTextCodec::codecForName("utf-8");
// QTextCodec::setCodecForLocale(codec);
 QTextCodec::setCodecForCStrings(codec);
#endif

 //tTimer.start();
 MainWindow w;
 //qDebug() << tTimer.elapsed(); return 0;

 int result = a.exec();

 return result;
}
