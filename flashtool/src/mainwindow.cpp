#include "mainwindow.h"
#include "frameless.h"
#include "httpclient.h"
#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QEvent>
#include <QWindow>

/* ================= 设计 token（styles.css oklch → hex 换算） ================= */
namespace Pal {
const QColor Bg("#0C131B"), Fg("#E2E9F0"), Card("#141C27"), Primary("#4CD3C4"),
	PrimFg("#0C131B"), Sec("#1A242F"), MutedFg("#7B8EA3"), Border("#283440"),
	Ok("#64C06A"), Warn("#E4A339"), Err("#E85A48"), LogBg("#060B10");
}

/* 行角色：col6 item 上
 *   UserRole   = 进度百分比（-1 无）
 *   UserRole+1 = 状态色 key（ok/active/warn/err/idle）
 * col0 item 上 UserRole+2 = 锁定（env 运行态 / extended 容器，不可勾选） */

/* ---------- 全行自绘 delegate：复选框/等宽列/状态点，匹配参考稿 ---------- */
class RowDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	void paint(QPainter *p, const QStyleOptionViewItem &opt,
		   const QModelIndex &idx) const override
	{
		const auto *tw = qobject_cast<const QTableWidget *>(opt.widget);
		if (!tw)
			return;
		const int row = idx.row(), col = idx.column();
		auto *chkItem = tw->item(row, 0);
		auto *stItem  = tw->item(row, 6);
		bool checked = chkItem && chkItem->checkState() == Qt::Checked;
		bool locked  = chkItem && chkItem->data(Qt::UserRole + 2).toBool();
		QString key  = stItem ? stItem->data(Qt::UserRole + 1).toString()
				      : QStringLiteral("idle");

		/* 背景：active=主色 8% 淡染；hover=次色 60%；否则透出表格底色 */
		int hoverRow = tw->property("hoverRow").toInt();
		if (key == "active")
			p->fillRect(opt.rect, QColor(76, 211, 196, 26));
		else if (row == hoverRow)
			p->fillRect(opt.rect, QColor(26, 36, 47, 160));
		/* 行分隔线（border-t） */
		p->setPen(QPen(Pal::Border, 1));
		p->drawLine(opt.rect.bottomLeft(), opt.rect.bottomRight());

		QRect tr = opt.rect.adjusted(12, 0, -12, 0);   /* 列内左右 padding */

		if (col == 0) {				  /* 16px 圆角复选框 */
			p->setRenderHint(QPainter::Antialiasing);
			QRect cb(0, 0, 16, 16);
			cb.moveCenter(opt.rect.center());
			if (locked) {			  /* 锁定：仅暗框 */
				p->setBrush(Qt::NoBrush);
				p->setPen(QPen(QColor(40, 52, 64), 1));
				p->drawRoundedRect(cb, 3, 3);
				return;
			}
			if (checked) {
				p->setBrush(Pal::Primary);
				p->setPen(Qt::NoPen);
				p->drawRoundedRect(cb, 3, 3);
				/* 对勾（两段折线） */
				p->setRenderHint(QPainter::Antialiasing, false);
				p->setPen(QPen(Pal::PrimFg, 2));
				p->drawLine(cb.left() + 4, cb.center().y() + 1,
					    cb.center().x() - 1, cb.bottom() - 4);
				p->drawLine(cb.center().x() - 1, cb.bottom() - 4,
					    cb.right() - 3, cb.top() + 4);
			} else {
				p->setBrush(Pal::Bg);
				p->setPen(QPen(Pal::Border, 1));
				p->drawRoundedRect(cb, 3, 3);
			}
			return;
		}
		if (col == 6) {				  /* 状态：6px 圆点 + 文字 */
			QString text = stItem ? stItem->text() : QString();
			QColor c = key == "ok"	   ? Pal::Ok
				 : key == "active" ? Pal::Primary
				 : key == "warn"   ? Pal::Warn
				 : key == "err"	   ? Pal::Err
						   : Pal::MutedFg;
			QFont f = opt.font;
			f.setPixelSize(12);
			f.setWeight(QFont::DemiBold);
			p->setFont(f);
			int dotY = opt.rect.center().y();
			p->setRenderHint(QPainter::Antialiasing);
			p->setPen(Qt::NoPen);
			p->setBrush(c);
			p->drawEllipse(QPointF(tr.left() + 3, dotY + 0.5), 3, 3);
			p->setPen(c);
			p->drawText(tr.adjusted(14, 0, 0, 0),
				    Qt::AlignLeft | Qt::AlignVCenter, text);
			return;
		}

		QFont f = opt.font;
		QColor c = Pal::Fg;
		QString text;
		switch (col) {
		case 1:				  /* 分区名：选中亮/未选灰 */
			f.setPixelSize(12);
			f.setWeight(QFont::DemiBold);
			c = checked ? Pal::Fg : Pal::MutedFg;
			text = idx.data().toString();
			break;
		case 2:				  /* 偏移：等宽灰 */
			f.setPixelSize(11);
			f.setFamily("Consolas");
			c = Pal::MutedFg;
			text = idx.data().toString();
			break;
		case 3:				  /* 大小：等宽亮 */
			f.setPixelSize(12);
			f.setFamily("Consolas");
			text = idx.data().toString();
			break;
		case 4:				  /* 类型：灰 */
			f.setPixelSize(12);
			c = Pal::MutedFg;
			text = idx.data().toString();
			break;
		case 5: {			  /* SHA256：等宽灰，右省略 */
			f.setPixelSize(11);
			f.setFamily("Consolas");
			c = Pal::MutedFg;
			QFontMetrics fm(f);
			text = fm.elidedText(idx.data().toString(),
					     Qt::ElideRight, tr.width());
			break;
		}
		}
		p->setFont(f);
		p->setPen(c);
		p->drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, text);
	}
};

