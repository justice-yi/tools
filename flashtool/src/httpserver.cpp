#include "httpserver.h"
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <cstring>

static const quint16 kDataPort = 8080;	/* 板端拉数据/回报 */
static const quint16 kQueryPort = 8081;	/* 板端 serve :8081 体检 */

bool HttpServer::start(const QString &imagePath, const QByteArray &manifestRaw)
{
	m_imagePath = imagePath;
	m_manifestRaw = manifestRaw;
	Manifest m;
	if (m.loadFromImage(imagePath))
		m_parts = m.parts;

	auto findPart = [this](const std::string &name) -> const PartInfo * {
		for (const auto &p : m_parts)
			if (p.name.toStdString() == name)
				return &p;
		return nullptr;
	};

	m_srv.Get("/manifest", [this](const httplib::Request &, httplib::Response &res) {
		res.set_content(m_manifestRaw.constData(), (size_t)m_manifestRaw.size(),
				"text/csv");
	});

	m_srv.Get(R"(/part/([A-Za-z0-9_]+))",
		[this, findPart](const httplib::Request &req,
				 httplib::Response &res) {
		const PartInfo *p = findPart(req.matches[1].str());
		if (!p) {
			res.status = 404;
			return;
		}
		qint64 from = 0;
		if (req.has_header("Range")) {
			QString r = QString::fromStdString(req.get_header_value("Range"));
			/* 只支持 "bytes=N-" 单区间（agent 只发这种） */
			if (r.startsWith("bytes=")) {
				qint64 n = r.mid(6).section('-', 0, 0).toLongLong();
				if (n > 0)
					from = n;
			}
		}
		QFile *img = new QFile(m_imagePath);	/* 每请求独立句柄（多线程安全） */
		if (!img->open(QIODevice::ReadOnly)) {
			delete img; res.status = 500; return;
		}
		qint64 total = p->size;
		if (from > total) { delete img; res.status = 416; return; }
		qint64 len = total - from;
		res.status = (from > 0) ? 206 : 200;
		if (from > 0)
			res.set_header("Content-Range",
				QString("bytes %1-%2/%3").arg(from).arg(total - 1).arg(total)
					.toStdString());
		res.set_header("Accept-Ranges", "bytes");
		res.set_content_provider((size_t)len, "application/octet-stream",
			[this, img, from, p](size_t offset, size_t length, httplib::DataSink &sink) {
				if (m_abort)
					return false;	/* 停止烧录：立即断流 */
				char buf[65536];
				qint64 want = (qint64)length;
				if (want > (qint64)sizeof(buf))
					want = (qint64)sizeof(buf);
				if (!img->seek(p->offset + from + (qint64)offset))
					return false;
				qint64 n = img->read(buf, (qint64)want);
				if (n <= 0)
					return false;
				sink.write(buf, (size_t)n);
				return true;
			}, [img](bool success) { Q_UNUSED(success); delete img; });
	});

	m_srv.Get("/job", [this](const httplib::Request &, httplib::Response &res) {
		QMutexLocker l(&m_jobMutex);
		res.set_content(m_job.toStdString(), "text/csv");
	});

	m_srv.Post("/report", [this](const httplib::Request &req, httplib::Response &res) {
		emit reportLine(QString::fromUtf8(req.body.c_str(), (int)req.body.size()));
		res.set_content("ok", "text/plain");
	});

	m_running = true;
	m_thread = std::thread([this] {
		m_srv.listen("0.0.0.0", (int)kDataPort);
	});
	return true;
}

void HttpServer::stop()
{
	if (m_running) {
		m_srv.stop();
		if (m_thread.joinable())
			m_thread.join();
		m_running = false;
	}
}

void HttpServer::setJobText(const QString &job)
{
	{
		QMutexLocker l(&m_jobMutex);
		m_job = job;
	}
	if (!job.isEmpty())
		m_abort = false;		/* 新任务起，清除上一次停止 */
}

void HttpServer::queryBoard(const QString &addr)
{
	if (!m_nam)
		m_nam = new QNetworkAccessManager(this);
	QNetworkReply *r = m_nam->get(QNetworkRequest(QUrl(
		QString("http://%1:%2/query").arg(addr).arg(kQueryPort))));
	connect(r, &QNetworkReply::finished, this, [this, r] {
		bool ok = r->error() == QNetworkReply::NoError;
		emit queryResult(ok, ok ? QString::fromUtf8(r->readAll())
					: r->errorString());
		r->deleteLater();
	});
}
