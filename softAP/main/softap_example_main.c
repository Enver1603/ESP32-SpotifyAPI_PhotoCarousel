/* WiFi softAP + Web Configuration Page */

#include <string.h>

#include "cJSON.h"
#include "jpeg_decoder.h"
#include "driver/gpio.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "lvgl.h"
LV_FONT_DECLARE(spotify_font_tr);
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <setjmp.h>

#include "jpeglib.h"
#include "jerror.h"

/*Includes For SD Card Reading*/
#include <stdio.h>
#include <dirent.h>

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

/* WiFi configuration */
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN

static lv_obj_t *track_label = NULL;
static lv_obj_t *artist_label = NULL;
static lv_obj_t *album_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *progress_bar = NULL;
static lv_obj_t *time_left_label = NULL;
static lv_obj_t *time_right_label = NULL;

static lv_obj_t *cover_image = NULL;
static lv_obj_t *cover_icon = NULL;
static lv_obj_t *wifi_icon = NULL;

/*
 * Wi-Fi gercekten internete cikacak IP'yi aldiginda true olur.
 * Event handler sadece bu degiskeni degistirir.
 * LVGL objesine event handler'dan dokunmuyoruz.
 */
static volatile bool wifi_connected = false;
static volatile bool spotify_ui_waiting = true;
static volatile bool spotify_playback_active = false;

static lv_image_dsc_t cover_image_dsc = { 0 };

static uint8_t *current_cover_pixels = NULL;

static uint8_t *pending_cover_pixels = NULL;
static uint16_t pending_cover_width = 0;
static uint16_t pending_cover_height = 0;

static portMUX_TYPE cover_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t jpeg_work_buffer[3100];

static const char *TAG = "wifi softAP";

static void spotify_https_task(void *pvParameters);
static TaskHandle_t spotify_task_handle = NULL;
static esp_lcd_panel_handle_t lcd_panel_handle = NULL;
static esp_http_client_handle_t spotify_current_client = NULL;
static esp_http_client_handle_t album_http_client = NULL;

/* =========================================================
 * SD CARD CONFIGURATION
 * ========================================================= */

#define SD_MOSI_GPIO GPIO_NUM_11
#define SD_MISO_GPIO GPIO_NUM_13
#define SD_CLK_GPIO  GPIO_NUM_12
#define SD_CS_GPIO   GPIO_NUM_10

#define SD_MOUNT_POINT "/sdcard"

static sdmmc_card_t *sd_card = NULL;
static bool sd_card_ready = false;

/*
 * SD karta ayni anda iki task dokunmasin.
 *
 * Slideshow fotograf okurken web upload yazmaya
 * calisirsa burada siraya girecekler.
 */
static SemaphoreHandle_t sd_mutex = NULL;

/*
 * Slideshow ayarlari
 */
#define PHOTO_SLIDESHOW_INTERVAL_MS 6000
#define PHOTO_FADE_DURATION_MS      650

static int sd_photo_count = 0;
static int slideshow_next_photo = 2;

/*
 * Slideshow gecis durumu
 */
static volatile bool photo_fade_out_requested = false;
static volatile bool photo_fade_out_complete = false;
static volatile bool photo_fade_in_requested = false;

/*
 * SD fotograf goruntuleme sistemi
 */
static lv_obj_t *photo_layer = NULL;
static lv_obj_t *photo_image = NULL;

static lv_image_dsc_t photo_image_dsc = { 0 };

static uint8_t *current_photo_pixels = NULL;

static uint8_t *pending_photo_pixels = NULL;
static uint16_t pending_photo_width = 0;
static uint16_t pending_photo_height = 0;

static portMUX_TYPE photo_mux = portMUX_INITIALIZER_UNLOCKED;
/* =========================================================
 * HTML PAGE
 * ========================================================= */

static const char html_page[] =
"<!DOCTYPE html>"
"<html lang='tr'>"

"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>heart_steel Wi-Fi Setup</title>"

"<style>"

"*{"
"box-sizing:border-box;"
"}"

"body{"
"margin:0;"
"font-family:Arial,sans-serif;"
"background:#121212;"
"color:white;"
"display:flex;"
"align-items:center;"
"justify-content:center;"
"min-height:100vh;"
"padding:20px;"
"}"

".box{"
"width:100%;"
"max-width:400px;"
"background:#1e1e1e;"
"padding:30px;"
"border-radius:18px;"
"box-shadow:0 10px 30px rgba(0,0,0,0.4);"
"}"

"h1{"
"margin-top:0;"
"font-size:28px;"
"}"

"h2{"
"margin-bottom:10px;"
"}"

"p{"
"color:#aaaaaa;"
"margin-bottom:25px;"
"}"

"label{"
"display:block;"
"text-align:left;"
"margin-top:15px;"
"margin-bottom:6px;"
"}"

"input{"
"width:100%;"
"padding:14px;"
"border-radius:10px;"
"border:1px solid #444;"
"background:#2a2a2a;"
"color:white;"
"font-size:16px;"
"outline:none;"
"}"

"input:focus{"
"border-color:#1DB954;"
"}"

"button{"
"width:100%;"
"margin-top:25px;"
"padding:14px;"
"border:none;"
"border-radius:10px;"
"background:#1DB954;"
"color:white;"
"font-size:17px;"
"font-weight:bold;"
"cursor:pointer;"
"}"

"button:disabled{"
"opacity:0.6;"
"cursor:not-allowed;"
"}"

"hr{"
"border:none;"
"border-top:1px solid #333;"
"margin:30px 0;"
"}"

"#photoStatus{"
"margin-top:15px;"
"margin-bottom:0;"
"font-size:14px;"
"}"

"</style>"
"</head>"

"<body>"

"<div class='box'>"

"<h1>heart_steel</h1>"
"<p>Wi-Fi bağlantısını ayarla</p>"

"<form action='/connect' method='POST'>"

"<label>Wi-Fi Adı</label>"

"<input "
"name='ssid' "
"type='text' "
"placeholder='Wi-Fi ağının adını gir' "
"required>"

"<label>Wi-Fi Şifresi</label>"

"<input "
"name='password' "
"type='password' "
"placeholder='Wi-Fi şifresini gir' "
"required>"

"<button type='submit'>"
"Bağlan"
"</button>"

"</form>"

"<hr>"

"<h2>Spotify</h2>"

"<p>"
"Spotify hesabını heart_steel'e bağla"
"</p>"

"<form action='/spotify' method='POST'>"

"<label>Client ID</label>"

"<input "
"name='client_id' "
"type='text' "
"placeholder='Spotify Client ID' "
"required>"

"<label>Refresh Token</label>"

"<input "
"name='refresh_token' "
"type='password' "
"placeholder='Spotify Refresh Token' "
"required>"

"<button type='submit'>"
"Spotify'ı Kaydet"
"</button>"

"</form>"

"<hr>"

"<h2>Fotoğraflar</h2>"

"<p>"
"Slideshow'a yeni bir fotoğraf ekle"
"</p>"

"<input "
"id='photoInput' "
"type='file' "
"accept='image/*'>"

"<button "
"type='button' "
"id='photoUploadButton' "
"onclick='uploadPhoto()'>"
"Fotoğraf Ekle"
"</button>"

"<p id='photoStatus'></p>"

"<script>"

"async function uploadPhoto(){"

"const input="
"document.getElementById('photoInput');"

"const button="
"document.getElementById('photoUploadButton');"

"const status="
"document.getElementById('photoStatus');"


"if(!input.files || input.files.length===0){"

"status.textContent="
"'Önce bir fotoğraf seç.';"

"return;"

"}"


"const file=input.files[0];"


"button.disabled=true;"

"button.textContent="
"'Hazırlanıyor...';"

"status.textContent="
"'Fotoğraf hazırlanıyor...';"


"try{"


"const bitmap="
"await createImageBitmap("
"file,"
"{imageOrientation:'from-image'}"
");"


"const maxWidth=480;"

"const maxHeight=272;"


"const scale=Math.min("
"maxWidth/bitmap.width,"
"maxHeight/bitmap.height,"
"1"
");"


"const width=Math.max("
"1,"
"Math.round(bitmap.width*scale)"
");"


"const height=Math.max("
"1,"
"Math.round(bitmap.height*scale)"
");"


"const canvas="
"document.createElement('canvas');"

"canvas.width=width;"

"canvas.height=height;"


"const ctx="
"canvas.getContext('2d');"


"ctx.drawImage("
"bitmap,"
"0,"
"0,"
"width,"
"height"
");"


"bitmap.close();"


"const blob="
"await new Promise((resolve,reject)=>{"


"canvas.toBlob("

"(result)=>{"

"if(result){"

"resolve(result);"

"}else{"

"reject("
"new Error('JPEG oluşturulamadı')"
");"

"}"

"},"

"'image/jpeg',"

"0.90"

");"


"});"


"status.textContent="
"'Fotoğraf yükleniyor... ('+"
"Math.round(blob.size/1024)+"
"' KB)';"


"button.textContent="
"'Yükleniyor...';"


"const response="
"await fetch("

"'/upload-photo',"

"{"

"method:'POST',"

"headers:{"
"'Content-Type':'image/jpeg'"
"},"

"body:blob"

"}"

");"


"if(!response.ok){"

"const errorText="
"await response.text();"

"throw new Error("
"errorText || 'Upload başarısız'"
");"

"}"


"const result="
"await response.json();"


"status.textContent="
"'✓ Fotoğraf eklendi: photo-'+"
"result.photo+"
"'.jpeg';"


"input.value='';"


"}catch(error){"


"console.error(error);"


"status.textContent="
"'Hata: '+error.message;"


"}finally{"


"button.disabled=false;"

"button.textContent="
"'Fotoğraf Ekle';"


"}"


"}"

"</script>"

"</div>"

"</body>"

"</html>";

/* =========================================================
 * HTTP /
 * ========================================================= */

typedef struct {
	char *buffer;
	size_t buffer_size;
	size_t data_len;
} spotify_http_response_t;

static esp_err_t spotify_http_event_handler(esp_http_client_event_t *evt) {
	spotify_http_response_t *response =
			(spotify_http_response_t*) evt->user_data;

	if (evt->event_id == HTTP_EVENT_ON_DATA && response != NULL
			&& evt->data != NULL && evt->data_len > 0) {
		size_t remaining = response->buffer_size - response->data_len - 1;

		size_t copy_len = (size_t) evt->data_len;

		if (copy_len > remaining) {
			copy_len = remaining;
		}

		if (copy_len > 0) {
			memcpy(response->buffer + response->data_len, evt->data, copy_len);

			response->data_len += copy_len;

			response->buffer[response->data_len] = '\0';
		}
	}

	return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req) {
	ESP_LOGI("WEB", "Ana sayfa istendi");

	httpd_resp_set_type(req, "text/html; charset=utf-8");

	esp_err_t err = httpd_resp_send(req, html_page, sizeof(html_page) - 1);

	if (err != ESP_OK) {
		ESP_LOGE("WEB", "Ana sayfa gonderilemedi: %s", esp_err_to_name(err));

		return err;
	}

	ESP_LOGI("WEB", "Ana sayfa basariyla gonderildi");

	return ESP_OK;
}

static bool load_wifi_credentials(char *ssid, size_t ssid_size, char *password,
		size_t password_size) {
	nvs_handle_t nvs_handle;

	esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);

	if (err != ESP_OK) {
		ESP_LOGI("NVS", "Kayitli Wi-Fi bilgisi bulunamadi");

		return false;
	}

	size_t required_ssid_size = ssid_size;

	err = nvs_get_str(nvs_handle, "ssid", ssid, &required_ssid_size);

	if (err != ESP_OK) {
		ESP_LOGI("NVS", "Kayitli SSID bulunamadi");

		nvs_close(nvs_handle);

		return false;
	}

	size_t required_password_size = password_size;

	err = nvs_get_str(nvs_handle, "password", password,
			&required_password_size);

	if (err != ESP_OK) {
		ESP_LOGI("NVS", "Kayitli sifre bulunamadi");

		nvs_close(nvs_handle);

		return false;
	}

	nvs_close(nvs_handle);

	/*
	 * Guvenlik nedeniyle sifrenin
	 * kendisini log'a yazmiyoruz.
	 */

	ESP_LOGI("NVS", "Kayitli Wi-Fi bulundu. SSID: %s", ssid);

	ESP_LOGI("NVS", "Kayitli sifre uzunlugu: %d", (int) strlen(password));

	return true;
}

static esp_err_t save_spotify_credentials(const char *client_id,
		const char *refresh_token) {
	nvs_handle_t nvs_handle;

	esp_err_t err = nvs_open("spotify", NVS_READWRITE, &nvs_handle);

	if (err != ESP_OK) {
		ESP_LOGE("SPOTIFY", "Spotify NVS acilamadi: %s", esp_err_to_name(err));

		return err;
	}

	err = nvs_set_str(nvs_handle, "client_id", client_id);

	if (err != ESP_OK) {
		nvs_close(nvs_handle);
		return err;
	}

	err = nvs_set_str(nvs_handle, "refresh", refresh_token);

	if (err != ESP_OK) {
		nvs_close(nvs_handle);
		return err;
	}

	err = nvs_commit(nvs_handle);

	nvs_close(nvs_handle);

	if (err == ESP_OK) {
		ESP_LOGI("SPOTIFY", "Spotify bilgileri NVS'ye kaydedildi");
	}

	return err;
}

static bool load_spotify_credentials(char *client_id, size_t client_id_size,
		char *refresh_token, size_t refresh_token_size) {
	nvs_handle_t nvs_handle;

	esp_err_t err = nvs_open("spotify", NVS_READONLY, &nvs_handle);

	if (err != ESP_OK) {
		ESP_LOGE("SPOTIFY", "Spotify NVS acilamadi: %s", esp_err_to_name(err));

		return false;
	}

	size_t required_client_id_size = client_id_size;

	err = nvs_get_str(nvs_handle, "client_id", client_id,
			&required_client_id_size);

	if (err != ESP_OK) {
		ESP_LOGE("SPOTIFY", "Client ID NVS'den okunamadi: %s",
				esp_err_to_name(err));

		nvs_close(nvs_handle);
		return false;
	}

	size_t required_refresh_token_size = refresh_token_size;

	err = nvs_get_str(nvs_handle, "refresh", refresh_token,
			&required_refresh_token_size);

	if (err != ESP_OK) {
		ESP_LOGE("SPOTIFY", "Refresh Token NVS'den okunamadi: %s",
				esp_err_to_name(err));

		nvs_close(nvs_handle);
		return false;
	}

	nvs_close(nvs_handle);

	/*
	 * Guvenlik:
	 * Client ID veya Refresh Token'in kendisini
	 * kesinlikle loglamiyoruz.
	 */

	ESP_LOGI("SPOTIFY", "Spotify bilgileri NVS'den okundu");

	ESP_LOGI("SPOTIFY", "Client ID uzunlugu: %d", (int) strlen(client_id));

	ESP_LOGI("SPOTIFY", "Refresh Token uzunlugu: %d",
			(int) strlen(refresh_token));

	return true;
}

