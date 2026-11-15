# ============================================================
# MuPDF 1.27.2 静态库集成（仅 Windows + MSVC）。
#
# 只启用 PDF 文档代理：
#   - 关闭 HTML / XPS / SVG / CBZ / Reflow、JavaScript、OCR、
#     DOCX/ODT 输出、条码，避免引入 harfbuzz / mujs / libarchive /
#     tesseract / zxing 等额外第三方库；
#   - 资源只嵌入 Base14 字体（resources/fonts/urw/*.cff），用于没有
#     内嵌字体（标准 14 字体）的 PDF 的文字抽取。
#
# 源文件清单直接取自 MuPDF 自带的 MSVC 工程（platform/win32/*.vcxproj），
# 升级 MuPDF 版本时不需要手工维护。
# ============================================================

set(MUPDF_SOURCE_DIR "E:/third_party/mupdf-1.27.2-source"
    CACHE PATH "MuPDF source tree (1.27.x)")

# 从 vcxproj 中提取 ClCompile 清单。ExcludedFromBuild 的条目
# （MSVC 工程里 mujs 被排除）会被跳过。
function(mapdfr_mupdf_collect_sources vcxproj out_var)
    if(NOT EXISTS "${vcxproj}")
        message(FATAL_ERROR "MuPDF project file not found: ${vcxproj}")
    endif()

    file(READ "${vcxproj}" _xml)

    string(REGEX REPLACE
        "<ClCompile Include=\"[^\"]+\"[ \t\r\n]*>[ \t\r\n]*<ExcludedFromBuild>true</ExcludedFromBuild>[ \t\r\n]*</ClCompile>"
        "" _xml "${_xml}")

    string(REGEX MATCHALL "<ClCompile Include=\"[^\"]+\"" _items "${_xml}")

    get_filename_component(_base "${vcxproj}" DIRECTORY)

    set(_files "")
    foreach(_item IN LISTS _items)
        string(REGEX REPLACE "^<ClCompile Include=\"([^\"]+)\"" "\\1" _rel "${_item}")
        string(REPLACE "\\" "/" _rel "${_rel}")
        get_filename_component(_abs "${_base}/${_rel}" ABSOLUTE)
        list(APPEND _files "${_abs}")
    endforeach()

    set(${out_var} "${_files}" PARENT_SCOPE)
endfunction()

# ---- Linux：使用 MuPDF 自带 Makefile 构建出的共享库 ----
# 字体嵌入由 MuPDF 的 Makefile 通过 HAVE_OBJCOPY 完成（符号名
# _binary_resources_fonts_*），与 Windows 的 bin2coff 方案不同，不能复用。
#
# 用共享库而不是静态库，是因为静态链接时 MuPDF 自带的 libjpeg / zlib /
# libpng 等会与 Qt6Pdf 里 PDFium 的副本重复定义（jpeg_natural_order 等），
# 链接器直接报 multiple definition。libmupdf.so 用 -Bsymbolic 构建，内部
# 引用绑定到自己的副本，不会与 PDFium 互相抢占。
if(UNIX AND NOT APPLE)
    set(MAPDFR_MUPDF_BUILD_DIR "${MUPDF_SOURCE_DIR}/build/release"
        CACHE PATH "Directory containing libmupdf.so produced by MuPDF's Makefile")

    function(mapdfr_add_mupdf)
        if(NOT EXISTS "${MAPDFR_MUPDF_BUILD_DIR}/libmupdf.so")
            message(FATAL_ERROR
                "MuPDF shared library not found: ${MAPDFR_MUPDF_BUILD_DIR}/libmupdf.so. "
                "Build it with tools/build-mupdf-linux.sh or set MAPDFR_MUPDF_BUILD_DIR.")
        endif()

        add_library(mapdfr_mupdf_external SHARED IMPORTED GLOBAL)
        set_target_properties(mapdfr_mupdf_external PROPERTIES
            IMPORTED_LOCATION "${MAPDFR_MUPDF_BUILD_DIR}/libmupdf.so"
            INTERFACE_INCLUDE_DIRECTORIES "${MUPDF_SOURCE_DIR}/include")
    endfunction()

    function(mapdfr_link_mupdf target)
        target_link_libraries(${target} PRIVATE mapdfr_mupdf_external m pthread dl)
    endfunction()
    return()
endif()

