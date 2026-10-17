#include "icons.h"
#include <QCoreApplication>
#include <QBuffer>
#include <QDataStream>
#include <QFile>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) return 1;
    const QList<int> sizes{16, 24, 32, 48, 64, 128, 256};
    QList<QByteArray> pngs;
    for (const int size : sizes) {
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        if (!hotspotImage(size, true).save(&buffer, "PNG")) return 2;
        pngs.push_back(data);
    }
    QFile file(app.arguments().at(1));
    if (!file.open(QIODevice::WriteOnly)) return 3;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * sizes.size();
    for (int i = 0; i < sizes.size(); ++i) {
        const auto size = quint8(sizes[i] == 256 ? 0 : sizes[i]);
        stream << size << size << quint8(0) << quint8(0) << quint16(1) << quint16(32)
            << quint32(pngs[i].size()) << offset;
        offset += pngs[i].size();
    }
    for (const auto& png : pngs) stream.writeRawData(png.constData(), png.size());
    return stream.status() == QDataStream::Ok ? 0 : 4;
}
