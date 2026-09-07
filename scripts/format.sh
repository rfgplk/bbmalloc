#!/bin/sh
find src tests benches examples -type f \( -iname '*.hpp' -o -iname '*.cpp' \) -print0 | xargs -0 clang-format -i
