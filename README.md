# 🖥️ KVM Dongle - T-Dongle S3

**Versione stabile: v6.3**

Progetto KVM USB HID evoluto per LilyGo T-Dongle S3 (ESP32-S3). Trasforma il dongle in un dispositivo di controllo remoto per PC con supporto BLE, WiFi e USB HID.

---

## 📋 INDICE

1. [Caratteristiche](#-caratteristiche)
2. [Hardware richiesto](#-hardware-richiesto)
3. [Installazione](#-installazione)
4. [Prima configurazione](#-prima-configurazione)
5. [Uso quotidiano](#-uso-quotidiano)
6. [Modalità di connessione](#-modalità-di-connessione)
7. [File Manager](#-file-manager)
8. [Macro e Credenziali](#-macro-e-credenziali)
9. [Caratteri speciali](#-caratteri-speciali)
10. [Risoluzione problemi](#-risoluzione-problemi)
11. [Versioni](#-versioni)

---

## ✨ CARATTERISTICHE

| Funzionalità | Descrizione |
|:---|:---|
| **USB HID** | Tastiera e mouse emulati via USB |
| **BLE** | Controllo wireless da telefono/tablet |
| **WiFi AP** | Access Point integrato (SSID: KVM-S3) |
| **WiFi STA** | Connessione a reti WiFi esistenti |
| **Macro** | Predefinite (Win/Linux/BIOS) + personalizzabili |
| **Credenziali** | Login salvati con invio automatico |
| **File Manager** | Gestione file su LittleFS (memoria interna) |
| **Chat** | Invio testo con supporto UTF-8 |
| **Trackpad** | Mouse touch con click e scroll |
| **Tastiera virtuale** | QWERTY con tasti funzione e navigazione |
| **Auto-detect** | Rilevamento automatico del canale (BLE/WiFi) |

---

## 🛠️ HARDWARE RICHIESTO

| Componente | Specifiche |
|:---|:---|
| **Microcontrollore** | LilyGo T-Dongle S3 (ESP32-S3) |
| **USB** | Porta USB-A (connettore integrato) |
| **Alimentazione** | 5V via USB |

---

## 📦 INSTALLAZIONE

### File necessari

| File | Descrizione |
|:---|:---|
| `KVM_v6_3.ino` | Firmware per Arduino IDE |
| `KVM_v6_3.html` | Interfaccia utente (da caricare sul dongle) |

### 1. Impostazioni Arduino IDE

| Opzione | Valore |
|:---|:---|
| **Board** | `ESP32S3 Dev Module` |
| **USB Mode** | `USB-OTG (TinyUSB)` |
| **USB CDC On Boot** | `Enabled` |
| **Partition Scheme** | `Huge APP (3MB No OTA/1MB LittleFS)` |
| **PSRAM** | `Disabled` |

### 2. Carica il firmware

1. **Apri** `KVM_v6_3.ino` con Arduino IDE
2. **Compila** e **carica** sul T-Dongle S3
3. **Apri** il Serial Monitor (115200 baud)

### 3. Carica l'interfaccia HTML

1. **Forza AP**: tieni premuto BOOT per 3s all'avvio
2. **Connettiti** al WiFi `KVM-S3` (password: `12345678`)
3. **Apri** `http://192.168.4.1/wifi`
4. **Scorri** alla sezione "Interfaccia KVM"
5. **Clicca** "Carica interfaccia KVM"
6. **Seleziona** `KVM_v6_3.html`
7. **Aspetta** il 100%

**Metodo alternativo** (via File Manager):
1. **Apri** `http://192.168.4.1/` (se la pagina KVM è già caricata)
2. **Vai** su **Files**
3. **Elimina** `/kvm.html` (se esiste)
4. **Upload** del nuovo `KVM_v6_3.html`

---

## 🚀 PRIMA CONFIGURAZIONE

### Passo 1: Forza AP

1. **Collega** il T-Dongle S3 al PC
2. **Tieni premuto** il pulsante BOOT per 3 secondi
3. **Rilascia** BOOT
4. Il dongle si riavvia in modalità AP

### Passo 2: Connettiti al WiFi

1. **Cerca** reti WiFi
2. **Connettiti** a `KVM-S3` (password: `12345678`)

### Passo 3: Carica l'interfaccia

1. **Apri** il browser e vai a `http://192.168.4.1/wifi`
2. **Carica** `KVM_v6_3.html` nella sezione "Interfaccia KVM"
3. **Aspetta** il completamento

### Passo 4: Verifica

1. **Apri** `http://192.168.4.1/`
2. **Dovresti** vedere l'interfaccia KVM completa

---

## 📱 USO QUOTIDIANO

### Connessione in WiFi AP (consigliata)

1. **Connettiti** al WiFi `KVM-S3`
2. **Apri** `http://192.168.4.1/`
3. **Usa** il KVM dal browser

### Connessione in BLE

1. **Apri** la pagina KVM dal browser (o da GitHub)
2. **Clicca** su "Connetti" (BLE)
3. **Seleziona** `KVM-S3` dal dispositivo Bluetooth

### Connessione in WiFi STA (rete esistente)

1. **Connettiti** al dongle in AP
2. **Vai** su Impostazioni → Connessione WiFi
3. **Inserisci** SSID e password della tua rete
4. **Il dongle si riavvia** e si connette alla rete
5. **Trova l'IP** del dongle (dal router o da `http://kvm-s3.local`)
6. **Apri** l'IP nel browser

---

## 🔌 MODALITÀ DI CONNESSIONE

| Modalità | Come connettersi | Uso consigliato |
|:---|:---|:---|
| **BLE** | Pulsante "Connetti" nella pagina | Mobile, controllo rapido |
| **WiFi AP** | Connettiti a `KVM-S3` | PC, controllo stabile |
| **WiFi STA** | Connettiti alla rete di casa | Controllo remoto da rete locale |

**⚠️ Nota**: BLE e WiFi sono modalità esclusive. Cambiare modalità richiede riavvio del dongle.

---

## 📁 FILE MANAGER

| Funzione | Descrizione |
|:---|:---|
| **Navigazione** | Breadcrumb, cartelle, sottocartelle |
| **Upload** | Drag & drop, selezione multipla |
| **Download** | Download singolo file |
| **Editor TXT** | Modifica file di testo in-line |
| **Rinomina** | Clicca su ✏️ accanto al file/cartella |
| **Elimina** | Clicca su 🗑️ accanto al file/cartella |
| **Nuovo file** | Crea file di testo |
| **Nuova cartella** | Crea directory |

**Nota**: Il File Manager è disponibile in WiFi AP/STA. In BLE è disponibile in lettura (navigazione e visualizzazione).

---

## ⚡ MACRO E CREDENZIALI

### Macro predefinite

| Categoria | Esempi |
|:---|:---|
| **Windows** | Task Mgr, Ctrl+Alt+Del, Esegui, Explorer |
| **Linux** | apt update, df -h, top, lsblk |
| **BIOS** | F2, F10, F12, DEL |
| **DOS/CMD** | ipconfig, ping, chkdsk, sfc |

### Macro personalizzate

1. **Vai** al tab **Macro**
2. **Inserisci** Nome e Sequenza (es. `ping 8.8.8.8[ENTER]`)
3. **Clicca** su **+**
4. **La macro** è salvata sul dongle

### Credenziali

1. **Vai** al tab **Macro** → sezione Credenziali
2. **Inserisci** Nome, Username, Password
3. **Clicca** su **+**
4. **Clicca** sulla credenziale per usarla

---

## 🔤 CARATTERI SPECIALI

| Carattere | Metodo | Supporto |
|:---|:---|:---|
| **àèìòù** | Invio diretto (UTF-8) | ✅ Layout IT |
| **ÀÈÌÒÙ** | Invio diretto (UTF-8) | ✅ Layout IT |
| **€** | ALT+0128 | ✅ Universale |
| **! @ # $ &** | Invio diretto | ✅ Universale |
| **" '** | Invio diretto | ✅ Universale |

**Nota**: Il PC host deve avere layout tastiera IT per gli accenti.

---

## 🐛 RISOLUZIONE PROBLEMI

### La pagina KVM non si carica

1. **Apri** `http://192.168.4.1/wifi`
2. **Rimuovi** la pagina KVM esistente
3. **Ricarica** `KVM_v6_3.html`

### La pagina è vecchia/cache

1. **Premi** `Ctrl+F5` (refresh forzato)
2. **Oppure** apri in modalità incognito

### Accenti non funzionano

1. **Verifica** che il PC host abbia layout IT
2. **Controlla** Impostazioni → Modalità Invio → `AUTO`
3. **Ricarica** l'HTML con il fix

### WiFi AP non si vede

1. **Forza AP**: tieni premuto BOOT 3s all'avvio
2. **Collega** il dongle a una porta USB potente

### BLE non connette

1. **Verifica** che il browser supporti Web Bluetooth (Chrome/Edge)
2. **Su iPhone** usa l'app Bluefy

### Caricamento HTML fallisce

1. **Usa** il File Manager (se la pagina KVM è caricata)
2. **Oppure** usa curl: `curl -F "file=@KVM_v6_3.html" http://192.168.4.1/kvm/upload`

---

## 📌 VERSIONI

| Versione | Data | Modifiche |
|:---|:---|:---|
| **v6.3** | 2024 | Fix UTF-8 (Euro e caratteri a 3 byte). Rinomina file/cartelle. Upload binary-safe. File Manager in BLE. Sincronizzazione macro. UI migliorata. |

---

## 📝 NOTE TECNICHE

| Elemento | Specifica |
|:---|:---|
| **Memoria** | LittleFS (1MB) |
| **Storage macro** | `/macro_custom.json` |
| **Storage credenziali** | `/credenziali.json` |
| **Storage reti WiFi** | `/networks.json` |
| **Pagina KVM** | `/kvm.html` (LittleFS) |

---

## 🤝 CREDITI

- **Claude** (Anthropic) - Sviluppo e debug
- **DeepSeek** - Analisi e suggerimenti

---

## 📄 LICENZA

Progetto open source per uso personale e didattico.

---

**Buon utilizzo!** 🚀
