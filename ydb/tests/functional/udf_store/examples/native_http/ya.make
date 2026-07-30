DLL(native_http_host)

SRCS(
    native_http.cpp
)

PEERDIR(
    library/cpp/http/simple
)

# GetCurrentCompartment is provided by ydbd (strong) at dlopen time.
# Do not PEERDIR ydb/library/wasm/api — its Y_WEAK stub would bind locally.
# lld does not accept --allow-undefined (emscripten-only); ignore unresolved instead.
LDFLAGS(
    -Wl,--unresolved-symbols=ignore-all
)

END()
