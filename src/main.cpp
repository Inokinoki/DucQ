#include "ui/MainWindow.h"

#include "core/AppSettings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLocale>
#include <QSettings>
#include <QTranslator>
#include <memory>

namespace {
// Returns the language code saved in the settings, or the best match for the
// system locale; falls back to "en".
QString resolveStartupLanguage()
{
    static const char *const kKnown[] = { "en", "zh_CN", "ja", "ko", "de", "fr", "es", "pt_BR", "ru" };
    std::unique_ptr<QSettings> settings(ducq::AppSettings::create());
    const QString saved = settings->value(QStringLiteral("language")).toString();
    for (const char *code : kKnown) {
        if (saved == QLatin1String(code))
            return saved;
    }
    const QString system = QLocale::system().name(); // e.g. "zh_CN"
    for (const char *code : kKnown) {
        if (system == QLatin1String(code))
            return QLatin1String(code);
    }
    const QString systemPrefix = system.section(QLatin1Char('_'), 0, 0);
    for (const char *code : kKnown) {
        if (QLatin1String(code).startsWith(systemPrefix) && systemPrefix.size() >= 2)
            return QLatin1String(code);
    }
    return QStringLiteral("en");
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DucQ"));
    QApplication::setOrganizationName(QStringLiteral("Inoki"));
    QApplication::setOrganizationDomain(QStringLiteral("cc.inoki"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    const QString language = resolveStartupLanguage();
    auto *translator = new QTranslator(&app);
    if (language != QLatin1String("en") && translator->load(QStringLiteral(":/i18n/ducq_%1.qm").arg(language)))
        QApplication::installTranslator(translator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DucQ — DuckDB GUI"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("database"),
                                 QStringLiteral("Database file to open at startup (omit for in-memory)."),
                                 QStringLiteral("[database]"));
    QCommandLineOption executeOption(QStringLiteral("execute"),
                                     QStringLiteral("Run the SQL script after startup."));
    executeOption.setValueName(QStringLiteral("file"));
    parser.addOption(executeOption);
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    ducq::MainWindow window(positional.isEmpty() ? QString() : positional.first());
    window.show();

    const QString script = parser.value(executeOption);
    if (!script.isEmpty())
        window.executeScriptAtStartup(script);

    return QApplication::exec();
}
