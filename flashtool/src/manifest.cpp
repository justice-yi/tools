#include "manifest.h"
#include <QFile>
#include <QtGlobal>
#include <cctype>

/* CRC-32 IEEE（与 zlib/mk-image.sh 相同多项式） */
static quint32 crc32_ieee(const char *data, qint64 len)
{
	static quint32 table[256];
	static bool inited = false;
	if (!inited) {
		for (quint32 i = 0; i < 256; i++) {
			quint32 c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			table[i] = c;
		}
		inited = true;
	}
	quint32 c = 0xFFFFFFFFu;
	for (qint64 i = 0; i < len; i++)
		c = table[(c ^ (quint8)data[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

static QString trimCsv(QString s)
{
	while (!s.isEmpty() && (s.endsWith(' ') || s.endsWith('\r') || s.endsWith('\n')))
		s.chop(1);
	int i = 0;
	while (i < s.size() && s[i] == ' ')
		i++;
	return s.mid(i);
}

bool Manifest::loadFromImage(const QString &imagePath)
{
	parts.clear(); version.clear(); device.clear();
	QFile f(imagePath);
	if (!f.open(QIODevice::ReadOnly))
		return false;
	qint64 size = f.size();
	if (size < 16)
		return false;
	if (!f.seek(size - 16))
		return false;
	QByteArray desc = f.read(16);
	if (desc.mid(8) != "IMX6OTA1")
		return false;
	quint32 mlen = (quint8)desc[0] | ((quint8)desc[1] << 8) |
		       ((quint32)(quint8)desc[2] << 16) | ((quint32)(quint8)desc[3] << 24);
	quint32 mcrc = (quint8)desc[4] | ((quint8)desc[5] << 8) |
		       ((quint32)(quint8)desc[6] << 16) | ((quint32)(quint8)desc[7] << 24);
	if ((qint64)mlen + 16 > size || mlen == 0)
		return false;
	if (!f.seek(size - 16 - mlen))
		return false;
	QByteArray man = f.read(mlen);
	if ((qint64)man.size() != mlen)
		return false;
	if (crc32_ieee(man.constData(), man.size()) != mcrc)
		return false;

	const auto lines = QString::fromUtf8(man).split('\n');
	for (const QString &ln : lines) {
		QString line = trimCsv(ln);
		if (line.isEmpty() || line.startsWith('#')) {
			if (line.startsWith("# version="))
				version = line.mid(11).trimmed();
			if (line.startsWith("# device="))
				device = line.mid(9).trimmed();
			continue;
		}
		const auto col = line.split(',');
		if (col.size() < 6)
			continue;
		PartInfo p;
		p.name   = trimCsv(col[0]);
		p.offset = trimCsv(col[1]).toLongLong();
		p.size   = trimCsv(col[2]).toLongLong();
		p.ptype  = trimCsv(col[3]);
		p.fstype = trimCsv(col[4]);
		p.sha256 = trimCsv(col[5]);
		if (!p.name.isEmpty() && p.size > 0)
			parts.append(p);
	}
	return !parts.isEmpty();
}
