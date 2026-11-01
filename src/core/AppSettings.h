#pragma once

class QSettings;

namespace ducq {

// Creates the app-wide settings store, owned by the caller.
//
// On macOS Qt derives the plist name from the organization with a forced
// "com." prefix and dot-to-dash mangling ("com.cc-inoki.DucQ"), which is not
// the identifier we ship in the bundle (cc.inoki.DucQ). Pin the plist path
// explicitly there; other platforms keep Qt's default location.
class AppSettings {
public:
    static QSettings *create();
};

} // namespace ducq
