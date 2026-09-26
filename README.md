<div align="center">

# 🎮 ESP32-S3 GB/GBC Emulator

<img src="https://readme-typing-svg.demolab.com?font=Fira+Code&size=18&pause=1000&color=58A6FF&center=true&vCenter=true&width=440&lines=Game+Boy+%2F+Color+di+ESP32-S3;TFT+ST7789+%2B+I2S+Audio+%2B+Joystick;Build+otomatis+lewat+GitHub+Actions" alt="typing-svg" />

[![Build](https://img.shields.io/github/actions/workflow/status/USERNAME/REPO/build.yml?branch=main&label=build&logo=githubactions&logoColor=white)](../../actions)
[![Core](https://img.shields.io/badge/core-Peanut--GB-orange)](https://github.com/deltabeard/Peanut-GB)
[![Board](https://img.shields.io/badge/board-ESP32--S3%20N16R8-blueviolet?logo=espressif)](.)
[![License](https://img.shields.io/badge/license-MIT-green)](.)

</div>

---

## ⚙️ Hardware

| Komponen | Spek |
|---|---|
| 🖥️ Layar | IPS 1.14" ST7789, 240×135 |
| 🔊 Audio | MAX98357A (I2S) + speaker mini |
| 🕹️ Input | Joystick tactile, digital active-LOW |
| 💾 ROM | LittleFS internal, `/game.gb` |

## 🚀 Quick Start

```
1. taruh ROM      →  data/game.gb
2. cek pin di src/main.cpp (pin TFT ada di platformio.ini)
3. push ke GitHub  →  Actions build otomatis
4. download artifact di tab Actions → flash
```

<details>
<summary>📌 <b>Detail</b></summary>

**ROM** — rename jadi `game.gb`. Ekstensi asli `.gbc` gak masalah, Peanut-GB parse dari isi file.

**Pin**
- TFT → `platformio.ini` → `build_flags`
- Joystick & I2S → `src/main.cpp` → `PIN CONFIGURATION`

**Build** — `build.yml` narik `peanut_gb.h`, compile firmware + filesystem image, zip jadi satu artifact.

**Flash**
```bash
esptool.py --chip esp32s3 --port COMx --baud 921600 write_flash \
  0x0      bootloader.bin \
  0x8000   partitions.bin \
  0x10000  firmware.bin \
  0xC90000 littlefs.bin
```
atau `pio run -t upload && pio run -t uploadfs`.

</details>

## 🎨 Preview

```
┌────────────────────────────┐
│  ▓▓▓░░░  GAME BOY  ░░░▓▓▓  │
│                             │
│     ▲                      │
│  ◀     ▶      (A) (B)      │
│     ▼      SELECT START    │
└────────────────────────────┘
```

## 📝 Catatan

- Core-nya **DMG-only**, gak ada mode warna CGB. ROM `.gbc` yang CGB-compatible tetap jalan (grayscale), ROM CGB-only gak akan bisa.
- ROM **tidak** disertakan — hak cipta pemilik game. Repo public → masukin `data/` ke `.gitignore`.
- Letterbox/crop layar dihitung otomatis & simetris di `main.cpp`.
- Audio I2S masih skeleton — sesuaikan API `peanut_gb.h` versi terbaru di `audio_push_frame()`.

---

<div align="center">
<sub>by <a href="https://github.com/xiter00">@xiter00</a> · core by <a href="https://github.com/deltabeard/Peanut-GB">Peanut-GB</a></sub>
</div>
