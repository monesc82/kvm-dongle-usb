# 🖥️ KVM Dongle - T-Dongle S3

**Versione stabile: v6.3**

Un KVM USB HID evoluto basato su LilyGo T-Dongle S3 (ESP32-S3). Trasforma il dongle in un dispositivo per controllare qualsiasi PC, Mac, Linux, server o dispositivo con porta USB, tramite telefono, tablet o computer, via Bluetooth BLE o WiFi.

---

## 📖 COSA FA QUESTO PROGETTO

Immagina di avere un piccolo dongle USB che, inserito in un PC, ti permette di controllarlo da remoto usando il tuo telefono o un altro computer. Questo progetto fa esattamente questo:

- **🖱️ Controllo del mouse**: muovi il cursore, clicca, scrolla
- **⌨️ Digitazione**: scrivi testo sul PC usando la tastiera del telefono
- **⚡ Macro**: esegui comandi automatici con un solo tocco
- **🔑 Credenziali**: invia automaticamente username e password
- **📁 File Manager**: naviga e gestisci i file sul dongle

---

## 🎯 A COSA SERVE

| Scenario | Esempio |
|:---|:---|
| **Presentazioni** | Controlli il PC senza essere alla scrivania |
| **Home Theater** | Controlli il PC dal divano con il telefono |
| **Lab/Server** | Gestisci un server senza monitor o tastiera |
| **Accessibilità** | Usi il telefono come tastiera/mouse per il PC |
| **Supporto** | Aiuti un collega controllando il suo PC da remoto |
| **Sviluppo** | Testi applicazioni su diversi sistemi operativi |
| **Educazione** | Controlli il PC della classe dal tablet |

---

## 💻 SISTEMI COMPATIBILI

| Sistema | Compatibilità | Note |
|:---|:---|:---|
| **Windows 10/11** | ✅ Completa | Layout IT, US, UK, FR, DE, ES, PT |
| **Windows 7/8** | ✅ Completa | Layout IT, US, UK, FR, DE, ES, PT |
| **Windows Server** | ✅ Completa | Tutte le versioni |
| **macOS** | ✅ Completa | Supporto layout internazionali |
| **Linux (Ubuntu/Debian)** | ✅ Completa | Kernel 4.0+ |
| **Linux (Fedora/Arch)** | ✅ Completa | Kernel 4.0+ |
| **ChromeOS** | ✅ Completa | Supporto HID standard |
| **Android (USB OTG)** | ✅ Parziale | HID funziona, testato su Samsung/OnePlus |
| **iOS/iPadOS** | ⚠️ Limitato | HID funziona via USB-C su iPad |
| **BIOS/UEFI** | ✅ Completa | Supporto tastiera/mouse HID |
| **Console (PS4/PS5/Xbox)** | ⚠️ Limitato | Tastiera funziona, mouse no |
| **Smart TV** | ✅ Parziale | Tastiera funziona (testato su Samsung/LG) |

---

## 🖥️ CONTROLLO DI ALTRI DISPOSITIVI

Il dongle emula una tastiera e mouse USB standard. Può controllare:

| Dispositivo | Compatibilità | Note |
|:---|:---|:---|
| **PC/Desktop** | ✅ Completa | Windows, macOS, Linux |
| **Server** | ✅ Completa | Rack, blade, tower |
| **Laptop** | ✅ Completa | Qualsiasi marca |
| **Mini PC** | ✅ Completa | NUC, Raspberry Pi, ecc. |
| **Smart TV** | ✅ Parziale | Ingresso USB per tastiera |
| **Console (PS4/PS5/Xbox)** | ⚠️ Parziale | Tastiera funziona |
| **Thin Client** | ✅ Completa | HID standard |
| **Mac Mini/Studio** | ✅ Completa | Supporto HID |
| **Chromebook** | ✅ Completa | Supporto HID |
| **Router/Network appliance** | ⚠️ Limitato | Richiede porta USB host |

---

