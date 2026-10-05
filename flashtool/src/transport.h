#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>

/*
 * 传输层抽象：烧录会话与具体承载解耦（服务模式——PC 永远是发起方）。
 *
 * 会话层语义（与承载无关，UI/状态机只认这些）：
 *   - 选择清单是文本（每行分区名，可含 slot=A/B），全量覆盖、幂等；
 *     勾选/取消随时推送，板端永远只有"当前清单"，不存在半更新状态
 *   - 回报是文本行（part=x stage=... / slot=... / job result=...），
 *     由承载侧合成，UI 全套复用；"job result=" 一行即整个任务收尾
 *
 * 当前实现：HttpClientTransport（HTTP 客户端，对板端 ota-agent daemon）。
 * 裸 USB（bulk，无 IP 栈）将来另写一个 Transport 子类，MainWindow 不动。
 */
class Transport : public QObject {
	Q_OBJECT
public:
	explicit Transport(QObject *parent = nullptr) : QObject(parent) {}
	~Transport() override = default;

	/* 备好本地资源（HTTP 客户端=解析镜像分区表，无网络动作） */
	virtual bool start(const QString &imagePath,
			   const QByteArray &manifestRaw) = 0;
	/* 收尾：中断在途烧录并回收线程 */
	virtual void stop() = 0;
	/* 记录当前勾选清单；若已连过板端则异步推送（全量覆盖） */
	virtual void setJobText(const QString &job) = 0;
	/* 停止当前烧录：中断上传 + 通知板端清理断点临时文件 */
	virtual void abortJob() = 0;
	/* 轻量体检：拉板端安装记录（不实时 hash，毫秒级），应答走 queryResult */
	virtual void queryBoard(const QString &addr) = 0;
	/* 启动烧录（客户端驱动：上传→校验→提交→翻槽），进度走 reportLine */
	virtual void beginBurn(const QString &addr) = 0;
	/* 顶栏标签文案，如 "HTTP 客户端" */
	virtual QString describe() const = 0;
	/* 本承载下物理不可安全烧写的分区名（UI 行锁定 + 板端兜底拒收）：
	 * HTTP/WiFi=暂存区所在分区（板端临时文件与目标分区同区，dd 会覆盖
	 * 在途临时文件，半成品直进分区）；裸 USB 流式直写无此约束，默认空 */
	virtual QStringList bannedParts() const { return {}; }

signals:
	/* 烧录过程回报一行（transfer/verify/burn/done/slot/job result） */
	void reportLine(const QString &line);
	/* 体检应答：ok=false 时 body 为错误文案；ok=true 时为 /status 原文 */
	void queryResult(bool ok, const QString &body);
};
