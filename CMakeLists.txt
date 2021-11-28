cmake_minimum_required(VERSION 3.21)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 20)
#set(CMAKE_SYSTEM_NAME Generic)
#set(CMAKE_CROSSCOMPILING 1)
#set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(memprog_lib C CXX)

include_directories(.)
include_directories(inc)


SET(COMMON_FLAGS "-Os -Wall -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -Wuninitialized -Wmissing-declarations -Wlogical-op -Wfloat-equal")

SET(CMAKE_C_FLAGS "${COMMON_FLAGS} -MD -MP -MF \"$$(@:%.o=%.d)\" -MT\"$$(@:%.o=%.d)\" -MT\"$$(@:%.o=%.o)\"")
SET(CMAKE_CXX_FLAGS "${COMMON_FLAGS} -Wno-volatile -Wno-register -fno-rtti -fno-exceptions -MD -MP -MF \"$$(@:%.o=%.d)\" -MT\"$$(@:%.o=%.d)\" -MT\"$$(@:%.o=%.o)\"")

SET(CMAKE_EXE_LINKER_FLAGS "-Wl,--gc-sections,-Map,\"${PROJECT_NAME}.map\"")

add_executable(memprog_lib
        inc/memprog.h inc/memprog.hpp test/test.cpp)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND nm -C -l -n -S CMakeFiles/memprog_lib.dir/src/memprog.cpp.obj > ${PROJECT_NAME}.sym)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND nm -C -l --size-sort -S CMakeFiles/memprog_lib.dir/src/memprog.cpp.obj > ${PROJECT_NAME}.symsize)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND objdump -S -C CMakeFiles/memprog_lib.dir/src/memprog.cpp.obj > ${PROJECT_NAME}.lst)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND size CMakeFiles/memprog_lib.dir/src/memprog.cpp.obj | tee ${PROJECT_NAME}.siz)

