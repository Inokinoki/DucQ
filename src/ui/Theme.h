#pragma once

#include <QColor>
#include <QIcon>

class QPalette;

namespace ducq {
namespace Theme {

enum class Mode { System, Light, Dark };

// Reads the persisted mode ("theme" setting); System by default.
Mode savedMode();
// Persists the mode under the "theme" setting.
void saveMode(Mode mode);
// Resolves System against the platform color scheme (Qt >= 6.5) and applies
// the Fusion style + matching palette app-wide. Safe to call again on change.
void applyTheme(Mode mode);

// Semantic colors for the currently applied mode. Read at use time so UI
// code never hardcodes theme colors. Safe before applyTheme: light tokens.
struct Tokens {
    bool isDark = false;
    // palette roles
    QColor window, base, alternateBase, text, buttonText, windowText, placeholder;
    QColor highlight, highlightedText, link, toolTipBase, toolTipText, disabledText;
    // syntax
    QColor keyword, string, number, comment;
    // semantic feedback
    QColor nullText, error, cancelled, success;
    // surfaces
    QColor currentLine, border;
};

const Tokens &tokens();

QPalette paletteFor(const Tokens &t);

// Small programmatic icons drawn with QPainter from the current theme
// colors — no resource files, recolored for free when the theme changes.
namespace Icons {

enum class Icon { Open, NewDatabase, Refresh, Run, Cancel, Duck, Remote };

// Toolbar/action icon using the current theme's text color.
QIcon actionIcon(Icon which);
// Fixed-color app icon (rounded accent square with a duck silhouette).
QIcon appIcon();

enum class Status { Running, Ok, Error, Cancelled };
// Small dot/ring glyphs marking a result tab's query state.
QIcon statusIcon(Status status);

} // namespace Icons

} // namespace Theme
} // namespace ducq
