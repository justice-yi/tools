#include "mainwindow.h"
#include <QApplication>
#include <QScreen>
#include <QTimer>

int main(int argc, char **argv)
{
	bool shot = false;
	QString shotPath, imgPath;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
			shot = true; shotPath = argv[++i];
		} else if (argv[i][0] != '-') {
			imgPath = argv[i];
		}
	}

	QApplication app(argc, argv);
	MainWindow w;
	w.show();

	/* 自动加载镜像（截图/无头模式） */
	if (!imgPath.isEmpty())
		w.autoLoadImage(imgPath);

	if (shot) {
		QTimer::singleShot(1500, [&]() {
			QPixmap pm = w.grab();   /* 抓窗口本体，offscreen 平台可用 */
			pm.save(shotPath, "PNG");
			qInfo() << "saved:" << shotPath;
			app.quit();
		});
	}
	return app.exec();
}
