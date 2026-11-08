// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/MathRenderer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>

#include "latex.h"
#include "platform/qt/graphic_qt.h"

#include <cmath>
#include <memory>

namespace
{

// MicroTeX reads its metrics from data compiled into the library, but loads the
// glyphs from files under this directory, so its presence is what makes
// rendering possible at all.
bool looksLikeResourceRoot(const QString &path)
{
    return QFileInfo(path + QStringLiteral("/fonts")).isDir();
}

// Blank margin around the formula, so antialiased edges and the depth of
// descenders are not clipped by the bitmap. The TeX boxes report their extent
// a little tight, so this is generous on purpose.
constexpr int kPadding = 4;

// Whether anything at all was drawn. A parse that produces a bitmap without a
// single opaque pixel is a rendering defect -- a glyph the font backend could
// not produce -- rather than a legitimate empty formula.
bool hasInk(const QImage &bitmap)
{
    const int height = bitmap.height();
    const int width = bitmap.width();
    for (int y = 0; y < height; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(bitmap.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            if (qAlpha(line[x]) > 0)
                return true;
        }
    }
    return false;
}

} // namespace

QString MathRenderer::resourceDirectory()
{
    const QString base = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        base + QStringLiteral("/../share/total-markdown/microtex/res"), // installed
        base + QStringLiteral("/../third_party/microtex/res"), // build tree
        base + QStringLiteral("/microtex/res"), // a self-contained bundle
    };

    for (const QString &candidate : candidates) {
        if (looksLikeResourceRoot(candidate))
            return QDir::cleanPath(candidate);
    }
    return QString();
}

MathRenderer &MathRenderer::instance()
{
    static MathRenderer renderer;
    return renderer;
}

MathRenderer::~MathRenderer()
{
    if (m_available)
        tex::LaTeX::release();
}

void MathRenderer::setMaximumWidth(int width)
{
    m_maximumWidth = width > 0 ? width : 1024;
}

bool MathRenderer::isAvailable()
{
    if (m_initialised)
        return m_available;
    m_initialised = true;

    const QString directory = resourceDirectory();
    if (directory.isEmpty())
        return false;

    tex::LaTeX::init(directory.toStdString());
    m_available = true;
    return true;
}

QString MathRenderer::keyFor(const QString &tex, bool display, qreal pointSize, const QColor &color, qreal devicePixelRatio) const
{
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(tex, display ? QStringLiteral("D") : QStringLiteral("I"))
        .arg(pointSize)
        .arg(color.rgba())
        .arg(devicePixelRatio);
}

QImage MathRenderer::image(const QString &tex, bool display, qreal pointSize, const QColor &color, qreal devicePixelRatio)
{
    const QString source = tex.trimmed();
    if (source.isEmpty() || !isAvailable())
        return QImage();

    const QString key = keyFor(source, display, pointSize, color, devicePixelRatio);
    const auto cached = m_cache.constFind(key);
    if (cached != m_cache.constEnd())
        return cached.value();

    const qreal ratio = devicePixelRatio > 0 ? devicePixelRatio : 1.0;
    // MicroTeX works at one pixel per point and scales internally by the text
    // size, so asking for a bigger size is what makes the formula sharp on a
    // high-density screen; the bitmap is then tagged with the ratio.
    const float textSize = float(pointSize * ratio);
    const int width = display ? int(m_maximumWidth * ratio) : int(m_maximumWidth * 4 * ratio);

    std::unique_ptr<tex::TeXRender> render(
        tex::LaTeX::parse(source.toStdWString(), width, textSize, textSize * 0.5f, tex::rgb(color.red(), color.green(), color.blue())));
    if (!render)
        return QImage();

    const int bitmapWidth = int(std::ceil(render->getWidth())) + 2 * kPadding;
    // A formula's box is the part above the baseline plus the depth below it.
    const int bitmapHeight = int(std::ceil(render->getHeight() + render->getDepth())) + 2 * kPadding;
    if (bitmapWidth <= 2 * kPadding || bitmapHeight <= 2 * kPadding)
        return QImage();

    QImage bitmap(bitmapWidth, bitmapHeight, QImage::Format_ARGB32_Premultiplied);
    if (bitmap.isNull())
        return QImage();
    bitmap.fill(Qt::transparent);
    {
        QPainter painter(&bitmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        tex::Graphics2D_qt graphics(&painter);
        render->draw(graphics, kPadding, kPadding);
    }

    // Refuse a blank bitmap instead of handing the pane an invisible hole: the
    // caller shows the formula's source text when there is no image. The result
    // is deliberately not cached, so a fixed backend picks it up at once.
    if (!hasInk(bitmap))
        return QImage();

    bitmap.setDevicePixelRatio(ratio);

    m_cache.insert(key, bitmap);
    return bitmap;
}

void MathRenderer::clearCache()
{
    m_cache.clear();
}