## 🔌 COME FUNZIONA
┌─────────────────────────────┐
│ IL TUO PC │
│ (USB collegato) │
└──────────────┬──────────────┘
│
▼
┌─────────────────────────────┐
│ T-Dongle S3 (KVM) │
│ - Tastiera USB │
│ - Mouse USB │
└──────────────┬──────────────┘
│
┌──────────────────────┼──────────────────────┐
│ │ │
▼ ▼ ▼
┌───────────────┐ ┌───────────────┐ ┌───────────────┐
│ BLE │ │ WiFi AP │ │ WiFi STA │
│ (telefono) │ │ (192.168.4.1)│ │ (rete casa) │
└───────────────┘ └───────────────┘ └───────────────┘

text

---

## 📱 L'INTERFACCIA UTENTE

L'interfaccia è una pagina web che si apre nel browser del telefono/PC. È divisa in **7 schede**:

### 1. 🖱️ Track (Trackpad)

| Funzione | Come si usa |
|:---|:---|
| **Muovere il mouse** | Trascina il dito sul trackpad |
| **Click sinistro** | Tocca con un dito |
| **Click destro** | Tocca con due dita |
| **Scroll** | Muovi due dita su/giù |
| **Doppio click** | Tocca due volte velocemente |

**Pulsanti aggiuntivi**:
- **Hold**: blocca il click (per trascinare)
- **Copia/Taglia/Incolla**: comandi rapidi

### 2. ⌨️ Tasti (Tastiera)

| Sezione | Cosa contiene |
|:---|:---|
| **Modificatori** | CTRL, ALT, SHIFT, WIN, Caps Lock |
| **Funzione** | F1-F12 (tasti funzione) |
| **Speciali** | ESC, TAB, Backspace, Invio, Canc |
| **Navigazione** | Home, End, PgSu, PgGiù, frecce |
| **QWERTY** | Tastiera alfabetica (espandibile) |

**Come usare le combinazioni**:
1. Premi CTRL (si illumina)
2. Premi il tasto successivo (es. C)
3. Il dongle invia CTRL+C

### 3. ⚡ Macro

**Macro predefinite** (già presenti):

| Categoria | Esempi |
|:---|:---|
| **Windows** | Task Manager, Ctrl+Alt+Del, Esegui, Explorer, Blocca PC, CMD, PowerShell |
| **Linux** | apt update, df -h, top, lsblk |
| **BIOS** | F2, F10, F12, DEL |
| **DOS** | ipconfig, ping, chkdsk, sfc |

**Macro personalizzate**:
1. **Nome**: descrizione della macro (es. "Ping Google")
2. **Sequenza**: la sequenza di tasti da inviare
3. **Clicca "+"** per salvare

**Formato sequenza**:
testo normale
[ENTER] → Invio
[TAB] → Tab
[CTRL+C] → Ctrl+C
[WIN+R] → Windows + R
[WAIT] → Pausa 600ms

text

**Esempi utili**:
ping 8.8.8.8[ENTER]
[WIN+R][WAIT]cmd[ENTER]
[CTRL+ALT+DEL]
sudo apt update[ENTER]
[WIN+R][WAIT]notepad[ENTER]

text

**Credenziali**:
1. **Nome**: descrizione (es. "Admin")
2. **Username**: nome utente
3. **Password**: password
4. **Clicca "+"** per salvare

Quando clicchi sulla credenziale, il dongle digita:
username [TAB] password [ENTER]

text

### 4. 📟 Terminal

| Comando | Cosa fa |
|:---|:---|
| **Windows** | ipconfig, ping, chkdsk, sfc, winget, netstat, tasklist |
| **Linux** | apt update, apt upgrade, df, free, top, ps, lsblk |
| **Personalizzato** | Scrivi qualsiasi comando e invia |

**Come usare**:
1. Scegli un comando predefinito (clicca sul pulsante)
2. Oppure scrivi un comando personalizzato
3. Il dongle lo digita sul PC e preme ENTER

### 5. 💬 Chat

| Funzione | Come si usa |
|:---|:---|
| **Scrivere testo** | Usa la tastiera del telefono |
| **Dettatura** | Usa il microfono del telefono (supporto iOS/Android) |
| **Inviare** | Clicca "Invia" |
| **Supporto accenti** | àèìòù€ funzionano |

**Perché serve**: scrivere testi lunghi, email, password complesse, comandi multilinea. Il testo viene digitato sul PC come se lo stessi scrivendo direttamente.