/* ---------- 工具 ---------- */
static QString humanSize(qint64 b)
{
	const qint64 GiB = 1024LL * 1024 * 1024, MiB = 1024 * 1024, KiB = 1024;
	if (b >= GiB && b % GiB == 0) return QString::number(b / GiB) + "G";
	if (b >= MiB && b % MiB == 0) return QString::number(b / MiB) + "M";
	if (b >= KiB && b % KiB == 0) return QString::number(b / KiB) + "K";
	return QString::number(b) + "B";
}

/* 镜像尾读 manifest 原文（[csv][4B len LE][4B CRC][8B magic]） */
static QByteArray readTailManifest(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return QByteArray();
	f.seek(f.size() - 16);
	QByteArray desc = f.read(16);
	if (desc.size() < 16)
		return QByteArray();
	quint32 mlen = (quint8)desc[0]
		| ((quint8)desc[1] << 8)
		| ((quint32)(quint8)desc[2] << 16)
		| ((quint32)(quint8)desc[3] << 24);
	f.seek(f.size() - 16 - mlen);
	return f.read(mlen);
}

static void repolish(QWidget *w)
{
	w->style()->unpolish(w);
	w->style()->polish(w);
	w->update();
}

/* ================= 主窗口 ================= */
MainWindow::MainWindow()
{
	buildUi();
	applyStyle();

	/* 承载选择点：全工程只有这一行知道具体传输是什么（裸 USB 将来在此换类） */
	m_transport = new HttpClientTransport(this);
	connect(m_transport, &Transport::reportLine,
		this, &MainWindow::onReportLine);
	connect(m_transport, &Transport::queryResult,
		this, &MainWindow::onQueryResult);
	m_transLabel->setText(m_transport->describe() + tr(" · 待命"));
	connect(m_openBtn, &QPushButton::clicked, this, &MainWindow::openImage);
	connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::exportCsv);
	connect(m_queryBtn, &QPushButton::clicked, this, &MainWindow::queryDevice);
	connect(m_burnBtn, &QPushButton::clicked, this, &MainWindow::startBurn);

	m_clockTimer = new QTimer(this);
	connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::onClock);
	m_clockTimer->start(1000);
	onClock();

	setWindowTitle(tr("IMX6ULL FlashTool"));
	resize(1240, 880);			/* 完整容纳 8 分区行不出滚动条 */
	setMinimumSize(1080, 720);
	Frameless::apply(this);			/* 无边框 + 平台细节，见 frameless.cpp */
}

/* ---------- 标题栏交互（纯 Qt） ---------- */
void MainWindow::toggleMaximize()
{
	if (isMaximized())
		showNormal();
	else
		showMaximized();
}

void MainWindow::changeEvent(QEvent *e)
{
	QMainWindow::changeEvent(e);
	if (e->type() == QEvent::WindowStateChange && m_btnMax)
		m_btnMax->setText(isMaximized() ? "❐" : "□");
}

