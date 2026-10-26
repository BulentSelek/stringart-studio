#include "SyntheticImages.h"

#include <QDir>
#include <QGuiApplication>

#include <cstdio>

// Writes the synthetic sample images into the given directory (default: cwd).
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QDir dir(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath());
    if (!dir.exists() && !QDir().mkpath(dir.path())) {
        std::fprintf(stderr, "cannot create %s\n", qPrintable(dir.path()));
        return 1;
    }
    const bool ok = sa::synthetic::portrait(1024).save(dir.filePath(QStringLiteral("portrait.png")))
                    && sa::synthetic::disk(512).save(dir.filePath(QStringLiteral("disk.png")));
    if (!ok) {
        std::fprintf(stderr, "failed to write images\n");
        return 1;
    }
    std::printf("wrote portrait.png and disk.png to %s\n", qPrintable(dir.absolutePath()));
    return 0;
}