static int hex_to_int(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';

	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;

	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	return -1;
}

static void url_decode(char *str) {
	char *src = str;
	char *dst = str;

	while (*src) {
		if (*src == '+') {
			*dst++ = ' ';
			src++;
		} else if (*src == '%' && src[1] && src[2]) {
			int hi = hex_to_int(src[1]);
			int lo = hex_to_int(src[2]);

			if (hi >= 0 && lo >= 0) {
				*dst++ = (char) ((hi << 4) | lo);
				src += 3;
			} else {
				*dst++ = *src++;
			}
		} else {
			*dst++ = *src++;
		}
	}

	*dst = '\0';
}

static bool is_url_safe_char(char c) {
	if (c >= 'a' && c <= 'z')
		return true;
	if (c >= 'A' && c <= 'Z')
		return true;
	if (c >= '0' && c <= '9')
		return true;

	if (c == '-' || c == '_' || c == '.' || c == '~') {
		return true;
	}

	return false;
}

static char* url_encode(const char *input) {
	if (input == NULL) {
		return NULL;
	}

	size_t input_len = strlen(input);

	/*
	 * En kotu durumda her karakter %XX olur:
	 * 3 kat + null terminator.
	 */
	size_t output_size = (input_len * 3) + 1;

	char *output = malloc(output_size);

	if (output == NULL) {
		return NULL;
	}

	static const char hex[] = "0123456789ABCDEF";

	char *dst = output;

	while (*input) {
		unsigned char c = (unsigned char) *input;

		if (is_url_safe_char((char) c)) {
			*dst++ = (char) c;
		} else {
			*dst++ = '%';
			*dst++ = hex[(c >> 4) & 0x0F];
			*dst++ = hex[c & 0x0F];
		}

		input++;
	}

	*dst = '\0';

	return output;
}

static esp_err_t spotify_save_handler(httpd_req_t *req) {
	ESP_LOGI("SPOTIFY", "POST /spotify istegi geldi");

	int total_len = req->content_len;

	ESP_LOGI("SPOTIFY", "Gelen veri boyutu: %d byte", total_len);

	if (total_len <= 0 || total_len > 1800) {
		ESP_LOGE("SPOTIFY", "Gecersiz Spotify form boyutu");

		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Gecersiz veri");

		return ESP_FAIL;
	}

	/*
	 * Büyük dizileri stack'e koymuyoruz.
	 * HTTP server task stack'ini gereksiz yere doldurmayalim.
	 */
	char *content = malloc(total_len + 1);

	if (content == NULL) {
		ESP_LOGE("SPOTIFY", "Form icin RAM ayrilamadi");

		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
				"Bellek hatasi");

		return ESP_FAIL;
	}

	int received = 0;

	while (received < total_len) {
		int ret = httpd_req_recv(req, content + received, total_len - received);

		if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
			continue;
		}

		if (ret <= 0) {
			ESP_LOGE("SPOTIFY", "Form verisi alinamadi");

			free(content);

			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
					"Veri alinamadi");

			return ESP_FAIL;
		}

		received += ret;
	}

	content[received] = '\0';

	ESP_LOGI("SPOTIFY", "Form verisi tamamen alindi");

	/*
	 * Bunlari static yapiyoruz.
	 * Ozellikle refresh token buffer'ini
	 * HTTP server task stack'ine koymuyoruz.
	 */
	static char client_id[128];
	static char refresh_token[1024];

	memset(client_id, 0, sizeof(client_id));

	memset(refresh_token, 0, sizeof(refresh_token));

	esp_err_t client_result = httpd_query_key_value(content, "client_id",
			client_id, sizeof(client_id));

	esp_err_t token_result = httpd_query_key_value(content, "refresh_token",
			refresh_token, sizeof(refresh_token));

	free(content);
	content = NULL;

	if (client_result != ESP_OK || token_result != ESP_OK) {
		ESP_LOGE("SPOTIFY", "Client ID veya Refresh Token okunamadi");

		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
				"Eksik Spotify bilgisi");

		return ESP_FAIL;
	}

	/*
	 * HTML form application/x-www-form-urlencoded
	 * gonderdigi icin degerleri decode ediyoruz.
	 */
	url_decode(client_id);
	url_decode(refresh_token);

	size_t client_id_len = strlen(client_id);
	size_t refresh_token_len = strlen(refresh_token);

	ESP_LOGI("SPOTIFY", "Client ID alindi. Uzunluk: %d", (int) client_id_len);

	ESP_LOGI("SPOTIFY", "Refresh Token alindi. Uzunluk: %d",
			(int) refresh_token_len);

	/*
	 * Tokenin kendisini ASLA loglamiyoruz.
	 */
	if (client_id_len == 0 || refresh_token_len == 0) {
		ESP_LOGE("SPOTIFY", "Spotify bilgileri bos");

		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
				"Spotify bilgileri bos");

		return ESP_FAIL;
	}

	ESP_LOGI("SPOTIFY", "Spotify bilgileri NVS'ye kaydediliyor...");

	esp_err_t err = save_spotify_credentials(client_id, refresh_token);

	if (err != ESP_OK) {
		ESP_LOGE("SPOTIFY", "Spotify bilgileri kaydedilemedi: %s",
				esp_err_to_name(err));

		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
				"Kaydetme basarisiz");

		return ESP_FAIL;
	}

	ESP_LOGI("SPOTIFY", "Spotify bilgileri basariyla kaydedildi");

	static const char response[] = "<!DOCTYPE html>"
			"<html>"
			"<head>"
			"<meta charset='UTF-8'>"
			"<meta name='viewport' "
			"content='width=device-width,initial-scale=1'>"
			"</head>"

			"<body style='"
			"background:#111;"
			"color:white;"
			"font-family:Arial;"
			"text-align:center;"
			"padding:40px;'>"

			"<h1>Spotify Kaydedildi!</h1>"

			"<p>"
			"heart_steel Spotify icin hazir."
			"</p>"

			"<a href='/' style='color:#1DB954;'>"
			"Geri Don"
			"</a>"

			"</body>"
			"</html>";

	httpd_resp_set_type(req, "text/html; charset=utf-8");

	err = httpd_resp_send(req, response, sizeof(response) - 1);

	if (err == ESP_OK) {
		ESP_LOGI("SPOTIFY", "Basari sayfasi gonderildi");
	} else {
		ESP_LOGE("SPOTIFY", "Basari sayfasi gonderilemedi: %s",
				esp_err_to_name(err));
	}

	return err;
}

/* =========================================================
 * HTTP POST /connect
 * ========================================================= */
static esp_err_t connect_handler(httpd_req_t *req) {
	char content[256];

	int total_len = req->content_len;

	if (total_len <= 0 || total_len >= sizeof(content)) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Gecersiz veri");

		return ESP_FAIL;
	}

	/*
	 * Form verisinin tamamini al
	 */
	int received = 0;

	while (received < total_len) {

		int ret = httpd_req_recv(req, content + received, total_len - received);

		if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
			continue;
		}

		if (ret <= 0) {

			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
					"Veri alinamadi");

			return ESP_FAIL;
		}

		received += ret;
	}

	content[received] = '\0';

	/*
	 * SSID ve sifreyi ayir
	 */
	char ssid[33] = { 0 };
	char password[65] = { 0 };

	esp_err_t ssid_result = httpd_query_key_value(content, "ssid", ssid,
			sizeof(ssid));

	esp_err_t password_result = httpd_query_key_value(content, "password",
			password, sizeof(password));

	if (ssid_result != ESP_OK || password_result != ESP_OK) {
		ESP_LOGE("WEB", "Wi-Fi bilgileri ayrilamadi");

		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
				"Wi-Fi bilgileri gecersiz");

		return ESP_FAIL;
	}

	/*
	 * Form URL encoded geldigi icin
	 * SSID ve sifreyi decode et.
	 */
	url_decode(ssid);
	url_decode(password);

	ESP_LOGI("WEB", "Yeni Wi-Fi bilgileri alindi. SSID: %s", ssid);

	ESP_LOGI("WEB", "Sifre alindi. Uzunluk: %d", (int) strlen(password));

	/*
	 * ==========================================
	 * NVS'YE KAYDET
	 * ==========================================
	 */
	nvs_handle_t nvs_handle;

	esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);

	if (err != ESP_OK) {

		ESP_LOGE("NVS", "NVS acilamadi: %s", esp_err_to_name(err));

		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
				"Kayit alani acilamadi");

		return ESP_FAIL;
	}

	/*
	 * SSID kaydet
	 */
	err = nvs_set_str(nvs_handle, "ssid", ssid);

	if (err != ESP_OK) {

		ESP_LOGE("NVS", "SSID kaydedilemedi: %s", esp_err_to_name(err));

		nvs_close(nvs_handle);

		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
				"SSID kaydedilemedi");

		return ESP_FAIL;
	}

	/*
	 * Sifre kaydet
	 */
	err = nvs_set_str(nvs_handle, "password", password);

	if (err != ESP_OK) {

		ESP_LOGE("NVS", "Sifre kaydedilemedi: %s", esp_err_to_name(err));

		nvs_close(nvs_handle);

		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
				"Sifre kaydedilemedi");

		return ESP_FAIL;
	}

	/*
	 * Flash'a kalici olarak yaz.
	 */
	err = nvs_commit(nvs_handle);

	nvs_close(nvs_handle);

	if (err != ESP_OK) {

		ESP_LOGE("NVS", "NVS commit basarisiz: %s", esp_err_to_name(err));

		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
				"Wi-Fi bilgileri kaydedilemedi");

		return ESP_FAIL;
	}

	ESP_LOGI("NVS", "Yeni Wi-Fi bilgileri kalici olarak kaydedildi");

	/*
	 * ==========================================
	 * TARAYICIYA CEVAP
	 * ==========================================
	 *
	 * Restart'i cevap gonderilmeden yapmiyoruz.
	 * Once telefon/PC basari mesajini alsin.
	 */
	static const char response[] = "<!DOCTYPE html>"
			"<html lang='tr'>"

			"<head>"
			"<meta charset='UTF-8'>"
			"<meta name='viewport' "
			"content='width=device-width,initial-scale=1.0'>"
			"<title>heart_steel</title>"
			"</head>"

			"<body style='"
			"margin:0;"
			"background:#121212;"
			"color:white;"
			"font-family:Arial,sans-serif;"
			"display:flex;"
			"align-items:center;"
			"justify-content:center;"
			"min-height:100vh;"
			"text-align:center;"
			"'>"

			"<div>"

			"<h1 style='color:#1DB954;'>"
			"Kaydedildi!"
			"</h1>"

			"<p style='color:#aaaaaa;'>"
			"Wi-Fi bilgileri kaydedildi."
			"</p>"

			"<p style='color:#aaaaaa;'>"
			"heart_steel yeniden baslatiliyor..."
			"</p>"

			"</div>"

			"</body>"
			"</html>";

	httpd_resp_set_type(req, "text/html; charset=utf-8");

	err = httpd_resp_send(req, response, sizeof(response) - 1);

	if (err != ESP_OK) {

		ESP_LOGE("WEB", "Basari sayfasi gonderilemedi: %s",
				esp_err_to_name(err));

		return err;
	}

	ESP_LOGI("WEB", "Basari sayfasi gonderildi");

	ESP_LOGI("SYSTEM", "Yeni Wi-Fi icin ESP32 yeniden baslatiliyor...");

	/*
	 * Tarayicinin HTTP cevabini almasi icin
	 * kisa bir sure bekle.
	 */
	vTaskDelay(pdMS_TO_TICKS(1500));

	/*
	 * ESP32'yi yeniden baslat.
	 *
	 * Acilista app_main() NVS'den yeni
	 * SSID/sifreyi okuyup yeni aga baglanacak.
	 */
	esp_restart();

	return ESP_OK;
}

/* =========================================================
 * HTTP POST /upload-photo
 * ========================================================= */

