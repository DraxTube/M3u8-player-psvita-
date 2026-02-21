# M3U8 Player per PS Vita

Homebre per PS Vita che riproduce playlist **M3U8/M3U** locali.

## Funzionalità
- File browser per navigare il filesystem della Vita (ux0:, uma0:, ecc.)
- Parser M3U8 con supporto `#EXTINF` (titolo e durata)
- Riproduzione video/audio hardware via **SceAvPlayer**
- UI con tema scuro e barra di progresso
- Auto-avanzamento alla traccia successiva
- Supporto file M3U/M3U8 standard e playlist HLS (segmenti .ts)

## Controlli

### File Browser
| Tasto | Azione |
|-------|--------|
| D-Pad ↑↓ | Muovi selezione |
| Croce (✕) | Entra nella directory / Apri playlist |
| Cerchio (○) | Torna alla radice (ux0:) |

### Playlist
| Tasto | Azione |
|-------|--------|
| D-Pad ↑↓ | Muovi selezione |
| Croce (✕) | Riproduci traccia selezionata |
| Quadrato (□) | Torna al browser |

### Riproduzione
| Tasto | Azione |
|-------|--------|
| Croce (✕) | Pausa / Riprendi |
| Quadrato (□) | Stop (torna alla playlist) |
| L Trigger | Traccia precedente |
| R Trigger | Traccia successiva |

## Formato playlist supportato

```m3u
#EXTM3U
#EXTINF:180,Titolo Video 1
ux0:media/video1.mp4
#EXTINF:240,Titolo Video 2
ux0:media/video2.mp4
```

I percorsi possono essere:
- **Assoluti** (con device: es. `ux0:media/video.mp4`)
- **Relativi** (rispetto alla posizione del file .m3u8)

## Build

### Prerequisiti
- Account GitHub (il build avviene automaticamente in cloud via Actions)
- **Non serve** installare VitaSDK localmente

### Steps
1. Crea repository su GitHub e carica tutti i file
2. GitHub Actions compilerà automaticamente ad ogni push
3. Scarica il file `m3u8player.vpk` dagli Artifacts dell'Action
4. Installa il `.vpk` sulla Vita tramite VitaShell

### Build locale (opzionale)
```bash
export VITASDK=/path/to/vitasdk
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
make
```

## Installazione su Vita
1. Copia `m3u8player.vpk` sulla Vita (tramite FTP o USB)
2. Apri VitaShell → naviga al file `.vpk` → premi Croce per installare
3. L'app apparirà nella home con il nome **M3U8 Player**

## Struttura del progetto
```
.
├── CMakeLists.txt
├── src/
│   ├── main.c           # Loop principale e state machine
│   ├── m3u8_parser.c/h  # Parser M3U8
│   ├── player.c/h       # Wrapper SceAvPlayer
│   ├── file_browser.c/h # Navigatore filesystem
│   └── ui.c/h           # Utilità rendering
├── sce_sys/
│   ├── icon0.png
│   └── livearea/contents/
│       ├── template.xml
│       ├── bg.png
│       └── startup.png
└── .github/workflows/
    └── build.yml        # GitHub Actions CI
```

## Note
- **Formato video consigliato**: MP4 (H.264 + AAC), risoluzione max 960x544
- L'app richiede PS Vita con **HENkaku** o **Ensō** installato
- I file `.png` in `sce_sys/` sono placeholder: sostituiscili con immagini reali (icon0.png: 128x128px, bg.png: 840x500px, startup.png: 280x158px)
