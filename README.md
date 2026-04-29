# Smart JukeBox
<img width="1536" height="2048" alt="jukebox_digitalmonk, Jukebox_UI_digitalmonk " src="https://github.com/user-attachments/assets/9bb4b39d-456c-447b-942d-dbcc8569ff0e" />
ESP‑IDF / ESP‑ADF based **MP3 player** for **Espressif ESP32‑LyraT‑Mini V1.2** with ES8311 codec. Reads MP3 files from SD card, controllable via physical buttons or built‑in web dashboard.

 Built as part of **<a href="https://digitalmonk.biz/electronics-embedded-software-development-services/">Embedded Software Development Services**</a> — demonstrating real-time audio, WiFi networking, and hardware control on a resource-constrained ESP32 microcontroller.

---

## Hardware
<img width="1536" height="2048" alt="" src="https://github.com/user-attachments/assets/9bb4b39d-456c-447b-942d-dbcc8569ff0e" />

- **Board** : ESP32‑LyraT‑Mini V1.2
- **Audio codec** : ES8311 (initialised by `audio_board_init`)

---

## Software Features
<img width="1014" height="693" alt="hardware jpj" src="https://github.com/user-attachments/assets/da13b9f0-ffe0-45a2-aff1-d1af9d687f76" />


- **Only MP3** files are supported (decoder: `mp3_decoder`)
- Scans `/sdcard/music/` for `.mp3` files
- Playback control: play, stop, volume (0‑100)
- WiFi station mode
- Web server on port 80 – HTML dashboard with control and file upload
- Physical button debounce and long‑press detection

---

## Build & Flash


### Prerequisites

- [ESP‑IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/) (v4.4+)
- [ESP‑ADF](https://github.com/espressif/esp-adf) (audio development framework)
- SD card formatted as **FAT32** with a folder `music` at the root (`/sdcard/music/`)

### Configuration

WiFi credentials can be changed in `jukebox.h`.

Default volume = 60.

### Build & Flash

```bash
idf.py build
idf.py flash monitor
```
