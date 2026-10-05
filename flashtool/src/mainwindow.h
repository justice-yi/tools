#pragma once
#include <QMainWindow>
#include <QElapsedTimer>
#include "manifest.h"
#include "transport.h"

class QTableWidget;
class QProgressBar;
class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;
class QFrame;
class QTimer;

class RowDelegate;

/* 参考 web 设计稿（React/shadcn 深色工作台）重绘的烧录台界面。
 * 视觉规格源：01-flashtool-src.zip 的 index.tsx + styles.css（oklch→hex 已换算）
 * 功能保持与板端 ota-agent daemon 协议一致：体检对比(安装记录对账) /
 * 勾选实时推送 / beginBurn 一键烧录 / reportLine 驱动的行内状态与总进度。
 *
 * 传输承载只认 Transport 接口（HTTP=HttpClientTransport，裸 USB 将来另加
 * 实现类），本文件不出现任何 http/socket 细节。
 *
 * 无边框窗口的平台细节全部隔离在 frameless.{h,cpp}，本文件纯 Qt：
 * 头部栏 = 标题栏（拖拽 startSystemMove / 双击最大化 / 自绘三键）。 */
class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	MainWindow();

protected:
	void changeEvent(QEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *ev) override;

public:
	/* 供 --screenshot 模式自动加载镜像 */
	void autoLoadImage(const QString &path);

private slots:
	void openImage();
	void exportCsv();
	void queryDevice();
	void startBurn();          /* 开始/停止 双态 */
	void onReportLine(const QString &line);
	void onQueryResult(bool ok, const QString &body);
	void onClock();
	void onCellClick(int row, int col);
	void onCellHover(int row, int col);
	void clearLog();

private:
	void buildUi();
	void applyStyle();
	void rebuildTable();
	void updateSelInfo();
	void setRowState(int row, const QString &text, int percent,
			 const QString &stateKey);
	void setAgentState(const QString &state);  /* ok/err/warn/idle */
	void setFootOnline(bool known, bool online);
	void log(const QString &msg, const QString &role = QString());
	QString localIp() const;

	QString m_imagePath;
	Manifest m_manifest;
	bool m_jobActive = false;

	/* 头部 = 标题栏（无边框窗口自绘） */
	QFrame *m_header = nullptr;
	QPushButton *m_btnMin = nullptr;
	QPushButton *m_btnMax = nullptr;
	QPushButton *m_btnClose = nullptr;
	void toggleMaximize();

	QLabel *m_clock = nullptr;
	QLabel *m_transLabel = nullptr;		/* 顶栏承载标签（HTTP :8080 …） */
	/* 工具栏 */
	QPushButton *m_openBtn = nullptr;
	QPushButton *m_exportBtn = nullptr;
	QPushButton *m_queryBtn = nullptr;
	QLineEdit *m_ipEdit = nullptr;
	QFrame *m_agentPill = nullptr;
	QLabel *m_agentDot = nullptr;
	QLabel *m_agentText = nullptr;
	QPushButton *m_burnBtn = nullptr;
	/* 统计条 */
	QLabel *m_statDevice = nullptr;
	QLabel *m_statVersion = nullptr;
	QLabel *m_statAlgo = nullptr;
	QLabel *m_statMedia = nullptr;
	/* 分区表 */
	QLabel *m_tableTitle = nullptr;
	QLabel *m_selInfo = nullptr;
	QTableWidget *m_table = nullptr;
	int m_hoverRow = -1;
	/* 进度 */
	QProgressBar *m_total = nullptr;
	QLabel *m_progInfo = nullptr;
	/* 日志 */
	QPlainTextEdit *m_log = nullptr;
	/* 页脚 */
	QLabel *m_footDot = nullptr;
	QLabel *m_footIp = nullptr;
	QLabel *m_footRate = nullptr;
	QLabel *m_footHint = nullptr;

	Transport *m_transport = nullptr;	/* 当前承载（构造时定，见构造函数） */
	QTimer *m_clockTimer = nullptr;

	/* 烧录统计（report 驱动） */
	qint64 m_sumGot = 0, m_sumTotal = 0;
	qint64 m_rateBytes = 0;
	QElapsedTimer m_rateTimer;
	double m_rate = 0;
};
