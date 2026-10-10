# Sheep Shears guest tool and self-tests. Built by tools/shears/build.sh --apps-only with the Retro68 PowerPC toolchain file:
#   cmake -S guest/sheepshears -B build/sheepshears -DCMAKE_TOOLCHAIN_FILE=$RETRO68_PREFIX/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
cmake_minimum_required(VERSION 3.10)
project(SheepShears C CXX)    # Retro68 links through the C++ driver, so CXX must be enabled
add_application(SheepShears sheepshears.c sheepshears.r CREATOR "ShSh")
add_application(SheepShearsPanel panel.c panel.r CREATOR "ShSp")
add_application(SheepShearsInstaller installer.c installer.r CREATOR "ShSi")
add_application(ShearsScrapTest scraptest.c scraptest.r CREATOR "ShSt")
add_application(MicCheck miccheck.c miccheck.r CREATOR "ShMc")
add_application(SndChan sndchan.c sndchan.r CREATOR "ShSc")
# The record window: SimpleSound's sequence (probe the device, ask for 'best' quality) and five seconds on screen.
add_application(RecDlg recdlg.c recdlg.r CREATOR "ShRd")
target_compile_definitions(RecDlg PRIVATE "RD_QUALITY='best'" RD_PROBE RD_SECONDS=5)
