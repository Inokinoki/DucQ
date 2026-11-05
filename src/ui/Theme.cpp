#include "Theme.h"

#include "core/AppSettings.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPainterPath>
#include <QPolygonF>
#include <QSettings>
#include <QStyleHints>

#include <functional>

namespace ducq {
namespace Theme {

namespace {

Tokens lightTokens()
{
    Tokens t;
    t.isDark = false;
    // palette roles (GitHub Light canvas tones)
    t.window = QColor(0xf6, 0xf8, 0xfa);
    t.base = QColor(0xff, 0xff, 0xff);
    t.alternateBase = QColor(0xf6, 0xf8, 0xfa);
    t.text = QColor(0x1f, 0x23, 0x28);
    t.buttonText = t.text;
    t.windowText = t.text;
    t.placeholder = QColor(0x6e, 0x77, 0x81);
    t.highlight = QColor(0x09, 0x69, 0xda);
    t.highlightedText = QColor(0xff, 0xff, 0xff);
    t.link = QColor(0x09, 0x69, 0xda);
    t.toolTipBase = QColor(0x42, 0x4a, 0x53);
    t.toolTipText = QColor(0xff, 0xff, 0xff);
    t.disabledText = QColor(0x8c, 0x95, 0x9f);
    // syntax (matches the historical GitHub Light highlighting)
    t.keyword = QColor(0x05, 0x50, 0xae);
    t.string = QColor(0x0a, 0x7a, 0x2f);
    t.number = QColor(0x95, 0x38, 0x00);
    t.comment = QColor(0x6e, 0x77, 0x81);
    // semantic feedback (all pairs WCAG AA on base/alternate/window)
    t.nullText = QColor(0x65, 0x6d, 0x76);
    t.error = QColor(0xc6, 0x28, 0x28);
    t.cancelled = QColor(0x9e, 0x6a, 0x03);
    t.success = QColor(0x1a, 0x7f, 0x37);
    // surfaces
    t.currentLine = QColor(0xea, 0xee, 0xf2);
    t.border = QColor(0xd0, 0xd7, 0xde);
    return t;
}

Tokens darkTokens()
{
    Tokens t;
    t.isDark = true;
    // palette roles (GitHub Dark canvas tones)
    t.window = QColor(0x24, 0x29, 0x2f);
    t.base = QColor(0x1c, 0x21, 0x28);
    t.alternateBase = QColor(0x26, 0x2c, 0x33);
    t.text = QColor(0xe6, 0xed, 0xf3);
    t.buttonText = t.text;
    t.windowText = t.text;
    t.placeholder = QColor(0x8b, 0x94, 0x9e);
    t.highlight = QColor(0x31, 0x6d, 0xca);
    t.highlightedText = QColor(0xff, 0xff, 0xff);
    t.link = QColor(0x58, 0xa6, 0xff);
    t.toolTipBase = QColor(0x44, 0x4c, 0x56);
    t.toolTipText = QColor(0xe6, 0xed, 0xf3);
    t.disabledText = QColor(0x7d, 0x85, 0x90);
    // syntax (GitHub Dark Dimmed accents)
    t.keyword = QColor(0x79, 0xc0, 0xff);
    t.string = QColor(0x7e, 0xe7, 0x87);
    t.number = QColor(0xff, 0xa6, 0x57);
    t.comment = QColor(0x8b, 0x94, 0x9e);
    // semantic feedback (all pairs WCAG AA on base/alternate/window)
    t.nullText = QColor(0x8b, 0x94, 0x9e);
    t.error = QColor(0xff, 0x7b, 0x72);
    t.cancelled = QColor(0xd2, 0x99, 0x22);
    t.success = QColor(0x3f, 0xb9, 0x50);
    // surfaces
    t.currentLine = QColor(0x2d, 0x33, 0x3b);
    t.border = QColor(0x44, 0x4c, 0x56);
    return t;
}

bool systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;
#endif
}

Tokens s_tokens = lightTokens();

} // namespace

Mode savedMode()
{
    std::unique_ptr<QSettings> settings(AppSettings::create());
    const QString value = settings->value(QStringLiteral("theme")).toString();
    if (value == QLatin1String("light"))
        return Mode::Light;
    if (value == QLatin1String("dark"))
        return Mode::Dark;
    return Mode::System;
}

void saveMode(Mode mode)
{
    std::unique_ptr<QSettings> settings(AppSettings::create());
    const QString value = mode == Mode::Light ? QStringLiteral("light")
        : mode == Mode::Dark                  ? QStringLiteral("dark")
                                              : QStringLiteral("system");
    settings->setValue(QStringLiteral("theme"), value);
}

void applyTheme(Mode mode)
{
    // One app-wide style: Fusion makes the palettes below fully effective on
    // every platform (native styles ignore large parts of QPalette).
    static const bool styleInitialized = [] {
        QApplication::setStyle(QStringLiteral("Fusion"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        // Follow live system appearance switches while in System mode.
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                         [] { applyTheme(savedMode()); });
#endif
        return true;
    }();
    Q_UNUSED(styleInitialized);

    const bool dark = mode == Mode::Dark || (mode == Mode::System && systemPrefersDark());
    s_tokens = dark ? darkTokens() : lightTokens();
    QApplication::setPalette(paletteFor(s_tokens));
}

const Tokens &tokens()
{
    return s_tokens;
}

QPalette paletteFor(const Tokens &t)
{
    QPalette palette;
    palette.setColor(QPalette::Window, t.window);
    palette.setColor(QPalette::Base, t.base);
    palette.setColor(QPalette::AlternateBase, t.alternateBase);
    palette.setColor(QPalette::WindowText, t.windowText);
    palette.setColor(QPalette::Text, t.text);
    // Buttons, combo boxes, headers and scrollbars all render from the Button
    // role under Fusion — leaving it unset would keep native light chrome.
    palette.setColor(QPalette::Button, t.window);
    palette.setColor(QPalette::ButtonText, t.buttonText);
    palette.setColor(QPalette::PlaceholderText, t.placeholder);
    palette.setColor(QPalette::Highlight, t.highlight);
    palette.setColor(QPalette::HighlightedText, t.highlightedText);
    palette.setColor(QPalette::Link, t.link);
    palette.setColor(QPalette::ToolTipBase, t.toolTipBase);
    palette.setColor(QPalette::ToolTipText, t.toolTipText);
    palette.setColor(QPalette::BrightText, t.error);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, t.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Text, t.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, t.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Button, t.window);
    palette.setColor(QPalette::Disabled, QPalette::Base, t.base);
    return palette;
}

namespace Icons {

namespace {

// All glyphs are authored on a 16x16 grid and scaled to the request.
QPixmap drawGlyph(const std::function<void(QPainter &)> &paint, int size)
{
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 16.0, size / 16.0);
    paint(painter);
    return pm;
}

QPen glyphPen(const QColor &color, qreal width = 1.3)
{
    QPen pen(color, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

QPixmap drawIcon(Icon which, int size, const QColor &color)
{
    return drawGlyph([which, color](QPainter &p) {
        switch (which) {
        case Icon::Open: {
            QPainterPath folder;
            folder.moveTo(2, 12.5);
            folder.lineTo(2, 4.5);
            folder.quadTo(2, 3.5, 3, 3.5);
            folder.lineTo(6, 3.5);
            folder.quadTo(6.9, 3.5, 7.3, 4.2);
            folder.lineTo(8, 5.5);
            folder.lineTo(13, 5.5);
            folder.quadTo(14, 5.5, 14, 6.5);
            folder.lineTo(14, 12.5);
            folder.quadTo(14, 13.5, 13, 13.5);
            folder.lineTo(3, 13.5);
            folder.quadTo(2, 13.5, 2, 12.5);
            p.setPen(glyphPen(color));
            p.setBrush(Qt::NoBrush);
            p.drawPath(folder);
            break;
        }
        case Icon::NewDatabase: {
            p.setPen(glyphPen(color));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QRectF(2, 2.5, 9, 4.2));
            p.drawLine(QPointF(2, 4.6), QPointF(2, 11.5));
            p.drawLine(QPointF(11, 4.6), QPointF(11, 11.5));
            p.drawArc(QRectF(2, 9.4, 9, 4.2), 180 * 16, 180 * 16);
            p.drawLine(QPointF(11.75, 12), QPointF(14.25, 12));
            p.drawLine(QPointF(13, 10.75), QPointF(13, 13.25));
            break;
        }
        case Icon::Refresh: {
            p.setPen(glyphPen(color));
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(2.5, 2.5, 11, 11), 30 * 16, 300 * 16);
            // Arrowhead at the arc's start (30 deg).
            const QPointF tip(11.4, 3.2);
            QPolygonF head;
            head << tip << QPointF(13.9, 4.6) << QPointF(11.2, 6.1);
            p.setBrush(color);
            p.setPen(Qt::NoPen);
            p.drawPolygon(head);
            break;
        }
        case Icon::Run: {
            QPainterPath play;
            play.moveTo(5, 3.5);
            play.lineTo(12.5, 8);
            play.lineTo(5, 12.5);
            play.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawPath(play);
            break;
        }
        case Icon::Cancel: {
            p.setPen(glyphPen(color, 1.6));
            p.drawLine(QPointF(3.5, 3.5), QPointF(12.5, 12.5));
            p.drawLine(QPointF(12.5, 3.5), QPointF(3.5, 12.5));
            break;
        }
        case Icon::Remote: {
            // A little server rack: two stacked units with status LEDs.
            p.setPen(glyphPen(color));
            p.drawRoundedRect(QRectF(2.5, 3.5, 11, 4), 1.2, 1.2);
            p.drawRoundedRect(QRectF(2.5, 8.5, 11, 4), 1.2, 1.2);
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            for (const QPointF &led : { QPointF(4.6, 5.5), QPointF(4.6, 10.5) })
                p.drawEllipse(led, 0.8, 0.8);
            break;
        }
        case Icon::Duck:
            break;
        }
    }, size);
}

QIcon iconAtSizes(Icon which, const QColor &color)
{
    QIcon icon;
    for (const int size : { 16, 24, 32, 48 })
        icon.addPixmap(drawIcon(which, size, color));
    return icon;
}

} // namespace

QIcon actionIcon(Icon which)
{
    return iconAtSizes(which, tokens().text);
}

QIcon statusIcon(Status status)
{
    return drawGlyph([status](QPainter &p) {
        const QPointF center(8, 8);
        switch (status) {
        case Status::Running:
            // Hollow ring: the query is still going.
            p.setPen(glyphPen(tokens().text, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(center, 3.5, 3.5);
            break;
        case Status::Ok: {
            // Green dot with a check mark: colour + shape carry the state.
            p.setPen(Qt::NoPen);
            p.setBrush(tokens().success);
            p.drawEllipse(center, 4, 4);
            QPainterPath check;
            check.moveTo(5.6, 8.1);
            check.lineTo(7.3, 9.8);
            check.lineTo(10.5, 6.4);
            p.setPen(glyphPen(Qt::white, 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawPath(check);
            break;
        }
        case Status::Error: {
            // Red dot with an X.
            p.setPen(Qt::NoPen);
            p.setBrush(tokens().error);
            p.drawEllipse(center, 4, 4);
            p.setPen(glyphPen(Qt::white, 1.4));
            p.drawLine(QPointF(6.2, 6.2), QPointF(9.8, 9.8));
            p.drawLine(QPointF(9.8, 6.2), QPointF(6.2, 9.8));
            break;
        }
        case Status::Cancelled: {
            // Amber dot with a dash.
            p.setPen(Qt::NoPen);
            p.setBrush(tokens().cancelled);
            p.drawEllipse(center, 4, 4);
            p.setPen(glyphPen(Qt::white, 1.4));
            p.drawLine(QPointF(5.8, 8), QPointF(10.2, 8));
            break;
        }
        }
    }, 16);
}

QIcon appIcon()
{
    return drawGlyph([](QPainter &p) {
        // Rounded accent square with a white duck silhouette.
        const QColor accent = tokens().highlight;
        QPainterPath bg;
        bg.addRoundedRect(QRectF(1, 1, 30, 30), 7, 7);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawPath(bg);
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(4.5, 15, 17, 11));  // body
        p.drawEllipse(QRectF(11.5, 6.5, 12, 12)); // head
        QPolygonF beak;
        beak << QPointF(23.2, 10.8) << QPointF(28.5, 12.6) << QPointF(23.2, 14.8);
        p.drawPolygon(beak);
        p.setBrush(accent);
        p.drawEllipse(QPointF(19.4, 11.2), 1.5, 1.5); // eye
    }, 32);
}

} // namespace Icons

} // namespace Theme
} // namespace ducq
