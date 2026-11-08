# SPDX-FileCopyrightText: 2026 Total Markdown contributors
# SPDX-License-Identifier: GPL-3.0-or-later

qt_standard_project_setup()

# Internal static library: everything except main(). Tests link against it,
# so one binary under test exercises the real production code.
add_library(mmd_core STATIC
    ../third_party/fonts/fonts.qrc
    app/MainWindow.h
    app/MainWindow.cpp
    app/FindController.h
    app/FindController.cpp
    app/FontsDialog.h
    app/FontsDialog.cpp
    app/OutlinePanel.h
    app/OutlinePanel.cpp
    app/SidebarTitleBar.h
    app/SidebarTitleBar.cpp
    app/FileTreePanel.h
    app/FileTreePanel.cpp
    core/Theme.h
    core/Theme.cpp
    core/ThemeManager.h
    core/ThemeManager.cpp
    core/FontManager.h
    core/FontManager.cpp
    core/FileDocument.h
    core/FileDocument.cpp
    core/HtmlFragment.h
    core/HtmlFragment.cpp
    core/ImageResolver.h
    core/ImageResolver.cpp
    core/LocalImageCache.h
    core/LocalImageCache.cpp
    core/ImageFetcher.h
    core/ImageFetcher.cpp
    core/DocumentLimits.h
    core/DocumentLimits.cpp
    core/FindState.h
    core/FindState.cpp
    core/Outline.h
    core/Outline.cpp
    core/MathRenderer.h
    core/MathRenderer.cpp
    core/SystemThemeProvider.h
    core/SystemThemeProvider.cpp
    core/MarkdownBuilder.h
    core/MarkdownBuilder.cpp
    editor/MarkdownEditor.h
    editor/MarkdownEditor.cpp
    editor/MarkdownHighlighter.h
    editor/MarkdownHighlighter.cpp
    viewer/MarkdownView.h
    viewer/MarkdownView.cpp
    viewer/MathObjectHandler.h
    viewer/MathObjectHandler.cpp
    viewer/FindBar.h
    viewer/FindBar.cpp
    viewer/GifObjectHandler.h
    viewer/GifObjectHandler.cpp
    viewer/CheckboxObjectHandler.h
    viewer/CheckboxObjectHandler.cpp
    viewer/ChipObjectHandler.h
    viewer/ChipObjectHandler.cpp
    viewer/CodeHighlighter.h
    viewer/CodeHighlighter.cpp
)

target_include_directories(mmd_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mmd_core
    PUBLIC
        Qt6::Widgets
        Qt6::Svg
        Qt6::Network
        KF6::SyntaxHighlighting
        microtex
        md4c
        mmd_warnings
)

add_executable(total-markdown
    main.cpp
    ../resources/resources.qrc
)
target_link_libraries(total-markdown PRIVATE mmd_core)

install(TARGETS total-markdown RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Bundle KDE's KSyntaxHighlighting with the application. Some distros do not
# package it (Ubuntu 24.04 LTS, for one), so we ship the shared object and let
# the loader find it next to the executable via RPATH — no distro package
# needed on the target. Native distro packages turn this off; they depend on
# the system KF6 and must not ship a second copy of the engine.
if(MMD_BUNDLE_KSYNTAX)
install(FILES $<TARGET_FILE:KF6::SyntaxHighlighting>
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/total-markdown
    RENAME libKF6SyntaxHighlighting.so.6)
set_target_properties(total-markdown PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/total-markdown")
else()
# qt_standard_project_setup() leaves a default install RUNPATH pointing at a
# lib/ next to the binary; a distro package owns no such directory, so the
# loader must use the system paths alone.
set_target_properties(total-markdown PROPERTIES INSTALL_RPATH "")
endif()
