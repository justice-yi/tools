/* screenshot.cpp —— 工具自截图（WSLg X 截图不兼容，用 Qt QScreen::grabWindow） */
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include "mainwindow.h"

int main(int argc, char **argv)
{
	QApplication app(argc, argv, QApplication::GuiServer);
	MainWindow w;
	w.show();

	/* 加载测试镜像 */
	if (argc > 1) {
		/* 直接调 openImage 逻辑 */
		w.setProperty("testImage", argv[1]);
	}

	QTimer::singleShot(1500, [&]() {
		QScreen *scr = QGuiApplication::primaryScreen();
		QPixmap pm = scr->grabWindow(0);
		QString out = argc > 2 ? argv[2] : "/tmp/flashtool_ui.png";
		pm.save(out, "PNG");
		qInfo() << "截图保存:" << out << pm.width() << "x" << pm.height();
		app.quit();
	});
	return app.exec();
}
