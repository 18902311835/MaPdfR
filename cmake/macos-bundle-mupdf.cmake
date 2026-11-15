# ============================================================
# 把 MuPDF 动态库放进 MaPdfR.app/Contents/Frameworks。
#
# 由 CMakeLists.txt 的 POST_BUILD 调用（cmake -P），参数：
#   APP_BUNDLE   $<TARGET_BUNDLE_DIR:MaPdfR>
#   MUPDF_DYLIB  tools/build-mupdf-macos.sh 产出的 libmupdf.dylib
#
# 修改 install_name 会破坏链接器写入的 ad-hoc 签名，所以复制后必须
# 重新 codesign（arm64 上未签名的 Mach-O 无法执行）。
# ============================================================

if(NOT EXISTS "${MUPDF_DYLIB}")
    message(FATAL_ERROR "MuPDF dynamic library not found: ${MUPDF_DYLIB}")
endif()

set(_frameworks "${APP_BUNDLE}/Contents/Frameworks")
file(MAKE_DIRECTORY "${_frameworks}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${MUPDF_DYLIB}" "${_frameworks}/libmupdf.dylib"
    RESULT_VARIABLE _copy_result)
if(NOT _copy_result EQUAL 0)
    message(FATAL_ERROR "Failed to copy libmupdf.dylib into ${_frameworks}")
endif()

execute_process(
    COMMAND install_name_tool -id @rpath/libmupdf.dylib
            "${_frameworks}/libmupdf.dylib"
    RESULT_VARIABLE _id_result)
if(NOT _id_result EQUAL 0)
    message(FATAL_ERROR "install_name_tool failed for ${_frameworks}/libmupdf.dylib")
endif()

execute_process(
    COMMAND codesign --force --deep --sign - "${APP_BUNDLE}"
    RESULT_VARIABLE _sign_result)
if(NOT _sign_result EQUAL 0)
    message(WARNING "codesign failed for ${APP_BUNDLE}")
endif()