bool MainWindow::eventFilter(QObject *obj, QEvent *ev)
{
	if (obj == m_header) {
		if (ev->type() == QEvent::MouseButtonPress
		    && window()->windowHandle())
			window()->windowHandle()->startSystemMove();
		else if (ev->type() == QEvent::MouseButtonDblClick)
			toggleMaximize();
	}
	return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::buildUi()
{
	auto *page = new QWidget;
	page->setObjectName("page");
	auto *pageLay = new QVBoxLayout(page);
	pageLay->setContentsMargins(0, 0, 0, 0);

	auto *card = new QFrame;
	card->setObjectName("card");
	auto *lay = new QVBoxLayout(card);
	lay->setContentsMargins(0, 0, 0, 0);
	lay->setSpacing(0);

	/* ---- 头部标题栏（无边框窗口的标题区） ---- */
	m_header = new QFrame;
	m_header->setObjectName("headerBar");
	auto *hl = new QHBoxLayout(m_header);
	hl->setContentsMargins(16, 0, 0, 0);
	hl->setSpacing(8);
	auto *dot = new QFrame;
	dot->setObjectName("hdrDot");
	dot->setFixedSize(10, 10);
	auto *title = new QLabel(tr("IMX6ULL FlashTool"));
	title->setObjectName("title");
	auto *subtitle = new QLabel(tr("— 板载镜像烧录台"));
	subtitle->setObjectName("subtitle");
	m_clock = new QLabel;
	m_clock->setObjectName("monoDim");
	m_transLabel = new QLabel;		/* 文案在构造函数由承载填 */
	m_transLabel->setObjectName("monoDim");
	m_btnMin = new QPushButton("—");
	m_btnMax = new QPushButton("□");
	m_btnClose = new QPushButton("✕");
	m_btnMin->setObjectName("winBtn");
	m_btnMax->setObjectName("winBtn");
	m_btnClose->setObjectName("winBtnClose");
	m_btnMin->setFixedSize(44, 40);
	m_btnMax->setFixedSize(44, 40);
	m_btnClose->setFixedSize(44, 40);
	connect(m_btnMin, &QPushButton::clicked, this, &QWidget::showMinimized);
	connect(m_btnMax, &QPushButton::clicked, this,
		&MainWindow::toggleMaximize);
	connect(m_btnClose, &QPushButton::clicked, this, &QWidget::close);
	m_header->installEventFilter(this);
	hl->addWidget(dot);
	hl->addSpacing(6);
	hl->addWidget(title);
	hl->addWidget(subtitle);
	hl->addStretch(1);
	hl->addWidget(m_clock);
	hl->addWidget(new QLabel("·"));
	hl->addWidget(m_transLabel);
	hl->addSpacing(8);
	hl->addWidget(m_btnMin);
	hl->addWidget(m_btnMax);
	hl->addWidget(m_btnClose);
	lay->addWidget(m_header);

	/* ---- 工具栏 ---- */
	auto *bar = new QFrame;
	bar->setObjectName("toolbar");
	auto *bl = new QHBoxLayout(bar);
	bl->setContentsMargins(16, 12, 16, 12);
	bl->setSpacing(8);
	m_openBtn = new QPushButton(tr("打开镜像 board.img"));
	m_openBtn->setObjectName("tintBtn");
	m_exportBtn = new QPushButton(tr("导出分区表 CSV"));
	m_exportBtn->setObjectName("outlineBtn");
	m_exportBtn->setEnabled(false);
	m_queryBtn = new QPushButton(tr("体检对比"));
	m_queryBtn->setObjectName("ghostBtn");
	m_queryBtn->setEnabled(false);
	m_ipEdit = new QLineEdit;
	m_ipEdit->setPlaceholderText(tr("板端 IP"));
	m_ipEdit->setObjectName("ipEdit");
	m_ipEdit->setFixedWidth(150);
	m_agentPill = new QFrame;
	m_agentPill->setObjectName("agentPill");
	m_agentPill->setProperty("state", "idle");
	auto *pl = new QHBoxLayout(m_agentPill);
	pl->setContentsMargins(12, 0, 12, 0);
	pl->setSpacing(6);
	m_agentDot = new QLabel;
	m_agentDot->setObjectName("agentDot");
	m_agentDot->setFixedSize(6, 6);
	m_agentText = new QLabel(tr("Agent 未连接"));
	m_agentText->setObjectName("agentText");
	pl->addWidget(m_agentDot);
	pl->addWidget(m_agentText);
	m_burnBtn = new QPushButton(tr("▶ 开始烧录"));
	m_burnBtn->setObjectName("primaryBtn");
	m_burnBtn->setEnabled(false);
	m_burnBtn->setMinimumWidth(112);
	bl->addWidget(m_openBtn);
	bl->addWidget(m_exportBtn);
	bl->addWidget(m_queryBtn);
	bl->addStretch(1);
	bl->addWidget(m_ipEdit);
	bl->addWidget(m_agentPill);
	bl->addWidget(m_burnBtn);
	lay->addWidget(bar);

	/* ---- 四格统计条（1px 缝隙露出边框色） ---- */
	auto *stats = new QFrame;
	stats->setObjectName("statsBar");
	auto *sl = new QHBoxLayout(stats);
	sl->setContentsMargins(0, 0, 0, 0);
	sl->setSpacing(1);
	auto addStat = [&](const QString &label, QLabel **value) {
		auto *cell = new QFrame;
		cell->setObjectName("statCell");
		auto *cl = new QVBoxLayout(cell);
		cl->setContentsMargins(16, 12, 16, 12);
		cl->setSpacing(4);
		auto *lb = new QLabel(label);
		lb->setObjectName("statLabel");
		*value = new QLabel("—");
		(*value)->setObjectName("statValue");
		cl->addWidget(lb);
		cl->addWidget(*value);
		sl->addWidget(cell, 1);
	};
	addStat(tr("目标设备"), &m_statDevice);
	addStat(tr("镜像版本"), &m_statVersion);
	addStat(tr("校验算法"), &m_statAlgo);
	addStat(tr("存储介质"), &m_statMedia);
	lay->addWidget(stats);

	/* ---- 分区表 ---- */
	auto *sect = new QWidget;
	sect->setObjectName("tableSection");
	auto *tl0 = new QVBoxLayout(sect);
	tl0->setContentsMargins(16, 16, 16, 16);
	tl0->setSpacing(8);
	auto *titleRow = new QHBoxLayout;
	m_tableTitle = new QLabel(tr("分区表 · 0 个分区"));
	m_tableTitle->setObjectName("sectionLabel");
	m_selInfo = new QLabel(tr("已选择 0 个分区 · 点击复选框可排除"));
	m_selInfo->setObjectName("sectionHint");
	titleRow->addWidget(m_tableTitle);
	titleRow->addStretch(1);
	titleRow->addWidget(m_selInfo);
	tl0->addLayout(titleRow);

	m_table = new QTableWidget(0, 7);
	m_table->setHorizontalHeaderLabels(
		{ "", tr("分区"), tr("偏移"), tr("大小"), tr("类型"),
		  "SHA256", tr("状态") });
	m_table->verticalHeader()->hide();
	m_table->verticalHeader()->setDefaultSectionSize(34);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::NoSelection);
	m_table->setFocusPolicy(Qt::NoFocus);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setMouseTracking(true);
	m_table->setShowGrid(false);
	m_table->horizontalHeader()->setStretchLastSection(false);
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
	m_table->setColumnWidth(0, 48);
	m_table->setColumnWidth(1, 120);
	m_table->setColumnWidth(2, 125);
	m_table->setColumnWidth(3, 180);
	m_table->setColumnWidth(4, 110);
	m_table->setColumnWidth(6, 105);
	m_table->setItemDelegate(new RowDelegate(m_table));
	connect(m_table, &QTableWidget::cellClicked,
		this, &MainWindow::onCellClick);
	connect(m_table, &QTableWidget::cellEntered,
		this, &MainWindow::onCellHover);
	tl0->addWidget(m_table);

	/* ---- 总进度 ---- */
	auto *prog = new QFrame;
	prog->setObjectName("progressSect");
	auto *pg = new QVBoxLayout(prog);
	pg->setContentsMargins(16, 16, 16, 16);
	pg->setSpacing(8);
	auto *progRow = new QHBoxLayout;
	auto *progTitle = new QLabel(tr("总体进度"));
	progTitle->setObjectName("progTitle");
	m_progInfo = new QLabel(tr("0 个分区待烧录 · 0%"));
	m_progInfo->setObjectName("monoDim");
	progRow->addWidget(progTitle);
	progRow->addStretch(1);
	progRow->addWidget(m_progInfo);
	m_total = new QProgressBar;
	m_total->setTextVisible(false);
	m_total->setFixedHeight(10);
	m_total->setRange(0, 1000);
	m_total->setValue(0);
	pg->addLayout(progRow);
	pg->addWidget(m_total);

	/* ---- 日志 ---- */
	auto *logw = new QFrame;
	logw->setObjectName("logSect");
	auto *lg = new QVBoxLayout(logw);
	lg->setContentsMargins(16, 12, 16, 12);
	lg->setSpacing(6);
	auto *logRow = new QHBoxLayout;
	auto *logTitle = new QLabel(tr("实时日志"));
	logTitle->setObjectName("sectionLabel");
	logRow->addWidget(logTitle);
	logRow->addStretch(1);
	auto *clearBtn = new QPushButton(tr("清空"));
	clearBtn->setObjectName("ghostBtn");
	connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearLog);
	logRow->addWidget(clearBtn);
	m_log = new QPlainTextEdit;
	m_log->setReadOnly(true);
	m_log->setFixedHeight(110);
	m_log->setObjectName("logView");
	lg->addLayout(logRow);
	lg->addWidget(m_log);

	/* ---- 页脚 ---- */
	auto *foot = new QFrame;
	foot->setObjectName("footerBar");
	auto *fl = new QHBoxLayout(foot);
	fl->setContentsMargins(16, 0, 16, 0);
	fl->setSpacing(20);
	m_footDot = new QLabel;
	m_footDot->setObjectName("footDot");
	m_footDot->setFixedSize(6, 6);
	m_footDot->setProperty("state", "idle");
	auto *footDev = new QLabel(tr("设备未知"));
	footDev->setObjectName("monoDim");
	m_footIp = new QLabel(tr("板端 —"));
	m_footIp->setObjectName("monoDim");
	m_footRate = new QLabel(tr("传输速率 —"));
	m_footRate->setObjectName("monoDim");
	m_footHint = new QLabel(tr("等待任务"));
	m_footHint->setObjectName("monoDim");
	fl->addWidget(m_footDot);
	fl->addWidget(footDev);
	fl->addWidget(m_footIp);
	fl->addWidget(m_footRate);
	fl->addStretch(1);
	fl->addWidget(m_footHint);

	lay->addWidget(sect, 1);
	lay->addWidget(prog);
	lay->addWidget(logw);
	lay->addWidget(foot);
	pageLay->addWidget(card);
	setCentralWidget(page);
}

