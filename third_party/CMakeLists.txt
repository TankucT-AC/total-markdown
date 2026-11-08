# SPDX-FileCopyrightText: 2026 Total Markdown contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Vendored third-party code. Every directory keeps the upstream license file
# verbatim; do not strip license headers when updating these sources. See the
# UPSTREAM files for the exact commits each directory was taken from.

# md4c: Markdown parser (MIT). Upstream: https://github.com/mity/md4c
add_library(md4c STATIC
    md4c/src/md4c.c
    md4c/src/entity.c
)
target_include_directories(md4c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/md4c/src)

# tinyxml2: XML parser (zlib). MicroTeX's resource parsers read their XML with
# it, so it cannot be left out even though this project never calls it.
add_library(tinyxml2 STATIC
    tinyxml2/tinyxml2.cpp
)
target_include_directories(tinyxml2 SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tinyxml2)

# MicroTeX (cLaTeXMath): LaTeX rendering (MIT). Compiled without this project's
# warning set -- it is upstream code, judged by upstream's standards -- and its
# headers are SYSTEM ones, so nothing from it leaks warnings into our builds.
# BUILD_QT selects the QPainter backend; the TeX resources are read from res/,
# which is installed next to the application (fonts are not embedded).
add_library(microtex STATIC
    microtex/src/platform/qt/graphic_qt.cpp
    microtex/src/atom/atom_basic.cpp
    microtex/src/atom/atom_char.cpp
    microtex/src/atom/atom_impl.cpp
    microtex/src/atom/atom_matrix.cpp
    microtex/src/atom/atom_row.cpp
    microtex/src/atom/atom_space.cpp
    microtex/src/atom/colors_def.cpp
    microtex/src/atom/unit_conversion.cpp
    microtex/src/box/box.cpp
    microtex/src/box/box_factory.cpp
    microtex/src/box/box_group.cpp
    microtex/src/box/box_single.cpp
    microtex/src/core/core.cpp
    microtex/src/core/formula.cpp
    microtex/src/core/formula_def.cpp
    microtex/src/core/glue.cpp
    microtex/src/core/localized_num.cpp
    microtex/src/core/macro.cpp
    microtex/src/core/macro_def.cpp
    microtex/src/core/macro_impl.cpp
    microtex/src/core/parser.cpp
    microtex/src/fonts/alphabet.cpp
    microtex/src/fonts/font_basic.cpp
    microtex/src/fonts/font_info.cpp
    microtex/src/fonts/fonts.cpp
    microtex/src/latex.cpp
    microtex/src/render.cpp
    microtex/src/res/builtin/formula_mappings.res.cpp
    microtex/src/res/builtin/symbol_mapping.res.cpp
    microtex/src/res/builtin/tex_param.res.cpp
    microtex/src/res/builtin/tex_symbols.res.cpp
    microtex/src/res/font/bi10.def.cpp
    microtex/src/res/font/bx10.def.cpp
    microtex/src/res/font/cmbsy10.def.cpp
    microtex/src/res/font/cmbx10.def.cpp
    microtex/src/res/font/cmbxti10.def.cpp
    microtex/src/res/font/cmex10.def.cpp
    microtex/src/res/font/cmmi10.def.cpp
    microtex/src/res/font/cmmi10_unchanged.def.cpp
    microtex/src/res/font/cmmib10.def.cpp
    microtex/src/res/font/cmmib10_unchanged.def.cpp
    microtex/src/res/font/cmr10.def.cpp
    microtex/src/res/font/cmss10.def.cpp
    microtex/src/res/font/cmssbx10.def.cpp
    microtex/src/res/font/cmssi10.def.cpp
    microtex/src/res/font/cmsy10.def.cpp
    microtex/src/res/font/cmti10.def.cpp
    microtex/src/res/font/cmti10_unchanged.def.cpp
    microtex/src/res/font/cmtt10.def.cpp
    microtex/src/res/font/dsrom10.def.cpp
    microtex/src/res/font/eufb10.def.cpp
    microtex/src/res/font/eufm10.def.cpp
    microtex/src/res/font/i10.def.cpp
    microtex/src/res/font/moustache.def.cpp
    microtex/src/res/font/msam10.def.cpp
    microtex/src/res/font/msbm10.def.cpp
    microtex/src/res/font/r10.def.cpp
    microtex/src/res/font/r10_unchanged.def.cpp
    microtex/src/res/font/rsfs10.def.cpp
    microtex/src/res/font/sb10.def.cpp
    microtex/src/res/font/sbi10.def.cpp
    microtex/src/res/font/si10.def.cpp
    microtex/src/res/font/special.def.cpp
    microtex/src/res/font/ss10.def.cpp
    microtex/src/res/font/stmary10.def.cpp
    microtex/src/res/font/tt10.def.cpp
    microtex/src/res/parser/font_parser.cpp
    microtex/src/res/parser/formula_parser.cpp
    microtex/src/res/reg/builtin_font_reg.cpp
    microtex/src/res/reg/builtin_syms_reg.cpp
    microtex/src/res/sym/amsfonts.def.cpp
    microtex/src/res/sym/amssymb.def.cpp
    microtex/src/res/sym/base.def.cpp
    microtex/src/res/sym/stmaryrd.def.cpp
    microtex/src/res/sym/symspecial.def.cpp
    microtex/src/utils/string_utils.cpp
    microtex/src/utils/utf.cpp
    microtex/src/utils/utils.cpp
)
target_compile_definitions(microtex PUBLIC BUILD_QT)
target_include_directories(microtex SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/microtex/src)
target_link_libraries(microtex PUBLIC Qt6::Gui tinyxml2)
# Upstream code, upstream standards: the warning set this project holds its own
# sources to does not apply to the vendored tree, so its chatter stays out of
# our builds entirely.
target_compile_options(microtex PRIVATE -w)

# MicroTeX loads the glyphs from res/fonts at run time (its metrics are compiled
# in, the outlines are not), and MathRenderer looks for the directory relative
# to the executable. Linking it into the build tree keeps that lookup valid
# without compiling an absolute path into the binary.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/microtex)
if(WIN32)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/microtex/res
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/microtex)
else()
    file(CREATE_LINK ${CMAKE_CURRENT_SOURCE_DIR}/microtex/res
        ${CMAKE_CURRENT_BINARY_DIR}/microtex/res SYMBOLIC)
endif()