static esp_err_t photo_upload_handler(httpd_req_t *req)
{
    ESP_LOGI(
        "UPLOAD",
        "Fotograf upload istegi geldi: %u byte",
        (unsigned int) req->content_len
    );

    /*
     * Bos dosya kabul etme.
     */
    if (req->content_len == 0) {

        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Bos fotograf"
        );

        return ESP_FAIL;
    }

    /*
     * Guvenlik siniri.
     *
     * Tarayici tarafinda resmi zaten kucultecegiz.
     * Bu nedenle 2 MB'dan buyuk bir upload
     * normal kabul edilmiyor.
     */
    if (req->content_len > (2 * 1024 * 1024)) {

        httpd_resp_send_err(
            req,
            HTTPD_413_CONTENT_TOO_LARGE,
            "Fotograf cok buyuk"
        );

        return ESP_FAIL;
    }

    if (!sd_card_ready || sd_mutex == NULL) {

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "SD kart hazir degil"
        );

        return ESP_FAIL;
    }

    /*
     * Upload boyunca SD karti kilitle.
     */
    if (xSemaphoreTake(
            sd_mutex,
            portMAX_DELAY
        ) != pdTRUE) {

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "SD kart kilitlenemedi"
        );

        return ESP_FAIL;
    }

    char temp_path[64];

    snprintf(
        temp_path,
        sizeof(temp_path),
        SD_MOUNT_POINT "/.photo-upload.tmp"
    );

    /*
     * Onceki yarim kalmis upload varsa temizle.
     */
    remove(temp_path);

    FILE *file =
        fopen(
            temp_path,
            "wb"
        );

    if (file == NULL) {

        xSemaphoreGive(sd_mutex);

        ESP_LOGE(
            "UPLOAD",
            "Gecici dosya acilamadi"
        );

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "SD dosyasi acilamadi"
        );

        return ESP_FAIL;
    }

    /*
     * HTTP body'sini RAM'e komple almiyoruz.
     * 4 KB parcalar halinde direkt SD karta yaziyoruz.
     */
    char buffer[512];

    size_t remaining =
        req->content_len;

    size_t total_written = 0;

    bool receive_failed = false;
    bool timeout_occurred = false;

    while (remaining > 0) {

        size_t chunk_size =
            remaining > sizeof(buffer)
            ? sizeof(buffer)
            : remaining;

        int received =
            httpd_req_recv(
                req,
                buffer,
                chunk_size
            );

        if (received <= 0) {

            if (received ==
                    HTTPD_SOCK_ERR_TIMEOUT) {

                timeout_occurred = true;
            }

            receive_failed = true;
            break;
        }

        size_t written =
            fwrite(
                buffer,
                1,
                (size_t) received,
                file
            );

        if (written !=
                (size_t) received) {

            ESP_LOGE(
                "UPLOAD",
                "SD yazma hatasi"
            );

            receive_failed = true;
            break;
        }

        total_written += written;
        remaining -= (size_t) received;
    }

    fclose(file);

    /*
     * Upload yarim kaldiysa gecici dosyayi sil.
     */
    if (receive_failed
            || remaining != 0
            || total_written != req->content_len) {

        remove(temp_path);

        xSemaphoreGive(sd_mutex);

        ESP_LOGE(
            "UPLOAD",
            "Upload yarim kaldi"
        );

        if (timeout_occurred) {

            httpd_resp_send_408(req);

        } else {

            httpd_resp_send_err(
                req,
                HTTPD_500_INTERNAL_SERVER_ERROR,
                "Upload basarisiz"
            );
        }

        return ESP_FAIL;
    }

    /*
     * JPEG baslangic ve bitis imzasini kontrol et.
     *
     * JPEG:
     * FF D8 ........ FF D9
     */
    file =
        fopen(
            temp_path,
            "rb"
        );

    if (file == NULL) {

        remove(temp_path);
        xSemaphoreGive(sd_mutex);

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Fotograf kontrol edilemedi"
        );

        return ESP_FAIL;
    }

    uint8_t jpeg_start[2] = { 0 };
    uint8_t jpeg_end[2] = { 0 };

    bool jpeg_valid = false;

    if (fread(
            jpeg_start,
            1,
            2,
            file
        ) == 2) {

        if (fseek(
                file,
                -2,
                SEEK_END
            ) == 0) {

            if (fread(
                    jpeg_end,
                    1,
                    2,
                    file
                ) == 2) {

                jpeg_valid =
                    jpeg_start[0] == 0xFF
                    && jpeg_start[1] == 0xD8
                    && jpeg_end[0] == 0xFF
                    && jpeg_end[1] == 0xD9;
            }
        }
    }

    fclose(file);

    if (!jpeg_valid) {

        remove(temp_path);
        xSemaphoreGive(sd_mutex);

        ESP_LOGW(
            "UPLOAD",
            "Gelen dosya gecerli JPEG degil"
        );

        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Gecerli JPEG degil"
        );

        return ESP_FAIL;
    }

    /*
     * SD karttaki en buyuk photo-N.jpeg
     * numarasini bul.
     */
    int highest_photo_number = 0;

    DIR *dir =
        opendir(
            SD_MOUNT_POINT
        );

    if (dir != NULL) {

        struct dirent *entry;

        while ((entry = readdir(dir))
                != NULL) {

            int number = 0;
            char extra = '\0';

            /*
             * Sadece tam olarak:
             *
             * photo-123.jpeg
             *
             * formatini kabul et.
             */
            if (sscanf(
                    entry->d_name,
                    "photo-%d.jpeg%c",
                    &number,
                    &extra
                ) == 1) {

                if (number >
                        highest_photo_number) {

                    highest_photo_number =
                        number;
                }
            }
        }

        closedir(dir);

    } else {

        remove(temp_path);
        xSemaphoreGive(sd_mutex);

        ESP_LOGE(
            "UPLOAD",
            "SD klasoru okunamadi"
        );

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "SD klasoru okunamadi"
        );

        return ESP_FAIL;
    }

    int new_photo_number =
        highest_photo_number + 1;

    char final_path[64];

    snprintf(
        final_path,
        sizeof(final_path),
        SD_MOUNT_POINT "/photo-%d.jpeg",
        new_photo_number
    );

    /*
     * Gecici dosyayi gercek fotograf adina cevir.
     */
    if (rename(
            temp_path,
            final_path
        ) != 0) {

        remove(temp_path);
        xSemaphoreGive(sd_mutex);

        ESP_LOGE(
            "UPLOAD",
            "Fotograf yeniden adlandirilamadi"
        );

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Fotograf kaydedilemedi"
        );

        return ESP_FAIL;
    }

    /*
     * Slideshow yeni fotografi da gorsun.
     */
    sd_photo_count =
        new_photo_number;

    xSemaphoreGive(sd_mutex);

    ESP_LOGI(
        "UPLOAD",
        "Fotograf kaydedildi: photo-%d.jpeg (%u byte)",
        new_photo_number,
        (unsigned int) total_written
    );

    /*
     * Tarayiciya yeni fotograf numarasini dondur.
     */
    char response[128];

    snprintf(
        response,
        sizeof(response),
        "{\"success\":true,\"photo\":%d}",
        new_photo_number
    );

    httpd_resp_set_type(
        req,
        "application/json"
    );

    return httpd_resp_send(
        req,
        response,
        HTTPD_RESP_USE_STRLEN
    );
}

/* =========================================================
 * START WEB SERVER
 * ========================================================= */

static void start_webserver(void) {
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.stack_size = 8192;
	httpd_handle_t server = NULL;

	ESP_LOGI("WEB", "Web server baslatiliyor...");

	if (httpd_start(&server, &config) == ESP_OK) {
		/* Ana sayfa: GET / */
		httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler =
				root_handler, .user_ctx = NULL };

		ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root));

		/* Wi-Fi formu: POST /connect */
		httpd_uri_t connect = { .uri = "/connect", .method = HTTP_POST,
				.handler = connect_handler, .user_ctx = NULL };

		ESP_ERROR_CHECK(httpd_register_uri_handler(server, &connect));

		/* Spotify formu: POST /spotify */
		httpd_uri_t spotify = { .uri = "/spotify", .method = HTTP_POST,
				.handler = spotify_save_handler, .user_ctx = NULL };

		ESP_ERROR_CHECK(httpd_register_uri_handler(server, &spotify));

		/*
		 * Fotograf upload:
		 * POST /upload-photo
		 */
		httpd_uri_t upload_photo = {
		    .uri = "/upload-photo",
		    .method = HTTP_POST,
		    .handler = photo_upload_handler,
		    .user_ctx = NULL
		};

		ESP_ERROR_CHECK(
		    httpd_register_uri_handler(
		        server,
		        &upload_photo
		    )
		);

		ESP_LOGI("WEB", "Web server baslatildi: http://192.168.4.1");
	} else {
		ESP_LOGE("WEB", "Web server baslatilamadi!");
	}
}

/* =========================================================
 * WIFI EVENTS
 * ========================================================= */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
		int32_t event_id, void *event_data) {
	/*
	 * Bir cihaz heart_steel AP'sine baglandi.
	 */
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
		wifi_event_ap_staconnected_t *event =
				(wifi_event_ap_staconnected_t*) event_data;

		ESP_LOGI(TAG, "heart_steel agina cihaz baglandi. AID=%d", event->aid);
	}

	/*
	 * Bir cihaz heart_steel AP'sinden ayrildi.
	 */
	else if (event_base == WIFI_EVENT
			&& event_id == WIFI_EVENT_STA_DISCONNECTED) {
		wifi_event_sta_disconnected_t *event =
				(wifi_event_sta_disconnected_t*) event_data;
		wifi_connected = false;
		spotify_ui_waiting = true;
		spotify_playback_active = false;

		ESP_LOGW(TAG, "STA: Wi-Fi baglantisi kesildi. Reason: %d",
				event->reason);

		ESP_LOGI(TAG, "Wi-Fi'ye yeniden baglaniliyor...");

		esp_err_t err = esp_wifi_connect();

		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Yeniden baglanti baslatilamadi: %s",
					esp_err_to_name(err));
		}
	}

	/*
	 * ESP32 kayitli Wi-Fi agina baglandi.
	 *
	 * Bu noktada router'a baglandik fakat
	 * henuz IP adresi almamis olabiliriz.
	 */
	else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
		ESP_LOGI(TAG, "STA: Wi-Fi agina baglandi");
	}

	else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		wifi_connected = true;
		ip_event_got_ip_t *event = (ip_event_got_ip_t*) event_data;

		ESP_LOGI(TAG, "================================");

		ESP_LOGI(TAG, "INTERNET BAGLANTISI HAZIR");

		ESP_LOGI(TAG, "ESP32 IP: " IPSTR, IP2STR(&event->ip_info.ip));

		ESP_LOGI(TAG, "================================");

		if (spotify_task_handle == NULL) {
			BaseType_t result = xTaskCreate(spotify_https_task, "spotify_https",
					8192, NULL, 5, &spotify_task_handle);

			if (result == pdPASS) {
				ESP_LOGI("SPOTIFY", "Spotify task olusturuldu");
			} else {
				ESP_LOGE("SPOTIFY", "Spotify task olusturulamadi");

				spotify_task_handle = NULL;
			}
		} else {
			ESP_LOGI("SPOTIFY", "Spotify task zaten calisiyor");
		}
	}
}

/* =========================================================
 * START SOFT AP
 * ========================================================= */

static void wifi_init_softap(void) {
	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());

	/*
	 * Iki network interface olusturuyoruz:
	 *
	 * AP  = heart_steel kurulum agi
	 * STA = Ev Wi-Fi agina baglanmak icin
	 */

	esp_netif_create_default_wifi_ap();
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	/*
	 * Wi-Fi eventlerini dinle
	 */

	ESP_ERROR_CHECK(
			esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
					&wifi_event_handler, NULL, NULL));
	ESP_ERROR_CHECK(
			esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
					&wifi_event_handler, NULL, NULL));

	/*
	 * heart_steel Access Point ayarlari
	 */

	wifi_config_t wifi_config = { .ap = { .ssid = EXAMPLE_ESP_WIFI_SSID,
			.ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID), .channel =
					EXAMPLE_ESP_WIFI_CHANNEL, .password = EXAMPLE_ESP_WIFI_PASS,
			.max_connection = EXAMPLE_MAX_STA_CONN, .authmode =
					WIFI_AUTH_WPA2_PSK, .pmf_cfg = { .required = false, }, }, };

	/*
	 * Sifre bos ise acik ag kullan
	 */

	if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
		wifi_config.ap.authmode = WIFI_AUTH_OPEN;
	}

	/*
	 * AP + STA modu
	 *
	 * AP:
	 * heart_steel agini yayinlar.
	 *
	 * STA:
	 * Daha sonra kayitli ev Wi-Fi'sine
	 * baglanacak.
	 */

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

	/*
	 * Guvenlik:
	 * AP sifresini Serial Monitor'e yazmiyoruz.
	 */

	ESP_LOGI(TAG, "Wi-Fi AP+STA modu baslatildi");

	ESP_LOGI(TAG, "Kurulum SSID: %s", EXAMPLE_ESP_WIFI_SSID);

	ESP_LOGI(TAG, "Kurulum IP: http://192.168.4.1");
}

static void wifi_connect_sta(const char *ssid, const char *password) {
	wifi_config_t sta_config = { 0 };

	/*
	 * NVS'den gelen Wi-Fi bilgilerini
	 * STA konfigurasyonuna kopyala.
	 */

	strncpy((char*) sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);

	strncpy((char*) sta_config.sta.password, password,
			sizeof(sta_config.sta.password) - 1);

	/*
	 * WPA2/WPA3 dahil modern aglarla
	 * uyumlu baglanti ayarlari.
	 */

	sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

	sta_config.sta.pmf_cfg.capable = true;
	sta_config.sta.pmf_cfg.required = false;

	ESP_LOGI(TAG, "Wi-Fi baglantisi hazirlaniyor. SSID: %s", ssid);

	esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "STA konfigurasyonu basarisiz: %s", esp_err_to_name(err));

		return;
	}

	err = esp_wifi_connect();

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Wi-Fi baglantisi baslatilamadi: %s",
				esp_err_to_name(err));

		return;
	}

	ESP_LOGI(TAG, "Wi-Fi baglantisi baslatildi...");
}

typedef struct {
	char track_name[256];
	char artist_name[256];
	char album_name[256];
	char album_art_url[512];

	int progress_ms;
	int duration_ms;
	bool is_playing;

} spotify_now_playing_t;

typedef struct {
	uint8_t *buffer;
	size_t capacity;
	size_t data_len;
	bool overflow;
} album_art_response_t;

static esp_err_t album_art_http_event_handler(esp_http_client_event_t *evt) {
	album_art_response_t *response = (album_art_response_t*) evt->user_data;

	if (evt->event_id == HTTP_EVENT_ON_DATA && response != NULL
			&& evt->data != NULL && evt->data_len > 0) {
		size_t incoming = (size_t) evt->data_len;

		if (response->data_len + incoming > response->capacity) {
			response->overflow = true;

			ESP_LOGE("ALBUM", "Album kapagi buffer'a sigmadi");

			return ESP_FAIL;
		}

		memcpy(response->buffer + response->data_len, evt->data, incoming);

		response->data_len += incoming;
	}

	return ESP_OK;
}

