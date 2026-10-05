#include "httpclient.h"
#include <QFile>
#include <QHash>
#include <cstring>
#include <functional>

#define HTTPLIB_IMPLEMENTATION
#include "httplib.h"

static const int kBoardPort = 8080;			/* 板端 daemon */
static const qint64 kChunk = 1024LL * 1024;		/* 1MiB/块 */
static const long kStreamGapMs = 90000;		/* 流式进度行间隔上限（看门狗）*/

/* ---------- 一次性 RPC（每次独立连接，无状态） ---------- */
struct HttpOp {
	const char *method = "POST";
	std::string path, body, xoffset;	/* X-Offset: 续传偏移头 */
	long timeout_ms = 10000;		/* 读超时；verify/commit 放宽 */
};

static bool rpc(const std::string &addr, const HttpOp &op, int *status,
		std::string *resp)
{
	httplib::Client cli(addr, kBoardPort);
	cli.set_connection_timeout(5, 0);
	cli.set_read_timeout(op.timeout_ms / 1000,
			     (op.timeout_ms % 1000) * 1000);
	httplib::Result r = (op.method == std::string("GET"))
		? cli.Get(op.path)
		: cli.Post(op.path,
			   op.xoffset.empty()
			       ? httplib::Headers{}
			       : httplib::Headers{{"X-Offset", op.xoffset}},
			   op.body, "application/octet-stream");
	if (!r)
		return false;			/* 连不上/超时/连接被断 */
	if (status)
		*status = r->status;
	if (resp)
		*resp = r->body;
	return true;
}

/* ---------- 流式 RPC（verify/commit）：板端边干边推进度行 ----------
 * 响应体逐块回调：每完整一行调 onLine（板端真实进度，非 PC 猜测）；
 * *lastLine 带回尾行（ok / fail …）供决策。读超时=进度行间隔看门狗，
 * 只要板端持续回报就永不误杀（16MiB 一报，1MiB/s 慢速也远小于 90s） */
static bool rpcStream(const std::string &addr, const std::string &path,
		      const std::function<void(const std::string &)> &onLine,
		      int *status, std::string *lastLine)
{
	httplib::Client cli(addr, kBoardPort);
	cli.set_connection_timeout(5, 0);
	cli.set_read_timeout(kStreamGapMs / 1000,
			     (kStreamGapMs % 1000) * 1000);
	std::string pend;
	auto feed = [&](const std::string &ln) {
		if (ln.empty())
			return;
		if (lastLine)
			*lastLine = ln;
		if (onLine && ln.find('=') != std::string::npos)
			onLine(ln);			/* 进度行转发；裸决策行(ok/fail)不转发 */
	};
	auto receiver = [&](const char *data, size_t len) -> bool {
		pend.append(data, len);
		for (size_t nl; (nl = pend.find('\n')) != std::string::npos;) {
			std::string ln = pend.substr(0, nl);
			pend.erase(0, nl + 1);
			while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' '))
				ln.pop_back();
			feed(ln);
		}
		return true;
	};
	httplib::Result r = cli.Get(path, receiver);
	if (!r)
		return false;
	if (status)
		*status = r->status;
	while (!pend.empty() && (pend.back() == '\r' || pend.back() == '\n'))
		pend.pop_back();
	feed(pend);
	return true;
}

/* ---------- 生命周期 ---------- */
bool HttpClientTransport::start(const QString &imagePath,
				const QByteArray &manifestRaw)
{
	m_imagePath = imagePath;
	m_manifestRaw = manifestRaw;
	Manifest m;
	if (m.loadFromImage(imagePath))
		m_parts = m.parts;
	if (!m_pusher.joinable()) {
		m_quitting = false;
		m_pusher = std::thread([this] { pusherRun(); });
	}
	/* 注意：此处不推送 /manifest——推送线程是异步的，滞留的旧 manifest
	 * 若恰逢在途上传会被 daemon 判"换镜像"而清掉正在写的临时文件，
	 * 产出头零尾真的混合文件（校验必败）。manifest 只由烧录线程在上传
	 * 前同步推送，保证顺序 */
	return !m_parts.isEmpty();
}

void HttpClientTransport::stop()
{
	m_quitting = true;
	m_abort = true;
	m_pushCv.notify_all();
	if (m_pusher.joinable())
		m_pusher.join();
	if (m_queryThread.joinable())
		m_queryThread.join();
	if (m_burnThread.joinable())
		m_burnThread.join();
}

/* ---------- 选择/控制推送 ---------- */
void HttpClientTransport::push(const std::string &path, const std::string &body)
{
	if (m_quitting)
		return;
	{
		std::lock_guard<std::mutex> l(m_pushMx);
		m_pushQ.emplace_back(path, body);
	}
	m_pushCv.notify_one();
}

