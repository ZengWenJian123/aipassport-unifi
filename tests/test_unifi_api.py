"""Compile pure C normalizers and run deterministic fake-response checks."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class UniFiApiParserTests(unittest.TestCase):
    def test_streaming_fake_api_responses(self):
        with tempfile.TemporaryDirectory(prefix="unifi-api-test-") as directory:
            binary = pathlib.Path(directory) / "test-unifi-api"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "main"),
                str(ROOT / "main/unifi_json_stream.c"),
                str(ROOT / "main/unifi_api_parse.c"),
                str(ROOT / "tests/test_unifi_api.c"), "-lm", "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)

    def test_transport_with_fake_http(self):
        with tempfile.TemporaryDirectory(prefix="unifi-http-test-") as directory:
            folder = pathlib.Path(directory)
            (folder / "esp_err.h").write_text("""
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_INVALID_SIZE 4
#define ESP_ERR_INVALID_RESPONSE 5
#define ESP_ERR_TIMEOUT 6
#define ESP_ERR_NOT_FOUND 7
const char *esp_err_to_name(esp_err_t);
""")
            (folder / "esp_log.h").write_text("#define ESP_LOGW(tag,fmt,...) ((void)0)\n")
            (folder / "esp_timer.h").write_text("#include <stdint.h>\nint64_t esp_timer_get_time(void);\n")
            (folder / "esp_crt_bundle.h").write_text("#include \"esp_err.h\"\nesp_err_t esp_crt_bundle_attach(void *);\n")
            (folder / "esp_http_client.h").write_text("""
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define HTTP_METHOD_GET 0
#define HTTP_EVENT_ON_HEADER 1
typedef struct { int event_id; char *header_key; char *header_value; void *user_data; } esp_http_client_event_t;
typedef struct { const char *url; int timeout_ms, method; bool disable_auto_redirect;
esp_err_t (*event_handler)(esp_http_client_event_t *); void *user_data; int buffer_size, buffer_size_tx;
esp_err_t (*crt_bundle_attach)(void *); const char *cert_pem; } esp_http_client_config_t;
typedef struct mock_client *esp_http_client_handle_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t,const char *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t,int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,char *,int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
""")
            binary = folder / "test-unifi-transport"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(folder), "-I", str(ROOT / "main"),
                str(ROOT / "main/unifi_json_stream.c"), str(ROOT / "main/unifi_api_parse.c"),
                str(ROOT / "main/unifi_api.c"), str(ROOT / "tests/test_unifi_api_transport.c"),
                str(ROOT / "main/unifi_demo.c"),
                "-lm", "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
