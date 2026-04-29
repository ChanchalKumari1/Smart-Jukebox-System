# ESP32 LyraT-Mini JukeBox – MP3 Player

ESP‑IDF / ESP‑ADF based **MP3 player** for **Espressif ESP32‑LyraT‑Mini V1.2** with ES8311 codec.  
Reads MP3 files from SD card, controllable via physical buttons or built‑in web dashboard.

---

## Hardware (pins used)

- **Board** : ESP32‑LyraT‑Mini V1.2
- **Audio codec** : ES8311 (initialised by audio_board_init)
---

## Software Features

- **Only MP3** files are supported (decoder: `mp3_decoder`)
- Scans `/sdcard/music/` for `.mp3` files
- Playback control: play,stop, volume (0‑100)
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

WiFi credentials can be changed in `jukebox.h` .

Default volume = 60.

### Build & Flash

```bash
idf.py build
idf.py flash monitor
