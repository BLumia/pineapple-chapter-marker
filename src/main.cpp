#include <QApplication>
#include <QLocale>
#include <QPalette>
#include <QStringList>
#include <QTranslator>

#include "ui/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Pineapple"));
    QApplication::setApplicationName(QStringLiteral("pineapple-chapter-marker"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QTranslator translator;
    if (translator.load(QLocale::system(), QStringLiteral("pineapple-chapter-marker"),
                        QStringLiteral("_"), QStringLiteral(":/i18n")))
        app.installTranslator(&translator);

    pcm::MainWindow win;
    win.show();

    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        win.openFile(args.at(1));

    return app.exec();
}