void MainWindow::applyStyle()
{
	setStyleSheet(R"==(
	* { font-family: 'Microsoft YaHei UI', 'Segoe UI', sans-serif;
	    font-size: 12px; color: #E2E9F0; }
	#page { background-color: #0C131B; }
	#card { background-color: #141C27; border: 1px solid #283440;
		border-radius: 0px; }

	QPushButton#winBtn, QPushButton#winBtnClose { background: transparent;
		border: none; color: #7B8EA3; font-size: 12px;
		border-radius: 0px; padding: 0px; }
	QPushButton#winBtn:hover { background: rgba(226,233,240,0.10);
		color: #E2E9F0; }
	QPushButton#winBtnClose:hover { background: #E81123; color: #FFFFFF; }

	#headerBar { background-color: rgba(26,36,47,0.7);
		border-bottom: 1px solid #283440;
		min-height: 40px; max-height: 40px; }
	#footerBar { background-color: rgba(26,36,47,0.7);
		border-top: 1px solid #283440;
		min-height: 32px; max-height: 32px; }
	#hdrDot { background-color: #4CD3C4; border-radius: 5px; }
	#title { font-size: 13px; font-weight: 600; }
	#subtitle { font-size: 11px; color: #7B8EA3; }
	#monoDim, QLabel#monoDim { font-family: 'Consolas', 'Cascadia Mono', monospace;
		font-size: 11px; color: #7B8EA3; }
	#headerBar QLabel#monoDim { font-size: 11px; }

	#toolbar { border-bottom: 1px solid #283440; }
	QPushButton { border-radius: 4px; padding: 6px 12px; }
	QPushButton#primaryBtn { background: #4CD3C4; color: #0C131B;
		border: none; font-weight: 600; }
	QPushButton#primaryBtn:hover { background: #66D9CD; }
	QPushButton#primaryBtn:pressed { background: #3CBFB2; }
	QPushButton#primaryBtn:disabled { background: #283440; color: #7B8EA3; }
	QPushButton#tintBtn { background: rgba(76,211,196,0.10); color: #4CD3C4;
		border: 1px solid rgba(76,211,196,0.40); }
	QPushButton#tintBtn:hover { background: rgba(76,211,196,0.16); }
	QPushButton#tintBtn:disabled { color: #56606E;
		border-color: #283440; background: transparent; }
	QPushButton#outlineBtn { background: #1A242F; color: #E2E9F0;
		border: 1px solid #283440; }
	QPushButton#outlineBtn:hover { background: #223040; }
	QPushButton#outlineBtn:disabled { color: #56606E; }
	QPushButton#ghostBtn { background: transparent; color: #7B8EA3; border: none; }
	QPushButton#ghostBtn:hover { color: #E2E9F0; background: #1A242F; }
	QPushButton#ghostBtn:disabled { color: #4A5462; }

	QLineEdit#ipEdit { background: #0C131B; border: 1px solid #283440;
		border-radius: 4px; padding: 5px 10px; color: #E2E9F0;
		font-family: 'Consolas', monospace; font-size: 12px; }
	QLineEdit#ipEdit:focus { border-color: #4CD3C4; }

	#agentPill { border: 1px solid #283440; border-radius: 4px;
		background: transparent; }
	#agentPill[state="ok"] { border-color: rgba(76,211,196,0.30);
		background: rgba(76,211,196,0.10); }
	#agentPill[state="warn"] { border-color: rgba(228,163,57,0.40);
		background: rgba(228,163,57,0.10); }
	#agentPill[state="err"] { border-color: rgba(232,90,72,0.40);
		background: rgba(232,90,72,0.10); }
	#agentText { font-size: 12px; color: #7B8EA3; }
	#agentPill[state="ok"] #agentText { color: #4CD3C4; font-weight: 600; }
	#agentPill[state="warn"] #agentText { color: #E4A339; }
	#agentPill[state="err"] #agentText { color: #E85A48; }
	#agentDot { background: #56606E; border-radius: 3px; }
	#agentPill[state="ok"] #agentDot { background: #4CD3C4; }
	#agentPill[state="warn"] #agentDot { background: #E4A339; }
	#agentPill[state="err"] #agentDot { background: #E85A48; }

	#statsBar { background-color: #283440; border-bottom: 1px solid #283440; }
	#statCell { background-color: #141C27; }
	#statLabel { font-size: 10px; color: #7B8EA3; }
	#statValue { font-family: 'Consolas', 'Cascadia Mono', monospace;
		font-size: 12px; color: #E2E9F0; }

	#tableSection { background: transparent; }
	#sectionLabel { font-size: 11px; color: #7B8EA3; }
	#sectionHint { font-size: 11px; color: #7B8EA3; }
	QTableWidget { background: #141C27; border: 1px solid #283440;
		border-radius: 4px; gridline-color: transparent; }
	QTableWidget::item { border: none; }
	QHeaderView::section { background: #1A242F; color: #7B8EA3;
		border: none; border-bottom: 1px solid #283440;
		padding: 8px 12px; font-size: 10px; }
	QTableWidget QHeaderView { background: #1A242F; }

	#progressSect { border-top: 1px solid #283440; }
	#progTitle { font-size: 12px; font-weight: 600; }
	QProgressBar { background: #0C131B; border: 1px solid #283440;
		border-radius: 5px; }
	QProgressBar::chunk { background: #4CD3C4; border-radius: 4px; }

	#logSect { background-color: #060B10; border-top: 1px solid #283440; }
	QPlainTextEdit#logView { background: transparent; border: none;
		font-family: 'Consolas', 'Cascadia Mono', monospace;
		font-size: 11px; color: #7B8EA3; }
	#footDot { border-radius: 3px; background: #56606E; }
	#footDot[state="ok"] { background: #64C06A; }
	#footDot[state="err"] { background: #E85A48; }
	)==");
}