static void download_and_decode_album_art(const char *url) {
	if (url == NULL || url[0] == '\0') {
		return;
	}

	ESP_LOGI("ALBUM", "Album kapagi indiriliyor...");

	/*
	 * Orta boy Spotify album kapagi icin
	 * JPEG indirme buffer'i.
	 *
	 * ESP32-S3'te PSRAM kullaniyoruz.
	 * Boylece INTERNAL RAM'i bosuna doldurmuyoruz.
	 */
	const size_t jpeg_capacity = 128 * 1024;

	uint8_t *jpeg_buffer = heap_caps_malloc(jpeg_capacity,
			MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

	if (jpeg_buffer == NULL) {
		ESP_LOGE("ALBUM", "PSRAM JPEG buffer ayrilamadi");
		return;
	}

	if (jpeg_buffer == NULL) {
		ESP_LOGE("ALBUM", "Internal JPEG buffer ayrilamadi");
		return;
	}

	album_art_response_t response = { .buffer = jpeg_buffer, .capacity =
			jpeg_capacity, .data_len = 0, .overflow = false };

			esp_http_client_config_t config = {
			    .url = url,
			    .method = HTTP_METHOD_GET,
			    .crt_bundle_attach = esp_crt_bundle_attach,
			    .timeout_ms = 15000,
			    .event_handler = album_art_http_event_handler,
			    .user_data = &response,

			    .keep_alive_enable = true,
			    .keep_alive_idle = 30,
			    .keep_alive_interval = 10,
			    .keep_alive_count = 3,
			};

			if (album_http_client == NULL) {

			    album_http_client =
			        esp_http_client_init(&config);

			    if (album_http_client == NULL) {
			        ESP_LOGE(
			            "ALBUM",
			            "Album HTTP client olusturulamadi"
			        );

			        free(jpeg_buffer);
			        return;
			    }

			} else {

			    esp_err_t set_url_err =
			        esp_http_client_set_url(
			            album_http_client,
			            url
			        );

			    if (set_url_err != ESP_OK) {
			        ESP_LOGE(
			            "ALBUM",
			            "Album URL ayarlanamadi: %s",
			            esp_err_to_name(set_url_err)
			        );

			        free(jpeg_buffer);
			        return;
			    }

			    esp_http_client_set_user_data(
			        album_http_client,
			        &response
			    );
			}

			esp_err_t err =
			    esp_http_client_perform(
			        album_http_client
			    );

			int status_code =
			    esp_http_client_get_status_code(
			        album_http_client
			    );

			esp_http_client_set_user_data(
			    album_http_client,
			    NULL
			);

	if (err != ESP_OK || status_code != 200 || response.data_len == 0
			|| response.overflow) {
		ESP_LOGE("ALBUM", "Kapak indirilemedi. HTTP=%d err=%s", status_code,
				esp_err_to_name(err));

		free(jpeg_buffer);
		return;
	}

	ESP_LOGI("ALBUM", "JPEG alindi: %d byte", (int) response.data_len);

	/*
	 * JPEG bilgisini oku.
	 */
	esp_jpeg_image_cfg_t jpeg_cfg = { .indata = jpeg_buffer, .indata_size =
			response.data_len,

	.out_format = JPEG_IMAGE_FORMAT_RGB565,

	.out_scale = JPEG_IMAGE_SCALE_1_2,

	.flags = { .swap_color_bytes = 0 },

	.advanced = { .working_buffer = jpeg_work_buffer, .working_buffer_size =
			sizeof(jpeg_work_buffer) } };

	esp_jpeg_image_output_t image_info = { 0 };

	err = esp_jpeg_get_image_info(&jpeg_cfg, &image_info);

	if (err != ESP_OK) {
		ESP_LOGE("ALBUM", "JPEG bilgisi okunamadi: %s", esp_err_to_name(err));

		free(jpeg_buffer);
		return;
	}

	ESP_LOGI("ALBUM", "Kapak boyutu: %dx%d", image_info.width,
			image_info.height);

	size_t pixel_buffer_size = image_info.output_len;
	/*
	 * 64x64 RGB565 = sadece 8192 byte.
	 *
	 * BUNU DA INTERNAL RAM'E ALIYORUZ.
	 * Böylece LVGL album resmi icin de
	 * PSRAM okumayacak.
	 */
	uint8_t *pixel_buffer = heap_caps_malloc(pixel_buffer_size,
			MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

	if (pixel_buffer == NULL) {
		ESP_LOGE("ALBUM", "PSRAM RGB565 buffer ayrilamadi");

		free(jpeg_buffer);
		return;
	}

	jpeg_cfg.outbuf = pixel_buffer;

	jpeg_cfg.outbuf_size = pixel_buffer_size;

	esp_jpeg_image_output_t decoded = { 0 };

	err = esp_jpeg_decode(&jpeg_cfg, &decoded);

	/*
	 * JPEG artik gerekmiyor.
	 */
	free(jpeg_buffer);

	if (err != ESP_OK) {
		ESP_LOGE("ALBUM", "JPEG decode basarisiz: %s", esp_err_to_name(err));

		free(pixel_buffer);
		return;
	}

	ESP_LOGI("ALBUM", "Kapak PSRAM'de decode edildi: %dx%d", decoded.width,
			decoded.height);

	/*
	 * Yeni resmi LVGL task'ina teslim et.
	 */
	portENTER_CRITICAL(&cover_mux);

	uint8_t *old_pending = pending_cover_pixels;

	pending_cover_pixels = pixel_buffer;

	pending_cover_width = decoded.width;

	pending_cover_height = decoded.height;

	portEXIT_CRITICAL(&cover_mux);

	if (old_pending != NULL) {
		free(old_pending);
	}

	ESP_LOGI("ALBUM", "Kapak LVGL icin hazir");
}

static spotify_now_playing_t now_playing = { 0 };

static void spotify_get_currently_playing(const char *access_token) {
	ESP_LOGI("SPOTIFY", "Currently Playing isteniyor...");

	const size_t response_buffer_size = 16384;

	char *response_buffer = calloc(response_buffer_size, 1);

	if (response_buffer == NULL) {
		ESP_LOGE("SPOTIFY", "Currently Playing response buffer ayrilamadi");
		return;
	}

	spotify_http_response_t response = { .buffer = response_buffer,
			.buffer_size = response_buffer_size, .data_len = 0 };

			if (spotify_current_client == NULL) {

			    esp_http_client_config_t config = {
			        .url =
			            "https://api.spotify.com/v1/me/player/currently-playing",
			        .method = HTTP_METHOD_GET,
			        .crt_bundle_attach = esp_crt_bundle_attach,
			        .timeout_ms = 15000,
			        .event_handler = spotify_http_event_handler,
			        .user_data = NULL,

			        .keep_alive_enable = true,
			        .keep_alive_idle = 30,
			        .keep_alive_interval = 10,
			        .keep_alive_count = 3,
			    };

			    spotify_current_client =
			        esp_http_client_init(&config);

			    if (spotify_current_client == NULL) {

			        ESP_LOGE(
			            "SPOTIFY",
			            "Currently Playing HTTP client olusturulamadi"
			        );

			        free(response_buffer);
			        return;
			    }
			}

			esp_http_client_set_user_data(
			    spotify_current_client,
			    &response
			);

	/*
	 * Authorization:
	 * Bearer <access_token>
	 */
	size_t auth_len = strlen(access_token) + 8;

	char *auth_header = malloc(auth_len);

	if (auth_header == NULL) {
	    ESP_LOGE(
	        "SPOTIFY",
	        "Authorization header icin RAM ayrilamadi"
	    );

	    esp_http_client_set_user_data(
	        spotify_current_client,
	        NULL
	    );

	    free(response_buffer);
	    return;
	}

	snprintf(auth_header, auth_len, "Bearer %s", access_token);

	esp_err_t err = esp_http_client_set_header(
	    spotify_current_client,
	    "Authorization",
	    auth_header
	);

	if (err != ESP_OK) {
	    ESP_LOGE(
	        "SPOTIFY",
	        "Authorization header ayarlanamadi: %s",
	        esp_err_to_name(err)
	    );

	    memset(auth_header, 0, auth_len);
	    free(auth_header);

	    esp_http_client_set_user_data(
	        spotify_current_client,
	        NULL
	    );

	    free(response_buffer);
	    return;
	}

	/*
	 * Spotify istegini yap
	 */
	 err =
	     esp_http_client_perform(
	         spotify_current_client
	     );

	 int status_code =
	     esp_http_client_get_status_code(
	         spotify_current_client
	     );

	ESP_LOGI("SPOTIFY", "Currently Playing HTTP Status: %d", status_code);

	/*
	 * HTTP client'i temizlemeden once
	 * Authorization header'ini client'tan ayir.
	 */
	 esp_http_client_delete_header(
	     spotify_current_client,
	     "Authorization"
	 );

	/*
	 * Token kopyasini RAM'den temizle.
	 */
	memset(auth_header, 0, auth_len);

	free(auth_header);

	esp_http_client_set_user_data(
	    spotify_current_client,
	    NULL
	);

	if (err != ESP_OK) {
		ESP_LOGE("SPOTIFY", "Currently Playing istegi basarisiz: %s",
				esp_err_to_name(err));

		memset(response_buffer, 0, response_buffer_size);

		free(response_buffer);
		return;
	}

	/*
	 * 204 = aktif playback yok
	 */
	 if (status_code == 204 || response.data_len == 0) {

	     ESP_LOGI(
	         "SPOTIFY",
	         "Su anda aktif bir Spotify oynatimi yok"
	     );

	     spotify_playback_active = false;

	     free(response_buffer);
	     return;
	 }

	if (status_code != 200) {
		ESP_LOGE("SPOTIFY", "Currently Playing beklenmeyen HTTP status: %d",
				status_code);

		memset(response_buffer, 0, response_buffer_size);

		free(response_buffer);
		return;
	}

	ESP_LOGI("SPOTIFY", "Currently Playing JSON alindi: %d byte",
			(int) response.data_len);

	/*
	 * ==========================================
	 * JSON PARSE
	 * ==========================================
	 */

	cJSON *root = cJSON_Parse(response_buffer);

	if (root == NULL) {
		ESP_LOGE("SPOTIFY", "Currently Playing JSON parse edilemedi");

		memset(response_buffer, 0, response_buffer_size);

		free(response_buffer);
		return;
	}

	cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "item");

	cJSON *is_playing_json = cJSON_GetObjectItemCaseSensitive(root,
			"is_playing");

	cJSON *progress_json = cJSON_GetObjectItemCaseSensitive(root,
			"progress_ms");

	if (!cJSON_IsObject(item)) {
		ESP_LOGW("SPOTIFY", "Currently Playing icinde item bulunamadi");

		cJSON_Delete(root);

		memset(response_buffer, 0, response_buffer_size);

		free(response_buffer);
		return;
	}

	cJSON *track_name_json = cJSON_GetObjectItemCaseSensitive(item, "name");

	cJSON *duration_json = cJSON_GetObjectItemCaseSensitive(item,
			"duration_ms");

	cJSON *artists = cJSON_GetObjectItemCaseSensitive(item, "artists");

	cJSON *album = cJSON_GetObjectItemCaseSensitive(item, "album");

	/*
	 * Varsayilan degerler
	 */
	const char *track_name = "Bilinmiyor";

	const char *artist_name = "Bilinmiyor";

	const char *album_name = "Bilinmiyor";

	const char *album_art_url = "";

	int progress_ms = 0;
	int duration_ms = 0;
	bool is_playing = false;

	/*
	 * ==========================================
	 * SARKI
	 * ==========================================
	 */

	if (cJSON_IsString(track_name_json) && track_name_json->valuestring != NULL) {
		track_name = track_name_json->valuestring;
	}

	/*
	 * ==========================================
	 * ILERLEME / SURE / PLAY STATE
	 * ==========================================
	 */

	if (cJSON_IsNumber(progress_json)) {
		progress_ms = progress_json->valueint;
	}

	if (cJSON_IsNumber(duration_json)) {
		duration_ms = duration_json->valueint;
	}

	if (cJSON_IsBool(is_playing_json)) {
		is_playing = cJSON_IsTrue(is_playing_json);
	}

	/*
	 * ==========================================
	 * ILK SANATCI
	 * ==========================================
	 */

	if (cJSON_IsArray(artists) && cJSON_GetArraySize(artists) > 0) {
		cJSON *first_artist = cJSON_GetArrayItem(artists, 0);

		if (cJSON_IsObject(first_artist)) {

			cJSON *artist_name_json = cJSON_GetObjectItemCaseSensitive(
					first_artist, "name");

			if (cJSON_IsString(artist_name_json) &&
			artist_name_json->valuestring != NULL) {
				artist_name = artist_name_json->valuestring;
			}
		}
	}

	/*
	 * ==========================================
	 * ALBUM ADI + ALBUM KAPAGI
	 * ==========================================
	 *
	 * Spotify images dizisinde birden fazla
	 * boyutta kapak verebilir.
	 *
	 * Burada ilk/buyuk resmi DEGIL,
	 * dizideki EN KUCUK resmi seciyoruz.
	 *
	 * Amac:
	 * - daha az JPEG verisi
	 * - daha az PSRAM kullanimi
	 * - daha hizli JPEG decode
	 * - RGB LCD'ye daha az yuk bindirmek
	 */

	if (cJSON_IsObject(album)) {

		/*
		 * Album adi
		 */
		cJSON *album_name_json = cJSON_GetObjectItemCaseSensitive(album,
				"name");

		if (cJSON_IsString(album_name_json) &&
		album_name_json->valuestring != NULL) {
			album_name = album_name_json->valuestring;
		}

		/*
		 * Album resimleri
		 */
		cJSON *images = cJSON_GetObjectItemCaseSensitive(album, "images");

		if (cJSON_IsArray(images)) {

			int image_count = cJSON_GetArraySize(images);

			if (image_count > 0) {

				/*
				 * Son resmi al.
				 * Spotify genellikle buyukten
				 * kucuge siralar.
				 */
				/*
				 * Spotify kapaklari buyukten kucuge siralar.
				 *
				 * Genellikle:
				 * 0 -> buyuk
				 * 1 -> orta (~300x300)
				 * 2 -> kucuk (~64x64)
				 *
				 * Ekranimiz 116x116 oldugu icin
				 * orta boy resmi kullanmak kalite/RAM
				 * acisindan en mantikli secim.
				 */
				int selected_index = (image_count >= 2) ? 1 : 0;

				cJSON *selected_image = cJSON_GetArrayItem(images,
						selected_index);

				if (cJSON_IsObject(selected_image)) {

					cJSON *url_json = cJSON_GetObjectItemCaseSensitive(
							selected_image, "url");

					if (cJSON_IsString(url_json) &&
					url_json->valuestring != NULL) {
						album_art_url = url_json->valuestring;
					}

					/*
					 * Secilen resmin boyutunu
					 * Monitor'da gorelim.
					 */
					cJSON *width_json = cJSON_GetObjectItemCaseSensitive(
							selected_image, "width");

					cJSON *height_json = cJSON_GetObjectItemCaseSensitive(
							selected_image, "height");

					if (cJSON_IsNumber(width_json)
							&& cJSON_IsNumber(height_json)) {
						ESP_LOGI("SPOTIFY", "Secilen album kapagi: %dx%d",
								width_json->valueint, height_json->valueint);
					}
				}
			}
		}
	}

	/*
	 * ==========================================
	 * NOW PLAYING VERILERINI RAM'E KOPYALA
	 * ==========================================
	 */

	snprintf(now_playing.track_name, sizeof(now_playing.track_name), "%s",
			track_name);

	snprintf(now_playing.artist_name, sizeof(now_playing.artist_name), "%s",
			artist_name);

	snprintf(now_playing.album_name, sizeof(now_playing.album_name), "%s",
			album_name);

	snprintf(now_playing.album_art_url, sizeof(now_playing.album_art_url), "%s",
			album_art_url);

	/*
	 * ==========================================
	 * YENI ALBUM KAPAGI VAR MI?
	 * ==========================================
	 *
	 * URL degismediyse tekrar indirmiyoruz.
	 */

	static char last_album_art_url[512] = "";

	if (now_playing.album_art_url[0] != '\0'
			&& strcmp(last_album_art_url, now_playing.album_art_url) != 0) {
		snprintf(last_album_art_url, sizeof(last_album_art_url), "%s",
				now_playing.album_art_url);

		ESP_LOGI("ALBUM", "Yeni album kapagi tespit edildi");

		download_and_decode_album_art(now_playing.album_art_url);
	}

	/*
	 * Progress bilgilerini kaydet
	 */
	now_playing.progress_ms = progress_ms;

	now_playing.duration_ms = duration_ms;

	now_playing.is_playing = is_playing;

	/*
	 * Spotify'dan gecerli Now Playing verisi geldi.
	 * Artik bekleme ekranindan cikabiliriz.
	 */
	spotify_ui_waiting = false;
	spotify_playback_active = true;

	/*
	 * ==========================================
	 * LOG
	 * ==========================================
	 */

	ESP_LOGI("SPOTIFY", "NOW_PLAYING RAM -> track='%s' artist='%s'",
			now_playing.track_name, now_playing.artist_name);

	ESP_LOGI("SPOTIFY", "================================");

	ESP_LOGI("SPOTIFY", "SARKI: %s", track_name);

	ESP_LOGI("SPOTIFY", "SANATCI: %s", artist_name);

	ESP_LOGI("SPOTIFY", "ALBUM: %s", album_name);

	ESP_LOGI("SPOTIFY", "DURUM: %s", is_playing ? "CALIYOR" : "DURAKLATILDI");

	ESP_LOGI("SPOTIFY", "ILERLEME: %d / %d ms", progress_ms, duration_ms);

	if (strlen(album_art_url) > 0) {
		ESP_LOGI("SPOTIFY", "ALBUM KAPAGI URL'SI ALINDI");
	}

	ESP_LOGI("SPOTIFY", "================================");

	/*
	 * ==========================================
	 * TEMIZLIK
	 * ==========================================
	 */

	cJSON_Delete(root);

	memset(response_buffer, 0, response_buffer_size);

	free(response_buffer);
}

static bool spotify_request_access_token(const char *client_id,
		const char *refresh_token, char *access_token, size_t access_token_size,
		int *expires_in) {
	ESP_LOGI("SPOTIFY", "Access token isteniyor...");

	if (client_id == NULL || refresh_token == NULL || access_token == NULL
			|| access_token_size == 0|| expires_in == NULL) {
		ESP_LOGE("SPOTIFY", "Token fonksiyonuna gecersiz parametre");
		return false;
	}

	access_token[0] = '\0';
	*expires_in = 0;

	char *encoded_client_id = url_encode(client_id);
	char *encoded_refresh_token = url_encode(refresh_token);

	if (encoded_client_id == NULL || encoded_refresh_token == NULL) {
		ESP_LOGE("SPOTIFY", "URL encode icin bellek ayrilamadi");

		free(encoded_client_id);

		if (encoded_refresh_token != NULL) {
			memset(encoded_refresh_token, 0, strlen(encoded_refresh_token));
			free(encoded_refresh_token);
		}

		return false;
	}

	const char *format = "grant_type=refresh_token"
			"&refresh_token=%s"
			"&client_id=%s";

	int body_len = snprintf(NULL, 0, format, encoded_refresh_token,
			encoded_client_id);

	if (body_len <= 0) {
		ESP_LOGE("SPOTIFY", "POST body boyutu hesaplanamadi");

		memset(encoded_refresh_token, 0, strlen(encoded_refresh_token));

		free(encoded_refresh_token);
		free(encoded_client_id);

		return false;
	}

	char *post_data = malloc((size_t) body_len + 1);

	if (post_data == NULL) {
		ESP_LOGE("SPOTIFY", "POST body icin RAM ayrilamadi");

		memset(encoded_refresh_token, 0, strlen(encoded_refresh_token));

		free(encoded_refresh_token);
		free(encoded_client_id);

		return false;
	}

	snprintf(post_data, (size_t) body_len + 1, format, encoded_refresh_token,
			encoded_client_id);

	memset(encoded_refresh_token, 0, strlen(encoded_refresh_token));

	free(encoded_refresh_token);
	free(encoded_client_id);

	const size_t response_buffer_size = 4096;

	char *response_buffer = calloc(response_buffer_size, 1);

	if (response_buffer == NULL) {
		ESP_LOGE("SPOTIFY", "Response buffer icin RAM ayrilamadi");

		memset(post_data, 0, (size_t) body_len);
		free(post_data);

		return false;
	}

	spotify_http_response_t response = { .buffer = response_buffer,
			.buffer_size = response_buffer_size, .data_len = 0 };

	esp_http_client_config_t config = { .url =
			"https://accounts.spotify.com/api/token",
			.method = HTTP_METHOD_POST, .crt_bundle_attach =
					esp_crt_bundle_attach, .timeout_ms = 15000, .event_handler =
					spotify_http_event_handler, .user_data = &response, };

	esp_http_client_handle_t client = esp_http_client_init(&config);

	if (client == NULL) {
		ESP_LOGE("SPOTIFY", "HTTP client olusturulamadi");

		memset(post_data, 0, (size_t) body_len);
		memset(response_buffer, 0, response_buffer_size);

		free(post_data);
		free(response_buffer);

		return false;
	}

	esp_err_t err = esp_http_client_set_header(client, "Content-Type",
			"application/x-www-form-urlencoded");

	if (err == ESP_OK) {
		err = esp_http_client_set_post_field(client, post_data, body_len);
	}

	if (err == ESP_OK) {
		err = esp_http_client_perform(client);
	}

	int status_code = esp_http_client_get_status_code(client);

	ESP_LOGI("SPOTIFY", "Token endpoint HTTP Status: %d", status_code);

	/*
	 * HTTP client'i temizlemeden once
	 * POST body ve header referanslarini ayir.
	 */
	esp_http_client_set_post_field(client,
	NULL, 0);

	esp_http_client_delete_all_headers(client);

	esp_http_client_cleanup(client);

	memset(post_data, 0, (size_t) body_len);

	free(post_data);

	if (err != ESP_OK || status_code != 200 || response.data_len == 0) {
		ESP_LOGE("SPOTIFY", "Access token cevabi alinamadi: %s",
				esp_err_to_name(err));

		memset(response_buffer, 0, response_buffer_size);
		free(response_buffer);

		return false;
	}

	cJSON *json = cJSON_Parse(response_buffer);

	if (json == NULL) {
		ESP_LOGE("SPOTIFY", "Token JSON parse edilemedi");

		memset(response_buffer, 0, response_buffer_size);
		free(response_buffer);

		return false;
	}

	cJSON *access_token_json = cJSON_GetObjectItemCaseSensitive(json,
			"access_token");

	cJSON *expires_in_json = cJSON_GetObjectItemCaseSensitive(json,
			"expires_in");

	if (!cJSON_IsString(access_token_json) ||
	access_token_json->valuestring == NULL) {
		ESP_LOGE("SPOTIFY", "JSON icinde access_token bulunamadi");

		cJSON_Delete(json);

		memset(response_buffer, 0, response_buffer_size);
		free(response_buffer);

		return false;
	}

	size_t token_len = strlen(access_token_json->valuestring);

	if (token_len + 1 > access_token_size) {
		ESP_LOGE("SPOTIFY", "Access token buffer'a sigmiyor");

		cJSON_Delete(json);

		memset(response_buffer, 0, response_buffer_size);
		free(response_buffer);

		return false;
	}

	memcpy(access_token, access_token_json->valuestring, token_len + 1);

	if (cJSON_IsNumber(expires_in_json)) {
		*expires_in = expires_in_json->valueint;
	}

	ESP_LOGI("SPOTIFY", "ACCESS TOKEN ALINDI!");

	ESP_LOGI("SPOTIFY", "Access Token uzunlugu: %d", (int) token_len);

	ESP_LOGI("SPOTIFY", "Gecerlilik suresi: %d saniye", *expires_in);

	cJSON_Delete(json);

	memset(response_buffer, 0, response_buffer_size);

	free(response_buffer);

	return true;
}

static void spotify_https_task(void *pvParameters) {
	ESP_LOGI("SPOTIFY", "Spotify canli takip task'i baslatildi");

	static char client_id[128];
	static char refresh_token[1024];
	static char access_token[2048];

	memset(client_id, 0, sizeof(client_id));
	memset(refresh_token, 0, sizeof(refresh_token));
	memset(access_token, 0, sizeof(access_token));

	bool spotify_saved = load_spotify_credentials(client_id, sizeof(client_id),
			refresh_token, sizeof(refresh_token));

	if (!spotify_saved) {
		ESP_LOGW("SPOTIFY", "Kayitli Spotify bilgisi yok");

		vTaskDelete(NULL);
		return;
	}

	while (1) {
		int expires_in = 0;

		/*
		 * Yeni access token al.
		 */
		bool token_ok = spotify_request_access_token(client_id, refresh_token,
				access_token, sizeof(access_token), &expires_in);

		if (!token_ok) {
			ESP_LOGE("SPOTIFY", "Access token alinamadi");

			/*
			 * Spotify veya internet gecici olarak
			 * sorunlu olabilir.
			 *
			 * 30 saniye sonra tekrar dene.
			 */
			vTaskDelay(pdMS_TO_TICKS(30000));

			continue;
		}

		ESP_LOGI("SPOTIFY", "================================");

		ESP_LOGI("SPOTIFY", "CANLI SPOTIFY TAKIBI BASLADI");

		ESP_LOGI("SPOTIFY", "Token gecerlilik suresi: %d saniye", expires_in);

		ESP_LOGI("SPOTIFY", "================================");

		/*
		 * Tokenin gercek alindigi zamani tutuyoruz.
		 */
		TickType_t token_start_tick = xTaskGetTickCount();

		/*
		 * Spotify expires_in degeri gecersiz/bos
		 * gelirse guvenli varsayim.
		 */
		if (expires_in <= 0) {
			expires_in = 3600;
		}

		/*
		 * Tokeni bitmesine 5 dakika kala
		 * yenileyecegiz.
		 *
		 * Ornegin:
		 *
		 * expires_in = 3600
		 * refresh_after = 3300 saniye
		 */
		int refresh_after_seconds = expires_in - 300;

		/*
		 * Cok kisa omurlu bir token gelirse
		 * negatif/sacma sure olusmasin.
		 */
		if (refresh_after_seconds < 60) {
			refresh_after_seconds = expires_in / 2;
		}

		while (1) {
			/*
			 * Spotify'dan guncel sarkiyi al.
			 */
			spotify_get_currently_playing(access_token);

			TickType_t now_tick = xTaskGetTickCount();

			uint32_t elapsed_seconds = (uint32_t) ((now_tick - token_start_tick)
					/ configTICK_RATE_HZ);

			/*
			 * Token yenileme zamani geldiyse
			 * ic donguden cik.
			 *
			 * Dis dongu yeni access token alacak.
			 */
			if (elapsed_seconds >= (uint32_t) refresh_after_seconds) {
				ESP_LOGI("SPOTIFY", "Access token yenileme zamani geldi");

				break;
			}

			/*
			 * Currently Playing:
			 * 1 saniyede bir.
			 */
			vTaskDelay(pdMS_TO_TICKS(1000));
		}

		/*
		 * Eski access token artik kullanilmayacak.
		 * RAM'den temizle.
		 */
		memset(access_token, 0, sizeof(access_token));

		ESP_LOGI("SPOTIFY", "Eski access token RAM'den temizlendi");

		/*
		 * while(1)'in basina donuyoruz.
		 * Refresh token ile yeni access token alinacak.
		 */
	}
}

static void display_init(void) {
	ESP_LOGI("DISPLAY", "RGB LCD baslatiliyor...");

	esp_lcd_rgb_panel_config_t panel_config = { .clk_src = LCD_CLK_SRC_DEFAULT,

	.timings = { .pclk_hz = 4000000,

	.h_res = 480, .v_res = 272,

	.hsync_pulse_width = 4, .hsync_back_porch = 43, .hsync_front_porch = 8,

	.vsync_pulse_width = 4, .vsync_back_porch = 12, .vsync_front_porch = 8,

	.flags = { .pclk_active_neg = true, }, },

	.data_width = 16, .num_fbs = 1,

	.hsync_gpio_num = GPIO_NUM_39, .vsync_gpio_num = GPIO_NUM_41, .de_gpio_num =
			GPIO_NUM_40, .pclk_gpio_num = GPIO_NUM_42,

	.data_gpio_nums = { GPIO_NUM_8,   // B0
			GPIO_NUM_3,   // B1
			GPIO_NUM_46,  // B2
			GPIO_NUM_9,   // B3
			GPIO_NUM_1,   // B4

			GPIO_NUM_5,   // G0
			GPIO_NUM_6,   // G1
			GPIO_NUM_7,   // G2
			GPIO_NUM_15,  // G3
			GPIO_NUM_16,  // G4
			GPIO_NUM_4,   // G5

			GPIO_NUM_45,  // R0
			GPIO_NUM_48,  // R1
			GPIO_NUM_47,  // R2
			GPIO_NUM_21,  // R3
			GPIO_NUM_14   // R4
			},

	.disp_gpio_num = -1, .flags = { .fb_in_psram = true, },

	};

	ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &lcd_panel_handle));

	ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel_handle));

	ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel_handle));

	ESP_LOGI("DISPLAY", "RGB LCD baslatildi");
}

