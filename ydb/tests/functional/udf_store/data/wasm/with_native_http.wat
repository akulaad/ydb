;; UDF that imports native_http.host_http_get (mock:// or http://).
(module
    (import "env" "memory" (memory i64 8 2097152))
    (import "native_http" "host_http_get"
        (func $host_http_get (param i64 i32 i64 i32) (result i64)))

    ;; Scratch response buffer (guest offset).
    (global $out_buf i64 (i64.const 2048))
    (global $out_cap i32 (i32.const 65536))

    (type $t_http_get (func (param i64 i64 i64)))

    (func $http_get (type $t_http_get)
        (param $context i64)
        (param $result i64)
        (param $url i64)

        (local $url_ptr i64)
        (local $url_len i32)
        (local $packed i64)
        (local $nbytes i32)

        (local.set $url_len
            (i32.load (i64.add (local.get $url) (i64.const 4))))
        (local.set $url_ptr
            (i64.load (i64.add (local.get $url) (i64.const 8))))

        (local.set $packed
            (call $host_http_get
                (local.get $url_ptr)
                (local.get $url_len)
                (global.get $out_buf)
                (global.get $out_cap)))

        ;; Negative packed value means host error.
        (if (i64.lt_s (local.get $packed) (i64.const 0))
            (then unreachable))

        (local.set $nbytes
            (i32.wrap_i64 (i64.and (local.get $packed) (i64.const 0xffffffff))))

        ;; TUnversionedValue: Type=String(0x10) at +2, Length at +4, Data.String at +8
        (i32.store8
            (i64.add (local.get $result) (i64.const 2))
            (i32.const 16))
        (i32.store
            (i64.add (local.get $result) (i64.const 4))
            (local.get $nbytes))
        (i64.store
            (i64.add (local.get $result) (i64.const 8))
            (global.get $out_buf))
    )

    (export "http_get" (func $http_get))
)
