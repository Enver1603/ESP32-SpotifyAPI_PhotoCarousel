# ESP32-SpotifyAPI_PhotoCarousel

**heart_steel** is ESP-IDF firmware for the **Elecrow CrowPanel 4.3" (ESP32-S3-WROOM, HMI v2.1)** that turns the panel into a desktop "Now Playing" screen for Spotify and a digital photo frame.

- When something is playing on your Spotify account, the screen shows the track, artist, album, album art, play/pause state and a live progress bar.
- When nothing is playing, it switches to a full-screen photo slideshow from the microSD card, with fade transitions.
- You set everything up from a phone or laptop: the board runs its own Wi-Fi access point (**softAP**) with a small web page where you enter your home Wi-Fi details and Spotify credentials, and upload photos.

The firmware is in the [`softAP/`](softAP/) directory. It started from Espressif's `wifi/getting_started/softAP` example and has been extended into the full application.

---

## Table of Contents

- [Features](#features)
- [Hardware](#hardware)
- [Pin Mapping](#pin-mapping)
- [Software Stack](#software-stack)
- [Repository Layout](#repository-layout)
- [How It Works](#how-it-works)
  - [Boot Sequence](#boot-sequence)
  - [Wi-Fi: SoftAP + Station](#wi-fi-softap--station)
  - [Setup Web Portal](#setup-web-portal)
  - [Spotify Integration](#spotify-integration)
  - [Display and UI](#display-and-ui)
  - [Photo Slideshow](#photo-slideshow)
  - [FreeRTOS Tasks](#freertos-tasks)
  - [Persistent Storage (NVS)](#persistent-storage-nvs)
  - [Memory Usage](#memory-usage)
- [Getting Started](#getting-started)
  - [1. Install ESP-IDF](#1-install-esp-idf)
  - [2. Configure the Access Point](#2-configure-the-access-point)
  - [3. Build and Flash](#3-build-and-flash)
  - [4. Get Spotify Credentials](#4-get-spotify-credentials)
  - [5. Prepare the microSD Card](#5-prepare-the-microsd-card)
  - [6. First-Time Setup](#6-first-time-setup)
- [HTTP API Reference](#http-api-reference)
- [Configuration Reference](#configuration-reference)
- [Known Limitations](#known-limitations)
- [Troubleshooting](#troubleshooting)
- [Security Notes](#security-notes)

---

## Features

- **Two Wi-Fi roles at once (AP + STA).** The board always runs a setup network called `heart_steel` and, at the same time, connects to your home Wi-Fi for internet access.
- **Browser-based setup.** A mobile-friendly page at `http://192.168.4.1` handles:
  - home Wi-Fi SSID and password
  - Spotify Client ID and Refresh Token
  - photo uploads to the SD card
- **Credentials survive reboots.** Wi-Fi and Spotify credentials are saved in NVS (flash), so you only enter them once.
- **Live Spotify "Now Playing" view:**
  - track, artist and album, with circular scrolling for long names
  - album art downloaded over HTTPS and decoded on the device
  - status badge: playing (green), paused (orange) or waiting (grey)
  - progress bar and elapsed/total time; the device advances the timer locally between API polls, so it moves smoothly
  - Wi-Fi status icon: green when online, grey when offline
- **Automatic access token refresh.** A new token is requested 5 minutes before the current one expires.
- **Photo slideshow from microSD:**
  - JPEGs are decoded with libjpeg-turbo straight to RGB565
  - large photos are downscaled during decoding (1/2, 1/4 or 1/8) to save memory
  - photos are fitted to the 480×272 screen with the aspect ratio kept and nothing cropped
  - 650 ms fade-out/fade-in between photos, each shown for about 6 s
- **Photo upload from the browser.** The browser resizes the image to at most 480×272 and re-encodes it as a JPEG before upload. The device checks it and saves it as the next `photo-N.jpeg`.
- **Turkish character support.** A custom LVGL font adds Turkish glyphs (`ç ğ ı İ ö ş ü Â Î Û`…) and falls back to Montserrat 14 for everything else.
- **Safe SD access.** A mutex stops the slideshow reader and the upload handler from using the card at the same time.

---

## Hardware

| Item | Details |
|---|---|
| Board | **Elecrow CrowPanel 4.3" ESP32 HMI display, v2.1** |
| Module | **ESP32-S3-WROOM** (dual-core Xtensa LX7, Wi-Fi 2.4 GHz) |
| Flash | 4 MB (as configured in `sdkconfig`) |
| PSRAM | Quad SPI PSRAM, 40 MHz (used for the frame buffer, album art and photos) |
| Display | 4.3" TFT, **480 × 272**, 16-bit parallel RGB interface |
| Storage | On-board microSD slot, SPI mode, FAT32 |
| Power / USB | USB-C (power, flashing and serial monitor) |

No extra hardware is needed. Everything above is on the CrowPanel board.

---

## Pin Mapping

These are the pins used in [`softap_example_main.c`](softAP/main/softap_example_main.c).

### RGB LCD

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| HSYNC | 39 | | VSYNC | 41 |
| DE | 40 | | PCLK | 42 |
| B0 | 8 | | G0 | 5 |
| B1 | 3 | | G1 | 6 |
| B2 | 46 | | G2 | 7 |
| B3 | 9 | | G3 | 15 |
| B4 | 1 | | G4 | 16 |
| R0 | 45 | | G5 | 4 |
| R1 | 48 | | | |
| R2 | 47 | | | |
| R3 | 21 | | | |
| R4 | 14 | | | |
| **Backlight** | **2** (driven HIGH at boot) | | | |

Panel timings: `h_res = 480`, `v_res = 272`, HSYNC pulse/back/front = 4/43/8, VSYNC pulse/back/front = 4/12/8, PCLK active on the falling edge, one frame buffer in PSRAM.

### microSD (SPI)

| Signal | GPIO |
|---|---|
| MOSI | 11 |
| MISO | 13 |
| CLK | 12 |
| CS | 10 |

The card is mounted at `/sdcard`.

---

## Software Stack

| Component | Version | Purpose |
|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) | **6.1.0** | Framework, Wi-Fi, HTTP server/client, NVS, FATFS, LCD driver |
| [LVGL](https://lvgl.io) (`lvgl/lvgl`) | 9.6.0 | UI rendering |
| `espressif/cjson` | 1.7.19 | Parsing Spotify API JSON |
| `espressif/esp_jpeg` | 1.3.1 | Decoding album art (from RAM) |
| `espressif/libjpeg-turbo` | 3.2.0 | Decoding SD-card photos (streamed from file) |
| mbedTLS + ESP x509 certificate bundle | (IDF) | HTTPS to `accounts.spotify.com` and `api.spotify.com` |

Component versions are pinned in [`softAP/dependencies.lock`](softAP/dependencies.lock). The IDF Component Manager downloads them on the first build.

---

## Repository Layout

```
ESP32-SpotifyAPI_PhotoCarousel/
├── README.md                     ← this file
└── softAP/                       ← ESP-IDF project
    ├── CMakeLists.txt            ← project definition (minimal build)
    ├── partitions.csv            ← custom partition table (3 MB app)
    ├── sdkconfig                 ← full project configuration
    ├── dependencies.lock         ← pinned component versions
    └── main/
        ├── CMakeLists.txt        ← component sources & IDF dependencies
        ├── Kconfig.projbuild     ← "Example Configuration" menu (AP SSID, password…)
        ├── idf_component.yml     ← managed component manifest
        ├── softap_example_main.c ← the whole application
        └── spotify_font_tr.c     ← LVGL font with Turkish glyphs
```

### Partition Table

| Name | Type | SubType | Offset | Size |
|---|---|---|---|---|
| nvs | data | nvs | `0x9000` | 24 KB |
| phy_init | data | phy | `0xF000` | 4 KB |
| factory | app | factory | `0x10000` | **3 MB** |

The app partition is enlarged to 3 MB to fit LVGL, TLS, the certificate bundle and both JPEG decoders. There is no OTA partition.

---

## How It Works

```
      Phone / Laptop                                   Internet
            │  Wi-Fi: "heart_steel"                       ▲
            ▼  http://192.168.4.1                         │ HTTPS
   ┌──────────────────────────────────────────────────────┴──────────┐
   │                       CrowPanel ESP32-S3                         │
   │                                                                  │
   │  [ softAP + HTTP server ]        [ STA → home router ]           │
   │     /  /connect  /spotify          │                              │
   │     /upload-photo                  ▼                              │
   │        │                   spotify_https_task                     │
   │        │                    ├─ POST accounts.spotify.com/api/token│
   │        │                    ├─ GET  /v1/me/player/currently-playing (1 s)
   │        │                    └─ GET  album art JPEG → esp_jpeg     │
   │        ▼                              │                           │
   │   NVS / SD card ◄── photo_slideshow_task (libjpeg-turbo)          │
   │                                       │                           │
   │                     lvgl_task (20 ms) ◄┘ → RGB LCD 480×272       │
   └──────────────────────────────────────────────────────────────────┘
```

### Boot Sequence

`app_main()` runs these steps in order:

1. **Backlight on.** GPIO 2 is set HIGH.
2. **Display init.** The RGB LCD panel, LVGL (partial render, 480×20-line buffer in internal RAM) and the UI widgets are created, then `lvgl_task` starts.
3. **NVS init.** If the NVS partition is full or from an older version, it is erased and re-initialised.
4. **SD card init.** The card is mounted over SPI and the firmware counts the `photo-1.jpeg` … `photo-999.jpeg` files on it. If the SD card fails, the firmware logs a warning and carries on without photos.
5. **First photo.** `photo-1.jpeg` is decoded so it is ready to show. If there is more than one photo, `photo_slideshow_task` is started.
6. **Load saved Wi-Fi details** from NVS (namespace `wifi_config`).
7. **Start Wi-Fi in AP+STA mode.** The `heart_steel` AP comes up at `192.168.4.1`.
8. **Connect to home Wi-Fi** if saved credentials exist.
9. **Start the HTTP server.**

### Wi-Fi: SoftAP + Station

The radio runs in `WIFI_MODE_APSTA` the whole time, with power saving disabled (`WIFI_PS_NONE`) so HTTP responses stay fast.

- **Access point (setup network)**
  - SSID, password, channel and maximum clients come from `menuconfig` (defaults in `sdkconfig`: SSID `heart_steel`, channel 1, up to 4 clients).
  - Security is WPA2-PSK. If the password is left empty, the AP is open.
  - PMF is available but not required. GTK rekeying is on, every 600 s.
  - The AP password is never written to the serial log.
- **Station (internet connection)**
  - The minimum security accepted is WPA2-PSK, with PMF available.
  - If the connection drops (`WIFI_EVENT_STA_DISCONNECTED`), the firmware reconnects straight away, shows the Spotify waiting screen and marks playback inactive. That inactive state makes the screen fall back to the photo slideshow.
  - On `IP_EVENT_STA_GOT_IP` the Wi-Fi icon turns green and `spotify_https_task` is created. It is created once only; later reconnects reuse the same task.

### Setup Web Portal

Join the `heart_steel` network and open **http://192.168.4.1**. The page is dark-themed with Spotify-green accents and has three sections:

1. **Wi-Fi.** Enter the SSID and password and press *Bağlan* (Connect). The details are URL-decoded, saved to NVS and confirmed in the browser. **The device restarts after about 1.5 s** and connects to the new network.
2. **Spotify.** Enter the Client ID and Refresh Token and press *Spotify'ı Kaydet* (Save Spotify). Both are saved to NVS. The Spotify task reads them when it starts, which happens after the device gets an IP address on the home network. If Wi-Fi is already connected, restart the device so the new credentials are used.
3. **Photos.** Pick an image and press *Fotoğraf Ekle* (Add Photo). In the browser:
   - the image is decoded with `createImageBitmap` (EXIF orientation is applied)
   - it is scaled down to fit 480×272, never up
   - it is re-encoded as a JPEG at quality 0.90
   - it is sent as raw `image/jpeg` to `/upload-photo`

   On the device, the upload is streamed to a temporary file in 512-byte chunks. The firmware then checks the JPEG start (`FF D8`) and end (`FF D9`) markers and renames the file to `photo-<highest+1>.jpeg`.

> The web UI text is in Turkish.

### Spotify Integration

All Spotify work runs in `spotify_https_task`:

1. **Load credentials** (`client_id`, `refresh`) from the NVS namespace `spotify`. If none are saved, the task ends.
2. **Get an access token.** The task sends `POST https://accounts.spotify.com/api/token` with
   `grant_type=refresh_token&refresh_token=…&client_id=…` (form-encoded, no client secret).
   If this fails, it tries again after 30 s.
3. **Poll every second.** It calls `GET https://api.spotify.com/v1/me/player/currently-playing` with `Authorization: Bearer <token>`.
   - **200:** the firmware parses `item.name`, the first artist, `album.name`, `progress_ms`, `duration_ms` and `is_playing`. The Spotify view is shown.
   - **204 / empty body:** nothing is playing. The screen switches to photo mode.
   - The HTTP client keeps the connection alive between polls, so the TLS handshake isn't repeated every second.
4. **Album art.** From `album.images[]` the firmware takes the **second entry** (about 300×300). This is a balance between quality and RAM for the 116×116 cover slot. The image is only downloaded when its URL changes:
   - the JPEG is downloaded into a 128 KB PSRAM buffer
   - `esp_jpeg` decodes it at **1/2 scale** to RGB565
   - the result is passed to the LVGL task, which scales it to fit
5. **Token refresh.** The inner polling loop exits 5 minutes before `expires_in` runs out (or at half the lifetime for very short tokens). The old token is zeroed in RAM and a new one is requested.

Tokens, refresh tokens and the `Authorization` header are never logged; only their lengths are. Buffers that held them are cleared with `memset` before they are freed.

### Display and UI

The UI is drawn with LVGL 9 on a 480×272 screen and has two layers.

**Spotify card** (450×242, rounded, dark grey):

```
┌────────────────────────────────────────────────────────┐
│ ┌──────────┐  heart_steel                          📶  │
│ │          │  Track name (scrolling) ……………               │
│ │  album   │  Artist name                              │
│ │   art    │  Album name                               │
│ │          │  [ CALIYOR ]                              │
│ └──────────┘                                           │
│ ████████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │
│ 1:23                                              3:45 │
└────────────────────────────────────────────────────────┘
```

- **Status badge:** `CALIYOR` (playing, green `#1DB954`), `DURAKLATILDI` (paused, orange `#D97706`), `BEKLIYOR` (waiting, grey).
- **Progress:** the time is re-synced from Spotify on every poll. Between polls `lvgl_task` adds the elapsed time itself. The bar and labels only redraw when the displayed second changes.
- **Waiting screen:** when there is no internet, all labels are reset to `Spotify bekleniyor...` and the cover goes back to a music icon.

**Photo layer** (full-screen, black background):

- Shown when **Spotify playback is not active and a photo is loaded**. Otherwise it is hidden and the Spotify card shows.
- A paused track still counts as active, because Spotify still returns 200. So the Spotify card stays on screen with the *DURAKLATILDI* badge.

Only `lvgl_task` touches LVGL objects. Other tasks hand over new data through shared variables protected by spinlocks (`cover_mux`, `photo_mux`) or `volatile` flags.

### Photo Slideshow

- Photos must be named **`photo-1.jpeg`, `photo-2.jpeg`, …** in the root of the SD card. Use the `.jpeg` extension, not `.jpg`.
- `photo-1.jpeg` is shown at boot. The slideshow then moves on from `photo-2.jpeg` and wraps back to 1 after the last photo.
- Each cycle:
  1. wait 6 s (`PHOTO_SLIDESHOW_INTERVAL_MS`)
  2. fade out over 650 ms (`PHOTO_FADE_DURATION_MS`)
  3. decode the next photo while the screen is dark
  4. fade in
- If Spotify starts playing partway through a transition, the transition is cancelled and the opacity is restored in the background.
- Decoding with libjpeg-turbo reads straight from the file, so the compressed JPEG is never held fully in RAM. The decoder picks a scale:

  | Source is at least… | Decode scale |
  |---|---|
  | 8× the screen (3840×2176) | 1/8 |
  | 4× the screen (1920×1088) | 1/4 |
  | 2× the screen (960×544) | 1/2 |
  | smaller | 1/1 |

  LVGL then does the final contain-fit scaling with anti-aliasing.

### FreeRTOS Tasks

| Task | Stack | Priority | Role |
|---|---|---|---|
| `lvgl_task` | 8 KB | 5 | All UI updates, fades, mode switching, `lv_timer_handler()` every 20 ms |
| `spotify_https` | 8 KB | 5 | Token refresh, 1 s Now Playing polling, album art download/decode |
| `photo_slideshow` | 8 KB | 4 | Slideshow timing, SD decode of the next photo |
| `httpd` | 8 KB | default | Setup portal and photo upload |

### Persistent Storage (NVS)

| Namespace | Key | Content |
|---|---|---|
| `wifi_config` | `ssid` | Home Wi-Fi SSID (≤ 32 chars) |
| `wifi_config` | `password` | Home Wi-Fi password (≤ 64 chars) |
| `spotify` | `client_id` | Spotify app Client ID |
| `spotify` | `refresh` | Spotify Refresh Token (≤ 1023 chars) |

To wipe all saved settings, run `idf.py -p PORT erase-flash` and flash the firmware again.

### Memory Usage

| Buffer | Location | Size |
|---|---|---|
| LCD frame buffer | PSRAM | 480 × 272 × 2 B ≈ 255 KB |
| LVGL draw buffer | Internal RAM | 480 × 20 × 2 B = 19.2 KB |
| LVGL heap | Internal RAM | 64 KB |
| Album art JPEG download | PSRAM | 128 KB (temporary) |
| Album art RGB565 | PSRAM | about 150×150 × 2 B |
| SD photo RGB565 | PSRAM | up to about 480×272 × 2 B for portal uploads |
| Now Playing JSON | Heap | 16 KB (temporary) |
| Token response | Heap | 4 KB (temporary) |

---

## Getting Started

### 1. Install ESP-IDF

Install **ESP-IDF v6.1** by following the [official guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html), or use the ESP-IDF extension for VS Code / Eclipse. Then open an ESP-IDF terminal:

```bash
cd softAP
idf.py set-target esp32s3   # only needed if you start from a clean sdkconfig
```

### 2. Configure the Access Point

```bash
idf.py menuconfig
```

Under **Example Configuration**:

| Option | Default in this repo | Notes |
|---|---|---|
| WiFi SSID | `heart_steel` | Name of the setup network |
| WiFi Password | *(set in sdkconfig)* | 8–63 chars for WPA2; leave empty for an open AP |
| WiFi Channel | `1` | 1–13 |
| Maximal STA connections | `4` | Clients allowed on the setup AP |
| Enable GTK Rekeying | `y` | |
| GTK rekey interval | `600` s | |

The `sdkconfig` in the repo already has PSRAM, the custom partition table, the certificate bundle, LVGL with 16-bit colour, Montserrat 14 and the other required options enabled.

### 3. Build and Flash

Connect the CrowPanel over USB-C and run:

```bash
idf.py build
idf.py -p COM5 flash monitor     # Windows (use your port)
idf.py -p /dev/ttyUSB0 flash monitor   # Linux / macOS
```

Press `Ctrl+]` to exit the monitor.

### 4. Get Spotify Credentials

The device needs a **Client ID** and a **Refresh Token**. It does not do the browser login itself.

1. Go to the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard) and create an app.
2. Add a Redirect URI, for example `http://127.0.0.1:8888/callback`. Copy the **Client ID**.
3. Get a refresh token with the **Authorization Code with PKCE** flow, using at least the scope:
   - `user-read-currently-playing` (optionally also `user-read-playback-state`)

   The firmware refreshes the token **without a client secret**, using only `client_id`. That only works for refresh tokens issued through **PKCE**. A refresh token from the plain Authorization Code flow (with a client secret) will be rejected.
4. Keep the Client ID and Refresh Token. You will enter them in the portal.

### 5. Prepare the microSD Card

1. Format the card as **FAT32**.
2. Copy JPEG files to the **root** of the card, numbered one after another:
   ```
   /photo-1.jpeg
   /photo-2.jpeg
   /photo-3.jpeg
   ```
3. Use baseline JPEGs. Resizing them close to 480×272 beforehand saves decode time and RAM. Photos uploaded through the portal are already resized.

You can skip this step. Without a card the device still works as a Spotify display.

### 6. First-Time Setup

1. Power the board. It shows the Spotify card with *Spotify bekleniyor...*, or photo-1 if an SD card is inserted.
2. On your phone, join the **`heart_steel`** Wi-Fi network using the AP password.
3. Open **http://192.168.4.1**.
4. Save the **Spotify** credentials first. Saving Wi-Fi restarts the device.
5. Enter your **home Wi-Fi** SSID and password and press *Bağlan*. The device restarts.
6. After it restarts, the Wi-Fi icon turns green. Start playing something on Spotify; the screen should update within about a second.
7. You can keep uploading photos at any time through `heart_steel` → `http://192.168.4.1`.

Example serial output after a successful setup:

```
I (...) wifi softAP: Wi-Fi AP+STA modu baslatildi
I (...) wifi softAP: Kurulum SSID: heart_steel
I (...) wifi softAP: Kurulum IP: http://192.168.4.1
I (...) WEB: Web server baslatildi: http://192.168.4.1
I (...) wifi softAP: INTERNET BAGLANTISI HAZIR
I (...) SPOTIFY: ACCESS TOKEN ALINDI!
I (...) SPOTIFY: CANLI SPOTIFY TAKIBI BASLADI
I (...) SPOTIFY: SARKI: <track>
I (...) LVGL: ALBUM KAPAGI EKRANA VERILDI -> 150x150 scale=197
```

---

## HTTP API Reference

All endpoints are served on the softAP interface at `http://192.168.4.1`.

| Method | Path | Body | Response | Notes |
|---|---|---|---|---|
| `GET` | `/` | – | `text/html` | Setup portal |
| `POST` | `/connect` | `application/x-www-form-urlencoded`: `ssid`, `password` (body < 256 B) | HTML confirmation | Saves to NVS and **restarts the device** |
| `POST` | `/spotify` | `application/x-www-form-urlencoded`: `client_id`, `refresh_token` (body ≤ 1800 B) | HTML confirmation | Saves to NVS |
| `POST` | `/upload-photo` | raw `image/jpeg` (≤ 2 MB) | `{"success":true,"photo":N}` | Validates SOI/EOI markers; saves as `/sdcard/photo-N.jpeg` |

Error responses:

| Code | When |
|---|---|
| `400` | Bad or missing form fields, empty upload, or not a JPEG |
| `408` | Upload timed out |
| `413` | Upload larger than 2 MB |
| `500` | NVS or SD failure, or SD card not mounted |

Example upload with `curl` (while connected to `heart_steel`):

```bash
curl -X POST -H "Content-Type: image/jpeg" \
     --data-binary @my_photo.jpeg http://192.168.4.1/upload-photo
```

> Unlike the browser, `curl` sends the file as-is. Resize it yourself first.

---

## Configuration Reference

These compile-time constants are in [`softap_example_main.c`](softAP/main/softap_example_main.c):

| Constant | Value | Meaning |
|---|---|---|
| `PHOTO_SLIDESHOW_INTERVAL_MS` | `6000` | Time each photo stays on screen |
| `PHOTO_FADE_DURATION_MS` | `650` | Length of each fade-out / fade-in |
| `SD_MOUNT_POINT` | `/sdcard` | FAT mount point |
| `SD_MOSI/MISO/CLK/CS_GPIO` | 11 / 13 / 12 / 10 | SD card SPI pins |
| Spotify poll interval | 1000 ms | `vTaskDelay` in `spotify_https_task` |
| Token refresh margin | 300 s | Refresh this long before expiry |
| Token retry delay | 30 s | Wait after a failed token request |

---

## Known Limitations

- **The slideshow task only starts at boot,** and only if the card has more than one photo then. If the card starts with 0 or 1 photos, uploads are saved but you need to restart the device before they rotate.
- **Photo numbers must have no gaps.** The slideshow assumes `photo-1` … `photo-N` all exist, so a missing number shows up as a load error in the log and that slot is skipped. Only the `.jpeg` extension is recognised.
- **New Spotify credentials apply on the next start of the Spotify task.** In practice, restart the device after changing them.
- **The setup AP is always on.** There is no setting to turn it off after setup.
- **There is no OTA update partition.** Firmware updates go over USB.
- **The UI and portal text are in Turkish.**
- The firmware polls Spotify every second. That is fine for personal use, but Spotify may rate-limit (`429`) heavy use.

---

## Troubleshooting

| Symptom | What to check |
|---|---|
| Blank screen | Backlight GPIO 2 and LCD timings; check the serial log for `RGB LCD baslatildi` |
| `SD kart mount edilemedi` | Card is FAT32, properly inserted, ≤ 32 GB recommended |
| `photo-1.jpeg yuklenemedi` | File missing, wrong extension (`.jpg`) or progressive/corrupt JPEG |
| `RGB565 fotograf buffer'i ayrilamadi` | Photo too large; resize it or upload through the portal |
| Wi-Fi icon stays grey | Wrong SSID/password, a 5 GHz-only network, or WPA2 not available; re-enter Wi-Fi in the portal |
| `Kayitli Spotify bilgisi yok` | Spotify credentials not saved yet; save them, then restart |
| `Token endpoint HTTP Status: 400` | Refresh token invalid or revoked, or not issued with PKCE; generate a new one |
| `Currently Playing HTTP Status: 401` | Token expired early; it is refreshed automatically |
| Stuck on `BEKLIYOR` while music plays | Device has no internet, or the token lacks the `user-read-currently-playing` scope |
| Album art missing | Download failed or larger than 128 KB; see the `ALBUM` log lines |
| Cannot reach `192.168.4.1` | Turn off mobile data so the phone doesn't route around a Wi-Fi network with no internet |

---

## Security Notes

- Credentials are stored **unencrypted** in NVS. Anyone with physical access can read them from flash. Enable NVS encryption / flash encryption if this matters to you.
- The setup portal uses **plain HTTP** over the WPA2-protected AP. Use a strong AP password.
- The AP password is set in `sdkconfig`. If this repository is public, change it before committing, or keep `sdkconfig` out of version control.
- Secrets (tokens, passwords) are never printed to the serial log, and token buffers are zeroed after use.
- All outbound Spotify traffic uses HTTPS, verified against the ESP-IDF certificate bundle.
