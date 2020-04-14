project(libunwind)

cmake_minimum_required(VERSION 2.6)

#set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} -Werror -Wno-#pragma-messages -Wno-unused-parameter -Wno-maybe-uninitialized)	
#set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -Wno-old-style-cast)

add_definitions(-DHAVE_CONFIG_H -D_GNU_SOURCE)

if(CMAKE_BUILD_TYPE EQUAL "RELEASE")
	add_definitions(-DNDEBUG)
endif()

set(SOURCE_ROOT
    ${CMAKE_SOURCE_DIR}/../libunwind
)

set(libunwind_includes
    ${SOURCE_ROOT}/src
    ${SOURCE_ROOT}/include
)

if(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(libunwind_includes ${libunwind_includes} ${SOURCE_ROOT}/include/tdep-arm)
elseif(ANDROID_ABI STREQUAL "arm64-v8a")
    set(libunwind_includes ${libunwind_includes} ${SOURCE_ROOT}/include/tdep-aarch64)
elseif(ANDROID_ABI STREQUAL "x86")
    set(libunwind_includes ${libunwind_includes} ${SOURCE_ROOT}/include/tdep-x86)
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(libunwind_includes ${libunwind_includes} ${SOURCE_ROOT}/include/tdep-x86_64)
else()
    message(FATAL_ERROR "Unsupported ANDROID_ABI: ${ANDROID_ABI}")
endif()

include_directories(${libunwind_includes}) 

set(libunwind_source
    ${SOURCE_ROOT}/src/mi/init.c
    ${SOURCE_ROOT}/src/mi/flush_cache.c
    ${SOURCE_ROOT}/src/mi/mempool.c
    ${SOURCE_ROOT}/src/mi/strerror.c
    ${SOURCE_ROOT}/src/mi/backtrace.c
    ${SOURCE_ROOT}/src/mi/dyn-cancel.c
    ${SOURCE_ROOT}/src/mi/dyn-info-list.c
    ${SOURCE_ROOT}/src/mi/dyn-register.c
    ${SOURCE_ROOT}/src/mi/map.c
    ${SOURCE_ROOT}/src/mi/Lmap.c
    ${SOURCE_ROOT}/src/mi/Ldyn-extract.c
    ${SOURCE_ROOT}/src/mi/Lfind_dynamic_proc_info.c
    ${SOURCE_ROOT}/src/mi/Lget_proc_info_by_ip.c
    ${SOURCE_ROOT}/src/mi/Lget_proc_name.c
    ${SOURCE_ROOT}/src/mi/Lput_dynamic_unwind_info.c
    ${SOURCE_ROOT}/src/mi/Ldestroy_addr_space.c
    ${SOURCE_ROOT}/src/mi/Lget_reg.c
    ${SOURCE_ROOT}/src/mi/Lset_reg.c
    ${SOURCE_ROOT}/src/mi/Lget_fpreg.c
    ${SOURCE_ROOT}/src/mi/Lset_fpreg.c
    ${SOURCE_ROOT}/src/mi/Lset_caching_policy.c
    ${SOURCE_ROOT}/src/mi/Gdyn-extract.c
    ${SOURCE_ROOT}/src/mi/Gdyn-remote.c
    ${SOURCE_ROOT}/src/mi/Gfind_dynamic_proc_info.c
    ${SOURCE_ROOT}/src/mi/Gget_accessors.c
    ${SOURCE_ROOT}/src/mi/Gget_proc_info_by_ip.c
    ${SOURCE_ROOT}/src/mi/Gget_proc_name.c
    ${SOURCE_ROOT}/src/mi/Gput_dynamic_unwind_info.c
    ${SOURCE_ROOT}/src/mi/Gdestroy_addr_space.c
    ${SOURCE_ROOT}/src/mi/Gget_reg.c
    ${SOURCE_ROOT}/src/mi/Gset_reg.c
    ${SOURCE_ROOT}/src/mi/Gget_fpreg.c
    ${SOURCE_ROOT}/src/mi/Gset_fpreg.c
    ${SOURCE_ROOT}/src/mi/Gset_caching_policy.c
    ${SOURCE_ROOT}/src/dwarf/Lexpr.c
    ${SOURCE_ROOT}/src/dwarf/Lfde.c
    ${SOURCE_ROOT}/src/dwarf/Lparser.c
    ${SOURCE_ROOT}/src/dwarf/Lpe.c
    ${SOURCE_ROOT}/src/dwarf/Lstep_dwarf.c
    ${SOURCE_ROOT}/src/dwarf/Lfind_proc_info-lsb.c
    ${SOURCE_ROOT}/src/dwarf/Lfind_unwind_table.c
    ${SOURCE_ROOT}/src/dwarf/Gexpr.c
    ${SOURCE_ROOT}/src/dwarf/Gfde.c
    ${SOURCE_ROOT}/src/dwarf/Gfind_proc_info-lsb.c
    ${SOURCE_ROOT}/src/dwarf/Gfind_unwind_table.c
    ${SOURCE_ROOT}/src/dwarf/Gparser.c
    ${SOURCE_ROOT}/src/dwarf/Gpe.c
    ${SOURCE_ROOT}/src/dwarf/Gstep_dwarf.c
    ${SOURCE_ROOT}/src/dwarf/global.c
    ${SOURCE_ROOT}/src/os-common.c
    ${SOURCE_ROOT}/src/os-linux.c
    ${SOURCE_ROOT}/src/Los-common.c
    ${SOURCE_ROOT}/src/ptrace/_UPT_accessors.c
    ${SOURCE_ROOT}/src/ptrace/_UPT_access_fpreg.c
    ${SOURCE_ROOT}/src/ptrace/_UPT_access_mem.c
    ${SOURCE_ROOT}/src/ptrace/_UPT_access_reg.c
    ${SOURCE_ROOT}/src/ptrace/_UPT_create.c
    ${SOURCE_ROOT}/src/ptrace/_UPT_destroy.c
    ${SOURCE_ROOT}/src/ptrace/_UPT_find_proc_info.c
    ${SOURCE_ROOT}/src/ptrace/_UPT_get_dyn_info_list_addr.c
    ${SOURCE_ROOT}/src/ptrace/_UPT_put_unwind_info.c
    ${SOURCE_ROOT}/src/ptrace/_UPT_get_proc_name.c
    ${SOURCE_ROOT}/src/ptrace/_UPT_reg_offset.c
    ${SOURCE_ROOT}/src/ptrace/_UPT_resume.c
)

set(libunwind_arm_source
    ${SOURCE_ROOT}/src/arm/is_fpreg.c
    ${SOURCE_ROOT}/src/arm/regname.c
    ${SOURCE_ROOT}/src/arm/Gcreate_addr_space.c
    ${SOURCE_ROOT}/src/arm/Gget_proc_info.c
    ${SOURCE_ROOT}/src/arm/Gget_save_loc.c
    ${SOURCE_ROOT}/src/arm/Gglobal.c
    ${SOURCE_ROOT}/src/arm/Ginit.c
    ${SOURCE_ROOT}/src/arm/Ginit_local.c
    ${SOURCE_ROOT}/src/arm/Ginit_remote.c
    ${SOURCE_ROOT}/src/arm/Gregs.c
    ${SOURCE_ROOT}/src/arm/Gresume.c
    ${SOURCE_ROOT}/src/arm/Gstep.c
    ${SOURCE_ROOT}/src/arm/Lcreate_addr_space.c
    ${SOURCE_ROOT}/src/arm/Lget_proc_info.c
    ${SOURCE_ROOT}/src/arm/Lget_save_loc.c
    ${SOURCE_ROOT}/src/arm/Lglobal.c
    ${SOURCE_ROOT}/src/arm/Linit.c
    ${SOURCE_ROOT}/src/arm/Linit_local.c
    ${SOURCE_ROOT}/src/arm/Linit_remote.c
    ${SOURCE_ROOT}/src/arm/Lregs.c
    ${SOURCE_ROOT}/src/arm/Lresume.c
    ${SOURCE_ROOT}/src/arm/Lstep.c
    ${SOURCE_ROOT}/src/arm/getcontext.S
    ${SOURCE_ROOT}/src/arm/Gis_signal_frame.c
    ${SOURCE_ROOT}/src/arm/Gex_tables.c
    ${SOURCE_ROOT}/src/arm/Lis_signal_frame.c
    ${SOURCE_ROOT}/src/arm/Lex_tables.c
    ${SOURCE_ROOT}/src/elf32.c
)

set(libunwind_arm64_source
    ${SOURCE_ROOT}/src/aarch64/is_fpreg.c
    ${SOURCE_ROOT}/src/aarch64/regname.c
    ${SOURCE_ROOT}/src/aarch64/Gcreate_addr_space.c
    ${SOURCE_ROOT}/src/aarch64/Gget_proc_info.c
    ${SOURCE_ROOT}/src/aarch64/Gget_save_loc.c
    ${SOURCE_ROOT}/src/aarch64/Gglobal.c
    ${SOURCE_ROOT}/src/aarch64/Ginit.c
    ${SOURCE_ROOT}/src/aarch64/Ginit_local.c
    ${SOURCE_ROOT}/src/aarch64/Ginit_remote.c
    ${SOURCE_ROOT}/src/aarch64/Gregs.c
    ${SOURCE_ROOT}/src/aarch64/Gresume.c
    ${SOURCE_ROOT}/src/aarch64/Gstep.c
    ${SOURCE_ROOT}/src/aarch64/Lcreate_addr_space.c
    ${SOURCE_ROOT}/src/aarch64/Lget_proc_info.c
    ${SOURCE_ROOT}/src/aarch64/Lget_save_loc.c
    ${SOURCE_ROOT}/src/aarch64/Lglobal.c
    ${SOURCE_ROOT}/src/aarch64/Linit.c
    ${SOURCE_ROOT}/src/aarch64/Linit_local.c
    ${SOURCE_ROOT}/src/aarch64/Linit_remote.c
    ${SOURCE_ROOT}/src/aarch64/Lregs.c
    ${SOURCE_ROOT}/src/aarch64/Lresume.c
    ${SOURCE_ROOT}/src/aarch64/Lstep.c
    ${SOURCE_ROOT}/src/aarch64/Gis_signal_frame.c
    ${SOURCE_ROOT}/src/aarch64/Lis_signal_frame.c
    ${SOURCE_ROOT}/src/elf64.c
)

set(libunwind_x86_source
    ${SOURCE_ROOT}/src/x86/is_fpreg.c
    ${SOURCE_ROOT}/src/x86/regname.c
    ${SOURCE_ROOT}/src/x86/Gcreate_addr_space.c
    ${SOURCE_ROOT}/src/x86/Gget_proc_info.c
    ${SOURCE_ROOT}/src/x86/Gget_save_loc.c
    ${SOURCE_ROOT}/src/x86/Gglobal.c
    ${SOURCE_ROOT}/src/x86/Ginit.c
    ${SOURCE_ROOT}/src/x86/Ginit_local.c
    ${SOURCE_ROOT}/src/x86/Ginit_remote.c
    ${SOURCE_ROOT}/src/x86/Gregs.c
    ${SOURCE_ROOT}/src/x86/Gresume.c
    ${SOURCE_ROOT}/src/x86/Gstep.c
    ${SOURCE_ROOT}/src/x86/Lcreate_addr_space.c
    ${SOURCE_ROOT}/src/x86/Lget_proc_info.c
    ${SOURCE_ROOT}/src/x86/Lget_save_loc.c
    ${SOURCE_ROOT}/src/x86/Lglobal.c
    ${SOURCE_ROOT}/src/x86/Linit.c
    ${SOURCE_ROOT}/src/x86/Linit_local.c
    ${SOURCE_ROOT}/src/x86/Linit_remote.c
    ${SOURCE_ROOT}/src/x86/Lregs.c
    ${SOURCE_ROOT}/src/x86/Lresume.c
    ${SOURCE_ROOT}/src/x86/Lstep.c
    ${SOURCE_ROOT}/src/x86/getcontext-linux.S
    ${SOURCE_ROOT}/src/x86/Gos-linux.c
    ${SOURCE_ROOT}/src/x86/Los-linux.c
    ${SOURCE_ROOT}/src/elf32.c
)

set(libunwind_x86_64_source
    ${SOURCE_ROOT}/src/x86_64/is_fpreg.c
    ${SOURCE_ROOT}/src/x86_64/regname.c
    ${SOURCE_ROOT}/src/x86_64/Gcreate_addr_space.c
    ${SOURCE_ROOT}/src/x86_64/Gget_proc_info.c
    ${SOURCE_ROOT}/src/x86_64/Gget_save_loc.c
    ${SOURCE_ROOT}/src/x86_64/Gglobal.c
    ${SOURCE_ROOT}/src/x86_64/Ginit.c
    ${SOURCE_ROOT}/src/x86_64/Ginit_local.c
    ${SOURCE_ROOT}/src/x86_64/Ginit_remote.c
    ${SOURCE_ROOT}/src/x86_64/Gregs.c
    ${SOURCE_ROOT}/src/x86_64/Gresume.c
    ${SOURCE_ROOT}/src/x86_64/Gstep.c
    ${SOURCE_ROOT}/src/x86_64/Lcreate_addr_space.c
    ${SOURCE_ROOT}/src/x86_64/Lget_proc_info.c
    ${SOURCE_ROOT}/src/x86_64/Lget_save_loc.c
    ${SOURCE_ROOT}/src/x86_64/Lglobal.c
    ${SOURCE_ROOT}/src/x86_64/Linit.c
    ${SOURCE_ROOT}/src/x86_64/Linit_local.c
    ${SOURCE_ROOT}/src/x86_64/Linit_remote.c
    ${SOURCE_ROOT}/src/x86_64/Lregs.c
    ${SOURCE_ROOT}/src/x86_64/Lresume.c
    ${SOURCE_ROOT}/src/x86_64/Lstep.c
    ${SOURCE_ROOT}/src/x86_64/getcontext.S
    ${SOURCE_ROOT}/src/x86_64/Gstash_frame.c
    ${SOURCE_ROOT}/src/x86_64/Gtrace.c
    ${SOURCE_ROOT}/src/x86_64/Gos-linux.c
    ${SOURCE_ROOT}/src/x86_64/Lstash_frame.c
    ${SOURCE_ROOT}/src/x86_64/Ltrace.c
    ${SOURCE_ROOT}/src/x86_64/Los-linux.c
    ${SOURCE_ROOT}/src/x86_64/setcontext.S
    ${SOURCE_ROOT}/src/elf64.c
)

if(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(libunwind_source ${libunwind_source} ${libunwind_arm_source})
elseif(ANDROID_ABI STREQUAL "arm64-v8a")
    set(libunwind_source ${libunwind_source} ${libunwind_arm64_source})
elseif(ANDROID_ABI STREQUAL "x86")
    set(libunwind_source ${libunwind_source} ${libunwind_x86_source})
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(libunwind_source ${libunwind_source} ${libunwind_x86_64_source})
else()
    message(FATAL_ERROR "Unsupported ANDROID_ABI: ${ANDROID_ABI}")
endif()

#add_library(unwind SHARED ${libunwind_source})
add_library(unwind STATIC ${libunwind_source})

set(libunwindbacktrace_source
    ${SOURCE_ROOT}/src/unwind/BacktraceWrapper.c
    ${SOURCE_ROOT}/src/unwind/DeleteException.c
    ${SOURCE_ROOT}/src/unwind/FindEnclosingFunction.c
    ${SOURCE_ROOT}/src/unwind/ForcedUnwind.c
    ${SOURCE_ROOT}/src/unwind/GetBSP.c
    ${SOURCE_ROOT}/src/unwind/GetCFA.c
    ${SOURCE_ROOT}/src/unwind/GetDataRelBase.c
    ${SOURCE_ROOT}/src/unwind/GetGR.c
    ${SOURCE_ROOT}/src/unwind/GetIP.c
    ${SOURCE_ROOT}/src/unwind/GetIPInfo.c
    ${SOURCE_ROOT}/src/unwind/GetLanguageSpecificData.c
    ${SOURCE_ROOT}/src/unwind/GetRegionStart.c
    ${SOURCE_ROOT}/src/unwind/GetTextRelBase.c
    ${SOURCE_ROOT}/src/unwind/RaiseException.c
    ${SOURCE_ROOT}/src/unwind/Resume.c
    ${SOURCE_ROOT}/src/unwind/Resume_or_Rethrow.c
    ${SOURCE_ROOT}/src/unwind/SetGR.c
    ${SOURCE_ROOT}/src/unwind/SetIP.c
)

#set(libunwindbacktrace_cflags
#    -Wno-old-style-declaration
#    -fvisibility=hidden
#)

add_library(unwindbacktrace STATIC ${libunwindbacktrace_source})
#target_compile_definitions(unwindbacktrace ${libunwindbacktrace_cflags})