# ---- macOS：MuPDF 自带 Makefile 构建的 libmupdf.dylib ----
# 与 Linux 同理用共享库隔离 MuPDF 自带的 libjpeg/zlib/libpng 和 Qt6Pdf
# 内 PDFium 的副本。dylib 的 install_name 由 tools/build-mupdf-macos.sh
# 改成 @rpath/libmupdf.dylib，应用包把它放进 Contents/Frameworks。
if(APPLE)
    set(MAPDFR_MUPDF_BUILD_DIR "${MUPDF_SOURCE_DIR}/build/shared-release"
        CACHE PATH "Directory containing libmupdf.dylib produced by MuPDF's Makefile")

    function(mapdfr_add_mupdf)
        if(NOT EXISTS "${MAPDFR_MUPDF_BUILD_DIR}/libmupdf.dylib")
            message(FATAL_ERROR
                "MuPDF dynamic library not found: ${MAPDFR_MUPDF_BUILD_DIR}/libmupdf.dylib. "
                "Build it with tools/build-mupdf-macos.sh or set MAPDFR_MUPDF_BUILD_DIR.")
        endif()

        add_library(mapdfr_mupdf_external SHARED IMPORTED GLOBAL)
        set_target_properties(mapdfr_mupdf_external PROPERTIES
            IMPORTED_LOCATION "${MAPDFR_MUPDF_BUILD_DIR}/libmupdf.dylib"
            INTERFACE_INCLUDE_DIRECTORIES "${MUPDF_SOURCE_DIR}/include")
    endfunction()

    function(mapdfr_link_mupdf target)
        target_link_libraries(${target} PRIVATE mapdfr_mupdf_external)
    endfunction()
    return()
endif()

if(NOT (WIN32 AND MSVC))
    set(MAPDFR_MUPDF_LIBRARY "" CACHE FILEPATH "Path to a prebuilt MuPDF 1.27.2 shared library")

    function(mapdfr_add_mupdf)
        if(NOT EXISTS "${MAPDFR_MUPDF_LIBRARY}" OR
           NOT EXISTS "${MUPDF_SOURCE_DIR}/include/mupdf/fitz.h")
            message(FATAL_ERROR
                "Set MAPDFR_MUPDF_LIBRARY and MUPDF_SOURCE_DIR to matching MuPDF 1.27.2 binaries and headers, or disable MAPDFR_WITH_MUPDF.")
        endif()
        add_library(mapdfr_mupdf_external SHARED IMPORTED GLOBAL)
        set_target_properties(mapdfr_mupdf_external PROPERTIES
            IMPORTED_LOCATION "${MAPDFR_MUPDF_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MUPDF_SOURCE_DIR}/include")
    endfunction()

    function(mapdfr_link_mupdf target)
        target_link_libraries(${target} PRIVATE mapdfr_mupdf_external)
    endfunction()
    return()
endif()

