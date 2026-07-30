/* syntax version 1 */
SELECT
    WithNativeHttp::http_get("mock://ok") AS body;