static uint32_t lvgl_tick_get(void) {
	return (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void spotify_ui_create(void) {
	lv_obj_t *screen = lv_screen_active();

	/* =====================================================
	 * ANA EKRAN
	 * ===================================================== */

	lv_obj_set_style_bg_color(screen, lv_color_hex(0x090909), 0);

	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

	/* =====================================================
	 * ANA KART
	 * ===================================================== */

	lv_obj_t *card = lv_obj_create(screen);

	lv_obj_set_size(card, 450, 242);
	lv_obj_center(card);

	lv_obj_set_style_bg_color(card, lv_color_hex(0x151515), 0);

	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);

	lv_obj_set_style_radius(card, 18, 0);

	lv_obj_set_style_border_width(card, 1, 0);

	lv_obj_set_style_border_color(card, lv_color_hex(0x303030), 0);

	lv_obj_set_style_pad_all(card, 0, 0);

	lv_obj_set_scrollable(card, false);

	/* =====================================================
	 * WIFI DURUM IKONU
	 * ===================================================== */

	wifi_icon = lv_label_create(card);

	lv_label_set_text(wifi_icon,
	LV_SYMBOL_WIFI);

	/* Baslangicta baglanti yok -> gri */
	lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0x666666), 0);

	/* Kartin sag ust kosesi */
	lv_obj_align(wifi_icon, LV_ALIGN_TOP_RIGHT, -18, 16);

	/* =====================================================
	 * SOL ALBUM KAPAGI PLACEHOLDER
	 * ===================================================== */

	lv_obj_t *cover = lv_obj_create(card);

	lv_obj_set_size(cover, 126, 126);
	lv_obj_set_pos(cover, 18, 18);

	lv_obj_set_style_radius(cover, 14, 0);

	lv_obj_set_style_bg_color(cover, lv_color_hex(0x1DB954), 0);

	lv_obj_set_style_border_width(cover, 0, 0);

	lv_obj_set_scrollable(cover, false);

	cover_icon = lv_label_create(cover);

	lv_label_set_text(cover_icon,
	LV_SYMBOL_AUDIO);

	lv_obj_set_style_text_color(cover_icon, lv_color_hex(0xFFFFFF), 0);

	lv_obj_center(cover_icon);

	/*
	 * Gercek album kapagi burada gorunecek.
	 * Baslangicta kaynak yok.
	 */
	cover_image = lv_image_create(cover);

	lv_obj_center(cover_image);

	/* =====================================================
	 * HEART_STEEL
	 * ===================================================== */

	lv_obj_t *brand_label = lv_label_create(card);

	lv_label_set_text(brand_label, "heart_steel");

	lv_obj_set_style_text_color(brand_label, lv_color_hex(0x1DB954), 0);

	lv_obj_set_pos(brand_label, 163, 18);

	/* =====================================================
	 * SARKI
	 * ===================================================== */

	track_label = lv_label_create(card);
	lv_obj_set_style_text_font(track_label, &spotify_font_tr, 0);
	lv_label_set_text(track_label, "Spotify bekleniyor...");

	lv_obj_set_width(track_label, 265);

	lv_label_set_long_mode(track_label,
	LV_LABEL_LONG_SCROLL_CIRCULAR);

	lv_obj_set_style_text_color(track_label, lv_color_hex(0xFFFFFF), 0);

	lv_obj_set_pos(track_label, 163, 52);

	/* =====================================================
	 * SANATCI
	 * ===================================================== */

	artist_label = lv_label_create(card);
	lv_obj_set_style_text_font(artist_label, &spotify_font_tr, 0);
	lv_label_set_text(artist_label, "-");

	lv_obj_set_width(artist_label, 265);

	lv_label_set_long_mode(artist_label,
	LV_LABEL_LONG_SCROLL_CIRCULAR);

	lv_obj_set_style_text_color(artist_label, lv_color_hex(0xC5C5C5), 0);

	lv_obj_set_pos(artist_label, 163, 79);

	/* =====================================================
	 * ALBUM
	 * ===================================================== */

	album_label = lv_label_create(card);
	lv_obj_set_style_text_font(album_label, &spotify_font_tr, 0);
	lv_label_set_text(album_label, "-");

	lv_obj_set_width(album_label, 265);

	lv_label_set_long_mode(album_label,
	LV_LABEL_LONG_SCROLL_CIRCULAR);

	lv_obj_set_style_text_color(album_label, lv_color_hex(0x777777), 0);

	lv_obj_set_pos(album_label, 163, 106);

	/* =====================================================
	 * CALIYOR / DURAKLATILDI
	 * ===================================================== */

	status_label = lv_label_create(card);

	lv_label_set_text(status_label, "BEKLIYOR");

	lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFFFF), 0);

	lv_obj_set_style_bg_color(status_label, lv_color_hex(0x444444), 0);

	lv_obj_set_style_bg_opa(status_label, LV_OPA_COVER, 0);

	lv_obj_set_style_radius(status_label, 8, 0);

	lv_obj_set_style_pad_left(status_label, 8, 0);

	lv_obj_set_style_pad_right(status_label, 8, 0);

	lv_obj_set_style_pad_top(status_label, 4, 0);

	lv_obj_set_style_pad_bottom(status_label, 4, 0);

	lv_obj_set_pos(status_label, 163, 130);

	/* =====================================================
	 * PROGRESS BAR
	 * ===================================================== */

	progress_bar = lv_bar_create(card);

	lv_obj_set_size(progress_bar, 414, 10);

	lv_obj_set_pos(progress_bar, 18, 182);

	lv_bar_set_range(progress_bar, 0, 1000);

	lv_bar_set_value(progress_bar, 0,
	LV_ANIM_OFF);

	/* BAR ARKA PLANI */

	lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x333333),
			LV_PART_MAIN);

	lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, LV_PART_MAIN);

	lv_obj_set_style_radius(progress_bar, 8, LV_PART_MAIN);

	/* BAR YESIL KISMI */

	lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x1DB954),
			LV_PART_INDICATOR);

	lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);

	lv_obj_set_style_radius(progress_bar, 8, LV_PART_INDICATOR);

	/* =====================================================
	 * GECEN SURE
	 * ===================================================== */

	time_left_label = lv_label_create(card);

	lv_label_set_text(time_left_label, "0:00");

	lv_obj_set_style_text_color(time_left_label, lv_color_hex(0x909090), 0);

	lv_obj_set_pos(time_left_label, 18, 203);

	/* =====================================================
	 * TOPLAM SURE
	 * ===================================================== */

	time_right_label = lv_label_create(card);

	lv_label_set_text(time_right_label, "0:00");

	lv_obj_set_style_text_color(time_right_label, lv_color_hex(0x909090), 0);

	lv_obj_align(time_right_label, LV_ALIGN_TOP_RIGHT, -18, 203);

	/*
	 * =====================================================
	 * SD FOTOGRAF KATMANI
	 * =====================================================
	 *
	 * Spotify ekrani bunun altinda kalacak.
	 * Fotograf modu aktif oldugunda bu katmani gosterecegiz.
	 */

	photo_layer = lv_obj_create(screen);

	lv_obj_set_size(photo_layer, 480, 272);
	lv_obj_set_pos(photo_layer, 0, 0);

	lv_obj_set_style_bg_color(
	    photo_layer,
	    lv_color_hex(0x000000),
	    0
	);

	lv_obj_set_style_bg_opa(
	    photo_layer,
	    LV_OPA_COVER,
	    0
	);

	lv_obj_set_style_border_width(
	    photo_layer,
	    0,
	    0
	);

	lv_obj_set_style_radius(
	    photo_layer,
	    0,
	    0
	);

	lv_obj_set_style_pad_all(
	    photo_layer,
	    0,
	    0
	);

	lv_obj_set_scrollable(
	    photo_layer,
	    false
	);

	/*
	 * JPEG burada gorunecek.
	 */
	photo_image = lv_image_create(photo_layer);

	lv_obj_center(photo_image);

	lv_image_set_antialias(
	    photo_image,
	    true
	);

	/*
	 * Baslangicta fotograf katmani gizli.
	 */
	lv_obj_set_hidden(
	    photo_layer,
	    true
	);
	
	ESP_LOGI("LVGL", "Spotify ana ekrani olusturuldu");
}
static lv_display_t *lvgl_display = NULL;

