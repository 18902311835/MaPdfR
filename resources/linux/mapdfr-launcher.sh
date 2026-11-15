#!/bin/sh
# MaPdfR 便携版启动器：直接运行解压目录里的 ./mapdfr。
HERE=$(dirname "$(readlink -f "$0")")
exec "$HERE/bin/mapdfr" "$@"