### 6. 📁 Files (File Manager)

| Funzione | Come si usa |
|:---|:---|
| **Navigare** | Clicca sulle cartelle per entrare |
| **Tornare indietro** | Clicca su ".. (su)" |
| **Upload** | Clicca su "Upload" e seleziona file |
| **Download** | Clicca su ⬇️ accanto al file |
| **Modificare** | Clicca su ✏️ accanto al file .txt |
| **Rinominare** | Clicca su ✏️ (viola) accanto al file/cartella |
| **Eliminare** | Clicca su 🗑️ accanto al file/cartella |
| **Nuovo file** | Clicca su "Nuovo" |
| **Nuova cartella** | Clicca su "Cartella" |

**Memoria**: i file sono salvati nel dongle (LittleFS), non sulla SD.

### 7. ⚙️ Impostazioni

| Sezione | Cosa puoi fare |
|:---|:---|
| **Layout tastiera** | Scegli IT, US, UK, FR, DE, ES, PT |
| **Modalità invio** | Auto, Windows (ALT), Unicode, ASCII |
| **Connessione WiFi** | Cambia tra BLE, AP, STA |
| **Sensibilità** | Regola la sensibilità del trackpad |
| **Soglia movimento** | Regola la risposta ai movimenti |
| **Intervallo** | Regola la frequenza di invio |
| **Sincronizzazione** | Ricarica macro e credenziali |

---

## 🔌 MODALITÀ DI CONNESSIONE

### 🔵 BLE (Bluetooth Low Energy)

| Come fare | Dettaglio |
|:---|:---|
| **1. Apri la pagina** | Apri `KVM_v6_3.html` sul browser del telefono |
| **2. Clicca Connetti** | Nella schermata iniziale, clicca su "BLE" |
| **3. Seleziona dispositivo** | Scegli `KVM-S3` dalla lista Bluetooth |
| **4. Usa il KVM** | La pagina passa automaticamente al KVM |

**Quando usarla**:
- ✅ Sei fuori casa (nessuna rete WiFi)
- ✅ Vuoi un accesso rapido (pochi secondi)
- ✅ Usi un telefono Android (supporto nativo)
- ⚠️ Su iPhone serve l'app Bluefy

### 📶 WiFi AP (Access Point)

| Come fare | Dettaglio |
|:---|:---|
| **1. Connettiti al WiFi** | Cerca la rete `KVM-S3` (password `12345678`) |
| **2. Apri il browser** | Vai a `http://192.168.4.1/` |
| **3. Usa il KVM** | La pagina si carica automaticamente |

**Quando usarla**:
- ✅ PC di casa/ufficio
- ✅ Controllo stabile e veloce
- ✅ Nessuna dipendenza da Internet
- ✅ Funziona su qualsiasi dispositivo

### 📶 WiFi STA (Station - rete esistente)

| Come fare | Dettaglio |
|:---|:---|
| **1. Connettiti in AP** | Come sopra, vai su `192.168.4.1` |
| **2. Vai su Impostazioni** | Tab Impostazioni → Connessione WiFi |
| **3. Inserisci SSID/password** | I dati della tua rete WiFi |
| **4. Il dongle si riavvia** | Si connette alla rete |
| **5. Trova l'IP** | Dal router o da `http://kvm-s3.local` |
| **6. Apri l'IP** | Usa il KVM da qualsiasi dispositivo sulla rete |

**Quando usarla**:
- ✅ Sei in ufficio/casa con WiFi
- ✅ Vuoi controllare da più dispositivi
- ✅ Connessione via hotspot iPhone/Android

---

## 🎬 GUIDA RAPIDA PER INIZIARE

### Primo avvio (dopo aver caricato il firmware)

1. **Collega il dongle** al PC
2. **Forza AP**: tieni premuto BOOT per 3 secondi
3. **Connettiti** al WiFi `KVM-S3` (pwd `12345678`)
4. **Apri** `http://192.168.4.1/`
5. **La pagina KVM** dovrebbe apparire

### Uso quotidiano