function(mapdfr_add_mupdf)

    set(_win32 "${MUPDF_SOURCE_DIR}/platform/win32")

    mapdfr_mupdf_collect_sources(
        "${_win32}/libthirdparty.vcxproj" _thirdparty)
    mapdfr_mupdf_collect_sources(
        "${_win32}/libmupdf.vcxproj" _mupdf)

    # 只保留 PDF 需要的部分。
    list(FILTER _mupdf EXCLUDE REGEX "/source/(html|xps|svg|cbz|reflow)/")
    list(FILTER _thirdparty EXCLUDE REGEX "/thirdparty/mujs/")
    list(FILTER _thirdparty EXCLUDE REGEX "/thirdparty/gumbo-parser/")
    # lcms2 单独编成 mupdf_lcms（符号加前缀），见下面的「lcms2 符号隔离」。
    list(FILTER _thirdparty EXCLUDE REGEX "/thirdparty/lcms2/")

    if(NOT _mupdf OR NOT _thirdparty)
        message(FATAL_ERROR "Failed to parse MuPDF source lists")
    endif()

    set(_tp "${MUPDF_SOURCE_DIR}/thirdparty")
    set(_scripts "${MUPDF_SOURCE_DIR}/scripts")

    set(_common_includes
        "${MUPDF_SOURCE_DIR}/include"
        "${_scripts}/freetype"
        "${_scripts}/libjpeg"
        "${_tp}/brotli/c/include"
        "${_tp}/jbig2dec"
        "${_tp}/libjpeg"
        "${_tp}/openjpeg/src/lib/openjp2"
        "${_tp}/zlib"
        "${_tp}/freetype/include"
        "${_tp}/freetype/include/freetype"
        "${_tp}/lcms2/include")

    # 关闭不需要的文档代理和功能；PDF 保持默认启用。
    set(_feature_defines
        WIN64
        OPJ_STATIC
        HAVE_LCMS2MT=1
        FZ_ENABLE_JS=0
        FZ_ENABLE_XPS=0
        FZ_ENABLE_SVG=0
        FZ_ENABLE_CBZ=0
        FZ_ENABLE_IMG=0
        FZ_ENABLE_HTML=0
        FZ_ENABLE_EPUB=0
        FZ_ENABLE_FB2=0
        FZ_ENABLE_MOBI=0
        FZ_ENABLE_TXT=0
        FZ_ENABLE_OFFICE=0
        FZ_ENABLE_BARCODE=0
        FZ_ENABLE_OCR_OUTPUT=0
        FZ_ENABLE_DOCX_OUTPUT=0
        FZ_ENABLE_ODT_OUTPUT=0
        TOFU
        TOFU_CJK)

    add_library(mupdf_thirdparty STATIC ${_thirdparty})
    target_include_directories(mupdf_thirdparty PRIVATE ${_common_includes})
    target_compile_definitions(mupdf_thirdparty PRIVATE
        FT2_BUILD_LIBRARY
        USE_JPIP=1
        "FT_CONFIG_MODULES_H=\"slimftmodules.h\""
        "FT_CONFIG_OPTIONS_H=\"slimftoptions.h\""
        verbose=-1)
    target_compile_options(mupdf_thirdparty PRIVATE /W0 /bigobj)

    # ---- lcms2 符号隔离 ----
    # Qt6Pdf 里静态链接了 PDFium，而 PDFium 自带一份 lcms2，与 MuPDF 的
    # thirdparty/lcms2 重名。链接器会把 MuPDF 对 cms* 的调用绑到 PDFium
    # 那一份上，两者实现并不兼容，结果就是 GUI 进程里 fz_new_context()
    # 直接崩溃（纯转换器测试不链接 Qt6Pdf，所以测不出来）。
    #
    # 解决办法：把 MuPDF 这一份 lcms2 拿出来单独编译，用前缀头（/FI）把
    # 它所有的全局符号改名为 mpdf_<原名>；mupdf_core 里的调用点用同一个
    # 头文件，因此也一起改名。符号清单见 cmake/lcms2-symbols.txt
    # （由 dumpbin /symbols 列出 mupdf_lcms.lib 的全局符号得到；升级 MuPDF
    # 后按同样方法重列，漏掉的符号会在链接时报 LNK2005 重定义）。
    set(_lcms_prefix_header
        "${CMAKE_CURRENT_BINARY_DIR}/mapdfr-lcms/lcms_prefix.h")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/lcms2-symbols.txt"
        _lcms_symbols)
    set(_lcms_prefix_content
        "/* 由 cmake/mupdf.cmake 生成：给 MuPDF 自带的 lcms2 符号加前缀。 */\n#pragma once\n")
    foreach(_symbol IN LISTS _lcms_symbols)
        string(APPEND _lcms_prefix_content
            "#define ${_symbol} mpdf_${_symbol}\n")
    endforeach()
    file(WRITE "${_lcms_prefix_header}" "${_lcms_prefix_content}")

    file(GLOB _lcms_sources "${_tp}/lcms2/src/*.c")
    add_library(mupdf_lcms STATIC ${_lcms_sources})
    target_include_directories(mupdf_lcms PRIVATE ${_common_includes})
    target_compile_options(mupdf_lcms PRIVATE /W0
        "/FI${_lcms_prefix_header}")

    add_library(mupdf_core STATIC ${_mupdf})
    target_include_directories(mupdf_core PRIVATE ${_common_includes})
    target_compile_definitions(mupdf_core PRIVATE ${_feature_defines})
    target_compile_options(mupdf_core PRIVATE /W0 /bigobj
        "/FI${_lcms_prefix_header}")
    add_dependencies(mupdf_core mupdf_thirdparty)

    # ---- 内嵌字体资源 ----
    # bin2coff 把二进制文件转成 COFF .obj，导出
    # _binary_<name> / _binary_<name>_size 两个符号，
    # 这正是 source/fitz/noto.c 在非 objcopy 构建下引用的命名。
    add_executable(mupdf_bin2coff "${_scripts}/bin2coff.c")
    target_compile_options(mupdf_bin2coff PRIVATE /W0)
    set_target_properties(mupdf_bin2coff PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mupdf-tools")

    file(GLOB _base14_fonts
        "${MUPDF_SOURCE_DIR}/resources/fonts/urw/*.cff")

    set(_resource_objects "")
    foreach(_cff IN LISTS _base14_fonts)
        get_filename_component(_name "${_cff}" NAME_WE)
        string(REPLACE "-" "_" _symbol "${_name}")
        set(_obj "${CMAKE_CURRENT_BINARY_DIR}/mupdf-res/${_symbol}_cff.obj")
        add_custom_command(
            OUTPUT "${_obj}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/mupdf-res"
            COMMAND $<TARGET_FILE:mupdf_bin2coff>
                    "${_cff}" "${_obj}" "_binary_${_symbol}_cff" x64
            DEPENDS "${_cff}" mupdf_bin2coff
            COMMENT "Embedding MuPDF font ${_name}.cff"
            VERBATIM)
        list(APPEND _resource_objects "${_obj}")
    endforeach()

    if(NOT _resource_objects)
        message(FATAL_ERROR "Base14 fonts not found under ${MUPDF_SOURCE_DIR}/resources/fonts/urw")
    endif()

    set_source_files_properties(${_resource_objects} PROPERTIES
        EXTERNAL_OBJECT TRUE
        GENERATED TRUE)

    add_library(mupdf_resources STATIC ${_resource_objects})
    set_target_properties(mupdf_resources PROPERTIES LINKER_LANGUAGE C)

    list(LENGTH _resource_objects _font_count)
    message(STATUS ">>> MaPdfR: MuPDF from ${MUPDF_SOURCE_DIR} (PDF only, ${_font_count} Base14 fonts)")

    # 供 MaPdfR / 测试目标调用：头文件 + 库 + 系统库。
    function(mapdfr_link_mupdf target)
        target_include_directories(${target} PRIVATE "${MUPDF_SOURCE_DIR}/include")
        target_link_libraries(${target} PRIVATE
            mupdf_core
            mupdf_lcms
            mupdf_thirdparty
            mupdf_resources
            ws2_32)
    endfunction()
endfunction()