void HttpClientTransport::pusherRun()
{
	for (;;) {
		std::pair<std::string, std::string> item;
		{
			std::unique_lock<std::mutex> l(m_pushMx);
			m_pushCv.wait(l, [this] {
				return m_quitting || !m_pushQ.empty();
			});
			if (m_quitting && m_pushQ.empty())
				return;
			item = std::move(m_pushQ.front());
			m_pushQ.pop_front();
		}
		QString addr;
		{
			std::lock_guard<std::mutex> l(m_addrMx);
			addr = m_addr;
		}
		if (addr.isEmpty())
			continue;	/* 还没连过板端：丢弃（烧录时会同步补发） */
		rpc(addr.toStdString(), {"POST", item.first, item.second},
		    nullptr, nullptr);
	}
}

void HttpClientTransport::setJobText(const QString &job)
{
	m_job = job;
	QString addr;
	{
		std::lock_guard<std::mutex> l(m_addrMx);
		addr = m_addr;
	}
	if (!addr.isEmpty() && !m_quitting)
		push("/select", job.toStdString());
}

/* ---------- 体检 ---------- */
void HttpClientTransport::queryBoard(const QString &addr)
{
	if (m_queryThread.joinable())
		m_queryThread.join();	/* 上一个查询已结束（有超时上限），顺延 */
	if (m_quitting)
		return;
	m_queryThread = std::thread([this, addr] { queryRun(addr); });
}

void HttpClientTransport::queryRun(const QString &addr)
{
	int st = 0;
	std::string body;
	if (!rpc(addr.toStdString(), {"GET", "/status"}, &st, &body)
	    || st != 200) {
		emit queryResult(false,
				 tr("连不上 %1:8080（daemon 未起或 IP 不对）")
					 .arg(addr));
		return;
	}
	{
		std::lock_guard<std::mutex> l(m_addrMx);
		m_addr = addr;	/* 记住地址，后续 select 推送有目标 */
	}
	/* 不在此推 manifest：异步推送可能与在途上传交错（见 start() 注释） */
	emit queryResult(true, QString::fromStdString(body));
}

/* ---------- 烧录（工作线程：上传→校验→提交→翻槽，全程合成 reportLine） ---------- */
void HttpClientTransport::beginBurn(const QString &addr)
{
	if (m_burnThread.joinable())
		m_burnThread.join();	/* 上一轮已收尾（UI 收到 job result 才复位） */
	m_abort = false;
	QString job = m_job, image = m_imagePath;
	QVector<PartInfo> parts = m_parts;
	QByteArray man = m_manifestRaw;
	m_burnThread = std::thread([this, addr, job, image, parts, man] {
		burnRun(addr, job, image, parts, man);
	});
}

