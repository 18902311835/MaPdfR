cmake_minimum_required(VERSION 3.16)
project(MaPdfR VERSION 1.0.0 LANGUAGES C CXX)

# ============================================================
# MaPdfR：从 QtNotePad 的 PDF 阅读器提取出的独立 PDF 查看器。
#
# 阅读部分保留 PdfMainWindow 与 SafeFileCopy；另加基于 MuPDF 的
# PDF -> Markdown 导出（src/convert/PdfToMarkdown）。
# 不包含编辑器 / LLM / VNC / Office 转换等桌面模块。
# ============================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(MAPDFR_BUILD_TESTS "Build the Qt smoke tests" ON)
set(_mapdfr_mupdf_default OFF)
if(WIN32 AND MSVC)
    set(_mapdfr_mupdf_default ON)
endif()
option(MAPDFR_WITH_MUPDF "Enable MuPDF-backed printing, review and export" ${_mapdfr_mupdf_default})

# 这些宏只用于本工程自己的源码。MuPDF 的 time.c 依赖 windows.h 带进来的
# winsock.h（timeval），不能被 WIN32_LEAN_AND_MEAN 屏蔽，因此不能做成全局定义。
set(MAPDFR_PLATFORM_DEFINITIONS "")
if(WIN32)
    set(MAPDFR_PLATFORM_DEFINITIONS NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

if(MSVC)
    add_compile_options(/utf-8 /W4)
    # MaPdfR 与所引用的静态 Qt 使用 /MT，生成的 EXE 不依赖 VC 运行时 DLL。
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# === Qt 探测 ===
# 优先使用 -DCMAKE_PREFIX_PATH / 环境变量 QT_ROOT，其次回退到本机静态 Qt。
if(NOT CMAKE_PREFIX_PATH)
    if(WIN32 AND EXISTS "E:/QT/6.11.1/msvc2022_64-static")
        set(CMAKE_PREFIX_PATH "E:/QT/6.11.1/msvc2022_64-static"
            CACHE PATH "Qt installation prefix")
    endif()
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network Pdf PdfWidgets)
if(MAPDFR_BUILD_TESTS)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
endif()
if(MAPDFR_WITH_MUPDF)
    find_package(Qt6 6.8 REQUIRED COMPONENTS PrintSupport)
endif()

get_target_property(_mapdfr_qt_core_type Qt6::Core TYPE)
if(_mapdfr_qt_core_type STREQUAL "STATIC_LIBRARY")
    set(MAPDFR_QT_IS_STATIC TRUE)
    add_compile_definitions(QT_STATICPLUGIN)
    message(STATUS ">>> MaPdfR: static Qt detected")
else()
    set(MAPDFR_QT_IS_STATIC FALSE)
    message(STATUS ">>> MaPdfR: shared Qt")
endif()

# 静态 Qt 的插件不会被自动链接，必须显式列出。
# qpdf 是 Qt PDF 的图像格式插件（QImageReader 读 PDF 页），
# 缺了它 QPdfView 仍可显示，但「另存为图片」一类的路径会失效。
function(mapdfr_link_static_plugins target)
    if(NOT (WIN32 AND MAPDFR_QT_IS_STATIC))
        return()
    endif()

    set(_plugin_dir "${QT6_INSTALL_PREFIX}/plugins")

    foreach(_plugin
        platforms/qwindows
        styles/qmodernwindowsstyle
        imageformats/qico
        imageformats/qjpeg
        imageformats/qgif
        imageformats/qsvg
        imageformats/qtiff
        imageformats/qwebp
        imageformats/qpdf
        iconengines/qsvgicon)
        if(EXISTS "${_plugin_dir}/${_plugin}.lib")
            target_link_libraries(${target} PRIVATE "${_plugin_dir}/${_plugin}.lib")
        else()
            message(STATUS ">>> Optional Qt plugin not found: ${_plugin}.lib")
        endif()
    endforeach()

    target_link_libraries(${target} PRIVATE dwmapi)
endfunction()

# === MuPDF：打印、批注、文本复制与导出 ===
if(MAPDFR_WITH_MUPDF)
    include(cmake/mupdf.cmake)
    mapdfr_add_mupdf()
endif()