/* ================= 数据 → 界面 ================= */
void MainWindow::rebuildTable()
{
	m_table->setRowCount(m_manifest.parts.size());
	int row = 0;
	for (const auto &p : m_manifest.parts) {
		/* 勾选列：数据容器（视觉由 delegate 全权绘制） */
		auto *chk = new QTableWidgetItem;
		bool isContainer = p.ptype.startsWith("0x0f");
		bool isEnv = (p.sha256 == "-");		/* env 运行态，永不烧写 */
		bool isBanned = m_transport->bannedParts().contains(p.name);
		bool locked = isContainer || isEnv || isBanned;
		chk->setCheckState(locked ? Qt::Unchecked : Qt::Checked);
		chk->setData(Qt::UserRole + 2, locked);
		m_table->setItem(row, 0, chk);

		m_table->setItem(row, 1, new QTableWidgetItem(p.name));
		m_table->setItem(row, 2, new QTableWidgetItem(
			"0x" + QString::number(p.offset, 16).toUpper()));
		m_table->setItem(row, 3, new QTableWidgetItem(
			"0x" + QString::number(p.size, 16).toUpper()
			+ " (" + humanSize(p.size) + ")"));
		m_table->setItem(row, 4, new QTableWidgetItem(p.fstype));
		m_table->setItem(row, 5, new QTableWidgetItem(p.sha256));
		auto *st = new QTableWidgetItem;
		st->setData(Qt::UserRole, -1);
		m_table->setItem(row, 6, st);
		setRowState(row,
			    isContainer ? tr("容器分区") :
			    isBanned    ? tr("暂存区 · 不烧写") :
			    isEnv	    ? tr("运行态 · 不烧写") :
					  tr("镜像就绪"),
			    -1, "idle");
		/* 文本列不参与交互 */
		for (int c = 1; c <= 5; c++)
			m_table->item(row, c)->setFlags(Qt::NoItemFlags);
		row++;
	}
	m_tableTitle->setText(tr("分区表 · %1 个分区")
				      .arg(m_manifest.parts.size()));
	updateSelInfo();
}

void MainWindow::updateSelInfo()
{
	int sel = 0;
	QStringList checkedNames;			/* 当前勾选 → 实时推给板端 */
	for (int i = 0; i < m_table->rowCount(); i++) {
		auto *chk = m_table->item(i, 0);
		if (chk && chk->checkState() == Qt::Checked) {
			sel++;
			checkedNames << m_manifest.parts[i].name;
		}
	}
	m_selInfo->setText(tr("已选择 %1 个分区 · 点击复选框可排除").arg(sel));
	m_burnBtn->setEnabled(sel > 0 && !m_manifest.parts.isEmpty());
	if (!m_jobActive)
		m_progInfo->setText(tr("%1 个分区待烧录 · 0%").arg(sel));
	/* 勾选变化即推送（全量覆盖、幂等）：勾了又取消，板端只会拿到最新清单 */
	if (m_transport && !m_manifest.parts.isEmpty())
		m_transport->setJobText(checkedNames.join('\n') + '\n');
}

