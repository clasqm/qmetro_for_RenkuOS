#ifndef TEXTCODEC_H
#define TEXTCODEC_H

#include <QTextCodec>
#include <QDebug>

// qMetro's maps and INI files are Windows-1251. Qt 5 only builds UTF-8,
// UTF-16 and Latin-1 in; cp1251 comes from the optional codecs plugin, so
// codecForName() can return 0. Never pass a null codec to QSettings,
// QTextStream or INISettings - fall back to UTF-8 instead.
static QTextCodec *mapCodec()
{
 static QTextCodec *codec=0;
 if(codec) return codec;

 const char *names[]={"Windows-1251","CP1251","windows-1251"};
 for(const char *name : names)
  if(QTextCodec *found=QTextCodec::codecForName(name)) return codec=found;

 qWarning("Windows-1251 codec is unavailable; falling back to UTF-8");
 return codec=QTextCodec::codecForName("UTF-8");
}

#endif // TEXTCODEC_H
