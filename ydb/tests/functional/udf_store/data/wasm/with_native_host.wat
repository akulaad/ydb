;; UDF that imports a native host function from module "native_math".
(module
    (import "env" "memory" (memory i64 8 2097152))
    (import "native_math" "host_add" (func $host_add (param i64 i64) (result i64)))

    (type $t_add (func (param i64 i64 i64 i64)))

    (func $udf_add (type $t_add)
        (param $context i64)
        (param $result i64)
        (param $left i64)
        (param $right i64)

        (i32.store8
            (i64.add (local.get $result) (i64.const 2))
            (i32.const 3))
        (i64.store
            (i64.add (local.get $result) (i64.const 8))
            (call $host_add
                (i64.load (i64.add (local.get $left) (i64.const 8)))
                (i64.load (i64.add (local.get $right) (i64.const 8)))))
    )

    (export "udf_add" (func $udf_add))
)