static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area,
		uint8_t *px_map) {
	esp_lcd_panel_draw_bitmap(lcd_panel_handle, area->x1, area->y1,
			area->x2 + 1, area->y2 + 1, px_map);

	lv_display_flush_ready(display);
}

static void lvgl_init_display(void) {
	ESP_LOGI("LVGL", "LVGL baslatiliyor...");

	lv_init();
	lv_tick_set_cb(lvgl_tick_get);
	lvgl_display = lv_display_create(480, 272);

	if (lvgl_display == NULL) {
		ESP_LOGE("LVGL", "LVGL display olusturulamadi");
		return;
	}

	lv_display_set_color_format(lvgl_display, LV_COLOR_FORMAT_RGB565);

	const size_t buffer_size = 480 * 20 * sizeof(uint16_t);

	void *buf1 = heap_caps_malloc(buffer_size,
			MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

	if (buf1 == NULL) {
		ESP_LOGE("LVGL", "LVGL buffer ayrilamadi");
		return;
	}

	lv_display_set_buffers(lvgl_display, buf1,
	NULL, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

	lv_display_set_flush_cb(lvgl_display, lvgl_flush_cb);

	ESP_LOGI("LVGL", "LVGL hazir");
}

static void format_time_ms(int ms, char *buffer, size_t buffer_size) {
	if (ms < 0) {
		ms = 0;
	}

	int total_seconds = ms / 1000;
	int minutes = total_seconds / 60;
	int seconds = total_seconds % 60;

	snprintf(buffer, buffer_size, "%d:%02d", minutes, seconds);
}

static void lvgl_task(void *pvParameters) {
	ESP_LOGI("LVGL", "LVGL task baslatildi");

	char last_track[256] = "";
	char last_artist[256] = "";
	char last_album[256] = "";

	int last_spotify_progress = -1;
	int last_duration = -1;
	int last_playing_state = -1;

	int ui_progress_ms = 0;
	int last_shown_second = -1;
	bool waiting_screen_applied = false;
	int last_wifi_state = -1;
	int last_photo_visibility = -1;

	TickType_t last_tick = xTaskGetTickCount();
	

	while (1) {

		/* =====================================================
		 * WIFI IKONU
		 * Sadece durum degistiginde LVGL'i guncelle.
		 * ===================================================== */

		int current_wifi_state = wifi_connected ? 1 : 0;

		if (current_wifi_state != last_wifi_state) {

			last_wifi_state = current_wifi_state;

			if (wifi_icon != NULL) {

				if (wifi_connected) {

					/* Bagli -> Spotify yesili */
					lv_obj_set_style_text_color(wifi_icon,
							lv_color_hex(0x1DB954), 0);

				} else {

					/* Bagli degil -> gri */
					lv_obj_set_style_text_color(wifi_icon,
							lv_color_hex(0x666666), 0);
				}
			}
		}

		/* =====================================================
		 * INTERNET YOK -> SPOTIFY BEKLEME EKRANI
		 * ===================================================== */

		if (spotify_ui_waiting) {

			if (!waiting_screen_applied) {

				waiting_screen_applied = true;

				/*
				 * Cache'leri temizle.
				 * Internet geri geldiginde ayni sarki bile olsa
				 * UI tekrar yazilsin.
				 */
				last_track[0] = '\0';
				last_artist[0] = '\0';
				last_album[0] = '\0';

				last_spotify_progress = -1;
				last_duration = -1;
				last_playing_state = -1;

				ui_progress_ms = 0;
				last_shown_second = -1;

				/*
				 * Metinleri baslangic durumuna getir.
				 */
				lv_label_set_text(track_label, "Spotify bekleniyor...");

				lv_label_set_text(artist_label, "-");

				lv_label_set_text(album_label, "-");

				lv_label_set_text(status_label, "BEKLIYOR");

				lv_obj_set_style_bg_color(status_label, lv_color_hex(0x444444),
						0);

				/*
				 * Progress'i temizle.
				 */
				lv_bar_set_range(progress_bar, 0, 1000);

				lv_bar_set_value(progress_bar, 0,
				LV_ANIM_OFF);

				lv_label_set_text(time_left_label, "0:00");

				lv_label_set_text(time_right_label, "0:00");

				/*
				 * Album kapagini gizle.
				 * Muzik ikonunu geri getir.
				 */
				lv_obj_set_hidden(cover_image, true);

				if (cover_icon != NULL) {

					lv_obj_set_hidden(cover_icon, false);
				}
			}

		} else {

			waiting_screen_applied = false;
		}

		TickType_t current_tick = xTaskGetTickCount();

		int elapsed_ms = (int) ((current_tick - last_tick) * portTICK_PERIOD_MS);

		last_tick = current_tick;

		if (!spotify_ui_waiting && now_playing.track_name[0] != '\0') {

			/* =============================
			 * SARKI
			 * ============================= */

			if (strcmp(last_track, now_playing.track_name) != 0) {

				snprintf(last_track, sizeof(last_track), "%s",
						now_playing.track_name);

				lv_label_set_text(track_label, last_track);

				/* Yeni sarki geldiyse zamani da senkronla */
				ui_progress_ms = now_playing.progress_ms;
				last_spotify_progress = now_playing.progress_ms;
				last_shown_second = -1;
			}

			/* =============================
			 * SANATCI
			 * ============================= */

			if (strcmp(last_artist, now_playing.artist_name) != 0) {

				snprintf(last_artist, sizeof(last_artist), "%s",
						now_playing.artist_name);

				lv_label_set_text(artist_label, last_artist);
			}

			/* =============================
			 * ALBUM
			 * ============================= */

			if (strcmp(last_album, now_playing.album_name) != 0) {

				snprintf(last_album, sizeof(last_album), "%s",
						now_playing.album_name);

				lv_label_set_text(album_label, last_album);
			}

			/* =============================
			 * SPOTIFY'DAN YENI ZAMAN GELDIYSE
			 * ============================= */

			if (last_spotify_progress != now_playing.progress_ms) {

				last_spotify_progress = now_playing.progress_ms;

				ui_progress_ms = now_playing.progress_ms;
			}

			/* =============================
			 * CALIYOR / DURAKLATILDI
			 * ============================= */

			if (last_playing_state != (int) now_playing.is_playing) {

				last_playing_state = (int) now_playing.is_playing;

				if (now_playing.is_playing) {

					lv_label_set_text(status_label, "CALIYOR");

					lv_obj_set_style_bg_color(status_label,
							lv_color_hex(0x1DB954), 0);

				} else {

					lv_label_set_text(status_label, "DURAKLATILDI");

					lv_obj_set_style_bg_color(status_label,
							lv_color_hex(0xD97706), 0);
				}
			}

			/* =============================
			 * ESP32 KENDI SAYACINI ILERLETSIN
			 * ============================= */

			if (now_playing.is_playing) {

				ui_progress_ms += elapsed_ms;
			}

			if (ui_progress_ms < 0) {
				ui_progress_ms = 0;
			}

			if (now_playing.duration_ms > 0
					&& ui_progress_ms > now_playing.duration_ms) {

				ui_progress_ms = now_playing.duration_ms;
			}

			/* =============================
			 * PROGRESS BAR
			 * ============================= */

			if (last_duration != now_playing.duration_ms) {

				last_duration = now_playing.duration_ms;

				int bar_max = now_playing.duration_ms;

				if (bar_max <= 0) {
					bar_max = 1;
				}

				lv_bar_set_range(progress_bar, 0, bar_max);
			}

			/* =============================
			 * SURELER + PROGRESS BAR
			 * sadece saniye degisince guncelle
			 * ============================= */

			int current_second = ui_progress_ms / 1000;

			if (current_second != last_shown_second) {

				last_shown_second = current_second;

				/*
				 * Progress bar'i her 20 ms yerine
				 * sadece saniye degisince guncelle.
				 */
				lv_bar_set_value(progress_bar, ui_progress_ms,
				LV_ANIM_OFF);

				char current_time[16];
				char total_time[16];

				format_time_ms(ui_progress_ms, current_time,
						sizeof(current_time));

				format_time_ms(now_playing.duration_ms, total_time,
						sizeof(total_time));

				lv_label_set_text(time_left_label, current_time);

				lv_label_set_text(time_right_label, total_time);
			}
		}

		/* =============================================
		 * YENI ALBUM KAPAGI VAR MI?
		 * ============================================= */

		uint8_t *new_cover_pixels = NULL;
		uint16_t new_cover_width = 0;
		uint16_t new_cover_height = 0;

		portENTER_CRITICAL(&cover_mux);

		if (!spotify_ui_waiting && pending_cover_pixels != NULL) {

			new_cover_pixels = pending_cover_pixels;

			new_cover_width = pending_cover_width;

			new_cover_height = pending_cover_height;

			pending_cover_pixels = NULL;
			pending_cover_width = 0;
			pending_cover_height = 0;
		}

		portEXIT_CRITICAL(&cover_mux);

		if (new_cover_pixels != NULL) {

			uint8_t *old_pixels = current_cover_pixels;

			/* LVGL image descriptor */
			memset(&cover_image_dsc, 0, sizeof(cover_image_dsc));

			cover_image_dsc.header.magic =
			LV_IMAGE_HEADER_MAGIC;

			cover_image_dsc.header.cf = LV_COLOR_FORMAT_RGB565;

			cover_image_dsc.header.w = new_cover_width;

			cover_image_dsc.header.h = new_cover_height;

			cover_image_dsc.header.stride = new_cover_width * sizeof(uint16_t);

			cover_image_dsc.data_size = new_cover_width * new_cover_height
					* sizeof(uint16_t);

			cover_image_dsc.data = new_cover_pixels;

			current_cover_pixels = new_cover_pixels;

			lv_image_set_src(cover_image, &cover_image_dsc);

			/*
			 * 116x116 alana sigdir.
			 * LVGL'de 256 = %100 boyut.
			 */
			uint32_t scale_x = (116U * 256U) / new_cover_width;

			uint32_t scale_y = (116U * 256U) / new_cover_height;

			uint32_t scale = (scale_x < scale_y) ? scale_x : scale_y;

			lv_image_set_scale(cover_image, scale);

			lv_image_set_antialias(cover_image, true);

			lv_obj_center(cover_image);

			/* Müzik ikonunu gizle */
			lv_obj_set_hidden(cover_icon, true);

			ESP_LOGI("LVGL", "ALBUM KAPAGI EKRANA VERILDI -> %dx%d scale=%lu",
					new_cover_width, new_cover_height, (unsigned long) scale);

			/*
			 * Descriptor artik yeni buffer'i kullaniyor.
			 * Eski kapak RAM'i bosalt.
			 */
			if (old_pixels != NULL) {
				free(old_pixels);
			}
		}

		/*
		 * Spotify tekrar hazirsa mevcut album
		 * kapagini yeniden gorunur yap.
		 */
		 if (!spotify_ui_waiting && current_cover_pixels != NULL) {

		     lv_obj_set_hidden(cover_image, false);

		     lv_obj_set_hidden(cover_icon, true);
		 }

		 /*
		  * =====================================================
		  * SD FOTOGRAF -> LVGL
		  * =====================================================
		  */

		 uint8_t *new_photo_pixels = NULL;
		 uint16_t new_photo_width = 0;
		 uint16_t new_photo_height = 0;

		 portENTER_CRITICAL(&photo_mux);

		 if (pending_photo_pixels != NULL) {

		     new_photo_pixels =
		         pending_photo_pixels;

		     new_photo_width =
		         pending_photo_width;

		     new_photo_height =
		         pending_photo_height;

		     pending_photo_pixels = NULL;
		     pending_photo_width = 0;
		     pending_photo_height = 0;
		 }

		 portEXIT_CRITICAL(&photo_mux);

		 if (new_photo_pixels != NULL) {

		     if (new_photo_width == 0
		             || new_photo_height == 0) {

		         free(new_photo_pixels);

		     } else {

		         uint8_t *old_photo_pixels =
		             current_photo_pixels;

		         memset(
		             &photo_image_dsc,
		             0,
		             sizeof(photo_image_dsc)
		         );

		         photo_image_dsc.header.magic =
		             LV_IMAGE_HEADER_MAGIC;

		         photo_image_dsc.header.cf =
		             LV_COLOR_FORMAT_RGB565;

		         photo_image_dsc.header.w =
		             new_photo_width;

		         photo_image_dsc.header.h =
		             new_photo_height;

		         photo_image_dsc.header.stride =
		             new_photo_width
		             * sizeof(uint16_t);

		         photo_image_dsc.data_size =
		             new_photo_width
		             * new_photo_height
		             * sizeof(uint16_t);

		         photo_image_dsc.data =
		             new_photo_pixels;

		         current_photo_pixels =
		             new_photo_pixels;

		         lv_image_set_src(
		             photo_image,
		             &photo_image_dsc
		         );

		         /*
		          * =================================================
		          * RESPONSIVE CONTAIN
		          * =================================================
		          *
		          * 480x272 ekran icine sigdir.
		          *
		          * Dikey / yatay / kare fark etmez.
		          * Aspect ratio BOZULMAZ.
		          * Kirpma YOK.
		          */

		         uint32_t scale_x =
		             (480U * 256U)
		             / new_photo_width;

		         uint32_t scale_y =
		             (272U * 256U)
		             / new_photo_height;

		         uint32_t scale =
		             (scale_x < scale_y)
		             ? scale_x
		             : scale_y;

		         if (scale == 0) {
		             scale = 1;
		         }

		         lv_image_set_scale(
		             photo_image,
		             scale
		         );

		         lv_image_set_antialias(
		             photo_image,
		             true
		         );

		         lv_obj_center(
		             photo_image
		         );

		         ESP_LOGI(
		             "PHOTO",
		             "FOTOGRAF EKRANA VERILDI -> %dx%d scale=%lu",
		             new_photo_width,
		             new_photo_height,
		             (unsigned long) scale
		         );

		         /*
		          * LVGL artik yeni buffer'i kullaniyor.
		          * Eski fotografi temizle.
		          */
		         if (old_photo_pixels != NULL) {
		             free(old_photo_pixels);
		         }
		     }
		 }

		 /*
		  * =====================================================
		  * FOTOGRAF FADE GECISI
		  * =====================================================
		  */

		 static bool fade_out_active = false;
		 static bool fade_in_active = false;
		 static uint32_t fade_start_tick = 0;

		 /*
		  * Fade-out istegi geldiyse baslat.
		  */
		 if (photo_fade_out_requested
		         && !fade_out_active
		         && !fade_in_active
		         && photo_image != NULL
		         && current_photo_pixels != NULL) {

		     photo_fade_out_requested = false;
		     photo_fade_out_complete = false;

		     fade_out_active = true;
		     fade_start_tick = lv_tick_get();

		     ESP_LOGI(
		         "PHOTO",
		         "Fade-out basladi"
		     );
		 }

		 /*
		  * Fade-out:
		  * 255 -> 0
		  */
		 if (fade_out_active) {

		     uint32_t elapsed =
		         lv_tick_elaps(fade_start_tick);

		     if (elapsed >= PHOTO_FADE_DURATION_MS) {

		         lv_obj_set_style_opa(
		             photo_image,
		             LV_OPA_TRANSP,
		             0
		         );

		         fade_out_active = false;
		         photo_fade_out_complete = true;

		         ESP_LOGI(
		             "PHOTO",
		             "Fade-out tamamlandi"
		         );

		     } else {

		         uint32_t opa =
		             255U
		             - ((elapsed * 255U)
		                 / PHOTO_FADE_DURATION_MS);

		         lv_obj_set_style_opa(
		             photo_image,
		             (lv_opa_t) opa,
		             0
		         );
		     }
		 }

		 /*
		  * Yeni fotograf hazir oldugunda
		  * fade-in istegi gelecek.
		  */
		 if (photo_fade_in_requested
		         && !fade_out_active
		         && !fade_in_active
		         && photo_image != NULL) {

		     photo_fade_in_requested = false;

		     fade_in_active = true;
		     fade_start_tick = lv_tick_get();

		     lv_obj_set_style_opa(
		         photo_image,
		         LV_OPA_TRANSP,
		         0
		     );

		     ESP_LOGI(
		         "PHOTO",
		         "Fade-in basladi"
		     );
		 }

		 /*
		  * Fade-in:
		  * 0 -> 255
		  */
		 if (fade_in_active) {

		     uint32_t elapsed =
		         lv_tick_elaps(fade_start_tick);

		     if (elapsed >= PHOTO_FADE_DURATION_MS) {

		         lv_obj_set_style_opa(
		             photo_image,
		             LV_OPA_COVER,
		             0
		         );

		         fade_in_active = false;

		         ESP_LOGI(
		             "PHOTO",
		             "Fade-in tamamlandi"
		         );

		     } else {

		         uint32_t opa =
		             (elapsed * 255U)
		             / PHOTO_FADE_DURATION_MS;

		         lv_obj_set_style_opa(
		             photo_image,
		             (lv_opa_t) opa,
		             0
		         );
		     }
		 }

		 /*
		  * =====================================================
		  * SPOTIFY <-> FOTOGRAF MODU
		  * =====================================================
		  */

		 int should_show_photo =
		     (!spotify_playback_active
		         && current_photo_pixels != NULL)
		     ? 1
		     : 0;

		 if (should_show_photo
		         != last_photo_visibility) {

		     last_photo_visibility =
		         should_show_photo;

		     if (photo_layer != NULL) {

		         lv_obj_set_hidden(
		             photo_layer,
		             !should_show_photo
		         );
		     }

		     if (should_show_photo) {

		         ESP_LOGI(
		             "PHOTO",
		             "FOTOGRAF MODU AKTIF"
		         );

		     } else {

		         ESP_LOGI(
		             "PHOTO",
		             "SPOTIFY MODU AKTIF"
		         );
		     }
		 }
		 
		lv_timer_handler();

		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

/* =========================================================
 * SD CARD INITIALIZATION
 * ========================================================= */

static esp_err_t sd_card_init(void)
{
    ESP_LOGI("SD", "SD kart baslatiliyor...");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_GPIO,
        .miso_io_num = SD_MISO_GPIO,
        .sclk_io_num = SD_CLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16 * 1024,
    };

    esp_err_t err = spi_bus_initialize(
        host.slot,
        &bus_cfg,
        SPI_DMA_CH_AUTO
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            "SD",
            "SPI bus baslatilamadi: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    sdspi_device_config_t slot_config =
        SDSPI_DEVICE_CONFIG_DEFAULT();

    slot_config.gpio_cs = SD_CS_GPIO;
    slot_config.host_id = host.slot;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    err = esp_vfs_fat_sdspi_mount(
        SD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &sd_card
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            "SD",
            "SD kart mount edilemedi: %s",
            esp_err_to_name(err)
        );

        spi_bus_free(host.slot);

        return err;
    }

    sd_card_ready = true;

    if (sd_mutex == NULL) {

        sd_mutex = xSemaphoreCreateMutex();

        if (sd_mutex == NULL) {

            ESP_LOGE(
                "SD",
                "SD mutex olusturulamadi"
            );

            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI("SD", "SD kart basariyla mount edildi");

    sdmmc_card_print_info(stdout, sd_card);

    int photo_count = 0;

    for (int i = 1; i <= 999; i++) {

        char path[64];

        snprintf(
            path,
            sizeof(path),
            SD_MOUNT_POINT "/photo-%d.jpeg",
            i
        );

        FILE *file = fopen(path, "rb");

        if (file != NULL) {

            photo_count++;

            ESP_LOGI(
                "SD",
                "Bulundu: photo-%d.jpeg",
                i
            );

            fclose(file);
        }
    }

    sd_photo_count = photo_count;

    ESP_LOGI(
        "SD",
        "Toplam %d fotograf bulundu",
        photo_count
    );

    return ESP_OK;
}

/* =========================================================
 * SD JPEG LOAD + DECODE
 * ========================================================= */

 typedef struct {
     struct jpeg_error_mgr pub;
     jmp_buf jump_buffer;
     char message[JMSG_LENGTH_MAX];
 } photo_jpeg_error_t;

 static void photo_jpeg_error_exit(j_common_ptr cinfo)
 {
     photo_jpeg_error_t *err =
         (photo_jpeg_error_t *) cinfo->err;

     (*cinfo->err->format_message)(
         cinfo,
         err->message
     );

     longjmp(
         err->jump_buffer,
         1
     );
 }
 
 static esp_err_t load_photo_from_sd_unlocked(const char *path)
{
	    if (!sd_card_ready) {
	        ESP_LOGE(
	            "PHOTO",
	            "SD kart hazir degil"
	        );

	        return ESP_ERR_INVALID_STATE;
	    }

	    if (path == NULL) {
	        return ESP_ERR_INVALID_ARG;
	    }

	    ESP_LOGI(
	        "PHOTO",
	        "Fotograf aciliyor: %s",
	        path
	    );

	    FILE *file = fopen(
	        path,
	        "rb"
	    );

	    if (file == NULL) {
	        ESP_LOGE(
	            "PHOTO",
	            "Fotograf acilamadi: %s",
	            path
	        );

	        return ESP_FAIL;
	    }

	    /*
	     * Dosya boyutunu sadece log icin al.
	     */
	    if (fseek(file, 0, SEEK_END) != 0) {
	        ESP_LOGE(
	            "PHOTO",
	            "Dosya sonuna gidilemedi"
	        );

	        fclose(file);
	        return ESP_FAIL;
	    }

	    long file_size_long =
	        ftell(file);

	    if (file_size_long <= 0) {
	        ESP_LOGE(
	            "PHOTO",
	            "JPEG dosya boyutu gecersiz"
	        );

	        fclose(file);
	        return ESP_FAIL;
	    }

	    rewind(file);

	    ESP_LOGI(
	        "PHOTO",
	        "JPEG dosya boyutu: %ld byte",
	        file_size_long
	    );

	    /*
	     * =============================================
	     * LIBJPEG-TURBO
	     * =============================================
	     */

	    struct jpeg_decompress_struct cinfo;
	    photo_jpeg_error_t jerr;

	    memset(
	        &cinfo,
	        0,
	        sizeof(cinfo)
	    );

	    memset(
	        &jerr,
	        0,
	        sizeof(jerr)
	    );

	    cinfo.err =
	        jpeg_std_error(
	            &jerr.pub
	        );

	    jerr.pub.error_exit =
	        photo_jpeg_error_exit;

	    /*
	     * libjpeg fatal hata verirse
	     * buraya geri donecegiz.
	     */
	    if (setjmp(jerr.jump_buffer)) {

	        ESP_LOGE(
	            "PHOTO",
	            "libjpeg hatasi: %s",
	            jerr.message
	        );

	        jpeg_destroy_decompress(
	            &cinfo
	        );

	        fclose(file);

	        return ESP_FAIL;
	    }

	    jpeg_create_decompress(
	        &cinfo
	    );

	    /*
	     * JPEG'i direkt SD dosyasindan oku.
	     * Tum sikistirilmis JPEG'i RAM'e
	     * almamiza gerek kalmiyor.
	     */
	    jpeg_stdio_src(
	        &cinfo,
	        file
	    );

	    int header_result =
	        jpeg_read_header(
	            &cinfo,
	            TRUE
	        );

	    if (header_result
	            != JPEG_HEADER_OK) {

	        ESP_LOGE(
	            "PHOTO",
	            "JPEG header okunamadi"
	        );

	        jpeg_destroy_decompress(
	            &cinfo
	        );

	        fclose(file);

	        return ESP_FAIL;
	    }

	    ESP_LOGI(
	        "PHOTO",
	        "Orijinal fotograf: %ux%u",
	        (unsigned int) cinfo.image_width,
	        (unsigned int) cinfo.image_height
	    );

	    ESP_LOGI(
	        "PHOTO",
	        "PSRAM bos: %u byte | En buyuk blok: %u byte",
	        (unsigned int) heap_caps_get_free_size(
	            MALLOC_CAP_SPIRAM
	        ),
	        (unsigned int) heap_caps_get_largest_free_block(
	            MALLOC_CAP_SPIRAM
	        )
	    );

	    ESP_LOGI(
	        "PHOTO",
	        "INTERNAL bos: %u byte | En buyuk blok: %u byte",
	        (unsigned int) heap_caps_get_free_size(
	            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
	        ),
	        (unsigned int) heap_caps_get_largest_free_block(
	            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
	        )
	    );

	    /*
	     * =============================================
	     * RESPONSIVE DECODE SCALE
	     * =============================================
	     *
	     * Buyuk telefon fotograflarini
	     * tam boyutta RGB565'e cevirmiyoruz.
	     *
	     * Son ince olceklendirmeyi LVGL
	     * yapacak.
	     */

	    cinfo.scale_num = 1;

	    if (cinfo.image_width >= (480U * 8U)
	            || cinfo.image_height >= (272U * 8U)) {

	        cinfo.scale_denom = 8;

	    } else if (
	        cinfo.image_width >= (480U * 4U)
	        || cinfo.image_height >= (272U * 4U)
	    ) {

	        cinfo.scale_denom = 4;

	    } else if (
	        cinfo.image_width >= (480U * 2U)
	        || cinfo.image_height >= (272U * 2U)
	    ) {

	        cinfo.scale_denom = 2;

	    } else {

	        cinfo.scale_denom = 1;
	    }

	    /*
	     * Direkt RGB565 istiyoruz.
	     */
	    cinfo.out_color_space =
	        JCS_RGB565;

	    /*
	     * Dithering kapali.
	     */
	    cinfo.dither_mode =
	        JDITHER_NONE;

	    /*
	     * Decode'u baslat.
	     */
	    if (!jpeg_start_decompress(
	            &cinfo)) {

	        ESP_LOGE(
	            "PHOTO",
	            "JPEG decode baslatilamadi"
	        );

	        jpeg_destroy_decompress(
	            &cinfo
	        );

	        fclose(file);

	        return ESP_FAIL;
	    }

	    uint32_t output_width =
	        cinfo.output_width;

	    uint32_t output_height =
	        cinfo.output_height;

	    ESP_LOGI(
	        "PHOTO",
	        "Decode boyutu: %lux%lu",
	        (unsigned long) output_width,
	        (unsigned long) output_height
	    );

	    if (output_width == 0
	            || output_height == 0
	            || output_width > UINT16_MAX
	            || output_height > UINT16_MAX) {

	        ESP_LOGE(
	            "PHOTO",
	            "Decode boyutu gecersiz"
	        );

	        jpeg_abort_decompress(
	            &cinfo
	        );

	        jpeg_destroy_decompress(
	            &cinfo
	        );

	        fclose(file);

	        return ESP_FAIL;
	    }

	    /*
	     * RGB565:
	     * her pixel 2 byte.
	     */
	    size_t row_size =
	        (size_t) output_width
	        * sizeof(uint16_t);

	    size_t pixel_buffer_size =
	        row_size
	        * (size_t) output_height;

	    ESP_LOGI(
	        "PHOTO",
	        "RGB565 buffer: %u byte",
	        (unsigned int) pixel_buffer_size
	    );

	    uint8_t *pixel_buffer =
	        heap_caps_malloc(
	            pixel_buffer_size,
	            MALLOC_CAP_SPIRAM
	                | MALLOC_CAP_8BIT
	        );

	    if (pixel_buffer == NULL) {

	        ESP_LOGE(
	            "PHOTO",
	            "RGB565 fotograf buffer'i ayrilamadi"
	        );

	        jpeg_abort_decompress(
	            &cinfo
	        );

	        jpeg_destroy_decompress(
	            &cinfo
	        );

	        fclose(file);

	        return ESP_ERR_NO_MEM;
	    }

	    /*
	     * =============================================
	     * SCANLINE DECODE
	     * =============================================
	     */

	    while (
	        cinfo.output_scanline
	        < cinfo.output_height
	    ) {

	        JSAMPROW row_pointer[1];

	        row_pointer[0] =
	            pixel_buffer
	            + ((size_t)
	                cinfo.output_scanline
	                * row_size);

	        JDIMENSION lines_read =
	            jpeg_read_scanlines(
	                &cinfo,
	                row_pointer,
	                1
	            );

	        if (lines_read != 1) {

	            ESP_LOGE(
	                "PHOTO",
	                "JPEG scanline okunamadi"
	            );

	            free(
	                pixel_buffer
	            );

	            jpeg_abort_decompress(
	                &cinfo
	            );

	            jpeg_destroy_decompress(
	                &cinfo
	            );

	            fclose(file);

	            return ESP_FAIL;
	        }
	    }

	    /*
	     * Decode tamamlandi.
	     */
	    if (!jpeg_finish_decompress(
	            &cinfo)) {

	        ESP_LOGE(
	            "PHOTO",
	            "JPEG decode tamamlanamadi"
	        );

	        free(
	            pixel_buffer
	        );

	        jpeg_destroy_decompress(
	            &cinfo
	        );

	        fclose(file);

	        return ESP_FAIL;
	    }

	    jpeg_destroy_decompress(
	        &cinfo
	    );

	    fclose(file);

	    ESP_LOGI(
	        "PHOTO",
	        "JPEG decode edildi: %lux%lu",
	        (unsigned long) output_width,
	        (unsigned long) output_height
	    );

	    /*
	     * =============================================
	     * LVGL TASK'INA TESLIM ET
	     * =============================================
	     */

	    portENTER_CRITICAL(
	        &photo_mux
	    );

	    uint8_t *old_pending =
	        pending_photo_pixels;

	    pending_photo_pixels =
	        pixel_buffer;

	    pending_photo_width =
	        (uint16_t) output_width;

	    pending_photo_height =
	        (uint16_t) output_height;

	    portEXIT_CRITICAL(
	        &photo_mux
	    );

	    if (old_pending != NULL) {
	        free(
	            old_pending
	        );
	    }

	    ESP_LOGI(
	        "PHOTO",
	        "Fotograf LVGL icin hazir"
	    );

	    return ESP_OK;
}

 /*
  * =========================================================
  * THREAD-SAFE SD PHOTO LOAD
  * =========================================================
  *
  * SD karta ayni anda slideshow ve web upload
  * erismesin diye asil decoder'i mutex ile koruyoruz.
  */
 static esp_err_t load_photo_from_sd(const char *path)
 {
     if (sd_mutex == NULL) {

         ESP_LOGE(
             "SD",
             "SD mutex hazir degil"
         );

         return ESP_ERR_INVALID_STATE;
     }

     /*
      * SD kart bosalana kadar bekle.
      */
     if (xSemaphoreTake(
             sd_mutex,
             portMAX_DELAY
         ) != pdTRUE) {

         ESP_LOGE(
             "SD",
             "SD mutex alinamadi"
         );

         return ESP_FAIL;
     }

     /*
      * Fotograf okuma + JPEG decode islemini yap.
      */
     esp_err_t result =
         load_photo_from_sd_unlocked(path);

     /*
      * Ne olursa olsun SD kart kilidini birak.
      */
     xSemaphoreGive(sd_mutex);

     return result;
 }

static void photo_slideshow_task(void *pvParameters)
{
    ESP_LOGI(
        "PHOTO",
        "Slideshow task baslatildi"
    );

    while (1) {

        /*
         * Spotify caliyorsa slideshow ilerlemesin.
         */
        if (spotify_playback_active
                || !sd_card_ready
                || sd_photo_count <= 0) {

            vTaskDelay(
                pdMS_TO_TICKS(500)
            );

            continue;
        }

        /*
         * Fotograf ekranda biraz kalsin.
         */
        vTaskDelay(
            pdMS_TO_TICKS(
                PHOTO_SLIDESHOW_INTERVAL_MS
            )
        );

        /*
         * Beklerken Spotify baslamis olabilir.
         */
        if (spotify_playback_active) {
            continue;
        }

        /*
         * Once mevcut fotografi yumusakca karart.
         */
        photo_fade_out_complete = false;
        photo_fade_out_requested = true;

        ESP_LOGI(
            "PHOTO",
            "Slideshow gecisi basliyor"
        );

        /*
         * Fade-out tamamlanana kadar bekle.
         */
        while (!photo_fade_out_complete) {

            /*
             * Spotify bu sirada baslarsa bile
             * fade-out'un temizce bitmesini bekliyoruz.
             */
            vTaskDelay(
                pdMS_TO_TICKS(20)
            );
        }

        photo_fade_out_complete = false;

        /*
         * Fade sirasinda Spotify baslamissa
         * fotograf degistirme.
         *
         * Fotograf katmani Spotify tarafindan gizlenecek.
         * Opacity'yi arkada tekrar normale getiriyoruz.
         */
        if (spotify_playback_active) {

            photo_fade_in_requested = true;

            ESP_LOGI(
                "PHOTO",
                "Spotify basladi, slideshow gecisi iptal edildi"
            );

            continue;
        }

        /*
         * Siradaki fotograf yolunu hazirla.
         */
        char photo_path[64];

        snprintf(
            photo_path,
            sizeof(photo_path),
            SD_MOUNT_POINT "/photo-%d.jpeg",
            slideshow_next_photo
        );

        ESP_LOGI(
            "PHOTO",
            "Siradaki slideshow fotografi: %s",
            photo_path
        );

        /*
         * Ekran tamamen karanlikken
         * yeni fotografi decode et.
         */
        esp_err_t err =
            load_photo_from_sd(
                photo_path
            );

        if (err == ESP_OK) {

            slideshow_next_photo++;

            if (slideshow_next_photo
                    > sd_photo_count) {

                slideshow_next_photo = 1;
            }

        } else {

            ESP_LOGE(
                "PHOTO",
                "Slideshow fotografi yuklenemedi: %s",
                photo_path
            );
        }

        /*
         * Yeni fotograf hazir.
         * Yumusakca tekrar gorunur yap.
         *
         * Decode basarisiz olsa bile eski fotograf
         * tekrar gorunur hale gelir.
         */
        photo_fade_in_requested = true;
    }
}


/* =========================================================
 * MAIN
 * ========================================================= */

void app_main(void) {
	gpio_config_t backlight_config = { .pin_bit_mask = (1ULL << GPIO_NUM_2),
			.mode = GPIO_MODE_OUTPUT, .pull_up_en = GPIO_PULLUP_DISABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type =
					GPIO_INTR_DISABLE };

	ESP_ERROR_CHECK(gpio_config(&backlight_config));

	gpio_set_level(GPIO_NUM_2, 1);

	ESP_LOGI("DISPLAY", "LCD backlight ACILDI");
	/*
	 * NVS baslat
	 */
	display_init();
	lvgl_init_display();
	spotify_ui_create();

	xTaskCreate(lvgl_task, "lvgl_task", 8192,
	NULL, 5,
	NULL);

	//display_fill_color(0x07E0);

	esp_err_t ret = nvs_flash_init();

	if (ret == ESP_ERR_NVS_NO_FREE_PAGES
			|| ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());

		ret = nvs_flash_init();
	}

	ESP_ERROR_CHECK(ret);
	
	/*
	 * SD karti baslat.
	 * SD kartta hata olsa bile Spotify sistemi calismaya devam etsin.
	 */
	esp_err_t sd_err = sd_card_init();

	if (sd_err != ESP_OK) {
		ESP_LOGW(
			"SD",
			"SD kart kullanilamayacak ama sistem devam edecek"
		);
	}

	/*
	 * Ilk test:
	 * SD karttan photo-1.jpeg dosyasini hazirla.
	 */
	if (sd_err == ESP_OK) {

	    esp_err_t photo_err =
	        load_photo_from_sd(
	            SD_MOUNT_POINT "/photo-1.jpeg"
	        );

	    if (photo_err != ESP_OK) {

	        ESP_LOGE(
	            "PHOTO",
	            "photo-1.jpeg yuklenemedi: %s",
	            esp_err_to_name(photo_err)
	        );

	    } else {

	        ESP_LOGI(
	            "PHOTO",
	            "Ilk fotograf hazir"
	        );

	        /*
	         * Birden fazla fotograf varsa
	         * slideshow task'ini baslat.
	         */
	        if (sd_photo_count > 1) {

	            BaseType_t task_result =
	                xTaskCreate(
	                    photo_slideshow_task,
	                    "photo_slideshow",
	                    8192,
	                    NULL,
	                    4,
	                    NULL
	                );

	            if (task_result == pdPASS) {

	                ESP_LOGI(
	                    "PHOTO",
	                    "Slideshow task olusturuldu"
	                );

	            } else {

	                ESP_LOGE(
	                    "PHOTO",
	                    "Slideshow task olusturulamadi"
	                );
	            }
	        }
	    }
	}
	
	/*
	 * Kayitli Wi-Fi bilgilerini oku.
	 */

	char saved_ssid[33] = { 0 };
	char saved_password[65] = { 0 };

	bool wifi_saved = load_wifi_credentials(saved_ssid, sizeof(saved_ssid),
			saved_password, sizeof(saved_password));

	if (wifi_saved) {
		ESP_LOGI(TAG, "Kayitli Wi-Fi bilgileri bulundu");
	} else {
		ESP_LOGI(TAG, "Henuz kayitli Wi-Fi yok");
	}

	/*
	 * AP + STA sistemini baslat.
	 *
	 * heart_steel kurulum agi
	 * yayinlanmaya devam edecek.
	 */

	ESP_LOGI(TAG, "ESP_WIFI_MODE_APSTA");

	wifi_init_softap();

	/*
	 * Kayitli Wi-Fi varsa
	 * STA baglantisini baslat.
	 */

	if (wifi_saved) {
		wifi_connect_sta(saved_ssid, saved_password);
	}

	/*
	 * Kurulum web sayfasini baslat.
	 */

	start_webserver();

	ESP_LOGI(TAG, "Sistem hazir.");
}