**Se sei davanti al PC**:
1. Inserisci il dongle nella USB
2. Connettiti al WiFi `KVM-S3`
3. Apri `http://192.168.4.1/`

**Se sei in mobilità**:
1. Tieni il dongle sempre in tasca
2. Inseriscilo nel PC da controllare
3. Usa l'app Bluefy (iPhone) o Chrome (Android)
4. Connettiti via BLE al dongle

### Esempi pratici

**Esempio 1: Accedere a un server Linux**

1. Inserisci il dongle nel server
2. Dal telefono, connettiti via BLE
3. Vai su **Chat**
4. Scrivi `ssh root@192.168.1.100` e invia
5. Il dongle digita il comando
6. Vai su **Macro** → Credenziali
7. Clicca su "Server" (username e password salvate)
8. Il dongle digita username, TAB, password, ENTER

**Esempio 2: Controllare una presentazione**

1. Inserisci il dongle nel PC della riunione
2. Dal telefono, connettiti al WiFi `KVM-S3`
3. Vai su **Track** e usa il trackpad
4. Le frecce su **Tasti** permettono di navigare le slide

**Esempio 3: Gestire un PC da casa**

1. Configura il dongle in modalità STA (rete di casa)
2. Trovane l'IP dal router
3. Apri l'IP dal browser del telefono
4. Usa il KVM come se fossi davanti al PC

---

## 🐛 PROBLEMI COMUNI

| Problema | Causa | Soluzione |
|:---|:---|:---|
| **Non vedo il WiFi KVM-S3** | AP non attivo | Forza AP (BOOT 3s all'avvio) |
| **Pagina KVM non si carica** | HTML non caricato | Vai su `/wifi` e carica l'HTML |
| **Accenti non funzionano** | Layout PC non IT | Cambia layout su Impostazioni |
| **BLE non connette** | Browser non supporta Web Bluetooth | Usa Chrome/Edge o Bluefy |
| **Dongle si riavvia** | Alimentazione insufficiente | Usa cavo USB di qualità, porta potente |
| **File Manager vuoto** | BLE non supporta upload/download | Usa WiFi per upload |
| **Macro non si salvano** | LittleFS pieno | Elimina file non necessari via File Manager |
| **Credenziali non funzionano** | Formato JSON corrotto | Elimina e ricrea la credenziale |

---

## 📁 STRUTTURA FILE

| File sul dongle | Contenuto |
|:---|:---|
| `/kvm.html` | Interfaccia utente |
| `/macro_custom.json` | Macro personalizzate |
| `/credenziali.json` | Credenziali salvate |
| `/networks.json` | Reti WiFi salvate |
| `/payloads/*.ducky` | Script DuckyScript da eseguire |

---

## ⚠️ NOTE IMPORTANTI

1. **BLE e WiFi sono esclusivi**: non possono funzionare contemporaneamente
2. **Cambiare modalità richiede riavvio**: il dongle si riavvia automaticamente
3. **Layout IT richiesto**: per accenti e caratteri speciali
4. **Caricare l'HTML**: fatto una volta, resta in memoria
5. **Backup**: esporta la configurazione da Impostazioni
6. **Alimentazione**: usa cavo USB di qualità, non hub USB economici
7. **Aggiornamenti**: l'HTML si aggiorna via upload, il firmware via Arduino IDE

---

## 📊 SPECIFICHE TECNICHE

| Elemento | Specifica |
|:---|:---|
| **Microcontrollore** | ESP32-S3 (LilyGo T-Dongle S3) |
| **Memoria Flash** | 16MB |
| **Memoria disponibile** | 1MB LittleFS |
| **Connessioni** | BLE, WiFi 2.4GHz |
| **USB** | USB-OTG (TinyUSB) |
| **Alimentazione** | 5V via USB |
| **Temperatura operativa** | -20°C ~ 85°C |
| **Dimensioni** | 50 x 20 x 10 mm |
| **Peso** | ~10g |

---

## 📝 VERSIONI

| Versione | Data | Modifiche principali |
|:---|:---|:---|
| **v6.3** | 2024 | Fix UTF-8, Euro, rinomina file/cartelle, upload binary-safe, File Manager in BLE |

---

**Buon utilizzo!** 🚀