void MainWindow::setRowState(int row, const QString &text, int percent,
			     const QString &stateKey)
{
	if (row < 0 || row >= m_table->rowCount())
		return;
	auto *st = m_table->item(row, 6);
	if (!st)
		return;
	st->setText(text);
	st->setData(Qt::UserRole, percent);
	st->setData(Qt::UserRole + 1, stateKey);
	m_table->viewport()->update();
}

void MainWindow::setAgentState(const QString &state)
{
	m_agentPill->setProperty("state", state);
	repolish(m_agentPill);
	m_agentDot->setProperty("state", state);
	repolish(m_agentDot);
	repolish(m_agentText);
}

void MainWindow::setFootOnline(bool known, bool online)
{
	m_footDot->setProperty("state", known ? (online ? "ok" : "err") : "idle");
	repolish(m_footDot);
}

void MainWindow::log(const QString &msg, const QString &role)
{
	QString color = role == "ok"    ? "#64C06A"
			: role == "warn"  ? "#E4A339"
			: role == "err"   ? "#E85A48"
			: role == "hi"    ? "#4CD3C4"
					  : "#7B8EA3";
	QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
	m_log->appendHtml(QString("<span style='color:%1'>[%2] %3</span>")
				  .arg(color, ts, msg.toHtmlEscaped()));
}

QString MainWindow::localIp() const
{
	for (const auto &iface : QNetworkInterface::allInterfaces()) {
		if (!(iface.flags() & QNetworkInterface::IsUp) ||
		    iface.flags() & QNetworkInterface::IsLoopBack)
			continue;
		for (const auto &e : iface.addressEntries()) {
			auto ip = e.ip();
			if (ip.protocol() == QAbstractSocket::IPv4Protocol
			    && !ip.isLoopback() && !ip.toString().startsWith("169.254"))
				return ip.toString();
		}
	}
	return "本机IP";
}

/* ================= 交互 ================= */
void MainWindow::onCellClick(int row, int col)
{
	if (col != 0)
		return;
	auto *chk = m_table->item(row, 0);
	if (!chk || chk->data(Qt::UserRole + 2).toBool())
		return;
	if (m_jobActive)
		return;				/* 烧录中锁定选择 */
	chk->setCheckState(chk->checkState() == Qt::Checked
				   ? Qt::Unchecked : Qt::Checked);
	setRowState(row, chk->checkState() == Qt::Checked
			     ? tr("镜像就绪") : tr("已跳过"), -1,
		     chk->checkState() == Qt::Checked ? "ok" : "idle");
	updateSelInfo();
}

void MainWindow::onCellHover(int row, int col)
{
	Q_UNUSED(col);
	int old = m_hoverRow;
	m_hoverRow = row;
	m_table->setProperty("hoverRow", row);
	if (old >= 0 && old < m_table->rowCount() && old != row)
		for (int c = 0; c < 7; c++)
			m_table->update(m_table->model()->index(old, c));
	if (row >= 0)
		for (int c = 0; c < 7; c++)
			m_table->update(m_table->model()->index(row, c));
}

void MainWindow::onClock()
{
	m_clock->setText(
		QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
}

void MainWindow::clearLog()
{
	m_log->clear();
}

/* ================= 业务流程（保持 agent 协议不变） ================= */
void MainWindow::autoLoadImage(const QString &path)
{
	if (!m_manifest.loadFromImage(path))
		return;
	m_imagePath = path;
	m_transport->start(path, readTailManifest(path));	/* 备好分区表 */
	log(tr("已载入镜像 %1").arg(QFileInfo(path).fileName()));
	log(tr("识别设备 %1 · 版本 %2").arg(m_manifest.device,
					    m_manifest.version), "hi");
	rebuildTable();
	m_exportBtn->setEnabled(true);
	m_queryBtn->setEnabled(true);
	m_statDevice->setText(m_manifest.device);
	m_statVersion->setText(m_manifest.version);
	m_statAlgo->setText("SHA256");
	m_statMedia->setText(tr("A/B 双槽 · %1 个分区")
				     .arg(m_manifest.parts.size()));
}

void MainWindow::openImage()
{
	QString path = QFileDialog::getOpenFileName(this, tr("选择镜像"),
			QString(), tr("镜像 (*.img);;所有文件 (*)"));
	if (path.isEmpty())
		return;
	if (!m_manifest.loadFromImage(path)) {
		log(tr("解析失败：尾部无有效 manifest（需 mk-image.sh 产物）: %1")
			    .arg(path), "err");
		return;
	}
	autoLoadImage(path);
}

void MainWindow::exportCsv()
{
	if (m_manifest.parts.isEmpty()) {
		log(tr("未加载镜像，无可导出"), "warn");
		return;
	}
	QString path = QFileDialog::getSaveFileName(this, tr("导出分区表"),
			"partitions.csv", tr("CSV (*.csv)"));
	if (path.isEmpty())
		return;
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
		log(tr("无法写入 %1").arg(path), "err");
		return;
	}
	f.write(QString("# version=%1 device=%2\n")
			.arg(m_manifest.version, m_manifest.device).toUtf8());
	f.write("# name, offset, size, ptype, fstype, sha256\n");
	for (const auto &p : m_manifest.parts)
		f.write(QString("%1, 0x%2, 0x%3, %4, %5, %6\n")
				.arg(p.name,
				     QString::number(p.offset, 16),
				     QString::number(p.size, 16),
				     p.ptype, p.fstype, p.sha256)
				.toUtf8());
	f.close();
	log(tr("分区表已导出: %1").arg(path), "ok");
}

