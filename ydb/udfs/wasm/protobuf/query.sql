/* syntax version 1 */
-- Protobuf WASM smoke: TypeConfig for Demo { string name = 1; int32 value = 2; }
-- Parse/TryParse return JSON strings; Serialize accepts JSON and returns protobin.

$config = @@{
  "name": "Demo",
  "format": "protobin",
  "skip": 0,
  "meta": "H4sIANv0p2oC/+Oy5uJKSc3N1ysoyi/JF2JQUuZicQHyhXi4WPISc1MlGBUYNTiFeLlYyxJzSlMlmIBc1iQ2sGpjAAwlvkk9AAAA",
  "view": {
    "enum": "number",
    "recursion": "fail"
  },
  "lists": {
    "optional": false
  }
}@@;

$parse = YQL::Udf(AsAtom("Protobuf.Parse"), Void(), Void(), AsAtom($config));
$try_parse = YQL::Udf(AsAtom("Protobuf.TryParse"), Void(), Void(), AsAtom($config));
$serialize = YQL::Udf(AsAtom("Protobuf.Serialize"), Void(), Void(), AsAtom($config));

-- Demo wire: name="hello", value=42
$wire = String::HexDecode("0a0568656c6c6f102a");

SELECT
    $parse($wire) AS parsed_json,
    $try_parse($wire) AS try_ok,
    $try_parse("not-a-protobuf") AS try_fail,  -- NULL on failure
    String::HexEncode($serialize(@@{"name":"hello","value":42}@@)) AS roundtrip_hex;
