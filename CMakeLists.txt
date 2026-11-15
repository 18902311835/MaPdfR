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
    if(APPLE)
        if(MAPDFR_QT_IS_STATIC)
            # macOS 静态 Qt：qcocoa 是唯一的窗口平台插件；offscreen 让
            # ctest 在没有窗口会话（ssh）时也能跑。图像格式插件与
            # Windows 列表对应。
            qt_import_plugins(${target}
                INCLUDE
                    Qt6::QCocoaIntegrationPlugin
                    Qt6::QOffscreenIntegrationPlugin
                    Qt6::QICOPlugin
                    Qt6::QICNSPlugin
                    Qt6::QJpegPlugin
                    Qt6::QGifPlugin
                    Qt6::QSvgPlugin
                    Qt6::QSvgIconPlugin
                    Qt6::QPdfPlugin)
        endif()
        return()
    endif()

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

if(APPLE)
    # macOS 应用包：Info.plist 声明 PDF 文档类型，Finder 里 .pdf 的
    # 「打开方式」才会列出 MaPdfR；图标用 iconutil 生成的 .icns。
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/mapdfr.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND MAPDFR_APP_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/mapdfr.icns")

    add_executable(MaPdfR MACOSX_BUNDLE ${MAPDFR_APP_SOURCES})

    set_target_properties(MaPdfR PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "MaPdfR"
        MACOSX_BUNDLE_GUI_IDENTIFIER "cn.madeyou.mapdfr"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "mapdfr.icns"
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mapdfr-info.plist.in"
        # MuPDF 的 dylib 随包放在 Contents/Frameworks。
        BUILD_RPATH "${MAPDFR_MUPDF_BUILD_DIR}"
        # 让构建树与安装后的 .app 都能找到 Contents/Frameworks 下的副本。
        # 用链接选项而不是 INSTALL_RPATH，避免安装时重复添加同一条 rpath。
        LINK_OPTIONS "-Wl,-rpath,@executable_path/../Frameworks")
else()
add_executable(MaPdfR WIN32 ${MAPDFR_APP_SOURCES})
endif()

# Linux：安装后的二进制叫 mapdfr（与 .desktop 的 Exec 一致），
# MuPDF 共享库放在 /usr/lib/mapdfr，运行时通过相对 RPATH 找到。
if(UNIX AND NOT APPLE)
    set_target_properties(MaPdfR PROPERTIES
        OUTPUT_NAME mapdfr
        INSTALL_RPATH "$ORIGIN/../lib/mapdfr")
endif()

target_include_directories(MaPdfR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io
    ${CMAKE_CURRENT_SOURCE_DIR}/src/convert
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app)

target_compile_definitions(MaPdfR PRIVATE
    ${MAPDFR_PLATFORM_DEFINITIONS}
    MAPDFR_APP_VERSION="${PROJECT_VERSION}")

# 发行目录里放 pdf.ico，注册表的 DefaultIcon 直接引用它。
if(WIN32)
    add_custom_command(TARGET MaPdfR POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/pdf.ico"
            "$<TARGET_FILE_DIR:MaPdfR>/pdf.ico"
        COMMENT "Copying pdf.ico next to MaPdfR.exe")
endif()

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

# macOS：PDF 关联走 LaunchServices（CoreServices 框架）。
if(APPLE)
    target_link_libraries(MaPdfR PRIVATE "-framework CoreServices")
endif()

mapdfr_link_static_plugins(MaPdfR)

# macOS：把 libmupdf.dylib 拷进 MaPdfR.app/Contents/Frameworks 并重新签名。
if(APPLE AND MAPDFR_WITH_MUPDF AND EXISTS "${MAPDFR_MUPDF_BUILD_DIR}/libmupdf.dylib")
    add_custom_command(TARGET MaPdfR POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DAPP_BUNDLE=$<TARGET_BUNDLE_DIR:MaPdfR>"
            "-DMUPDF_DYLIB=${MAPDFR_MUPDF_BUILD_DIR}/libmupdf.dylib"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos-bundle-mupdf.cmake"
        COMMENT "Bundling libmupdf.dylib into MaPdfR.app")
