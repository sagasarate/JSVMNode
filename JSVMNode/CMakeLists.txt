cmake_minimum_required(VERSION 3.16)
project(JSVMNode)

# 设置 C++ 标准和全局编译选项
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -fPIC -fvisibility=hidden -MMD -Winvalid-pch")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++ -rdynamic")

set(GLOBAL_DEFINITIONS
    _GLIBCXX_USE_CXX11_ABI=1
    NODE_OPENSSL_CONF_NAME=nodejs_conf
    NODE_OPENSSL_HAS_QUIC
    ICU_NO_USER_DATA_OVERRIDE
    V8_ENABLE_CHECKS
    OPENSSL_THREADS
    FD_SETSIZE=1024
    NODE_USE_V8_PLATFORM=1
    NODE_V8_SHARED_RO_HEAP
    NODE_HAVE_I18N_SUPPORT=1
    NODE_BUNDLED_ZLIB
    HAVE_OPENSSL=1
    OPENSSL_API_COMPAT=0x10100000L
    UCONFIG_NO_SERVICE=1
    U_ENABLE_DYLOAD=0
    U_STATIC_IMPLEMENTATION=1
    U_HAVE_STD_STRING=1
    UCONFIG_NO_BREAK_ITERATION=0
    AES_ASM
    BSAES_ASM
    CMLL_ASM
    ECP_NISTZ256_ASM
    GHASH_ASM
    KECCAK1600_ASM
    MD5_ASM
    OPENSSL_BN_ASM_GF2m
    OPENSSL_BN_ASM_MONT
    OPENSSL_BN_ASM_MONT5
    OPENSSL_CPUID_OBJ
    OPENSSL_IA32_SSE2
    PADLOCK_ASM
    POLY1305_ASM
    RC4_ASM
    SHA1_ASM
    SHA256_ASM
    SHA512_ASM
    VPAES_ASM
    WHIRLPOOL_ASM
    X25519_ASM
    OPENSSL_PIC
    NGTCP2_STATICLIB
    NGHTTP2_STATICLIB
    NGHTTP3_STATICLIB
    _HAS_EXCEPTIONS=0
    HAVE_INSPECTOR=1
    NODE_WANT_INTERNALS=1
    NODEJSVM_EXPORTS
)

set(NODE_LIBS
    -l:libuv.a
    -l:libuvwasi.a
    -l:libicudata.a
    -l:libicui18n.a
    -l:libicuucx.a
    -l:libopenssl.a
    -l:libncrypto.a
    -l:libhistogram.a
    -l:libsimdjson.a
    -l:libsimdutf.a
    -l:libbrotli.a
    -l:libnbytes.a
    -l:libzlib.a
    -l:libzlib_inflate_chunk_simd.a
    -l:libzlib_adler32_simd.a
    -l:libada.a
    -l:libcares.a
    -l:libnghttp2.a
    -l:libnghttp3.a
    -l:libngtcp2.a
    -l:libllhttp.a
    -l:libsqlite.a
    -l:libv8_zlib.a
    -l:libv8_abseil.a
    -l:libv8_libbase.a
    -l:libv8_compiler.a
    -l:libv8_snapshot.a
    -l:libv8_base_without_compiler.a
    -l:libv8_libplatform.a
    -l:libv8_turboshaft.a
    -l:libnode.a
)

set(LIB_DIR
    ../../Libs/Bin
    ../../node/out/lib_linux
)

# 包含目录
include_directories(
    ../../node/src 
    ../../node/deps/uv/include 
    ../../node/deps/v8/include 
    ../../node/deps/openssl/openssl/include 
    ../../node/deps/simdjson
)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Lib)

# 添加全局库目录


# 添加源文件
file(GLOB SOURCES "./*.cpp" "./*.cc") # 假设源文件在 src 目录下

# Debug配置
add_library(JSVMNodeD SHARED ${SOURCES})
target_precompile_headers(JSVMNodeD PRIVATE pch.h)
target_compile_definitions(JSVMNodeD PRIVATE DEBUG _DEBUG) # 添加Debug定义
target_link_directories(JSVMNodeD PRIVATE ${LIB_DIR})
target_compile_definitions(JSVMNodeD PRIVATE ${GLOBAL_DEFINITIONS})
set_target_properties(JSVMNodeD PROPERTIES COMPILE_FLAGS "-g -O0")
target_link_libraries(JSVMNodeD 
    -Wl,--whole-archive
    ${NODE_LIBS}
    -l:UtilsD.a
    -Wl,--no-whole-archive
)

# Release配置
add_library(JSVMNode SHARED ${SOURCES})
target_precompile_headers(JSVMNode PRIVATE pch.h)
target_compile_definitions(JSVMNode PRIVATE NDEBUG) # 添加Release定义
target_link_directories(JSVMNode PRIVATE ${LIB_DIR})
target_compile_definitions(JSVMNode PRIVATE ${GLOBAL_DEFINITIONS})
set_target_properties(JSVMNode PROPERTIES COMPILE_FLAGS "-O3 -s")
target_link_libraries(JSVMNode 
    -Wl,--whole-archive
    ${NODE_LIBS}
    -l:Utils.a
    -Wl,--no-whole-archive
)

