#pragma once
#include <QString>
#include <QVector>

struct PartInfo {
	QString name;
	qint64 offset = 0, size = 0;
	QString ptype, fstype, sha256;
};

class Manifest {
public:
	QString version, device;
	QVector<PartInfo> parts;

	/* 从镜像尾部解析（[csv][4B len LE][4B crc32][8B "IMX6OTA1"]），失败返回 false */
	bool loadFromImage(const QString &imagePath);
};