# === 阅读器源码（均来自 QtNotePad 2.36.2 的 src/Readers）===
set(MAPDFR_READER_SOURCES
    src/ui/PdfMainWindow.cpp
    src/ui/PdfMainWindow.h
    src/ui/PdfWorkspace.cpp
    src/ui/PdfWorkspace.h
    src/ui/SelectablePdfView.cpp
    src/ui/SelectablePdfView.h
    resources/help.qrc
    src/ui/ReaderNavigation.cpp
    src/ui/ReaderNavigation.h
    src/io/SafeFileCopy.cpp
    src/io/SafeFileCopy.h)

# PDF -> Markdown 转换器（依赖 MuPDF）。
set(MAPDFR_CONVERT_SOURCES
    src/convert/PdfToMarkdown.cpp
    src/convert/PdfToMarkdown.h)

# 文件类型注册、关于窗口：阅读器菜单会调用，冒烟测试也要链接。
set(MAPDFR_SHELL_SOURCES
    src/app/FileAssociation.cpp
    src/app/FileAssociation.h
    src/ui/AboutDialog.cpp
    src/ui/AboutDialog.h)

set(MAPDFR_OPERATIONS_SOURCES
    src/io/PdfDocumentOperations.cpp
    src/io/PdfDocumentOperations.h
    src/ui/PdfDocumentDialogs.cpp
    src/ui/PdfDocumentDialogs.h)

set(MAPDFR_APP_SOURCES
    src/app/main.cpp
    src/app/LocalOpenRelay.cpp
    src/app/LocalOpenRelay.h
    resources/app.qrc
    ${MAPDFR_READER_SOURCES}
    ${MAPDFR_SHELL_SOURCES})
if(MAPDFR_WITH_MUPDF)
    list(APPEND MAPDFR_APP_SOURCES ${MAPDFR_CONVERT_SOURCES} ${MAPDFR_OPERATIONS_SOURCES})
endif()

