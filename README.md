# sofa-ledstreifen
ws-led-strifen von esp8266 kontrolliert

---

## cyd-lichtershow

Bunte Animationsshow auf dem eingebauten 2,8″-TFT-Display (ILI9341, 320 × 240 px)
des **ESP32 Cheap Yellow Display** (ESP32-2432S028).

### Szenen

| Szene | Beschreibung |
|---|---|
| Regenbogen-Welle | Kontinuierliche Farbwelle läuft über den gesamten Bildschirm |
| Ripple | Konzentrischer Wellenring wächst vom Mittelpunkt nach außen |
| Plasma | Überlagerte Sinus-Wellen erzeugen klassischen Demo-Scene-Effekt |
| Farbbalken | Leuchtende Vertikalstreifen scrollen von links nach rechts |
| Sternenhimmel | Zufällig aufblitzende Sterne auf schwarzem Hintergrund |

### Hardware (CYD-Pinbelegung)

| Funktion | GPIO |
|---|---|
| TFT_MOSI | 13 |
| TFT_MISO | 12 |
| TFT_SCLK | 14 |
| TFT_CS | 15 |
| TFT_DC | 2 |
| TFT_BL (Backlight) | 21 |

### Voraussetzungen

* [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/) ≥ v5.0
* ESP32 Cheap Yellow Display (ESP32-2432S028 / CYD)

### Kompilieren & Flashen

```bash
cd cyd-lichtershow
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
