# SheepBench, a benchmark app for Mac OS 9 in SheepShaver. Built by tools/sheepbench/build.sh with the Retro68
# PowerPC toolchain file:
#   cmake -S guest/sheepbench -B build/sheepbench -DCMAKE_TOOLCHAIN_FILE=$RETRO68_PREFIX/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
cmake_minimum_required(VERSION 3.10)
project(SheepBench C CXX)    # Retro68 links through the C++ driver, so CXX must be enabled

# The checksums every kernel case must produce, computed on the build machine (kernels/gen_expected.c).
set(HOST_CC "/usr/bin/cc" CACHE FILEPATH "the build machine's C compiler (for gen_expected)")
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sb_expected.h
    COMMAND ${HOST_CC} -O2 -o ${CMAKE_CURRENT_BINARY_DIR}/gen_expected
            ${CMAKE_CURRENT_SOURCE_DIR}/kernels/gen_expected.c ${CMAKE_CURRENT_SOURCE_DIR}/kernels/sb_kernels.c
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/gen_expected > ${CMAKE_CURRENT_BINARY_DIR}/sb_expected.h
    DEPENDS kernels/gen_expected.c kernels/sb_kernels.c kernels/sb_kernels.h kernels/sb_cases.h
    VERBATIM)

# The 68k kernels: assembled with Retro68's 68k assembler (next to the PowerPC compiler) into a byte array.
get_filename_component(TOOLBIN "${CMAKE_C_COMPILER}" DIRECTORY)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/k68_code.h
    COMMAND ${TOOLBIN}/m68k-apple-macos-as -m68020 -o ${CMAKE_CURRENT_BINARY_DIR}/kernels68k.o ${CMAKE_CURRENT_SOURCE_DIR}/kernels68k.s
    COMMAND ${TOOLBIN}/m68k-apple-macos-objcopy -O binary -j .text ${CMAKE_CURRENT_BINARY_DIR}/kernels68k.o ${CMAKE_CURRENT_BINARY_DIR}/k68_code.bin
    COMMAND cd ${CMAKE_CURRENT_BINARY_DIR} && xxd -i -n k68_code k68_code.bin > k68_code.h
    DEPENDS kernels68k.s
    VERBATIM)

add_application(SheepBench
    main.c bench_host.c bench_time.c bench_report.c textwin.c
    suite_common.c suite_cpu.c suite_mem.c suite_68k.c suite_mmu.c suite_qd.c suite_3d.c suite_qd3d.c suite_disk.c
    sb_altivec.c
    kernels/sb_kernels.c
    ${CMAKE_CURRENT_BINARY_DIR}/sb_expected.h ${CMAKE_CURRENT_BINARY_DIR}/k68_code.h
    sheepbench.r
    CREATOR "ShBn")
target_include_directories(SheepBench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(SheepBench PRIVATE -O2 -Wall -Wno-multichar -Wno-unused-function -Wno-pointer-sign)
set_source_files_properties(sb_altivec.c PROPERTIES COMPILE_FLAGS "-maltivec -Wa,-maltivec")
