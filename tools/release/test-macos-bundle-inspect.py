#!/usr/bin/env python3
"""Fixture tests for macOS ARM64 bundle inspection. No Homebrew and no Mach-O tools."""

import sys

import macos_bundle_inspect as insp

EXE = "Contents/MacOS/rigor-qt"
QTCORE = "Contents/Frameworks/QtCore.framework/Versions/5/QtCore"
PLUGINS = {
    "libqminimal.dylib": "Contents/PlugIns/platforms/libqminimal.dylib",
    "libqcocoa.dylib": "Contents/PlugIns/platforms/libqcocoa.dylib",
    "libqmacstyle.dylib": "Contents/PlugIns/styles/libqmacstyle.dylib",
}
RPATH = "          cmd LC_RPATH\n      cmdsize 48\n         path @executable_path/../Frameworks (offset 12)\n"
SYSTEM = """\
	/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)
	/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit (compatibility version 1.0.0, current version 1.0.0)
"""


def dep_line(path):
    return f"\t{path} (compatibility version 1.0.0, current version 1.0.0)\n"


def macho(rel, deps, rpath=RPATH, arch="arm64", lipo_status=0, otool_status=0, load_status=0, is_macho=True):
    body = f"{rel}:\n" + "".join(dep_line(item) for item in deps)
    return insp.MachOView(
        relpath=rel,
        is_macho=is_macho,
        lipo_status=lipo_status,
        lipo_stdout=arch,
        otool_status=otool_status,
        otool_stdout=body,
        load_status=load_status,
        load_stdout=rpath,
    )


def bundle_files():
    return {EXE, QTCORE, *PLUGINS.values()}


def good_views(main_deps=None, plugin_dep=None):
    qt = "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore"
    main = main_deps if main_deps is not None else [qt]
    views = [macho(EXE, main + [
        "/usr/lib/libSystem.B.dylib",
        "/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit",
    ])]
    for name, rel in PLUGINS.items():
        own = f"@executable_path/../{rel[len('Contents/'):]}"
        extra = plugin_dep(rel) if plugin_dep else [qt]
        views.append(macho(rel, [own] + extra + [
            "/usr/lib/libc++.1.dylib",
            "/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit",
        ]))
    return views


def expect_ok(name, views, files=None):
    errors = insp.inspect_records(views, files if files is not None else bundle_files(), EXE)
    if errors:
        print(f"FAIL {name}")
        for error in errors:
            print(f"  {error}")
        return 1
    print(f"PASS {name}")
    return 0


def expect_fail(name, views, needle, files=None):
    errors = insp.inspect_records(views, files if files is not None else bundle_files(), EXE)
    if not errors or not any(needle in error for error in errors):
        print(f"FAIL {name}: expected {needle!r} in {errors}")
        return 1
    print(f"PASS {name}")
    return 0


def main():
    failed = 0
    qt = "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore"
    failed += expect_ok("arm64-only Mach-O is accepted", good_views())

    bad_arch = good_views()
    bad_arch[0] = macho(EXE, [qt, "/usr/lib/libSystem.B.dylib"], arch="x86_64")
    failed += expect_fail("x86_64 Mach-O is rejected", bad_arch, "is not thin arm64")

    universal = good_views()
    universal[0] = macho(EXE, [qt, "/usr/lib/libSystem.B.dylib"], arch="x86_64 arm64")
    failed += expect_fail("universal arm64+x86_64 Mach-O is rejected", universal, "is not thin arm64")

    homebrew = good_views()
    cocoa = PLUGINS["libqcocoa.dylib"]
    homebrew[2] = macho(cocoa, [
        "/opt/homebrew/opt/qt@5/plugins/platforms/libqcocoa.dylib",
        qt,
        "/usr/lib/libc++.1.dylib",
    ])
    failed += expect_fail("absolute /opt/homebrew/opt/qt@5 dependency is rejected", homebrew, "/opt/homebrew/opt/qt@5/")

    local = good_views()
    local[1] = macho(PLUGINS["libqminimal.dylib"], [
        "@executable_path/../PlugIns/platforms/libqminimal.dylib",
        "/usr/local/opt/qt@5/lib/QtCore.framework/Versions/5/QtCore",
        "/usr/lib/libc++.1.dylib",
    ])
    failed += expect_fail("absolute /usr/local Homebrew dependency is rejected", local, "/usr/local/")

    rpath_views = good_views(main_deps=["@rpath/QtCore.framework/Versions/5/QtCore"])
    failed += expect_ok("valid @rpath Qt dependency is accepted when the target is bundled", rpath_views)

    def loader(rel):
        return ["@loader_path/../../Frameworks/QtCore.framework/Versions/5/QtCore"]

    failed += expect_ok("valid @loader_path dependency is accepted when the target is bundled", good_views(plugin_dep=loader))

    missing_files = bundle_files()
    missing_files.remove(QTCORE)
    failed += expect_fail("missing bundled Qt library is rejected", good_views(), "broken relocatable dependency", missing_files)

    broken = good_views(main_deps=["@rpath/QtCore.framework/Versions/5/QtCore"])
    broken[0] = macho(
        EXE,
        ["@rpath/libQt5Core.5.dylib", "/usr/lib/libSystem.B.dylib"],
        rpath="          cmd LC_RPATH\n         path @executable_path/../Frameworks (offset 12)\n",
    )
    failed += expect_fail("broken rpath is rejected", broken, "broken rpath")

    lipo_fail = good_views()
    lipo_fail[0] = macho(EXE, [qt, "/usr/lib/libSystem.B.dylib"], lipo_status=1)
    failed += expect_fail("lipo inspection failure is rejected", lipo_fail, "lipo inspection failed")

    otool_fail = good_views()
    otool_fail[0] = macho(EXE, [qt, "/usr/lib/libSystem.B.dylib"], otool_status=1)
    failed += expect_fail("otool inspection failure is rejected", otool_fail, "otool -L failed")

    skipped = good_views()
    skipped[2] = macho(cocoa, [qt], is_macho=False)
    failed += expect_fail("historical Qt plugins are inspected and not skipped", skipped, "was not inspected as Mach-O")

    absent = [view for view in good_views() if not view.relpath.endswith("libqmacstyle.dylib")]
    failed += expect_fail("a missing historical Qt plugin is rejected", absent, "missing required Qt plugin libqmacstyle.dylib")

    system_ok = good_views()
    failed += expect_ok("system /usr/lib and /System/Library dependencies stay accepted", system_ok)

    if failed:
        print(f"MACOS_BUNDLE_INSPECT_TESTS=FAIL count={failed}")
        return 1
    print("MACOS_BUNDLE_INSPECT_TESTS=PASS")
    print("NATIVE_APPLE_SILICON_ACCEPTANCE=NOT_CLAIMED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