void MainWindow::queryDevice()
{
	if (m_manifest.parts.isEmpty() || m_ipEdit->text().isEmpty()) {
		log(tr("体检需先打开镜像并填板端 IP"), "warn");
		return;
	}
	setAgentState("warn");
	m_agentText->setText(tr("查询中…"));
	log(tr("查询板端 %1 …").arg(m_ipEdit->text()));
	m_transport->queryBoard(m_ipEdit->text());
}

void MainWindow::onQueryResult(bool ok, const QString &body)
{
	if (!ok) {
		log(tr("查询失败: %1").arg(body), "err");
		setAgentState("err");
		m_agentText->setText(tr("Agent 离线"));
		setFootOnline(true, false);
		return;
	}
	setAgentState("ok");
	m_agentText->setText(tr("Agent 已连接 %1").arg(m_ipEdit->text()));
	setFootOnline(true, true);
	m_footIp->setText(tr("板端 %1").arg(m_ipEdit->text()));
	if (!m_jobActive)
		m_transLabel->setText(m_transport->describe()
				      + tr(" · 已连接 %1").arg(m_ipEdit->text()));

	/* /status 文本：# version=… / # slot=… 头 + "name, last_sha256, tmp_bytes"
	 * 行——安装记录对账（板端不实时 hash，只比上次烧录记录） */
	QString lastVer, slot;
	QHash<QString, QString> lastSha;	/* name → sha256（"-"=无记录） */
	QHash<QString, qint64> tmpBytes;	/* name → 断点字节数 */
	for (const QString &ln : body.split('\n')) {
		QString line = ln.trimmed();
		if (line.startsWith("# version="))
			lastVer = line.mid(10);
		else if (line.startsWith("# slot="))
			slot = line.mid(7);
		else if (!line.isEmpty() && !line.startsWith('#')) {
			auto col = line.split(',');
			if (col.size() >= 3) {
				lastSha[col[0].trimmed()] = col[1].trimmed();
				tmpBytes[col[0].trimmed()] =
					col[2].trimmed().toLongLong();
			}
		}
	}
	int row = 0, same = 0, diff = 0, norec = 0;
	for (const auto &p : m_manifest.parts) {
		row++;
		if (p.sha256 == "-") {
			setRowState(row - 1, tr("镜像无基准(env)"), -1, "idle");
			continue;
		}
		if (m_transport->bannedParts().contains(p.name)) {
			setRowState(row - 1, tr("暂存区 · 不烧写"), -1, "idle");
			continue;			/* 锁定行不参与对账展示 */
		}
		QString rec = lastSha.value(p.name);
		qint64 tmp = tmpBytes.value(p.name, 0);
		QString resume = tmp > 0
			? tr(" · 断点%1%").arg(tmp * 100 / p.size)
			: QString();
		if (rec.isEmpty() || rec == "-") {
			setRowState(row - 1, tr("无烧录记录") + resume, -1,
				    "warn");
			norec++;
		} else if (rec == p.sha256) {
			setRowState(row - 1, tr("一致"), -1, "ok");
			same++;
		} else {
			setRowState(row - 1, tr("上次不同 · 待更新") + resume,
				    -1, "warn");
			diff++;
		}
	}
	/* 版本判读：镜像 manifest 版本 vs 板端上次烧录版本（同为时间戳，可比较） */
	QString ver;
	if (lastVer.isEmpty())
		ver = tr("板端无烧录记录");
	else if (m_manifest.version == lastVer)
		ver = tr("与上次烧录同版本");
	else
		ver = m_manifest.version > lastVer ? tr("镜像为更新版本")
						   : tr("镜像为更旧版本");
	log(tr("体检完成：一致 %1，可更新 %2，无记录 %3 · 当前槽 %4 · %5")
		    .arg(same).arg(diff).arg(norec)
		    .arg(slot.isEmpty() ? QStringLiteral("-") : slot, ver),
	    diff + norec > 0 ? "warn" : "ok");
}

void MainWindow::startBurn()
{
	if (m_jobActive) {			/* === 停止 === */
		m_transport->setJobText("");	/* 清白名单（板端只认最新清单） */
		m_transport->abortJob();	/* 断上传 + 板端清断点临时文件 */
		m_jobActive = false;
		m_burnBtn->setText(tr("▶ 开始烧录"));
		for (int i = 0; i < m_table->rowCount(); i++) {
			auto *chk = m_table->item(i, 0);
			if (chk && chk->checkState() == Qt::Checked)
				setRowState(i, tr("镜像就绪"), -1, "ok");
		}
		m_total->setValue(0);
		m_footRate->setText(tr("传输速率 —"));
		m_footHint->setText(tr("等待任务"));
		const QString addr = m_ipEdit->text();
		m_transLabel->setText(addr.isEmpty()
			  ? m_transport->describe() + tr(" · 待命")
			  : m_transport->describe()
				    + tr(" · 已连接 %1").arg(addr));
		log(tr("已停止：上传已中断，板端断点临时文件已清理"), "warn");
		updateSelInfo();
		return;
	}

	const QString addr = m_ipEdit->text();
	if (addr.isEmpty()) {
		log(tr("请先填板端 IP（daemon 常驻 :8080，可先点体检确认在线）"),
		    "warn");
		return;
	}

	QStringList job;
	m_sumGot = 0;
	m_sumTotal = 0;
	for (int i = 0; i < m_manifest.parts.size(); i++) {
		auto *chk = m_table->item(i, 0);
		if (chk && chk->checkState() == Qt::Checked) {
			job << m_manifest.parts[i].name;
			m_sumTotal += m_manifest.parts[i].size;
			setRowState(i, tr("上传中"), 0, "active");
		}
	}
	if (job.isEmpty()) {
		log(tr("未勾选任何分区"), "warn");
		return;
	}
	m_transport->setJobText(job.join('\n') + '\n');	/* 最终清单保底推送 */
	m_jobActive = true;
	m_burnBtn->setText(tr("✕ 停止烧录"));
	m_total->setValue(0);
	m_progInfo->setText(tr("0 / %1 分区 · 0%").arg(job.size()));
	m_rate = 0;
	m_rateBytes = 0;
	m_rateTimer.start();
	m_transLabel->setText(m_transport->describe()
				      + tr(" · 烧录中 %1").arg(addr));
	m_footHint->setText(tr("板端 %1 · 无需板端操作").arg(addr));
	log(tr("烧录开始（%1 个分区，共 %2MB → %3）")
		    .arg(job.size()).arg(m_sumTotal / 1048576).arg(addr), "hi");
	m_transport->beginBurn(addr);		/* 客户端驱动，无需板端操作 */
}