# === Windows 资源：程序图标与文件属性里的版权 / 版本信息 ===
if(WIN32)
    set(MAPDFR_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
    string(REPLACE "." ";" _mapdfr_version_parts "${PROJECT_VERSION}")
    list(GET _mapdfr_version_parts 0 _mapdfr_version_major)
    list(GET _mapdfr_version_parts 1 _mapdfr_version_minor)
    list(LENGTH _mapdfr_version_parts _mapdfr_version_count)
    if(_mapdfr_version_count GREATER 2)
        list(GET _mapdfr_version_parts 2 _mapdfr_version_patch)
    else()
        set(_mapdfr_version_patch 0)
    endif()
    set(MAPDFR_RC_VERSION
        "${_mapdfr_version_major},${_mapdfr_version_minor},${_mapdfr_version_patch},0")
    configure_file(cmake/mapdfr-app.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/mapdfr-app.rc" @ONLY)
    list(APPEND MAPDFR_APP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/mapdfr-app.rc")
endif()

add_executable(MaPdfR WIN32 ${MAPDFR_APP_SOURCES})

target_include_directories(MaPdfR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io
    ${CMAKE_CURRENT_SOURCE_DIR}/src/convert
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app)

target_compile_definitions(MaPdfR PRIVATE
    ${MAPDFR_PLATFORM_DEFINITIONS}
    MAPDFR_APP_VERSION="${PROJECT_VERSION}")

# 发行目录里放 pdf.ico，注册表的 DefaultIcon 直接引用它。
add_custom_command(TARGET MaPdfR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/pdf.ico"
        "$<TARGET_FILE_DIR:MaPdfR>/pdf.ico"
    COMMENT "Copying pdf.ico next to MaPdfR.exe")

target_link_libraries(MaPdfR PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Pdf
    Qt6::PdfWidgets)

if(MAPDFR_WITH_MUPDF)
    target_compile_definitions(MaPdfR PRIVATE MAPDFR_WITH_MUPDF=1)
    target_link_libraries(MaPdfR PRIVATE Qt6::PrintSupport)
    mapdfr_link_mupdf(MaPdfR)
endif()

mapdfr_link_static_plugins(MaPdfR)

# Qt 自带对话框（取消 / 打开 / 另存为 / 错误提示）的中文翻译。
# 嵌进资源而不是随包放 translations/，静态构建仍是一个自包含的 EXE。
# 不同 Qt 版本的 CMake 变量名不完全一致：优先 QT6_INSTALL_TRANSLATIONS，
# 缺失时按安装前缀推导。
set(_mapdfr_qt_translations "${QT6_INSTALL_TRANSLATIONS}")
if(NOT _mapdfr_qt_translations)
    set(_mapdfr_qt_translations "${QT6_INSTALL_PREFIX}/translations")
elseif(NOT IS_ABSOLUTE "${_mapdfr_qt_translations}")
    # Qt 的 QtInstallPaths.cmake 把它定义为相对安装前缀的 "translations"。
    set(_mapdfr_qt_translations "${QT6_INSTALL_PREFIX}/${_mapdfr_qt_translations}")
endif()
if(EXISTS "${_mapdfr_qt_translations}/qtbase_zh_CN.qm")
    # qt_add_resources 不接受绝对路径，先复制到构建树再按 BASE 计算资源名。
    set(_mapdfr_qm_dir "${CMAKE_CURRENT_BINARY_DIR}/i18n")
    file(COPY "${_mapdfr_qt_translations}/qtbase_zh_CN.qm"
         DESTINATION "${_mapdfr_qm_dir}")
    qt_add_resources(MaPdfR "translations"
        PREFIX "/i18n"
        BASE "${_mapdfr_qm_dir}"
        FILES "${_mapdfr_qm_dir}/qtbase_zh_CN.qm")
    message(STATUS ">>> Embedded Qt Chinese translation: ${_mapdfr_qt_translations}/qtbase_zh_CN.qm")
else()
    message(STATUS ">>> qtbase_zh_CN.qm not found under ${_mapdfr_qt_translations}; Qt standard dialogs stay untranslated")
endif()

# === 冒烟测试 ===
if(MAPDFR_BUILD_TESTS)
    enable_testing()
    add_executable(mapdfr_reader_tests
        tests/reader_smoke.cpp
        ${MAPDFR_READER_SOURCES}
        ${MAPDFR_SHELL_SOURCES})

    target_include_directories(mapdfr_reader_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/io
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app)

    target_compile_definitions(mapdfr_reader_tests PRIVATE
        ${MAPDFR_PLATFORM_DEFINITIONS}
        MAPDFR_APP_VERSION="${PROJECT_VERSION}"
        MAPDFR_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

    target_link_libraries(mapdfr_reader_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::Pdf
        Qt6::PdfWidgets
        Qt6::Test)

    if(MAPDFR_WITH_MUPDF)
        target_sources(mapdfr_reader_tests PRIVATE
            ${MAPDFR_CONVERT_SOURCES}
            ${MAPDFR_OPERATIONS_SOURCES})
        target_include_directories(mapdfr_reader_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/convert")
        target_compile_definitions(mapdfr_reader_tests PRIVATE MAPDFR_WITH_MUPDF=1)
        target_link_libraries(mapdfr_reader_tests PRIVATE Qt6::PrintSupport)
        mapdfr_link_mupdf(mapdfr_reader_tests)
    endif()

    mapdfr_link_static_plugins(mapdfr_reader_tests)

    add_test(NAME MaPdfR.ReaderSmoke COMMAND mapdfr_reader_tests)
    set_tests_properties(MaPdfR.ReaderSmoke PROPERTIES TIMEOUT 60)

    # === PDF -> Markdown 测试（需要 MuPDF）===
    if(MAPDFR_WITH_MUPDF)
        add_executable(mapdfr_markdown_tests
            tests/markdown_export.cpp
            ${MAPDFR_CONVERT_SOURCES})

        target_include_directories(mapdfr_markdown_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/convert)

        target_compile_definitions(mapdfr_markdown_tests PRIVATE
            ${MAPDFR_PLATFORM_DEFINITIONS}
            MAPDFR_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

        # 链接 Qt6::Pdf：coexistsWithQtPdf 用例要验证 MuPDF 与 PDFium
        # 在同一进程里共存（两边都带 lcms2，符号必须隔离）。
        target_link_libraries(mapdfr_markdown_tests PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Pdf
            Qt6::Test)

        mapdfr_link_mupdf(mapdfr_markdown_tests)

        add_test(NAME MaPdfR.MarkdownExport COMMAND mapdfr_markdown_tests)
        set_tests_properties(MaPdfR.MarkdownExport PROPERTIES TIMEOUT 120)
    endif()
endif()

# === 安装（便携式布局：EXE + 许可声明）===
install(TARGETS MaPdfR RUNTIME DESTINATION . BUNDLE DESTINATION .)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/pdf.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/help/user-guide.zh_CN.html"
    DESTINATION .)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/licenses/" DESTINATION licenses)
foreach(_mapdfr_notice README.md THIRD_PARTY.md LICENSE.txt COPYRIGHT.md PROFESSIONAL_REVIEW.md)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_mapdfr_notice}")
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${_mapdfr_notice}" DESTINATION .)
    endif()
endforeach()
