#pragma once
#include <QString>
#include <QVector>
#include <QMutex>
#include <atomic>
#include <thread>
#include "transport.h"
#include "manifest.h"

/* [已退役 2026-10-04] 全拉模式（板端 HTTP 客户端）的实现，架构翻转为服务模式
 * （板端 daemon 监听、PC 客户端，见 httpclient.h）后不再参与构建，仅存档。
 * 本文件的 Transport 接口签名也是旧版（start 含端口语义），与新接口不兼容。 */

#define HTTPLIB_IMPLEMENTATION
#include "httplib.h"

class QNetworkAccessManager;

/*
 * Transport 的 HTTP 承载（WiFi/以太网，全拉模式，板端 ota-agent 是 HTTP 客户端）：
 *   GET /manifest        → 镜像尾部解出的 manifest 原文
 *   GET /part/<name>     → 分区数据（支持 Range: bytes=N- 断点续传，206）
 *   GET /job             → UI 勾选的任务（每行分区名，或 slot=A/B）；空=已停止
 *   POST /report         → 板端进度回报，转发 reportLine 信号到 UI 线程
 * 体检通道（原 MainWindow 里的 QNetworkAccessManager 收编至此）：
 *   queryBoard → GET http://板端:8081/query → queryResult 信号
 */
class HttpServer : public Transport {
	Q_OBJECT
public:
	using Transport::Transport;
	bool start(const QString &imagePath,
		   const QByteArray &manifestRaw) override;
	void stop() override;
	~HttpServer() override { stop(); }	/* 防止线程仍 joinable 时 terminate */

	void setJobText(const QString &job) override;
	void abortJob() override { m_abort = true; }
	void queryBoard(const QString &addr) override;
	QString describe() const override
	{
		return QStringLiteral("HTTP :8080");
	}
	QString boardCmd(const QString &pcIp) const override
	{
		return QStringLiteral("ota-agent flash %1 8080").arg(pcIp);
	}

private:
	QString m_imagePath;
	QByteArray m_manifestRaw;
	QVector<PartInfo> m_parts;		/* offset/size 定位用 */
	QMutex m_jobMutex;
	QString m_job;
	std::atomic<bool> m_abort{false};	/* 停止：在途 /part 立即断流 */
	std::atomic<bool> m_running{false};
	std::thread m_thread;
	httplib::Server m_srv;
	QNetworkAccessManager *m_nam = nullptr;	/* 体检通道用 */
};