void MainWindow::onReportLine(const QString &line)
{
	if (!m_jobActive) {
		/* 停止后工作线程的收尾行（job result=abort）——只记日志不动 UI */
		log(tr("[已停止] %1").arg(line.trimmed()), "warn");
		return;
	}
	/* 任务收尾行（ok/fail/abort）：复位按钮与标签 */
	if (line.startsWith("job result=")) {
		log(line.trimmed(), line.endsWith("ok") ? "ok" : "warn");
		m_jobActive = false;
		m_burnBtn->setText(tr("▶ 开始烧录"));
		m_footRate->setText(tr("传输速率 —"));
		const QString addr = m_ipEdit->text();
		m_transLabel->setText(m_transport->describe()
			      + (addr.isEmpty() ? tr(" · 待命")
				 : tr(" · 已连接 %1").arg(addr)));
		m_footHint->setText(tr("等待任务"));
		return;
	}
	log(line.trimmed());
	static QRegularExpression re(
		"part=([A-Za-z0-9_]+) stage=(\\w+)(?: got=([0-9]+) total=([0-9]+))?");
	auto m = re.match(line);
	if (!m.hasMatch())
		return;
	QString name = m.captured(1), stage = m.captured(2);
	int row = -1;
	for (int i = 0; i < m_manifest.parts.size(); i++)
		if (m_manifest.parts[i].name == name) { row = i; break; }
	if (row < 0)
		return;
	if (stage == "transfer") {
		qint64 got = m.captured(3).toLongLong();
		qint64 tot = m.captured(4).toLongLong();
		int pct = tot > 0 ? (int)(got * 100 / tot) : 0;
		setRowState(row, tr("传输 %1%").arg(pct), pct, "active");
		/* 总进度 = 已完成分区字节 + 当前分区已收（agent 逐分区串行） */
		qint64 doneBytes = 0;
		for (int i = 0; i < row; i++) {
			auto *chk = m_table->item(i, 0);
			if (chk && chk->checkState() == Qt::Checked)
				doneBytes += m_manifest.parts[i].size;
		}
		m_sumGot = doneBytes + got;
		m_total->setValue((int)(m_sumGot * 1000
					/ (m_sumTotal ? m_sumTotal : 1)));
		int doneParts = 0;
		for (int i = 0; i < row; i++)
			if (m_table->item(i, 0)->checkState() == Qt::Checked)
				doneParts++;
		m_progInfo->setText(tr("%1 / %2 分区")
					    .arg(doneParts)
					    .arg(m_table->rowCount())
		    + QString(" · <b style='color:#4CD3C4'>%1%</b>")
			      .arg(m_sumGot * 100 / (m_sumTotal ? m_sumTotal : 1)));
		/* 传输速率：字节增量/时间增量，EMA 平滑 */
		qint64 dt = m_rateTimer.elapsed();
		if (dt > 400 && m_sumGot > m_rateBytes) {
			double inst = (m_sumGot - m_rateBytes) * 1000.0 / dt;
			m_rate = m_rate ? m_rate * 0.6 + inst * 0.4 : inst;
			m_rateBytes = m_sumGot;
			m_rateTimer.restart();
			m_footRate->setText(tr("传输速率 %1 MB/s")
				.arg(QString::number(m_rate / 1048576.0, 'f', 1)));
		}
	} else if (stage == "verify") {
		if (m.capturedLength(3) > 0) {		/* 带 got/total＝板端流式进度 */
			qint64 got = m.captured(3).toLongLong();
			qint64 tot = m.captured(4).toLongLong();
			int pct = tot > 0 ? (int)(got * 100 / tot) : 0;
			setRowState(row, tr("校验 %1%").arg(pct), pct, "active");
		} else {
			setRowState(row, m.captured(0).contains("ok")
					      ? tr("校验通过") : tr("校验失败"),
				    -1, m.captured(0).contains("ok") ? "ok" : "err");
		}
	} else if (stage == "burn") {
		if (m.capturedLength(3) > 0) {		/* 带 got/total＝板端 dd 进度 */
			qint64 got = m.captured(3).toLongLong();
			qint64 tot = m.captured(4).toLongLong();
			int pct = tot > 0 ? (int)(got * 100 / tot) : 0;
			setRowState(row, tr("烧写 %1%").arg(pct), pct, "active");
		} else {
			setRowState(row, tr("烧写中"), 100, "active");
		}
	} else if (stage == "done") {
		setRowState(row, tr("完成"), 100, "ok");
	}
}