endif()

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
set(_mapdfr_qt_qm "${_mapdfr_qt_translations}/qtbase_zh_CN.qm")
if(NOT EXISTS "${_mapdfr_qt_qm}")
    # 自建的静态 Qt（如 macOS 上的 macos-static）常常没装 translations，
    # 仓库里带一份同版本 qtbase_zh_CN.qm 作为回退。
    set(_mapdfr_qt_qm
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/qtbase_zh_CN.qm")
endif()
if(EXISTS "${_mapdfr_qt_qm}")
    # qt_add_resources 不接受绝对路径，先复制到构建树再按 BASE 计算资源名。
    set(_mapdfr_qm_dir "${CMAKE_CURRENT_BINARY_DIR}/i18n")
    file(COPY "${_mapdfr_qt_qm}"
         DESTINATION "${_mapdfr_qm_dir}")
    qt_add_resources(MaPdfR "translations"
        PREFIX "/i18n"
        BASE "${_mapdfr_qm_dir}"
        FILES "${_mapdfr_qm_dir}/qtbase_zh_CN.qm")
    message(STATUS ">>> Embedded Qt Chinese translation: ${_mapdfr_qt_qm}")
else()
    message(STATUS ">>> qtbase_zh_CN.qm not found; Qt standard dialogs stay untranslated")
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

    if(APPLE)
        target_link_libraries(mapdfr_reader_tests PRIVATE "-framework CoreServices")
        set_target_properties(mapdfr_reader_tests PROPERTIES
            BUILD_RPATH "${MAPDFR_MUPDF_BUILD_DIR}")
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
        if(APPLE)
            # 静态 Qt 的 QGuiApplication 需要平台插件；ctest 在 ssh 会话里用
            # offscreen，所以这里和阅读器测试一样链入插件集合。
            mapdfr_link_static_plugins(mapdfr_markdown_tests)
        endif()

        if(APPLE)
            set_target_properties(mapdfr_markdown_tests PROPERTIES
                BUILD_RPATH "${MAPDFR_MUPDF_BUILD_DIR}")
        endif()

        add_test(NAME MaPdfR.MarkdownExport COMMAND mapdfr_markdown_tests)
        set_tests_properties(MaPdfR.MarkdownExport PROPERTIES TIMEOUT 120)
    endif()
endif()

# === 安装 ===
if(UNIX AND NOT APPLE)
    # Linux / 麒麟：FHS 布局，供 CPack DEB 打包。
    install(TARGETS MaPdfR RUNTIME DESTINATION bin)

    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/linux/mapdfr.desktop"
        DESTINATION share/applications)

    foreach(_mapdfr_icon_size 16 24 32 48 64 128 256)
        install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/linux/icons/mapdfr-${_mapdfr_icon_size}.png"
            DESTINATION "share/icons/hicolor/${_mapdfr_icon_size}x${_mapdfr_icon_size}/apps"
            RENAME mapdfr.png)
    endforeach()

    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/help/user-guide.zh_CN.html"
        DESTINATION share/doc/mapdfr)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/licenses/"
        DESTINATION share/doc/mapdfr/licenses)
    foreach(_mapdfr_notice README.md THIRD_PARTY.md LICENSE.txt COPYRIGHT.md PROFESSIONAL_REVIEW.md)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_mapdfr_notice}")
            install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${_mapdfr_notice}"
                DESTINATION share/doc/mapdfr)
        endif()
    endforeach()

    # MuPDF 共享库：与二进制一起分发，RPATH 指向 lib/mapdfr。
    if(MAPDFR_WITH_MUPDF)
        file(GLOB _mapdfr_mupdf_runtime
            "${MAPDFR_MUPDF_BUILD_DIR}/libmupdf.so.[0-9]*")
        list(FILTER _mapdfr_mupdf_runtime EXCLUDE REGEX "\\.in$")
        install(FILES ${_mapdfr_mupdf_runtime} DESTINATION lib/mapdfr)
    endif()

    # === DEB 打包 ===
    # cmake --build … 后执行：cpack --config build/<dir>/CPackConfig.cmake -G DEB
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "mapdfr")
    set(CPACK_PACKAGE_VENDOR "MaPdfR")
    set(CPACK_PACKAGE_CONTACT
        "MaPdfR <18902311835@users.noreply.github.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "MaPdfR PDF 阅读器（Qt 6 + MuPDF，静态链接）")
    set(CPACK_PACKAGE_DESCRIPTION
        "MaPdfR 是独立的 PDF 阅读器，支持多标签阅读、查找、打印、便笺、文本复制和 PDF 到 Markdown 导出。程序静态链接 Qt 6 与 MuPDF，除系统基础库外不依赖额外的运行时。")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_PACKAGE_CHECKSUM "SHA256")

    set(CPACK_DEBIAN_PACKAGE_NAME "mapdfr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER
        "MaPdfR <18902311835@users.noreply.github.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    # 用 dpkg-shlibdeps 计算系统库依赖（X11 / glib / fontconfig 等）。
    # 静态 Qt 与 MuPDF 自身的库不产生外部依赖。
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "xdg-utils")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/deb/postinst;${CMAKE_CURRENT_SOURCE_DIR}/tools/deb/postrm")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

    include(CPack)
else()
    # Windows 便携式布局：EXE + 许可声明。
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

    # macOS：MuPDF 动态库进应用包的 Contents/Frameworks；说明书与许可
    # 放在 .app 外（codesign 后修改包内容会破坏签名）。
    if(APPLE AND MAPDFR_WITH_MUPDF)
        install(FILES "${MAPDFR_MUPDF_BUILD_DIR}/libmupdf.dylib"
            DESTINATION "MaPdfR.app/Contents/Frameworks")
    endif()
endif()