void HttpClientTransport::burnRun(const QString &addr, QString job,
				  QString imagePath, QVector<PartInfo> parts,
				  QByteArray manifestRaw)
{
	const std::string a = addr.toStdString();
	auto emitLine = [this](const QString &s) { emit reportLine(s); };

	/* 0. 同步落 manifest/勾选（manifest 只走这条有序链路，杜绝异步交错）。
	 * daemon 发现在途上传时会拒收换 manifest（409）——先 abort 清场再推 */
	int st = 0;
	rpc(a, {"POST", "/manifest", manifestRaw.toStdString()}, &st, nullptr);
	if (st == 409) {
		rpc(a, {"POST", "/abort"}, &st, nullptr);
		rpc(a, {"POST", "/manifest", manifestRaw.toStdString()}, &st,
		    nullptr);
	}
	rpc(a, {"POST", "/select", job.toStdString()}, &st, nullptr);

	/* 1. /status 拿各分区断点字节数（换镜像时板端已自动清旧断点） */
	std::string sbody;
	QHash<QString, qint64> resume;
	if (!rpc(a, {"GET", "/status"}, &st, &sbody) || st != 200) {
		emitLine(tr("连接板端失败: %1:8080").arg(addr));
		emitLine(QStringLiteral("job result=fail"));
		return;
	}
	for (const QString &ln :
	     QString::fromStdString(sbody).split('\n')) {
		auto col = ln.trimmed().split(',');
		if (col.size() >= 3 && !ln.startsWith('#') && !col[0].isEmpty())
			resume[col[0].trimmed()] = col[2].trimmed().toLongLong();
	}

	int fails = 0;
	bool aborted = false;
	for (const QString &lineQ : job.split('\n')) {
		QString line = lineQ.trimmed();
		if (line.isEmpty() || line.startsWith('#'))
			continue;
		if (m_abort) {
			aborted = true;
			break;
		}

		if (line.startsWith("slot=")) {			/* 翻槽行 */
			QString ab = line.mid(5).trimmed();
			bool ok = rpc(a, {"POST", "/slot/" + ab.toStdString()},
				      &st, nullptr) && st == 200;
			emitLine(QStringLiteral("slot=%1 rc=%2")
					 .arg(ab).arg(ok ? 0 : 1));
			if (!ok)
				fails++;
			continue;
		}

		const PartInfo *p = nullptr;			/* 找分区 */
		for (const auto &c : parts)
			if (c.name == line) { p = &c; break; }
		if (!p) {
			emitLine(tr("job 里的 %1 不在 manifest").arg(line));
			fails++;
			continue;
		}
		emitLine(QStringLiteral("part=%1 stage=begin").arg(p->name));

		/* 分块上传，断点续传从板端已有字节开始 */
		qint64 got = resume.value(p->name, 0);
		if (got > p->size)
			got = 0;				/* 异常断点：重来 */
		QFile img(imagePath);
		bool upfail = false;
		if (!img.open(QIODevice::ReadOnly))
			upfail = true;
		while (!upfail && got < p->size) {
			if (m_abort)
				break;
			qint64 want = qMin<qint64>(kChunk, p->size - got);
			if (!img.seek(p->offset + got)) { upfail = true; break; }
			QByteArray buf = img.read(want);
			if (buf.size() != want) { upfail = true; break; }
			int cst = 0;
			if (!rpc(a, {"POST", "/part/" + p->name.toStdString(),
				     std::string(buf.constData(), buf.size()),
				     std::to_string(got), 30000},
				 &cst, nullptr)
			    || cst != 200) {
				upfail = true;
				break;
			}
			got += want;
			emitLine(QStringLiteral("part=%1 stage=transfer "
						"got=%2 total=%3")
					 .arg(p->name).arg(got)
					 .arg(p->size));
		}
		img.close();
		if (m_abort) { aborted = true; break; }
		if (upfail || got != p->size) {
			emitLine(tr("part=%1 上传中断（got=%2/%3）")
					 .arg(p->name).arg(got).arg(p->size));
			fails++;
			continue;
		}

		/* 板端对账：优先 GET 流式（板端每 16MiB 回报真实进度）；
		 * 旧板端 GET 不收（404/连不上）则回退 POST 非流式哑等——
		 * 此时无进度可看，超时按 1MiB/s 悲观吞吐 + 30s 余量给足 */
		std::string vlast;
		int vst = 0;
		if (!rpcStream(a, "/verify/" + p->name.toStdString(),
			       [&](const std::string &ln) {
				       emitLine(QString::fromStdString(ln));
			       }, &vst, &vlast)
		    || vst != 200) {
			long long tmo = p->size / (1024LL * 1024) * 1000 + 30000;
			if (tmo < 120000)
				tmo = 120000;
			rpc(a, {"POST", "/verify/" + p->name.toStdString(),
				"", "", tmo}, &vst, &vlast);
		}
		if (vst != 200) {
			emitLine(tr("part=%1 校验请求失败").arg(p->name));
			fails++;
			continue;
		}
		while (!vlast.empty() && vlast.back() == '\n')
			vlast.pop_back();
		bool vok = vlast.compare(0, 2, "ok") == 0;
		emitLine(QStringLiteral("part=%1 stage=verify result=%2")
					.arg(p->name).arg(vok ? "ok" : "fail"));
		if (!vok) {
			emitLine(tr("板端回报: %1")
					 .arg(QString::fromStdString(vlast)));
			fails++;
			continue;
		}

		/* 提交：流式 dd 进度；旧板端同样回退 POST 哑等 */
		std::string clast;
		int cst = 0;
		bool cok = rpcStream(a, "/commit/" + p->name.toStdString(),
				     [&](const std::string &ln) {
					     emitLine(QString::fromStdString(ln));
				     }, &cst, &clast)
			   && cst == 200;
		if (!cok) {
			long long tmo = p->size / (1024LL * 1024) * 1000 + 30000;
			if (tmo < 120000)
				tmo = 120000;
			cok = rpc(a, {"POST", "/commit/" + p->name.toStdString(),
				      "", "", tmo}, &cst, &clast)
			      && cst == 200;
		}
		while (!clast.empty() && clast.back() == '\n')
			clast.pop_back();
		if (!cok || clast.compare(0, 2, "ok") != 0) {
			emitLine(tr("part=%1 提交失败（%2）")
					 .arg(p->name)
					 .arg(QString::fromStdString(clast)));
			fails++;
			continue;
		}
		emitLine(QStringLiteral("part=%1 stage=burn done=%2")
					.arg(p->name).arg(p->size));
		emitLine(QStringLiteral("part=%1 stage=done").arg(p->name));
	}

	if (aborted || m_abort) {
		int cst = 0;
		rpc(a, {"POST", "/abort"}, &cst, nullptr);	/* 板端清断点 */
		emitLine(QStringLiteral("job result=abort"));
	} else {
		emitLine(QStringLiteral("job result=%1")
					.arg(fails ? "fail" : "ok"));
	}
}
