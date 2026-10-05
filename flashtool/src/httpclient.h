#pragma once
#include <QString>
#include <QVector>
#include <QByteArray>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include "transport.h"
#include "manifest.h"

/*
 * Transport 的 HTTP 客户端承载（服务模式）：
 * PC 永远是发起方，板端 ota-agent daemon（:8080）常驻监听。接口映射：
 *   POST /manifest    镜像 manifest（板端发现换 manifest 会作废旧断点）
 *   POST /select      当前勾选清单（全量覆盖、幂等）
 *   POST /part/<名>   分块上传（X-Offset 续传，1MiB/块）
 *   POST /verify/<名> 板端 sha256 对账
 *   POST /commit/<名> 板端 dd+fsync+写安装记录
 *   POST /slot/A|B    fw_setenv 翻槽
 *   POST /abort       清理断点临时文件
 *   GET  /status      轻量体检（安装记录+断点+槽+上次版本）
 *
 * 上传进度由本类合成为 reportLine 文本行（沿用旧全拉模式的行格式，UI 复用）。
 * 线程模型：小请求（manifest/select）走推送队列线程；/status 一次一个查询线程；
 * 烧录走独立工作线程。信号从工作线程 emit，跨线程自动排队到 UI 线程。
 */
class HttpClientTransport : public Transport {
	Q_OBJECT
public:
	using Transport::Transport;
	~HttpClientTransport() override { stop(); }

	bool start(const QString &imagePath,
		   const QByteArray &manifestRaw) override;
	void stop() override;
	void setJobText(const QString &job) override;
	void abortJob() override { m_abort = true; }
	void queryBoard(const QString &addr) override;
	void beginBurn(const QString &addr) override;
	QString describe() const override
	{
		return QStringLiteral("HTTP 客户端");
	}
	/* WiFi 承载禁烧 data：板端暂存区约定 /data/.ota（OTA_DIR），
	 * 烧 data=dd 覆盖在途临时文件。裸 USB 子类返回空即可解锁 */
	QStringList bannedParts() const override
	{
		return { QStringLiteral("data") };
	}

private:
	void push(const std::string &path, const std::string &body);
	void pusherRun();			/* 控制请求队列线程 */
	void queryRun(const QString &addr);	/* /status 查询线程 */
	void burnRun(const QString &addr, QString job, QString imagePath,
		     QVector<PartInfo> parts, QByteArray manifestRaw);

	QString m_imagePath;
	QByteArray m_manifestRaw;
	QVector<PartInfo> m_parts;
	QString m_job;				/* UI 线程写，beginBurn 快照给工作线程 */

	std::mutex m_addrMx;			/* 保护 m_addr（查询线程写，推送线程读） */
	QString m_addr;				/* 最近一次连通的板端地址 */

	std::atomic<bool> m_abort{false};
	std::atomic<bool> m_quitting{false};

	std::thread m_pusher;			/* 控制请求队列 */
	std::mutex m_pushMx;
	std::condition_variable m_pushCv;
	std::deque<std::pair<std::string, std::string>> m_pushQ;

	std::thread m_queryThread;		/* 一次一个，重复点击顺延 */
	std::thread m_burnThread;
};
