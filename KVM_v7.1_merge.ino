/*
 * ============================================================
 * KVM Dongle Unified - T-Dongle S3 - v6.1_claude
 * ============================================================
 *
 * NOVITA' v6.1 RISPETTO v6.0:
 *   1. RIMOSSO tutto il codice SD Card (lettore hardware
 *      inaffidabile su questo modello — conflitti SPI/pin mai
 *      risolti in modo stabile). Il File Manager ora usa
 *      LittleFS (storage interno, sempre disponibile).
 *   2. RIMOSSO il redirect "/kvm" verso GitHub. La pagina KVM
 *      completa (touchpad, tastiera, macro, credenziali) viene
 *      ora servita DIRETTAMENTE dal dongle via LittleFS, quindi
 *      funziona anche senza Internet, da AP o da STA.
 *   3. La pagina KVM va caricata UNA SOLA VOLTA sul dongle
 *      tramite il pulsante "Carica interfaccia KVM" nella
 *      pagina di setup (/wifi) — da quel momento resta salvata
 *      in LittleFS e sopravvive ai riavvii.
 *   4. La pagina GitHub resta invariata come opzione alternativa
 *      di accesso via BLE (nessuna modifica richiesta, il file
 *      HTML è lo stesso — vedi KVM_v6_1.html).
 *   5. Payload DuckyScript-like ora eseguiti da LittleFS
 *      (cartella /payloads/) invece che da SD.
 *   6. Comandi BLE per File Manager (BLE_FS_LIST, BLE_FS_READ,
 *      BLE_FS_WRITE, BLE_FS_DELETE, BLE_FS_MKDIR) per accesso
 *      all'archivio anche in modalità BLE.
 *   7. Fix carattere Euro: ALT+0128 (universale, funziona su
 *      tutti i layout).
 *
 * IMPOSTAZIONI IDE ARDUINO:
 *   Board:            ESP32S3 Dev Module (LilyGo T-Dongle S3)
 *   USB Mode:         Hardware CDC and JTAG ← IMPORTANTE: NON USB-OTG!
 *   USB CDC On Boot:  Enabled
 *   Partition Scheme: Huge APP (3MB No OTA/1MB LittleFS) ← obbligatorio
 *   PSRAM:            disabled (T-Dongle S3 non ha PSRAM esterna)
 *
 * MODALITA' OPERATIVE (esclusive):
 *   - BLE   : solo Bluetooth LE, nessun WiFi attivo (massima stabilità)
 *   - AP    : Access Point WiFi proprio (SSID: KVM-S3), no BLE
 *   - STA   : connesso a rete WiFi esistente, no BLE
 *   Cambio modalità = riavvio del dongle (necessario per liberare la radio)
 *
 * PRIMA CONFIGURAZIONE (dopo aver caricato questo firmware):
 *   1. Il dongle parte in modalità BLE di default.
 *   2. Passare a modalità AP (dalla pagina GitHub via BLE, comando
 *      WIFI_AP, oppure tenendo premuto BOOT 3s all'avvio).
 *   3. Connettersi al WiFi "KVM-S3" (password: 12345678).
 *   4. Aprire http://192.168.4.1/wifi
 *   5. In fondo alla pagina, sezione "Interfaccia KVM": caricare
 *      il file KVM_v6_1.html tramite il pulsante di upload.
 *   6. Da quel momento http://192.168.4.1/ (o l'IP STA) mostra
 *      direttamente l'interfaccia KVM completa, senza Internet.
 * ============================================================
 */

#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <vector>
// ============================================================
// DISPLAY — v7.2: ESP-IDF LCD API + Adafruit_GFX + QR CODE
// ============================================================
// Il display ST7735S del T-Dongle S3 NON è compatibile con le
// librerie TFT_eSPI (crash StoreProhibited) e Adafruit_ST7735
// (non supporta BGR, invert, gap 1,26). Usiamo l'API LCD di
// ESP-IDF (come l'esempio ufficiale LilyGo) per l'inizializzazione,
// e Adafruit_GFX (software puro) per il disegno di testo e forme.
// ============================================================
#include <SPI.h>
#include <Adafruit_GFX.h>
#include "esp_idf_version.h"  // necessario per ESP_IDF_VERSION_VAL nel fix color_space/rgb_ele_order
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7735.h"  // driver ufficiale LilyGo per ST7735

// NOTA v7.1: "qrcode.h" con le VIRGOLETTE, non <qrcode.h> con le
// parentesi angolari. Il core ESP32 include un proprio componente
// interno chiamato anch'esso "qrcode" (usato per il WiFi
// Provisioning/RainMaker, API esp_qrcode_*), completamente diverso
// da questa libreria di Richard Moore (API qrcode_*, struct QRCode).
// Con <qrcode.h> il compilatore trova quello di ESP-IDF per primo e
// lo sketch non compila. Le virgolette forzano la ricerca prima
// nella cartella dello sketch stesso: copia qrcode.h e qrcode.c
// dalla cartella della libreria QRCode (Arduino/libraries/QRCode/src/)
// direttamente accanto a questo file .ino, cosi' non c'e' ambiguita'
// possibile, su nessuna macchina, indipendentemente da cos'altro e'
// installato nel core o nelle librerie globali.
#include "qrcode.h"

// Pin display ST7735 del T-Dongle S3 (confermati dal produttore)
// FIX core aggiornato: il compilatore più recente è più severo sulla
// conversione int->enum. Questi pin vengono usati solo dentro strutture
// esp_lcd che si aspettano gpio_num_t, quindi il cast qui risolve il
// problema alla fonte senza dover toccare ogni punto in cui sono usati.
#define TFT_CS   (gpio_num_t)4
#define TFT_DC   (gpio_num_t)2
#define TFT_RST  (gpio_num_t)1
#define TFT_MOSI (gpio_num_t)3
#define TFT_SCLK (gpio_num_t)5
#define TFT_BL   38   // backlight (ACTIVE LOW: LOW=ON, HIGH=OFF)

// Costanti display
#define LCD_WIDTH  160
#define LCD_HEIGHT 80

// Handle del pannello LCD (usato dall'API ESP-IDF)
static esp_lcd_panel_handle_t panel_handle = NULL;

// ★★★ v7.2: KVMDisplay — Adafruit_GFX + ESP-IDF LCD API ★★★
// Adafruit_GFX fornisce il disegno di testo e forme (software puro).
// ESP-IDF LCD API fornisce l'inizializzazione corretta del display.
// Un framebuffer DMA viene usato come ponte tra i due.
class KVMDisplay : public Adafruit_GFX {
private:
  uint16_t *fb;  // framebuffer (160*80*2 = 25600 byte)
public:
  KVMDisplay() : Adafruit_GFX(LCD_WIDTH, LCD_HEIGHT) {
    fb = NULL;
  }

  bool begin() {
    fb = (uint16_t *)heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * 2, MALLOC_CAP_DMA);
    if (!fb) {
      Serial.println("❌ Framebuffer DMA alloc failed");
      return false;
    }
    Serial.println("✅ Framebuffer DMA allocato");
    return true;
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || x >= _width || y < 0 || y >= _height) return;
    if (fb) fb[y * _width + x] = color;
  }

  void flush() {
    if (fb && panel_handle) {
      esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_WIDTH, LCD_HEIGHT, fb);
    }
  }

  void fillScreen(uint16_t color) override {
    if (fb) {
      size_t len = LCD_WIDTH * LCD_HEIGHT;
      for (size_t i = 0; i < len; i++) fb[i] = color;
    }
    flush();
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (!fb) return;
    for (int16_t j = y; j < y + h && j < _height; j++) {
      for (int16_t i = x; i < x + w && i < _width; i++) {
        if (i >= 0 && j >= 0) fb[j * _width + i] = color;
      }
    }
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    fillRect(x, y, w, 1, color);
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    fillRect(x, y, 1, h, color);
  }
};

KVMDisplay tft;

// ============================================================
// PROTOTIPI FUNZIONI
// ============================================================
// Modalità / avvio
String leggiModalitaAvvio();
void salvaModalita(String m);
bool bootButtonPremuto();

// WiFi
void setupWiFiAP();
void setupWiFiSTA();
void setupWebServer();
void handleWiFiCommandsBLE(String cmd);
void saveNetwork(String ssid, String pass);
void loadSavedNetworks();
void deleteNetwork(String ssid);
String scanNetworksJSON();

// Comandi HID generali
void eseguiComando(String cmd);

// ============================================================
// NUOVO v7.0 — prototipi display, QR code, bottone runtime
// ============================================================
void initDisplay();
void disegnaSchermata();
void disegnaQRCode(String testo, int x, int y, int scala);
void gestisciBottoneRuntime();
void cicloModalitaSuccessiva();
void resetAPDiFabbrica();
void eseguiCombinazione(String combo);
void eseguiSequenza(String seq);
void eseguiMacro(String id);
void inviaTesto(String testo);
void inviaCarattereAccentato(char c);
void inviaSequenzaALT(String seq);
void inviaBLE(String msg);
void resetTutto();
void setModoInvio(String modo);
void setLayout(String layout);

// Storage LittleFS (macro/credenziali)
void initStorage();
bool salvaSuFile(const char* path, String jsonContent);
String leggiFile(const char* path);
void salvaMacroCustom(String jsonItem);
void eliminaMacroCustom(String nome);
void salvaCredenziale(String jsonItem);
void eliminaCredenziale(String nome);
void eseguiCredenziale(String nome);
void inviaFileBLE(String prefix, const char* path);

// File Manager (LittleFS)
String fsListDirJSON(String path);
String fsReadFile(String path);
bool fsWriteFile(String path, String content);
bool fsAppendChunk(String path, String chunk, bool primoChunk);
bool fsDeleteFile(String path);
bool fsCreateDir(String path);
bool fsDeleteDir(String path);
String fsFileInfo(String path);
bool kvmPageDisponibile();

// Payload DuckyScript-like da LittleFS
void eseguiPayloadFS(String path);
void parseRigaPayload(String riga);

// ============================================================
// MODALITA' OPERATIVA (esclusiva: BLE | AP | STA)
// ============================================================
Preferences preferences;
String modalitaCorrente = "BLE";  // default sicuro al primo avvio
#define BOOT_BUTTON_PIN 0

// ============================================================
// NUOVO v7.0 — SOGLIE BOTTONE (durante il funzionamento normale,
// separate dalla pressione-all'avvio già esistente per forzare AP)
//   < 2000ms   → pressione breve:  cicla BLE → AP → STA → BLE
//   > 5000ms   → pressione lunga:  reset AP a SSID/password di
//                                  fabbrica (KVM-S3 / 12345678)
// Le due soglie sono volutamente distanti per non sovrapporsi
// mai per errore in un singolo gesto.
// ============================================================
#define BTN_SHORT_MAX_MS  2000    // < 2s = alterna schermata
#define BTN_MEDIUM_MIN_MS 2000    // >= 2s = cicla modalità connessione
#define BTN_LONG_MIN_MS   6000    // >= 6s = reset AP a valori di fabbrica
unsigned long btnPressStart = 0;
bool btnWasPressed = false;
bool btnMediumActionDone = false;
bool btnLongActionDone = false;
int schermataCorrente = 0;  // 0=QR, 1=info connessione, 2=istruzioni

// ============================================================
// CONFIGURAZIONE WIFI
// ============================================================
String ap_ssid = "KVM-S3";
String ap_password = "12345678";
String sta_ssid = "";
String sta_password = "";

struct SavedNetwork {
    String ssid;
    String password;
};
std::vector<SavedNetwork> savedNetworks;
const int MAX_SAVED_NETWORKS = 5;

// ============================================================
// WEBSERVER & DNS (attivi solo in modalita' AP/STA)
// ============================================================
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
bool webServerAttivo = false;

// ============================================================
// STORAGE (LittleFS) — sostituisce la SD Card (v6.1)
// Il campo "sdDisponibile" è mantenuto per compatibilità con la
// pagina HTML esistente (badge di stato), ma ora riflette la
// disponibilità di LittleFS, non più della SD.
// ============================================================
bool sdDisponibile = false;

// ============================================================
// PAGINA HTML CONFIGURAZIONE (embedded, servita su / quando WiFi attivo)
// ============================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>KVM-S3 Setup</title>
    <style>
        * { box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; max-width: 500px; margin: 20px auto; padding: 20px; background: #f5f5f5; }
        .card { background: white; padding: 20px; border-radius: 12px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); margin-bottom: 15px; }
        h1 { color: #007aff; margin-top: 0; font-size: 22px; }
        h2 { font-size: 16px; margin: 15px 0 10px; color: #333; }
        label { display: block; margin: 10px 0 5px; font-weight: 600; font-size: 14px; }
        input[type="text"], input[type="password"] { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 8px; box-sizing: border-box; font-size: 14px; }
        input:focus { outline: none; border-color: #007aff; }
        button { width: 100%; padding: 12px; background: #007aff; color: white; border: none; border-radius: 8px; font-size: 15px; font-weight: 600; cursor: pointer; margin-top: 10px; }
        button:active { transform: scale(0.96); opacity: 0.8; }
        button.secondary { background: #e0e0e0; color: #333; }
        button.danger { background: #ff3b30; }
        button.success { background: #34c759; }
        button.warning { background: #ff9500; }
        .status { margin: 10px 0; padding: 10px; border-radius: 8px; background: #e8f5e9; color: #2e7d32; font-size: 13px; }
        .status.error { background: #ffebee; color: #c62828; }
        .status.info { background: #e3f2fd; color: #0d47a1; }
        .info { font-size: 12px; color: #666; margin-top: 10px; line-height: 1.6; }
        .mode-toggle { display: flex; gap: 8px; margin: 10px 0; flex-wrap: wrap; }
        .mode-toggle button { flex: 1; min-width: 90px; padding: 10px; background: #e0e0e0; color: #333; border: none; border-radius: 8px; cursor: pointer; font-weight: 600; margin-top: 0; }
        .mode-toggle button.active { background: #007aff; color: white; }
        .network-item { display: flex; justify-content: space-between; align-items: center; padding: 10px; background: #f8f8f8; border-radius: 8px; margin-bottom: 5px; }
        .network-item .ssid { font-weight: 600; }
        .btn-small { padding: 4px 12px; font-size: 12px; width: auto; margin: 0; }
        .flex-row { display: flex; gap: 8px; }
        .flex-row .btn-small { flex: 1; }
        #toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #333; color: white; padding: 12px 24px; border-radius: 8px; font-size: 14px; opacity: 0; transition: opacity 0.3s; pointer-events: none; z-index: 999; }
        #toast.show { opacity: 1; }
        .scan-result { padding: 8px 10px; border-bottom: 1px solid #eee; display: flex; justify-content: space-between; align-items: center; }
        #scanResults { max-height: 200px; overflow-y: auto; margin-top: 8px; border: 1px solid #eee; border-radius: 8px; }
        .sd-badge { display: inline-block; padding: 3px 8px; border-radius: 10px; font-size: 11px; font-weight: 600; margin-left: 6px; }
        .sd-badge.ok { background: #e8f5e9; color: #2e7d32; }
        .sd-badge.no { background: #ffebee; color: #c62828; }
    </style>
</head>
<body>
    <div id="toast"></div>

    <div class="card">
        <h1>🔧 KVM-S3 Setup</h1>
        <div id="status"></div>

        <div class="mode-toggle">
            <button id="btnModeBLE" onclick="setMode('BLE')">🔵 BLE</button>
            <button id="btnModeAP" onclick="setMode('AP')">📡 AP</button>
            <button id="btnModeSTA" onclick="setMode('STA')">📶 STA</button>
        </div>

        <div id="staConfig" style="display:none;">
            <h2>📶 Connetti a WiFi</h2>
            <label>SSID</label>
            <input type="text" id="ssid" placeholder="Nome rete WiFi">
            <label>Password</label>
            <input type="password" id="password" placeholder="Password">
            <button class="success" onclick="connectSTA()">🔗 Connetti</button>

            <h2>📡 Scansiona reti</h2>
            <button class="warning" onclick="scanNetworks()">🔍 Scansiona reti WiFi</button>
            <div id="scanResults"></div>

            <h2>📋 Reti salvate</h2>
            <div id="networkList"></div>
        </div>

        <div id="apEditConfig" style="display:none;">
            <h2>🔧 Configura Access Point</h2>
            <label>Nome rete (SSID)</label>
            <input type="text" id="ap_ssid" value="KVM-S3">
            <label>Password (min 8 caratteri)</label>
            <input type="text" id="ap_password" value="12345678">
            <button class="success" onclick="updateAP()">💾 Salva e Riavvia AP</button>
        </div>

        <div class="info">
            <div id="infoText">Caricamento...</div>
            <div>Archivio: <span class="sd-badge" id="sdBadge">...</span></div>
        </div>

        <div style="margin-top:15px;border-top:1px solid #eee;padding-top:15px;">
            <h2>🖥️ Interfaccia KVM</h2>
            <div id="kvmPageStatus" class="status info">Verifica in corso...</div>

            <div id="kvmPageInstalled" style="display:none;">
                <a href="/kvm" style="display:block;padding:12px;background:#007aff;color:white;text-align:center;border-radius:8px;text-decoration:none;font-weight:600;font-size:15px;margin-bottom:8px;">
                    🖥️ Vai all'Interfaccia KVM
                </a>
                <button class="secondary" onclick="document.getElementById('kvmFileInput').click()">🔄 Sostituisci con nuova versione</button>
                <button class="danger btn-small" onclick="eliminaPaginaKVM()" style="width:auto;margin-top:8px;">🗑️ Rimuovi</button>
            </div>

            <div id="kvmPageMissing" style="display:none;">
                <p class="info" style="margin:0 0 10px;">
                    Nessuna interfaccia KVM caricata sul dongle. Carica il file
                    <code>KVM_v6_1.html</code> una sola volta: da quel momento
                    resterà salvato qui e funzionerà anche senza Internet.
                </p>
                <button class="success" onclick="document.getElementById('kvmFileInput').click()">📤 Carica interfaccia KVM</button>
            </div>

            <input type="file" id="kvmFileInput" accept=".html" style="display:none;" onchange="caricaPaginaKVM(this.files[0])">
            <div id="kvmUploadProgress" style="display:none;margin-top:8px;">
                <div class="status info">📤 Caricamento in corso... <span id="kvmUploadPct">0%</span></div>
            </div>
        </div>
    </div>

    <script>
        function showToast(msg, duration = 3000) {
            const t = document.getElementById('toast');
            t.textContent = msg;
            t.className = 'show';
            clearTimeout(t._timeout);
            t._timeout = setTimeout(() => t.className = '', duration);
        }

        async function getStatus() {
            try {
                const res = await fetch('/status');
                const data = await res.json();
                document.getElementById('infoText').innerHTML =
                    'Modalita: ' + data.mode + '<br>' +
                    'IP: ' + data.ip + '<br>' +
                    (data.mode === 'STA' ? 'Connesso a: ' + data.ssid : 'SSID AP: ' + data.ssid);

                document.getElementById('sdBadge').textContent = data.sd ? 'Disponibile' : 'Non rilevata';
                document.getElementById('sdBadge').className = 'sd-badge ' + (data.sd ? 'ok' : 'no');

                if (data.kvmPage) {
                    document.getElementById('kvmPageStatus').className = 'status';
                    document.getElementById('kvmPageStatus').textContent = '✅ Interfaccia KVM installata';
                    document.getElementById('kvmPageInstalled').style.display = 'block';
                    document.getElementById('kvmPageMissing').style.display = 'none';
                } else {
                    document.getElementById('kvmPageStatus').className = 'status error';
                    document.getElementById('kvmPageStatus').textContent = '⚠️ Interfaccia KVM non ancora caricata';
                    document.getElementById('kvmPageInstalled').style.display = 'none';
                    document.getElementById('kvmPageMissing').style.display = 'block';
                }

                ['BLE','AP','STA'].forEach(m => {
                    document.getElementById('btnMode' + m).classList.toggle('active', data.mode === m);
                });
                document.getElementById('staConfig').style.display = (data.mode === 'STA') ? 'block' : 'none';
                document.getElementById('apEditConfig').style.display = (data.mode === 'AP') ? 'block' : 'none';
                if (data.mode === 'AP') document.getElementById('ap_ssid').value = data.ssid || 'KVM-S3';

                loadNetworks();
            } catch(e) { console.error(e); }
        }

        function caricaPaginaKVM(file) {
            if (!file) return;
            if (!file.name.endsWith('.html')) {
                showToast('⚠️ Seleziona un file .html'); return;
            }
            const form = new FormData();
            form.append('file', file);
            document.getElementById('kvmUploadProgress').style.display = 'block';

            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/kvm/upload', true);
            xhr.upload.onprogress = (e) => {
                if (e.lengthComputable) {
                    const pct = Math.round((e.loaded / e.total) * 100);
                    document.getElementById('kvmUploadPct').textContent = pct + '%';
                }
            };
            xhr.onload = () => {
                document.getElementById('kvmUploadProgress').style.display = 'none';
                if (xhr.status === 200) {
                    showToast('✅ Interfaccia KVM caricata con successo');
                    getStatus();
                } else {
                    // FIX v7.1: mostra il messaggio reale del dongle (es. spazio
                    // insufficiente o upload troncato) invece del solo codice
                    // HTTP, cosi si capisce subito la causa vera
                    let msg = 'Errore ' + xhr.status;
                    try { msg = JSON.parse(xhr.responseText).message || msg; } catch(e) {}
                    showToast('❌ ' + msg);
                }
            };
            xhr.onerror = () => {
                document.getElementById('kvmUploadProgress').style.display = 'none';
                showToast('❌ Errore di rete durante il caricamento');
            };
            xhr.send(form);
        }

        async function eliminaPaginaKVM() {
            if (!confirm('Rimuovere l\'interfaccia KVM dal dongle? Potrai ricaricarla in qualsiasi momento.')) return;
            try {
                await fetch('/kvm/delete');
                showToast('🗑️ Interfaccia KVM rimossa');
                getStatus();
            } catch(e) { showToast('❌ Errore: ' + e.message); }
        }

        async function loadNetworks() {
            try {
                const res = await fetch('/networks');
                const data = await res.json();
                const list = document.getElementById('networkList');
                list.innerHTML = '';
                if (data.networks && data.networks.length > 0) {
                    data.networks.forEach(n => {
                        const div = document.createElement('div');
                        div.className = 'network-item';
                        div.innerHTML = `<span class="ssid">📶 ${n.ssid}</span>
                            <div class="flex-row">
                                <button class="btn-small success" onclick="connectSaved('${n.ssid}')">Connetti</button>
                                <button class="btn-small danger" onclick="deleteNetwork('${n.ssid}')">✕</button>
                            </div>`;
                        list.appendChild(div);
                    });
                } else {
                    list.innerHTML = '<div style="color:#999;font-size:13px;padding:10px;">Nessuna rete salvata</div>';
                }
            } catch(e) { console.error(e); }
        }

        async function setMode(mode) {
            if (mode === 'AP') {
                // mostra config invece di passare subito (l'utente potrebbe voler editare SSID/pass)
                document.getElementById('apEditConfig').style.display = 'block';
                document.getElementById('staConfig').style.display = 'none';
                ['BLE','AP','STA'].forEach(m => document.getElementById('btnMode'+m).classList.toggle('active', m === 'AP'));
                return;
            }
            if (mode === 'STA') {
                document.getElementById('staConfig').style.display = 'block';
                document.getElementById('apEditConfig').style.display = 'none';
                ['BLE','AP','STA'].forEach(m => document.getElementById('btnMode'+m).classList.toggle('active', m === 'STA'));
                return;
            }
            // BLE: richiede conferma perche' disattiva il WiFi (perderai questa pagina)
            if (!confirm('Passare a modalita BLE disattivera il WiFi e questa pagina diventera irraggiungibile. Continuare?')) return;
            const res = await fetch('/mode?mode=BLE');
            const data = await res.json();
            mostraAttesaRiavvio(data.message, null);
            showToast(data.message);
        }

        async function connectSTA() {
            const ssid = document.getElementById('ssid').value.trim();
            const pass = document.getElementById('password').value.trim();
            if (!ssid) { showToast('Inserisci SSID', 2000); return; }
            document.getElementById('status').innerHTML = '<div class="status info">🔄 Connessione in corso...</div>';
            const res = await fetch('/connect?ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass));
            const data = await res.json();
            mostraAttesaRiavvio(data.message, null);
            showToast(data.message);
        }

        async function connectSaved(ssid) {
            document.getElementById('status').innerHTML = '<div class="status info">🔄 Connessione a ' + ssid + '...</div>';
            const res = await fetch('/connect_saved?ssid=' + encodeURIComponent(ssid));
            const data = await res.json();
            mostraAttesaRiavvio(data.message, null);
            showToast(data.message);
        }

        async function deleteNetwork(ssid) {
            if (!confirm('Rimuovere la rete "' + ssid + '"?')) return;
            const res = await fetch('/delete_network?ssid=' + encodeURIComponent(ssid));
            const data = await res.json();
            showToast(data.message);
            loadNetworks();
        }

        async function updateAP() {
            const ssid = document.getElementById('ap_ssid').value.trim();
            const pass = document.getElementById('ap_password').value.trim();
            if (!ssid) { showToast('Inserisci SSID AP', 2000); return; }
            if (pass.length > 0 && pass.length < 8) { showToast('Password min 8 caratteri', 2000); return; }
            const res = await fetch('/ap?ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass));
            const data = await res.json();
            mostraAttesaRiavvio(data.message, '192.168.4.1');
            showToast(data.message);
        }

        function mostraAttesaRiavvio(msg, knownIP) {
            document.getElementById('status').innerHTML =
                '<div class="status info">⏳ ' + msg + '<br><small>Riconnessione automatica...</small></div>';
            if (!knownIP) {
                document.getElementById('status').innerHTML +=
                    '<div class="status info" style="margin-top:6px;">ℹ️ Se la modalita cambia IP, riconnettiti manualmente.</div>';
                return;
            }
            let tentativi = 0;
            const timer = setInterval(async () => {
                tentativi++;
                try {
                    const res = await fetch('http://' + knownIP + '/status', { signal: AbortSignal.timeout(1500) });
                    if (res.ok) { clearInterval(timer); showToast('✅ Dispositivo online'); location.href = 'http://' + knownIP + '/'; }
                } catch(e) {}
                if (tentativi > 30) clearInterval(timer);
            }, 2000);
        }

        async function scanNetworks() {
            const results = document.getElementById('scanResults');
            results.innerHTML = '🔍 Scansione in corso...';
            try {
                const res = await fetch('/scan');
                const data = await res.json();
                results.innerHTML = '';
                if (!data.networks || data.networks.length === 0) {
                    results.innerHTML = '<div style="padding:10px;color:#999;">📡 Nessuna rete trovata</div>';
                    return;
                }
                data.networks.sort((a, b) => b.rssi - a.rssi);
                data.networks.forEach(n => {
                    const div = document.createElement('div');
                    div.className = 'scan-result';
                    let signalIcon = n.rssi > -50 ? '🟢' : n.rssi > -65 ? '🟡' : '🔴';
                    let security = n.encryption === 0 ? '🔓' : '🔒';
                    const ssidEsc = n.ssid.replace(/'/g, "\\'");
                    div.innerHTML = `<span class="ssid">${signalIcon} ${n.ssid} ${security}</span>
                        <span style="font-size:11px;color:#666;">${n.rssi} dBm</span>
                        <button class="btn-small success" onclick="selezionaRete('${ssidEsc}', ${n.encryption === 0})">Sel.</button>`;
                    results.appendChild(div);
                });
            } catch(e) { results.innerHTML = '❌ Errore scansione: ' + e.message; }
        }

        function selezionaRete(ssid, isOpen) {
            document.getElementById('ssid').value = ssid;
            document.getElementById('password').value = '';
            const pwdField = document.getElementById('password');
            pwdField.scrollIntoView({ behavior: 'smooth', block: 'center' });
            if (isOpen) showToast('Rete aperta — premi Connetti');
            else { pwdField.focus(); showToast('SSID selezionato — inserisci la password'); }
        }

        getStatus();
        setInterval(getStatus, 10000);
    </script>
</body>
</html>
)rawliteral";

// ============================================================
// CLASSI BLE E HID
// ============================================================
USBHIDKeyboard Keyboard;
USBHIDMouse Mouse;

#define SERVICE_UUID      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLECharacteristic *pTX = nullptr;
BLECharacteristic *pRX = nullptr;
BLEServer *pBLEServer = nullptr;
bool deviceConnected = false;
String bleBuffer = "";

float mouseSmoothX = 0, mouseSmoothY = 0;
const float SMOOTH = 0.20;

bool holdModCtrl = false, holdModAlt = false;
bool holdModShift = false, holdModWin = false;
bool holdActive = false;
bool capsLockActive = false;

// ── Layout e modalità invio ─────────────────────────────────
enum LayoutTastiera { LAYOUT_IT, LAYOUT_US, LAYOUT_UK, LAYOUT_FR,
                      LAYOUT_DE, LAYOUT_ES, LAYOUT_PT, LAYOUT_ASCII };
enum ModoInvio      { MODO_AUTO, MODO_ALT, MODO_UNICODE, MODO_ASCII };

LayoutTastiera layoutCorrente = LAYOUT_IT;
ModoInvio      modoCorrente   = MODO_AUTO;

// ============================================================
// FIX v6.0 — ACCENTI: invio diretto via Keyboard.write() con
// keycode HID nativo del layout IT, invece di ALT+numerico.
// Il layout KeyboardLayout_it_IT di Arduino-ESP32 (TinyUSB) mappa
// gia' i tasti fisici della tastiera italiana: i caratteri accentati
// italiani si trovano sui tasti dedicati (è, à, ò, ù, ì) della
// disposizione IT, quindi Keyboard.write((uint8_t)c) con il layout
// IT caricato funziona per la maggior parte dei casi se il sistema
// operativo host ha anch'esso layout IT impostato.
//
// Il problema reale di v5.28 era che Keyboard.print(String(c))
// itera sui BYTE della stringa UTF-8 (2 byte per carattere accentato)
// anziche' sul carattere Unicode, inviando byte spuri.
//
// SOLUZIONE: decodifica UTF-8 a runtime in inviaTesto(), poi
// invio carattere singolo via Keyboard.write() per i caratteri ASCII
// e via inviaCarattereAccentato() per i caratteri accentati noti,
// con fallback automatico ad ALT+codice SOLO se modoCorrente=MODO_ALT
// (utile per Windows con tastiera USA/UK dove il layout IT lato host
// non e' disponibile).
// ============================================================
struct AccentInfo { uint16_t codepoint; const char* utf8; int altCode; };
AccentInfo accentTable[] = {
  { 0x00E0, "à", 224 }, { 0x00E8, "è", 232 }, { 0x00E9, "é", 233 },
  { 0x00EC, "ì", 236 }, { 0x00F2, "ò", 242 }, { 0x00F9, "ù", 249 },
  { 0x00C0, "À", 192 }, { 0x00C8, "È", 200 }, { 0x00C9, "É", 201 },
  { 0x00CC, "Ì", 204 }, { 0x00D2, "Ò", 210 }, { 0x00D9, "Ù", 217 },
};
#define NUM_ACCENTI (sizeof(accentTable)/sizeof(accentTable[0]))

// ── Macro predefinite ─────────────────────────────────────────
struct Macro { const char* id; const char* label; const char* sequenza; };
Macro macroList[] = {
  {"win_cad",      "Ctrl+Alt+Del",       "[CTRL+ALT+DEL]"},
  {"win_task_mgr", "Task Manager",       "[CTRL+SHIFT+ESC]"},
  {"win_lock",     "Blocca PC",          "[WIN+L]"},
  {"win_run",      "Esegui...",          "[WIN+R]"},
  {"win_explorer", "Esplora Risorse",    "[WIN+E]"},
  {"win_devmgmt",  "Gestione Disp.",     "[WIN+R][WAIT]devmgmt.msc[ENTER]"},
  {"win_control",  "Pannello Controllo", "[WIN+R][WAIT]control[ENTER]"},
  {"win_cmd",      "CMD",                "[WIN+R][WAIT]cmd[ENTER]"},
  {"win_ps",       "PowerShell",         "[WIN+R][WAIT]powershell[ENTER]"},
  {"win_terminal", "Terminale Win",      "[WIN+R][WAIT]wt[ENTER]"},
  {"win_terminal_admin", "Terminale Win (Admin)", "[WIN+X][WAIT]a[WAIT][ALT+Y]"},
  {"dos_ipconfig", "ipconfig /all",      "ipconfig /all[ENTER]"},
  {"dos_ping",     "ping 8.8.8.8",       "ping 8.8.8.8[ENTER]"},
  {"dos_netstat",  "netstat -ano",        "netstat -ano[ENTER]"},
  {"dos_chkdsk",   "chkdsk /f /r",       "chkdsk /f /r[ENTER]"},
  {"dos_sfc",      "sfc /scannow",        "sfc /scannow[ENTER]"},
  {"dos_winget_up","winget upgrade",      "winget upgrade --all[ENTER]"},
  {"dos_tasklist", "tasklist",            "tasklist[ENTER]"},
  {"lnx_term",     "Terminale Linux",    "[CTRL+ALT+T]"},
  {"lnx_update",   "apt update",         "sudo apt update[ENTER]"},
  {"lnx_upgrade",  "apt upgrade",        "sudo apt upgrade -y[ENTER]"},
  {"lnx_clean",    "apt autoremove",     "sudo apt autoremove -y[ENTER]"},
  {"lnx_free",     "free -h",            "free -h[ENTER]"},
  {"lnx_df",       "df -h",              "df -h[ENTER]"},
  {"lnx_top",      "top",                "top[ENTER]"},
  {"lnx_lsblk",    "lsblk",              "lsblk[ENTER]"},
  {"lnx_dmesg",    "dmesg tail",         "dmesg -T | tail -30[ENTER]"},
  {"lnx_ss",       "ss -tulpn",          "ss -tulpn[ENTER]"},
  {"bios_f2",      "BIOS F2",            "[F2]"},
  {"bios_f10",     "BIOS F10",           "[F10]"},
  {"bios_f12",     "Boot Menu F12",      "[F12]"},
  {"bios_del",     "BIOS DEL",           "[DELETE]"},
  {"dos_systeminfo","System info",       "systeminfo[ENTER]"},
  {"dos_wmic_disk", "Dischi status",      "wmic diskdrive get model,serialnumber,status[ENTER]"},
  {"dos_wmic_volume","Volumi dischi",     "wmic logicaldisk get caption,filesystem,freespace,size[ENTER]"},
  {"lnx_term_admin","Terminale Linux Admin","[CTRL+ALT+T][WAIT]sudo -i[ENTER]"},
  {"lnx_lsblk_f",   "lsblk -f",           "lsblk -f[ENTER]"},
  {"lnx_blkid",     "sudo blkid",         "sudo blkid[ENTER]"},
  {"lnx_df_i",      "df -i",              "df -i[ENTER]"},
  {"lnx_fdisk",     "sudo fdisk -l",      "sudo fdisk -l[ENTER]"},
  {"lnx_journal",   "journalctl -xe",     "sudo journalctl -xe | tail -40[ENTER]"},
  // ★★★ v7.2: macro aggiunte per allineamento con interfaccia HTML ★★★
  // ★★★ v7.2: macro aggiunte per allineamento con interfaccia HTML ★★★
  // Terminale Windows (admin)
  {"win_cmd_admin", "CMD Admin",          "[WIN+R][WAIT]cmd[ENTER][WAIT][ALT+Y]"},
  {"win_ps_admin",  "PowerShell Admin",   "[WIN+R][WAIT]powershell[ENTER][WAIT][ALT+Y]"},
  // Diagnostica rete (aggiuntive)
  {"dos_tracert",   "tracert 8.8.8.8",    "tracert 8.8.8.8[ENTER]"},
  {"dos_nslookup",  "nslookup google.com","nslookup google.com[ENTER]"},
  {"dos_pathping",  "pathping 8.8.8.8",   "pathping 8.8.8.8[ENTER]"},
  // Diagnostica dischi Windows (aggiuntive)
  {"dos_dism",      "DISM ripristino",    "DISM /Online /Cleanup-Image /RestoreHealth[ENTER]"},
  {"dos_diskpart",  "diskpart list",      "diskpart[ENTER][WAIT]list disk[ENTER]"},
  {"dos_fsutil",    "fsutil behavior",    "fsutil behavior query[ENTER]"},
  {"dos_cleanmgr",  "cleanmgr pulizia",   "cleanmgr /sageset:1[ENTER]"},
  {"dos_defrag",    "defrag C:",          "defrag C: /O[ENTER]"},
  // Sistema Windows (aggiuntive)
  {"dos_msinfo",    "msinfo32",           "msinfo32[ENTER]"},
  {"dos_driverquery","driverquery /v",    "driverquery /v[ENTER]"},
  {"dos_powercfg",  "powercfg energia",   "powercfg /energy[ENTER]"},
  // Diagnostica dischi Linux (aggiuntive)
  {"lnx_smartctl",  "smartctl /dev/sda",  "sudo smartctl -a /dev/sda[ENTER]"},
  {"lnx_fsck",      "fsck -Af",           "sudo fsck -Af[ENTER]"},
  {"lnx_du",        "du -sh /*",          "du -sh /*[ENTER]"},
  {"lnx_iostat",    "iostat -x 1 3",      "iostat -x 1 3[ENTER]"},
  // Sistema Linux (aggiuntive)
  {"lnx_uname",     "uname -a",           "uname -a[ENTER]"},
  // Alias per compatibilità HTML (lnx_journalctl vs lnx_journal)
  {"lnx_journalctl","journalctl -xe",     "sudo journalctl -xe | tail -40[ENTER]"},
};
#define NUM_MACRO (sizeof(macroList)/sizeof(macroList[0]))

// ── Storage LittleFS ─────────────────────────────────────────
#define FILE_MACRO    "/macro_custom.json"
#define FILE_CRED     "/credenziali.json"
#define FILE_NETWORKS "/networks.json"

void initStorage() {
  if (!LittleFS.begin(true)) {
    Serial.println("❌ LittleFS mount FAIL");
    sdDisponibile = false;
    return;
  }
  Serial.println("✅ LittleFS OK");
  sdDisponibile = true;
  if (!LittleFS.exists(FILE_MACRO))    { File f = LittleFS.open(FILE_MACRO, "w");    f.print("[]"); f.close(); }
  if (!LittleFS.exists(FILE_CRED))     { File f = LittleFS.open(FILE_CRED, "w");     f.print("[]"); f.close(); }
  if (!LittleFS.exists(FILE_NETWORKS)) { File f = LittleFS.open(FILE_NETWORKS, "w"); f.print("[]"); f.close(); }
  if (!LittleFS.exists("/payloads"))   { LittleFS.mkdir("/payloads"); }
}

bool salvaSuFile(const char* path, String jsonContent) {
  File f = LittleFS.open(path, "w");
  if (!f) { Serial.println("❌ Impossibile aprire " + String(path)); return false; }
  f.print(jsonContent);
  f.close();
  File check = LittleFS.open(path, "r");
  if (!check) return false;
  String content = check.readString();
  check.close();
  if (content != jsonContent) { Serial.println("❌ Verifica fallita su " + String(path)); return false; }
  return true;
}

String leggiFile(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return "[]";
  String s = f.readString();
  f.close();
  return s;
}

// ============================================================
// GESTIONE RETI SALVATE
// ============================================================
void saveNetwork(String ssid, String pass) {
  String existing = leggiFile(FILE_NETWORKS);
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, existing);
  JsonArray arr = doc.as<JsonArray>();
  for (int i = 0; i < (int)arr.size(); i++) {
    if (String(arr[i]["s"].as<const char*>()) == ssid) { arr.remove(i); i--; }
  }
  JsonObject newNet = arr.createNestedObject();
  newNet["s"] = ssid;
  newNet["p"] = pass;
  while ((int)arr.size() > MAX_SAVED_NETWORKS) arr.remove(arr.size() - 1);
  String out; serializeJson(doc, out);
  salvaSuFile(FILE_NETWORKS, out);
  loadSavedNetworks();
}

void loadSavedNetworks() {
  savedNetworks.clear();
  String existing = leggiFile(FILE_NETWORKS);
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, existing);
  JsonArray arr = doc.as<JsonArray>();
  for (int i = 0; i < (int)arr.size(); i++) {
    SavedNetwork net;
    net.ssid     = arr[i]["s"].as<String>();
    net.password = arr[i]["p"].as<String>();
    savedNetworks.push_back(net);
  }
}

void deleteNetwork(String ssid) {
  String existing = leggiFile(FILE_NETWORKS);
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, existing);
  JsonArray arr = doc.as<JsonArray>();
  for (int i = 0; i < (int)arr.size(); i++) {
    if (String(arr[i]["s"].as<const char*>()) == ssid) { arr.remove(i); break; }
  }
  String out; serializeJson(doc, out);
  salvaSuFile(FILE_NETWORKS, out);
  loadSavedNetworks();
}

String scanNetworksJSON() {
  String json = "{\"networks\":[";
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":"  + String(WiFi.RSSI(i)) + ",";
    json += "\"encryption\":" + String(WiFi.encryptionType(i)) + "}";
  }
  json += "]}";
  WiFi.scanDelete();
  return json;
}

// ============================================================
// SALVATAGGIO MACRO E CREDENZIALI
// ============================================================
void salvaMacroCustom(String jsonItem) {
  String existing = leggiFile(FILE_MACRO);
  DynamicJsonDocument docList(8192);
  deserializeJson(docList, existing);
  JsonArray arr = docList.as<JsonArray>();
  DynamicJsonDocument docItem(512);
  DeserializationError err = deserializeJson(docItem, jsonItem);
  if (err) { inviaBLE("ERR:JSON_MACRO\n"); return; }
  const char* nome = docItem["n"];
  bool found = false;
  for (int i = 0; i < (int)arr.size(); i++) {
    if (String(arr[i]["n"].as<const char*>()) == String(nome)) { arr[i]["s"] = docItem["s"].as<const char*>(); found = true; break; }
  }
  if (!found) {
    JsonObject newItem = arr.createNestedObject();
    newItem["n"] = docItem["n"].as<const char*>();
    newItem["s"] = docItem["s"].as<const char*>();
  }
  String out; serializeJson(docList, out);
  if (salvaSuFile(FILE_MACRO, out)) inviaBLE("MACRO_SAVED:" + String(nome) + "\n");
  else inviaBLE("ERR:SAVE_MACRO\n");
}

void eliminaMacroCustom(String nome) {
  String existing = leggiFile(FILE_MACRO);
  DynamicJsonDocument doc(8192);
  deserializeJson(doc, existing);
  JsonArray arr = doc.as<JsonArray>();
  for (int i = 0; i < (int)arr.size(); i++) {
    if (String(arr[i]["n"].as<const char*>()) == nome) {
      arr.remove(i);
      String out; serializeJson(doc, out);
      if (salvaSuFile(FILE_MACRO, out)) inviaBLE("MACRO_DELETED:" + nome + "\n");
      else inviaBLE("ERR:DEL_MACRO\n");
      return;
    }
  }
  inviaBLE("ERR:MACRO_NOT_FOUND\n");
}

void salvaCredenziale(String jsonItem) {
  String existing = leggiFile(FILE_CRED);
  DynamicJsonDocument docList(8192);
  deserializeJson(docList, existing);
  JsonArray arr = docList.as<JsonArray>();
  DynamicJsonDocument docItem(512);
  DeserializationError err = deserializeJson(docItem, jsonItem);
  if (err) { inviaBLE("ERR:JSON_CRED\n"); return; }
  const char* nome = docItem["n"];
  bool found = false;
  for (int i = 0; i < (int)arr.size(); i++) {
    if (String(arr[i]["n"].as<const char*>()) == String(nome)) {
      arr[i]["u"] = docItem["u"].as<const char*>();
      arr[i]["p"] = docItem["p"].as<const char*>();
      found = true; break;
    }
  }
  if (!found) {
    JsonObject newItem = arr.createNestedObject();
    newItem["n"] = docItem["n"].as<const char*>();
    newItem["u"] = docItem["u"].as<const char*>();
    newItem["p"] = docItem["p"].as<const char*>();
  }
  String out; serializeJson(docList, out);
  if (salvaSuFile(FILE_CRED, out)) inviaBLE("CRED_SAVED:" + String(nome) + "\n");
  else inviaBLE("ERR:SAVE_CRED\n");
}

void eliminaCredenziale(String nome) {
  String existing = leggiFile(FILE_CRED);
  DynamicJsonDocument doc(8192);
  deserializeJson(doc, existing);
  JsonArray arr = doc.as<JsonArray>();
  for (int i = 0; i < (int)arr.size(); i++) {
    if (String(arr[i]["n"].as<const char*>()) == nome) {
      arr.remove(i);
      String out; serializeJson(doc, out);
      if (salvaSuFile(FILE_CRED, out)) inviaBLE("CRED_DELETED:" + nome + "\n");
      else inviaBLE("ERR:DEL_CRED\n");
      return;
    }
  }
  inviaBLE("ERR:CRED_NOT_FOUND\n");
}

void eseguiCredenziale(String nome) {
  String existing = leggiFile(FILE_CRED);
  DynamicJsonDocument doc(8192);
  deserializeJson(doc, existing);
  JsonArray arr = doc.as<JsonArray>();
  for (int i = 0; i < (int)arr.size(); i++) {
    if (String(arr[i]["n"].as<const char*>()) == nome) {
      String user = arr[i]["u"].as<String>();
      String pass = arr[i]["p"].as<String>();
      if (user.length() > 0) { inviaTesto(user); delay(80); Keyboard.write(KEY_TAB); delay(80); }
      inviaTesto(pass); delay(50);
      Keyboard.write(KEY_RETURN);
      inviaBLE("OK:CRED\n");
      return;
    }
  }
  inviaBLE("ERR:CRED_NOT_FOUND\n");
}

void inviaFileBLE(String prefix, const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) { inviaBLE("ERR:FILE\n"); return; }
  String content = f.readString();
  f.close();
  int total = content.length();
  inviaBLE(prefix + "_START:" + String(total) + "\n");
  delay(50);
  const int CHUNK = 80;
  for (int i = 0; i < total; i += CHUNK) {
    String chunk = content.substring(i, min(i + CHUNK, total));
    inviaBLE(prefix + "_DATA:" + chunk + "\n");
    delay(100);
  }
  inviaBLE(prefix + "_END\n");
}

// ============================================================
// ============================================================
// FILE MANAGER — CRUD su LittleFS (v6.1, sostituisce la SD)
// ============================================================
String mimeIconFor(String filename) {
  String f = filename; f.toLowerCase();
  if (f.endsWith(".txt") || f.endsWith(".ducky") || f.endsWith(".log")) return "📄";
  if (f.endsWith(".json")) return "🧩";
  if (f.endsWith(".html") || f.endsWith(".htm")) return "🌐";
  if (f.endsWith(".jpg") || f.endsWith(".jpeg") || f.endsWith(".png") || f.endsWith(".gif") || f.endsWith(".bmp")) return "🖼️";
  if (f.endsWith(".mp3") || f.endsWith(".wav")) return "🎵";
  if (f.endsWith(".mp4") || f.endsWith(".avi")) return "🎬";
  if (f.endsWith(".pdf")) return "📕";
  if (f.endsWith(".zip") || f.endsWith(".rar") || f.endsWith(".7z")) return "🗜️";
  return "📦";
}

bool kvmPageDisponibile() {
  return LittleFS.exists("/kvm.html");
}

String fsListDirJSON(String path) {
  if (path.length() == 0) path = "/";
  File root = LittleFS.open(path);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return "{\"error\":\"Percorso non valido\",\"files\":[]}";
  }
  String json = "{\"path\":\"" + path + "\",\"files\":[";
  bool first = true;
  File entry = root.openNextFile();
  while (entry) {
    if (!first) json += ",";
    first = false;
    String name = String(entry.name());
    int lastSlash = name.lastIndexOf('/');
    if (lastSlash >= 0) name = name.substring(lastSlash + 1);
    bool isDir = entry.isDirectory();
    json += "{";
    json += "\"name\":\"" + name + "\",";
    json += "\"isDir\":" + String(isDir ? "true" : "false") + ",";
    json += "\"size\":" + String(isDir ? 0 : entry.size()) + ",";
    json += "\"icon\":\"" + (isDir ? String("📁") : mimeIconFor(name)) + "\"";
    json += "}";
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  json += "]}";
  return json;
}

String fsReadFile(String path) {
  File f = LittleFS.open(path, "r");
  if (!f || f.isDirectory()) { if (f) f.close(); return ""; }
  String content = f.readString();
  f.close();
  return content;
}

bool fsWriteFile(String path, String content) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  f.print(content);
  f.close();
  return true;
}

bool fsAppendChunk(String path, String chunk, bool primoChunk) {
  File f = LittleFS.open(path, primoChunk ? "w" : "a");
  if (!f) return false;
  f.print(chunk);
  f.close();
  return true;
}

bool fsDeleteFile(String path) {
  if (LittleFS.exists(path)) {
    File f = LittleFS.open(path);
    bool isDir = f.isDirectory();
    f.close();
    if (isDir) return LittleFS.rmdir(path);
    return LittleFS.remove(path);
  }
  return false;
}

bool fsCreateDir(String path) {
  return LittleFS.mkdir(path);
}

bool fsDeleteDir(String path) {
  return LittleFS.rmdir(path);
}

String fsFileInfo(String path) {
  if (!LittleFS.exists(path)) return "{\"error\":\"File non trovato\"}";
  File f = LittleFS.open(path);
  String json = "{";
  json += "\"name\":\"" + String(f.name()) + "\",";
  json += "\"size\":" + String(f.size()) + ",";
  json += "\"isDir\":" + String(f.isDirectory() ? "true" : "false");
  json += "}";
  f.close();
  return json;
}

// ============================================================
// PAYLOAD DUCKYSCRIPT-LIKE DA LITTLEFS (v6.1, sostituisce la SD)
// Formato supportato (un comando per riga, case-insensitive):
//   REM commento                → ignorato
//   STRING testo da digitare    → invia testo (con supporto accenti)
//   DELAY 1000                  → attesa in millisecondi
//   KEY CTRL+ALT+DEL            → combinazione tasti (stesso formato eseguiCombinazione)
//   ENTER / TAB / ESC           → tasti singoli rapidi (alias di KEY)
//   LOOP 3                      → ripete le righe successive fino a END_LOOP
//   END_LOOP                    → fine blocco loop
// ============================================================
bool payloadInEsecuzione = false;

void eseguiPayloadFS(String path) {
  File f = LittleFS.open(path, "r");
  if (!f) { inviaBLE("ERR:PAYLOAD_NOT_FOUND\n"); return; }

  payloadInEsecuzione = true;

  std::vector<String> righe;
  while (f.available()) {
    String riga = f.readStringUntil('\n');
    riga.trim();
    if (riga.length() > 0) righe.push_back(riga);
  }
  f.close();

  int loopStart = -1, loopCount = 0, loopRemaining = 0;
  for (size_t i = 0; i < righe.size(); i++) {
    if (!payloadInEsecuzione) break;
    String riga = righe[i];
    String rigaUpper = riga; rigaUpper.toUpperCase();

    if (rigaUpper.startsWith("LOOP ")) {
      loopCount = riga.substring(5).toInt();
      loopStart = i + 1;
      loopRemaining = loopCount;
      continue;
    }
    if (rigaUpper == "END_LOOP") {
      if (loopStart >= 0 && loopRemaining > 1) {
        loopRemaining--;
        i = loopStart - 1;
      } else {
        loopStart = -1;
      }
      continue;
    }
    parseRigaPayload(riga);
  }

  payloadInEsecuzione = false;
  inviaBLE("OK:PAYLOAD_DONE\n");
}

void parseRigaPayload(String riga) {
  String rigaUpper = riga; rigaUpper.toUpperCase();

  if (rigaUpper.startsWith("REM")) return; // commento

  if (rigaUpper.startsWith("STRING ")) {
    inviaTesto(riga.substring(7));
    return;
  }
  if (rigaUpper.startsWith("DELAY ")) {
    int ms = riga.substring(6).toInt();
    if (ms > 0 && ms < 60000) delay(ms);
    return;
  }
  if (rigaUpper.startsWith("KEY ")) {
    eseguiCombinazione(riga.substring(4));
    return;
  }
  if (rigaUpper == "ENTER") { eseguiCombinazione("ENTER"); return; }
  if (rigaUpper == "TAB")   { eseguiCombinazione("TAB");   return; }
  if (rigaUpper == "ESC")   { eseguiCombinazione("ESC");   return; }
  if (rigaUpper == "GUI" || rigaUpper == "WIN") { eseguiCombinazione("WIN"); return; }

  // Riga non riconosciuta: ignorata silenziosamente (compatibilità file futuri)
}

// ============================================================
// MODALITA' AVVIO — lettura/scrittura Preferences
// ============================================================
String leggiModalitaAvvio() {
  preferences.begin("kvm", true);
  String m = preferences.getString("mode", "BLE");
  ap_ssid     = preferences.getString("ap_ssid", "KVM-S3");
  ap_password = preferences.getString("ap_pass",  "12345678");
  sta_ssid    = preferences.getString("sta_ssid", "");
  sta_password= preferences.getString("sta_pass",  "");
  preferences.end();
  return m;
}

void salvaModalita(String m) {
  preferences.begin("kvm", false);
  preferences.putString("mode", m);
  preferences.end();
}

bool bootButtonPremuto() {
  // GPIO0 = BOOT button (active LOW)
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  return (digitalRead(BOOT_BUTTON_PIN) == LOW);
}

// ============================================================
// SETUP WIFI — AP
// ============================================================
void setupWiFiAP() {
  dnsServer.stop();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
  delay(100);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.println("📡 WiFi AP: " + ap_ssid + "  IP: " + WiFi.softAPIP().toString());
}

// ============================================================
// SETUP WIFI — STA
// ============================================================
void setupWiFiSTA() {
  dnsServer.stop();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(sta_ssid.c_str(), sta_password.c_str());
  Serial.print("📶 Connessione a: " + sta_ssid);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 24) {
    delay(500); Serial.print("."); attempts++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ Connesso! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("❌ Connessione fallita — fallback AP");
    salvaModalita("AP");
    setupWiFiAP();
  }
}

// ============================================================
// NUOVO v7.0 — DISPLAY ST7735 + QR CODE
// ============================================================

// Palette semplice, coerente con lo stile scuro dell'interfaccia web
// (valori RGB565, non dipendono da Adafruit_ST7735)
#define COL_BG      0x0000   // nero
#define COL_TESTO   0xFFFF   // bianco
#define COL_MUTO    0x7BEF   // grigio medio (565)
#define COL_ACCENTO 0x07FF   // ciano acceso
#define COL_OK      0x07E0   // verde
#define COL_WARN    0xFD20   // arancio
#define COL_LINK    0x07FF   // ciano (link evidenziato)

// ★★★ v7.2: inizializzazione display con ESP-IDF LCD API ★★★
// Usa il driver ufficiale LilyGo (esp_lcd_st7735) che funziona
// correttamente sul T-Dongle S3, a differenza di TFT_eSPI e
// Adafruit_ST7735 che hanno problemi su questa piattaforma.
void initDisplay() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);  // ★ FIX: ACTIVE LOW (LOW=ON, HIGH=OFF)

  Serial.println("🖥️ Inizializzazione display ST7735S...");

  // Configurazione SPI
  spi_bus_config_t spi_config = ST7735_PANEL_BUS_SPI_CONFIG(
      TFT_SCLK, TFT_MOSI, LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t));

  esp_lcd_panel_io_spi_config_t io_config = ST7735_PANEL_IO_SPI_CONFIG(
      TFT_CS, TFT_DC, NULL, NULL);

  // FIX: gli inizializzatori con designatore (.campo = valore) in C++
  // richiedono che i campi compaiano nello STESSO ORDINE in cui sono
  // dichiarati nella struct reale — un vincolo che ha gia' causato
  // errori diversi ad ogni cambio di versione di ESP-IDF, perche'
  // l'ordine dei campi cambia tra una versione e l'altra e non e'
  // sempre facile verificarlo in anticipo. Inizializzando la struct a
  // zero e assegnando ogni campo singolarmente si elimina questo
  // vincolo del tutto: le assegnazioni normali non hanno bisogno di
  // rispettare nessun ordine, quindi il codice resta valido anche se
  // l'ordine dei campi cambiasse di nuovo in futuro.
  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = TFT_RST;
  panel_config.bits_per_pixel = 16;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
#else
  panel_config.color_space = ESP_LCD_COLOR_SPACE_BGR;
#endif

  esp_lcd_panel_io_handle_t io_handle = NULL;

  // Inizializza bus SPI
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi_config, SPI_DMA_CH_AUTO));
  Serial.println("✅ SPI bus OK");

  // Crea IO handle
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
      (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));
  Serial.println("✅ Panel IO OK");

  // Crea e inizializza pannello ST7735
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7735(io_handle, &panel_config, &panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
  Serial.println("✅ Panel ST7735 init OK");

  // Configura landscape mode (parametri ufficiali LilyGo)
  esp_lcd_panel_set_gap(panel_handle, 1, 26);
  esp_lcd_panel_swap_xy(panel_handle, true);
  esp_lcd_panel_mirror(panel_handle, false, true);  // solo mirror Y = orientamento corretto
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
  Serial.println("✅ Landscape config OK");

  // Alloca framebuffer
  if (!tft.begin()) {
    Serial.println("❌ Framebuffer alloc fallito!");
    return;
  }

  // Sfondo nero
  tft.fillScreen(COL_BG);
  tft.setTextWrap(false);

  // Test visivo: mostra "Avvio KVM-S3..."
  tft.setTextColor(COL_MUTO);
  tft.setCursor(4, 30);
  tft.print("Avvio KVM-S3...");
  tft.flush();

  Serial.println("✅ Display pronto!");
}

// Disegna un QR code su schermo usando la libreria QRCode (Richard Moore).
// testo: contenuto (URL o stringa WIFI:S:...;;), x/y: angolo in alto a
// sinistra, scala: pixel fisici per ogni modulo del QR (2 o 3 è leggibile
// su questo schermo piccolo senza sforare).
void disegnaQRCode(String testo, int x, int y, int scala) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(4)]; // versione 4: fino a ~78 byte alfanumerici, sufficiente per un IP o SSID
  qrcode_initText(&qrcode, qrcodeData, 4, ECC_LOW, testo.c_str());
  for (uint8_t qy = 0; qy < qrcode.size; qy++) {
    for (uint8_t qx = 0; qx < qrcode.size; qx++) {
      uint16_t colore = qrcode_getModule(&qrcode, qx, qy) ? COL_TESTO : COL_BG;
      tft.fillRect(x + qx * scala, y + qy * scala, scala, scala, colore);
    }
  }
  tft.flush();  // v7.2: necessario con framebuffer
}

// ============================================================
// Schermata principale: 3 pagine alternate con pulsante breve.
//   Pagina 0: QR code centrato (leggibile, senza testo sopra)
//   Pagina 1: Info connessione (SSID, IP, password)
//   Pagina 2: Aiuto (come connettersi, istruzioni)
// ============================================================
void disegnaSchermata() {
  tft.fillScreen(COL_BG);

  // ── Intestazione comune: nome + modalità ──────────────────
  tft.setTextColor(COL_ACCENTO);
  tft.setTextSize(1);
  tft.setCursor(2, 2);
  tft.print("KVM-S3");
  tft.setTextColor(modalitaCorrente == "BLE" ? COL_ACCENTO :
                    (WiFi.status() == WL_CONNECTED || modalitaCorrente == "AP") ? COL_OK : COL_WARN);
  tft.setCursor(110, 2);
  tft.print(modalitaCorrente);

  // ========================================================
  // PAGINA 0: QR CODE CENTRATO (senza testo sovrapposto)
  // ========================================================
  if (schermataCorrente == 0) {
    int qrScala = 2;
    int qrSize = 33 * qrScala; // versione 4 = 33 moduli
    int qrX = (LCD_WIDTH - qrSize) / 2;
    int qrY = (LCD_HEIGHT - qrSize) / 2 + 2;

    if (modalitaCorrente == "BLE") {
      // QR per GitHub Pages (essenziale per connessione BLE)
      disegnaQRCode("https://monesc82.github.io/kvm-dongle-usb/", qrX, qrY, qrScala);
    } else if (modalitaCorrente == "AP") {
      String ssid = ap_ssid.length() ? ap_ssid : "KVM-S3";
      String pass = ap_password.length() ? ap_password : "12345678";
      disegnaQRCode("WIFI:S:" + ssid + ";T:WPA;P:" + pass + ";;", qrX, qrY, qrScala);
    } else if (modalitaCorrente == "STA" && WiFi.status() == WL_CONNECTED) {
      disegnaQRCode("http://" + WiFi.localIP().toString() + "/", qrX, qrY, qrScala);
    } else {
      tft.setTextColor(COL_MUTO);
      tft.setCursor(20, 35);
      tft.print("QR non disponibile");
    }

  // ========================================================
  // PAGINA 1: INFO CONNESSIONE (testo chiaro, niente QR)
  // ========================================================
  } else if (schermataCorrente == 1) {
    tft.setTextColor(COL_TESTO);
    tft.setTextSize(1);

    if (modalitaCorrente == "BLE") {
      tft.setCursor(4, 18);
      tft.print(deviceConnected ? "Stato: Connesso" : "Stato: In attesa");
      tft.setCursor(4, 28);
      tft.print("Nome: KVM-S3");
      tft.setCursor(4, 42);
      tft.setTextColor(COL_LINK);
      tft.print("monesc82.github.io");
      tft.setCursor(4, 52);
      tft.print("/kvm-dongle-usb/");
      tft.setCursor(4, 64);
      tft.setTextColor(COL_MUTO);
      tft.print("Apri su Chrome/Bluefy");

    } else if (modalitaCorrente == "AP") {
      String ssid = ap_ssid.length() ? ap_ssid : "KVM-S3";
      tft.setCursor(4, 18);
      tft.print("Rete: " + ssid);
      tft.setCursor(4, 30);
      tft.print("Pass: 12345678");
      tft.setCursor(4, 44);
      tft.setTextColor(COL_OK);
      tft.print("IP: 192.168.4.1");
      tft.setCursor(4, 56);
      tft.setTextColor(COL_MUTO);
      tft.print("Connettiti al WiFi");
      tft.setCursor(4, 66);
      tft.print("poi apri 192.168.4.1");

    } else if (modalitaCorrente == "STA") {
      if (WiFi.status() == WL_CONNECTED) {
        String ip = WiFi.localIP().toString();
        tft.setCursor(4, 18);
        tft.print("Rete: " + sta_ssid.substring(0, 14));
        tft.setCursor(4, 30);
        tft.setTextColor(COL_OK);
        tft.print("IP: " + ip);
        tft.setCursor(4, 44);
        tft.setTextColor(COL_MUTO);
        tft.print(kvmPageDisponibile() ? "Pagina KVM: OK" : "Carica pagina KVM");
        tft.setCursor(4, 56);
        tft.print("Apri http://" + ip);
      } else {
        tft.setTextColor(COL_WARN);
        tft.setCursor(4, 24);
        tft.print("Connessione WiFi...");
        tft.setCursor(4, 36);
        tft.setTextColor(COL_MUTO);
        tft.print("Rete: " + sta_ssid.substring(0, 14));
      }
    }

  // ========================================================
  // PAGINA 2: AIUTO / ISTRUZIONI
  // ========================================================
  } else {
    tft.setTextColor(COL_MUTO);
    tft.setTextSize(1);
    tft.setCursor(4, 18);
    tft.print("Pulsante BOOT:");
    tft.setCursor(4, 28);
    tft.setTextColor(COL_TESTO);
    tft.print(" <3s: cambia schermo");
    tft.setCursor(4, 40);
    tft.setTextColor(COL_ACCENTO);
    tft.print(" 3s: cambia modalita");
    tft.setCursor(4, 52);
    tft.setTextColor(COL_WARN);
    tft.print(" 6s: reset AP");
    tft.setCursor(4, 66);
    tft.setTextColor(COL_MUTO);
    tft.print("Modalita attuale:");
    tft.setCursor(4, 76);
    tft.setTextColor(modalitaCorrente == "BLE" ? COL_ACCENTO : COL_OK);
    tft.print(modalitaCorrente);
  }

  tft.flush();
}

// ============================================================
// v7.1 — BOTTONE FISICO A 3 LIVELLI
//   < 3s  = alterna schermata QR/info
//   >= 3s = cicla modalità connessione (BLE → AP → STA → ...)
//   >= 6s = reset AP a valori di fabbrica
// ============================================================
void gestisciBottoneRuntime() {
  bool premuto = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  unsigned long ora = millis();

  if (premuto && !btnWasPressed) {
    // fronte di discesa: inizio pressione
    btnWasPressed = true;
    btnPressStart = ora;
    btnMediumActionDone = false;
    btnLongActionDone = false;
  }

  if (premuto && btnWasPressed) {
    unsigned long durata = ora - btnPressStart;

    // Livello 3: >= 6s = reset AP
    if (!btnLongActionDone && durata >= BTN_LONG_MIN_MS) {
      btnLongActionDone = true;
      btnMediumActionDone = true;
      resetAPDiFabbrica();
      return;
    }

    // Livello 2: >= 3s = cicla modalità (eseguito subito, non al rilascio)
    if (!btnMediumActionDone && durata >= BTN_MEDIUM_MIN_MS && durata < BTN_LONG_MIN_MS) {
      btnMediumActionDone = true;
      cicloModalitaSuccessiva();
      return;
    }
  }

  if (!premuto && btnWasPressed) {
    // fronte di salita: rilascio
    unsigned long durata = ora - btnPressStart;
    btnWasPressed = false;

    // Livello 1: < 3s = cicla 3 schermate (0=QR, 1=info, 2=aiuto)
    if (!btnMediumActionDone && !btnLongActionDone && durata < BTN_SHORT_MAX_MS && durata > 50) {
      schermataCorrente = (schermataCorrente + 1) % 3;
      disegnaSchermata();
    }
  }
}

void cicloModalitaSuccessiva() {
  String prossima = (modalitaCorrente == "BLE") ? "AP" :
                     (modalitaCorrente == "AP")  ? "STA" : "BLE";

  // Feedback visivo immediato prima del riavvio
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_ACCENTO);
  tft.setTextSize(1);
  tft.setCursor(4, 30);
  tft.print("Passaggio a " + prossima + "...");
  tft.setCursor(4, 44);
  tft.setTextColor(COL_MUTO);
  tft.print("Riavvio in corso");

  Serial.println("🔘 Bottone: ciclo modalita' -> " + prossima);
  salvaModalita(prossima);
  delay(600);
  ESP.restart();
}

void resetAPDiFabbrica() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_WARN);
  tft.setTextSize(1);
  tft.setCursor(4, 24);
  tft.print("Reset AP di fabbrica");
  tft.setCursor(4, 38);
  tft.setTextColor(COL_TESTO);
  tft.print("SSID: KVM-S3");
  tft.setCursor(4, 48);
  tft.print("Pass: 12345678");
  tft.setCursor(4, 62);
  tft.setTextColor(COL_MUTO);
  tft.print("Riavvio in AP...");

  Serial.println("🔘 Bottone: reset AP a valori di fabbrica");
  preferences.begin("kvm", false);
  preferences.putString("ap_ssid", "KVM-S3");
  preferences.putString("ap_pass", "12345678");
  preferences.putString("mode", "AP");
  preferences.end();
  delay(1200);
  ESP.restart();
}

// ============================================================
// WEBSERVER — tutte le route
// ============================================================
void setupWebServer() {

  // ── Pagina radice: se la pagina KVM è stata caricata su
  //    LittleFS, servila direttamente. Altrimenti mostra il
  //    setup WiFi con le istruzioni per caricarla. ───────────
  server.on("/", []() {
    if (kvmPageDisponibile()) {
      File f = LittleFS.open("/kvm.html", "r");
      server.streamFile(f, "text/html");
      f.close();
    } else {
      server.send(200, "text/html", index_html);
    }
  });

  // ── /wifi — pagina di setup, sempre accessibile anche se
  //    la pagina KVM è già installata ──────────────────────
  server.on("/wifi", []() { server.send(200, "text/html", index_html); });

  // ── /kvm — alias esplicito della pagina KVM da LittleFS ──
  server.on("/kvm", []() {
    if (kvmPageDisponibile()) {
      File f = LittleFS.open("/kvm.html", "r");
      server.streamFile(f, "text/html");
      f.close();
    } else {
      server.send(404, "text/plain",
        "Pagina KVM non ancora caricata. Vai su /wifi e carica KVM_v6_1.html.");
    }
  });

  // ── /manifest.json — NUOVO v7.0: consente "Aggiungi a Home"
  //    con nome/icona corretti invece del titolo generico della
  //    pagina. Non garantisce la PWA "piena" (schermo intero,
  //    offline) che su Chrome richiede HTTPS — qui serve solo a
  //    rendere pulita l'icona-collegamento, che funziona comunque
  //    anche in HTTP semplice su entrambe le piattaforme.
  server.on("/manifest.json", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = "{";
    json += "\"name\":\"KVM Dongle\",";
    json += "\"short_name\":\"KVM-S3\",";
    json += "\"start_url\":\"/kvm\",";
    json += "\"display\":\"standalone\",";
    json += "\"background_color\":\"#000000\",";
    json += "\"theme_color\":\"#007aff\",";
    json += "\"icons\":[]";
    json += "}";
    server.send(200, "application/manifest+json", json);
  });

  // ── /kvm/upload — carica la pagina KVM completa su LittleFS ──
  // FIX v6.1.2: scrittura binary-safe con File handle persistente
  // invece di String((char*)buf) che tronca al primo byte NUL.
  // FIX v7.1: la soglia di spazio libero era rimasta fissa a 20000
  // byte in v7.0 per una svista mia — troppo piccola per l'HTML
  // attuale (~130KB+), causava probabilmente i falsi errori 500 che
  // hai visto. Ora si legge Content-Length della richiesta per
  // sapere la dimensione REALE in arrivo, con margine di sicurezza.
  // La verifica finale (dimensione scritta == dimensione attesa)
  // resta invece corretta così com'era: upload.totalSize è
  // documentato come affidabile proprio nel momento in cui viene
  // letto qui (UPLOAD_FILE_END), quindi il controllo ha senso e
  // intercetta upload realmente troncati, non li nasconde.
  static File kvmUploadFile;
  static size_t kvmUploadExpected = 0;
  server.on("/kvm/upload", HTTP_POST,
    []() {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      bool ok = false;
      if (kvmUploadFile) {
        size_t writtenSize = kvmUploadFile.size();
        kvmUploadFile.close();
        ok = (kvmUploadExpected > 0 && writtenSize == kvmUploadExpected);
      }
      if (!ok) {
        LittleFS.remove("/kvm.html");
        server.send(500, "application/json",
          "{\"status\":\"error\",\"message\":\"Upload incompleto o corrotto — riprova\"}");
        return;
      }
      server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Interfaccia KVM caricata\"}");
    },
    []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        size_t freeSpace = LittleFS.totalBytes() - LittleFS.usedBytes();
        size_t incoming = server.header("Content-Length").toInt();
        size_t needed = (incoming > 0) ? (incoming + 10000) : 200000; // margine di sicurezza
        if (freeSpace < needed) {
          Serial.println("❌ Spazio insufficiente: servono ~" + String(needed) +
                          " byte, disponibili " + String(freeSpace));
          kvmUploadExpected = 0;
          return;
        }
        kvmUploadFile = LittleFS.open("/kvm.html", "w");
        kvmUploadExpected = 0;
        Serial.println("📤 Upload interfaccia KVM in corso...");
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (kvmUploadFile) {
          kvmUploadFile.write(upload.buf, upload.currentSize); // binary-safe, no String()
        }
        yield(); // evita trigger watchdog su file grandi
      } else if (upload.status == UPLOAD_FILE_END) {
        kvmUploadExpected = upload.totalSize;
        Serial.println("✅ Interfaccia KVM ricevuta — " + String(upload.totalSize) + " byte");
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (kvmUploadFile) kvmUploadFile.close();
        LittleFS.remove("/kvm.html");
        Serial.println("⚠️ Upload interrotto — file rimosso");
      }
    }
  );

  // ── /kvm/delete — rimuove la pagina KVM da LittleFS (torna al setup) ──
  server.on("/kvm/delete", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    fsDeleteFile("/kvm.html");
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  // ── /status — stato completo del dispositivo ─────────────
  server.on("/status", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String mode = modalitaCorrente;
    String ip   = (mode == "AP")  ? WiFi.softAPIP().toString() :
                  (mode == "STA") ? WiFi.localIP().toString()  : "N/A";
    String ssid = (mode == "AP")  ? ap_ssid :
                  (mode == "STA") ? sta_ssid  : "";
    String json = "{";
    json += "\"mode\":\"" + mode + "\",";
    json += "\"ip\":\"" + ip + "\",";
    json += "\"ssid\":\"" + ssid + "\",";
    json += "\"sd\":" + String(sdDisponibile ? "true" : "false") + ",";
    json += "\"kvmPage\":" + String(kvmPageDisponibile() ? "true" : "false") + ",";
    json += "\"ble\":false";
    json += "}";
    server.send(200, "application/json", json);
  });

  // ── /api?run=CMD — comandi KVM via HTTP ──────────────────
  server.on("/api", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
    String cmd = server.arg("run");
    if (cmd.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"Parametro run mancante\"}");
      return;
    }
    eseguiComando(cmd);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  // ── /macros — FIX v6.1.2: endpoint mancante che causava la
  //    sincronizzazione unidirezionale (BLE→WiFi non funzionava
  //    perché non esisteva alcun modo per l'HTML di leggere le
  //    macro via HTTP; il vecchio codice chiamava inviaFileBLE()
  //    che notifica solo su BLE, quindi via WiFi non tornava nulla)
  server.on("/macros", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", leggiFile(FILE_MACRO));
  });

  // ── /creds — stesso fix per le credenziali ───────────────
  server.on("/creds", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", leggiFile(FILE_CRED));
  });
  // Nota v6.1.2: salvataggio/eliminazione macro e credenziali
  // funzionano già correttamente su entrambi i canali tramite
  // /api?run=MACRO_SAVE:...|CRED_SAVE:...|MACRO_DEL:...|CRED_DEL:...
  // (il dispatcher eseguiComando() li gestisce in modo uniforme).
  // Non servono endpoint POST dedicati: solo /macros e /creds in
  // lettura mancavano, perché passavano solo per notifica BLE.

  // ── /networks — reti WiFi salvate ────────────────────────
  server.on("/networks", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = "{\"networks\":[";
    for (int i = 0; i < (int)savedNetworks.size(); i++) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + savedNetworks[i].ssid + "\"}";
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  // ── /scan — scansione reti ───────────────────────────────
  server.on("/scan", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", scanNetworksJSON());
  });

  // ── /mode?mode=BLE|AP|STA — cambio modalita' (riavvio) ──
  server.on("/mode", []() {
    String mode = server.arg("mode");
    if (mode != "BLE" && mode != "AP" && mode != "STA") {
      server.send(400, "application/json", "{\"error\":\"Modalita non valida (BLE|AP|STA)\"}");
      return;
    }
    salvaModalita(mode);
    server.send(200, "application/json",
      "{\"message\":\"Passaggio a " + mode + " in corso. Riavvio.\",\"restart\":true}");
    server.client().flush(); delay(300); ESP.restart();
  });

  // ── /connect?ssid=&pass= — nuova rete STA ───────────────
  server.on("/connect", []() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"SSID richiesto\"}");
      return;
    }
    saveNetwork(ssid, pass);
    preferences.begin("kvm", false);
    preferences.putString("sta_ssid", ssid);
    preferences.putString("sta_pass", pass);
    preferences.putString("mode", "STA");
    preferences.end();
    server.send(200, "application/json",
      "{\"message\":\"Connessione a " + ssid + " in corso. Riavvio.\",\"restart\":true}");
    server.client().flush(); delay(300); ESP.restart();
  });

  // ── /connect_saved?ssid= — rete gia' salvata ────────────
  server.on("/connect_saved", []() {
    String ssid = server.arg("ssid");
    for (int i = 0; i < (int)savedNetworks.size(); i++) {
      if (savedNetworks[i].ssid == ssid) {
        preferences.begin("kvm", false);
        preferences.putString("sta_ssid", savedNetworks[i].ssid);
        preferences.putString("sta_pass", savedNetworks[i].password);
        preferences.putString("mode", "STA");
        preferences.end();
        server.send(200, "application/json",
          "{\"message\":\"Connessione a " + ssid + " in corso. Riavvio.\",\"restart\":true}");
        server.client().flush(); delay(300); ESP.restart();
        return;
      }
    }
    server.send(404, "application/json", "{\"error\":\"Rete non trovata\"}");
  });

  // ── /delete_network?ssid= ────────────────────────────────
  server.on("/delete_network", []() {
    String ssid = server.arg("ssid");
    deleteNetwork(ssid);
    server.send(200, "application/json", "{\"message\":\"Rete " + ssid + " rimossa\"}");
  });

  // ── /ap?ssid=&pass= — modifica config AP ────────────────
  server.on("/ap", []() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.length() == 0) { server.send(400, "application/json", "{\"error\":\"SSID richiesto\"}"); return; }
    if (pass.length() > 0 && pass.length() < 8) { server.send(400, "application/json", "{\"error\":\"Password min 8 caratteri\"}"); return; }
    preferences.begin("kvm", false);
    preferences.putString("ap_ssid", ssid);
    if (pass.length() >= 8) preferences.putString("ap_pass", pass);
    preferences.putString("mode", "AP");
    preferences.end();
    server.send(200, "application/json",
      "{\"message\":\"AP aggiornato. Riavvio.\",\"restart\":true}");
    server.client().flush(); delay(300); ESP.restart();
  });

  // ── /fs/list?path= — lista file su LittleFS ─────────────
  server.on("/fs/list", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String path = server.arg("path");
    if (path.length() == 0) path = "/";
    server.send(200, "application/json", fsListDirJSON(path));
  });

  // ── /fs/read?path= — leggi file LittleFS (testo) ────────
  server.on("/fs/read", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String path = server.arg("path");
    if (path.length() == 0) { server.send(400, "application/json", "{\"error\":\"path mancante\"}"); return; }
    File f = LittleFS.open(path, "r");
    if (!f || f.isDirectory()) {
      if (f) f.close();
      server.send(404, "application/json", "{\"error\":\"File non trovato\"}");
      return;
    }
    server.setContentLength(f.size());
    server.send(200, "text/plain; charset=utf-8", "");
    uint8_t buf[512];
    while (f.available()) {
      int n = f.read(buf, sizeof(buf));
      if (n > 0) server.client().write(buf, n);
    }
    f.close();
  });

  // ── /fs/write?path= + body — scrivi file LittleFS ───────
  server.on("/fs/write", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String path = server.arg("path");
    if (path.length() == 0) { server.send(400, "application/json", "{\"error\":\"path mancante\"}"); return; }
    String body = server.arg("plain");
    if (fsWriteFile(path, body)) {
      server.send(200, "application/json", "{\"status\":\"ok\",\"path\":\"" + path + "\"}");
    } else {
      server.send(500, "application/json", "{\"error\":\"Scrittura fallita\"}");
    }
  });

  // ── /fs/upload?path= — upload multipart (binario/testo) ─
  // FIX v6.1.2: stesso fix binary-safe di /kvm/upload
  static String uploadPath;
  static File fsUploadFile;
  server.on("/fs/upload", HTTP_POST,
    []() {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      bool ok = false;
      if (fsUploadFile) { ok = true; fsUploadFile.close(); }
      if (!ok) {
        server.send(500, "application/json", "{\"error\":\"Upload fallito\"}");
        return;
      }
      server.send(200, "application/json", "{\"status\":\"ok\",\"path\":\"" + uploadPath + "\"}");
    },
    []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        uploadPath = server.arg("path");
        if (uploadPath.length() == 0) uploadPath = "/" + upload.filename;
        fsUploadFile = LittleFS.open(uploadPath, "w");
        Serial.println("📤 Upload: " + uploadPath);
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize);
        yield();
      } else if (upload.status == UPLOAD_FILE_END) {
        Serial.println("✅ Upload completato: " + String(upload.totalSize) + " byte");
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (fsUploadFile) fsUploadFile.close();
        Serial.println("⚠️ Upload interrotto");
      }
    }
  );

  // ── /fs/delete?path= — elimina file o dir ───────────────
  server.on("/fs/delete", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String path = server.arg("path");
    if (path.length() == 0) { server.send(400, "application/json", "{\"error\":\"path mancante\"}"); return; }
    if (fsDeleteFile(path)) {
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      server.send(500, "application/json", "{\"error\":\"Eliminazione fallita\"}");
    }
  });

  // ── /fs/mkdir?path= — crea directory ────────────────────
  server.on("/fs/mkdir", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String path = server.arg("path");
    if (fsCreateDir(path)) server.send(200, "application/json", "{\"status\":\"ok\"}");
    else server.send(500, "application/json", "{\"error\":\"mkdir fallito\"}");
  });

    // ── /fs/rename?old=path&new=path — rinomina file o dir ──
  server.on("/fs/rename", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String oldPath = server.arg("old");
    String newPath = server.arg("new");
    
    if (oldPath.length() == 0 || newPath.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"old e new richiesti\"}");
      return;
    }
    
    if (!LittleFS.exists(oldPath)) {
      server.send(404, "application/json", "{\"error\":\"File non trovato\"}");
      return;
    }
    
    if (LittleFS.exists(newPath)) {
      server.send(409, "application/json", "{\"error\":\"Il file di destinazione esiste già\"}");
      return;
    }
    
    if (LittleFS.rename(oldPath, newPath)) {
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      server.send(500, "application/json", "{\"error\":\"Rinominazione fallita\"}");
    }
  });

  // ── /fs/info?path= — info su un file ────────────────────
  server.on("/fs/info", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", fsFileInfo(server.arg("path")));
  });

  // ── /payload/list — lista payload nella cartella /payloads ─
  server.on("/payload/list", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", fsListDirJSON("/payloads"));
  });

  // ── /payload/run?path= — esegui payload da LittleFS ──────
  server.on("/payload/run", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String path = server.arg("path");
    if (path.length() == 0) { server.send(400, "application/json", "{\"error\":\"path mancante\"}"); return; }
    // Risponde subito, poi esegue (il payload può durare secondi)
    server.send(200, "application/json", "{\"status\":\"running\",\"path\":\"" + path + "\"}");
    server.client().flush();
    delay(50);
    eseguiPayloadFS(path);
  });

  // ── /payload/stop — interrompi payload in esecuzione ─────
  server.on("/payload/stop", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    payloadInEsecuzione = false;
    server.send(200, "application/json", "{\"status\":\"stopped\"}");
  });

  // ── Fallback captive portal ──────────────────────────────
  server.onNotFound([]() {
    server.send(200, "text/html", index_html);
  });

  server.begin();
  webServerAttivo = true;
  Serial.println("✅ WebServer avviato");
}


// ============================================================
// BLE CALLBACKS
// ============================================================
bool pendingSync = false;
unsigned long connectTime = 0;

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    deviceConnected = true;
    pendingSync = true;
    connectTime = millis();
    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("✅ BLE Connesso");
    disegnaSchermata(); // NUOVO v7.0: feedback immediato su display
  }
  void onDisconnect(BLEServer* s) override {
    deviceConnected = false;
    pendingSync = false;
    digitalWrite(LED_BUILTIN, LOW);
    Serial.println("⚠️ BLE Disconnesso — riavvio advertising");
    s->startAdvertising();
    resetTutto();
    disegnaSchermata(); // NUOVO v7.0: feedback immediato su display
  }
};

class CharCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pC) override {
    uint8_t* data = pC->getData();
    size_t len = pC->getLength();
    if (len == 0) return;
    for (size_t i = 0; i < len; i++) {
      char c = (char)data[i];
      if (c == '\n' || c == '\r') {
        if (bleBuffer.length() > 0) {
          String cmd = bleBuffer;
          bleBuffer = "";
          eseguiComando(cmd);
        }
      } else {
        bleBuffer += c;
        if (bleBuffer.length() > 1024) bleBuffer = "";
      }
    }
  }
};

// ============================================================
// SETUP
// ============================================================
void setup() {
  USB.begin();
  Serial.begin(115200);
  delay(500);

  Serial.println("========================================");
  Serial.println("  KVM Dongle Unified v7.1_claude");
  Serial.println("========================================");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  // NUOVO v7.0: inizializza il display il prima possibile, così
  // può mostrare feedback anche durante il resto del boot
  initDisplay();
  tft.setTextColor(COL_MUTO);
  tft.setCursor(4, 30);
  tft.print("Avvio KVM-S3...");

  // Inizializza storage LittleFS
  initStorage();

  // Carica reti salvate da LittleFS
  loadSavedNetworks();

  // Leggi modalita' da Preferences
  modalitaCorrente = leggiModalitaAvvio();

  // BOOT button tenuto premuto > 3s → forza AP WiFi (escape hatch)
  if (bootButtonPremuto()) {
    Serial.println("🔴 BOOT button rilevato — attesa 3s per conferma AP forzato...");
    tft.fillScreen(COL_BG);
    tft.setTextColor(COL_WARN);
    tft.setCursor(4, 30);
    tft.print("Tieni premuto per");
    tft.setCursor(4, 42);
    tft.print("forzare modalita AP...");
    delay(3000);
    if (bootButtonPremuto()) {
      Serial.println("✅ Forzato AP WiFi via BOOT button");
      modalitaCorrente = "AP";
      salvaModalita("AP");
    }
  }

  Serial.println("📌 Modalita': " + modalitaCorrente);

  // Inizializza USB HID (sempre, indipendente da modalita' wireless)
  {
    Preferences pLay;
    pLay.begin("kvm", true);
    String layoutSalvato = pLay.getString("layout", "IT");
    pLay.end();
    layoutCorrente = (layoutSalvato == "US") ? LAYOUT_US : LAYOUT_IT;
  }
  Keyboard.begin(layoutCorrente == LAYOUT_US ? KeyboardLayout_en_US : KeyboardLayout_it_IT);
  Mouse.begin();

  // Storage LittleFS già inizializzato sopra da initStorage()
  // (sostituisce la SD Card — nessuna inizializzazione aggiuntiva necessaria)

  // ── Avvio in base alla modalita' ────────────────────────
  if (modalitaCorrente == "BLE") {
    // Modalita' BLE pura: nessun WiFi
    WiFi.mode(WIFI_OFF);
    BLEDevice::init("KVM-S3");
    BLEDevice::setMTU(512);
    pBLEServer = BLEDevice::createServer();
    pBLEServer->setCallbacks(new ServerCB());
    BLEService* svc = pBLEServer->createService(SERVICE_UUID);
    pTX = svc->createCharacteristic(CHARACTERISTIC_TX, BLECharacteristic::PROPERTY_NOTIFY);
    pRX = svc->createCharacteristic(CHARACTERISTIC_RX,
            BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    pRX->setCallbacks(new CharCB());
    svc->start();
    BLEAdvertising* adv = pBLEServer->getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->start();
    Serial.println("✅ BLE attivo — in ascolto come KVM-S3");
    digitalWrite(LED_BUILTIN, HIGH); delay(200); digitalWrite(LED_BUILTIN, LOW);
    delay(200); digitalWrite(LED_BUILTIN, HIGH); delay(200); digitalWrite(LED_BUILTIN, LOW);
    disegnaSchermata();

  } else if (modalitaCorrente == "AP") {
    setupWiFiAP();
    setupWebServer();
    // 1 lampeggio lungo = AP
    digitalWrite(LED_BUILTIN, HIGH); delay(800); digitalWrite(LED_BUILTIN, LOW);
    disegnaSchermata();

  } else if (modalitaCorrente == "STA") {
    if (sta_ssid.length() == 0) {
      Serial.println("⚠️ STA: nessun SSID — fallback AP");
      modalitaCorrente = "AP";
      salvaModalita("AP");
      setupWiFiAP();
    } else {
      setupWiFiSTA();
      // NUOVO v7.0: setupWiFiSTA() puo' fallire e passare internamente
      // ad AP (salva "AP" in Preferences per il prossimo avvio), ma
      // prima non aggiornava la variabile locale modalitaCorrente in
      // questa sessione — il display avrebbe mostrato "STA" mentre
      // la radio era gia' passata ad AP. Rileggiamo lo stato vero.
      if (WiFi.getMode() == WIFI_AP) {
        modalitaCorrente = "AP";
      }
    }
    setupWebServer();
    // 3 lampeggi veloci = STA
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_BUILTIN, HIGH); delay(150);
      digitalWrite(LED_BUILTIN, LOW);  delay(150);
    }
    disegnaSchermata();
  }
}

// ============================================================
// LOOP
// ============================================================
unsigned long ultimoRefreshDisplay = 0;
bool bleEraConnesso = false;

void loop() {
  delay(5);
  if (webServerAttivo) {
    server.handleClient();
    dnsServer.processNextRequest();
  }

  // NUOVO v7.0: gestione bottone a runtime (cicla modalita' / reset AP)
  gestisciBottoneRuntime();

  // NUOVO v7.0: ridisegna il display quando cambia qualcosa di
  // rilevante, senza rifarlo ad ogni giro di loop (che stresserebbe
  // inutilmente il bus SPI condiviso). Controlliamo ogni 2 secondi
  // se lo stato BLE o STA è cambiato rispetto all'ultimo disegno.
  if (millis() - ultimoRefreshDisplay > 2000) {
    ultimoRefreshDisplay = millis();
    bool bleOraConnesso = (modalitaCorrente == "BLE") && deviceConnected;
    static String ultimoIPMostrato = "";
    String ipAttuale = (modalitaCorrente == "STA" && WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
    if (bleOraConnesso != bleEraConnesso || ipAttuale != ultimoIPMostrato) {
      bleEraConnesso = bleOraConnesso;
      ultimoIPMostrato = ipAttuale;
      disegnaSchermata();
    }
  }

  // Sync automatico BLE: invia macro e credenziali 3s dopo connessione
  if (pendingSync && deviceConnected && (millis() - connectTime > 3000)) {
    pendingSync = false;
    inviaFileBLE("MACRO", FILE_MACRO);
    delay(300);
    inviaFileBLE("CRED", FILE_CRED);
    // Invia anche status SD e modalita'
    inviaBLE("STATUS:{\"mode\":\"BLE\",\"sd\":" + String(sdDisponibile ? "true" : "false") + "}\n");
  }
}


// ============================================================
// DISPATCHER COMANDI — eseguiComando()
// Unico punto di ingresso per comandi da BLE e da HTTP /api
// ============================================================
void eseguiComando(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  if (!cmd.startsWith("M:")) Serial.println("CMD: " + cmd.substring(0, 60));

  // ── WiFi (via BLE, per cambiare modalita' mentre si e' in BLE) ──
  if (cmd.startsWith("WIFI_")) { handleWiFiCommandsBLE(cmd); return; }

  // ── Storage LittleFS ────────────────────────────────────
  if (cmd == "MACRO_LIST")          { inviaFileBLE("MACRO", FILE_MACRO); return; }
  if (cmd == "CRED_LIST")           { inviaFileBLE("CRED",  FILE_CRED);  return; }
  if (cmd.startsWith("MACRO_SAVE:"))  { salvaMacroCustom(cmd.substring(11)); return; }
  if (cmd.startsWith("MACRO_DEL:"))   { eliminaMacroCustom(cmd.substring(10)); return; }
  if (cmd.startsWith("CRED_SAVE:"))   { salvaCredenziale(cmd.substring(10)); return; }
  if (cmd.startsWith("CRED_DEL:"))    { eliminaCredenziale(cmd.substring(9)); return; }
  if (cmd.startsWith("CRED_USE:"))    { eseguiCredenziale(cmd.substring(9)); return; }

  // ── File Manager via BLE (per accesso da BLE) ──
  if (cmd.startsWith("BLE_FS_LIST:")) {
    String path = cmd.substring(12);
    if (path.length() == 0) path = "/";
    String json = fsListDirJSON(path);
    const int CHUNK = 70;
    inviaBLE("FS_LIST_START\n");
    delay(30);
    for (int i = 0; i < (int)json.length(); i += CHUNK) {
        inviaBLE("FS_LIST_DATA:" + json.substring(i, min(i + CHUNK, (int)json.length())) + "\n");
        delay(80);
    }
    inviaBLE("FS_LIST_END\n");
    return;
  }

  if (cmd.startsWith("BLE_FS_READ:")) {
    String path = cmd.substring(12);
    String content = fsReadFile(path);
    if (content.length() == 0) {
        inviaBLE("FS_READ_ERR:File non trovato\n");
        return;
    }
    const int CHUNK = 70;
    inviaBLE("FS_READ_START\n");
    delay(30);
    for (int i = 0; i < (int)content.length(); i += CHUNK) {
        inviaBLE("FS_READ_DATA:" + content.substring(i, min(i + CHUNK, (int)content.length())) + "\n");
        delay(80);
    }
    inviaBLE("FS_READ_END\n");
    return;
  }

  if (cmd.startsWith("BLE_FS_WRITE:")) {
    // FIX v6.1.2: "BLE_FS_WRITE:" ha 13 caratteri, non 14 — il codice
    // precedente tagliava il primo carattere del path (es. lo "/"
    // iniziale), rompendo silenziosamente ogni scrittura via BLE.
    int sep = cmd.indexOf('|', 13);
    if (sep > 0) {
        String path = cmd.substring(13, sep);
        String content = cmd.substring(sep + 1);
        if (fsWriteFile(path, content)) {
            inviaBLE("FS_WRITE_OK:" + path + "\n");
        } else {
            inviaBLE("FS_WRITE_ERR\n");
        }
    }
    return;
  }

  if (cmd.startsWith("BLE_FS_DELETE:")) {
    String path = cmd.substring(14);
    if (fsDeleteFile(path)) {
        inviaBLE("FS_DELETE_OK:" + path + "\n");
    } else {
        inviaBLE("FS_DELETE_ERR\n");
    }
    return;
  }

  if (cmd.startsWith("BLE_FS_MKDIR:")) {
    String path = cmd.substring(13);
    if (fsCreateDir(path)) {
        inviaBLE("FS_MKDIR_OK:" + path + "\n");
    } else {
        inviaBLE("FS_MKDIR_ERR\n");
    }
    return;
  }

    if (cmd.startsWith("BLE_FS_RENAME:")) {
    String params = cmd.substring(14);
    int sep = params.indexOf('|');
    if (sep > 0) {
      String oldPath = params.substring(0, sep);
      String newPath = params.substring(sep + 1);
      
      if (!LittleFS.exists(oldPath)) {
        inviaBLE("FS_RENAME_ERR:File non trovato\n");
        return;
      }
      if (LittleFS.exists(newPath)) {
        inviaBLE("FS_RENAME_ERR:Il file di destinazione esiste già\n");
        return;
      }
      if (LittleFS.rename(oldPath, newPath)) {
        inviaBLE("FS_RENAME_OK:" + newPath + "\n");
      } else {
        inviaBLE("FS_RENAME_ERR:Operazione fallita\n");
      }
    }
    return;
  }

  // ── Payload da LittleFS (via BLE) ────────────────────────
  if (cmd.startsWith("PAYLOAD_RUN:")) {
    String path = cmd.substring(12);
    eseguiPayloadFS(path);
    return;
  }
  if (cmd == "PAYLOAD_STOP") { payloadInEsecuzione = false; inviaBLE("OK:PAYLOAD_STOP\n"); return; }

  // ── Lista/lettura file LittleFS via BLE (compatibilità
  //    protocollo: prefisso SD_ mantenuto per non rompere la
  //    pagina HTML esistente, ma ora legge da LittleFS) ─────
  if (cmd.startsWith("SD_LIST:")) {
    String path = cmd.substring(8);
    inviaBLE("SD_LIST:" + fsListDirJSON(path) + "\n");
    return;
  }
  if (cmd.startsWith("SD_READ:")) {
    String content = fsReadFile(cmd.substring(8));
    const int CHUNK = 80;
    inviaBLE("SD_DATA_START\n");
    delay(30);
    for (int i = 0; i < (int)content.length(); i += CHUNK) {
      inviaBLE("SD_DATA:" + content.substring(i, min(i + CHUNK, (int)content.length())) + "\n");
      delay(80);
    }
    inviaBLE("SD_DATA_END\n");
    return;
  }

  // ── Controllo base ──────────────────────────────────────
  if (cmd == "PING")    { inviaBLE("PONG\n"); return; }
  if (cmd == "RESET")   { resetTutto(); inviaBLE("OK:RESET\n"); return; }
  if (cmd == "STATUS")  {
    inviaBLE("STATUS:{\"mode\":\"" + modalitaCorrente + "\",\"sd\":" +
             String(sdDisponibile ? "true" : "false") + "}\n");
    return;
  }
  if (cmd == "HOLD_ON")  { holdActive = true;  inviaBLE("HOLD:ON\n");  return; }
  if (cmd == "HOLD_OFF") { holdActive = false; Keyboard.releaseAll(); inviaBLE("HOLD:OFF\n"); return; }
  if (cmd == "CAPS_TOGGLE") {
    capsLockActive = !capsLockActive;
    Keyboard.write(KEY_CAPS_LOCK); // v6.6b FIX: mancava l'invio reale del tasto —
                                   // prima aggiornava solo una variabile interna
                                   // senza mai toccare il PC target, per questo
                                   // le lettere non diventavano mai maiuscole.
    inviaBLE("CAPS:" + String(capsLockActive ? "ON" : "OFF") + "\n");
    return;
  }

  if (cmd.startsWith("MODE:"))   { setModoInvio(cmd.substring(5)); return; }
  if (cmd == "LAYOUT?") { inviaBLE(String("LAYOUT:") + (layoutCorrente == LAYOUT_US ? "US" : "IT") + "\n"); return; }
  if (cmd.startsWith("LAYOUT:")) { setLayout(cmd.substring(7)); return; }

  // ── Testo ───────────────────────────────────────────────
  if (cmd.startsWith("T:")) { inviaTesto(cmd.substring(2)); inviaBLE("OK:T\n"); return; }

  // ── Tasto / combinazione ─────────────────────────────────
  if (cmd.startsWith("K:")) { eseguiCombinazione(cmd.substring(2)); inviaBLE("OK:K\n"); return; }

  // ── Mouse ─────────────────────────────────────────────────
  if (cmd.startsWith("M:")) {
    int v = cmd.indexOf(',', 2);
    if (v > 2) {
      float rx = cmd.substring(2, v).toInt();
      float ry = cmd.substring(v + 1).toInt();
      if (rx == 0 && ry == 0) { mouseSmoothX = 0; mouseSmoothY = 0; return; }
      const float SMOOTH_K = 0.20;
      mouseSmoothX = mouseSmoothX * SMOOTH_K + rx * (1 - SMOOTH_K);
      mouseSmoothY = mouseSmoothY * SMOOTH_K + ry * (1 - SMOOTH_K);
      int fx = round(mouseSmoothX), fy = round(mouseSmoothY);
      if (fx != 0 || fy != 0) { Mouse.move(fx, fy); mouseSmoothX = mouseSmoothY = 0; }
    }
    return;
  }
  if (cmd.startsWith("SC:")) { Mouse.move(0, 0, -cmd.substring(3).toInt()); return; }
  if (cmd.startsWith("C:")) {
    char tipo = cmd.charAt(2);
    String act = (cmd.length() > 4) ? cmd.substring(4) : "";
    char btn = (tipo == 'L') ? MOUSE_LEFT : MOUSE_RIGHT;
    if      (act == "down") Mouse.press(btn);
    else if (act == "up")   Mouse.release(btn);
    else                    Mouse.click(btn);
    inviaBLE("OK:C\n"); return;
  }

  // ── Hold modificatori ────────────────────────────────────
  if (cmd.startsWith("HD:")) {
    String t = cmd.substring(3); t.toUpperCase();
    if (t == "OFF" || t == "RESET") {
      Keyboard.releaseAll();
      holdModCtrl = holdModAlt = holdModShift = holdModWin = false;
    } else {
      if (t.indexOf("CTRL")  >= 0) { holdModCtrl  = !holdModCtrl;
        holdModCtrl  ? Keyboard.press(KEY_LEFT_CTRL)  : Keyboard.release(KEY_LEFT_CTRL);  }
      if (t.indexOf("ALT")   >= 0) { holdModAlt   = !holdModAlt;
        holdModAlt   ? Keyboard.press(KEY_LEFT_ALT)   : Keyboard.release(KEY_LEFT_ALT);   }
      if (t.indexOf("SHIFT") >= 0) { holdModShift = !holdModShift;
        holdModShift ? Keyboard.press(KEY_LEFT_SHIFT) : Keyboard.release(KEY_LEFT_SHIFT); }
      if (t.indexOf("WIN")   >= 0) { holdModWin   = !holdModWin;
        holdModWin   ? Keyboard.press(KEY_LEFT_GUI)   : Keyboard.release(KEY_LEFT_GUI);   }
    }
    inviaBLE("OK:HD\n"); return;
  }

  // ── Macro predefinite ────────────────────────────────────
  if (cmd.startsWith("MACRO:"))    { eseguiMacro(cmd.substring(6)); inviaBLE("OK:MACRO\n"); return; }

  // ── Macro personalizzate da LittleFS ─────────────────────
  if (cmd.startsWith("MACRO_RUN:")) {
    String nome = cmd.substring(10);
    String existing = leggiFile(FILE_MACRO);
    DynamicJsonDocument doc(8192);
    deserializeJson(doc, existing);
    JsonArray arr = doc.as<JsonArray>();
    for (int i = 0; i < (int)arr.size(); i++) {
      if (String(arr[i]["n"].as<const char*>()) == nome) {
        eseguiSequenza(arr[i]["s"].as<String>());
        inviaBLE("OK:MACRO_RUN\n"); return;
      }
    }
    inviaBLE("ERR:MACRO_NOT_FOUND\n"); return;
  }

  inviaBLE("ERR:UNKNOWN_CMD\n");
}

// ============================================================
// COMANDI WIFI VIA BLE (cambio modalita' da remoto)
// ============================================================
void handleWiFiCommandsBLE(String cmd) {
  if (cmd == "WIFI_STATUS") {
    inviaBLE("WIFI:{\"mode\":\"" + modalitaCorrente + "\",\"sd\":" +
             String(sdDisponibile ? "true" : "false") + "}\n");
    return;
  }
  if (cmd.startsWith("WIFI_CONNECT:")) {
    String params = cmd.substring(13);
    int sep = params.indexOf('|');
    if (sep > 0) {
      String ssid = params.substring(0, sep);
      String pass = params.substring(sep + 1);
      saveNetwork(ssid, pass);
      preferences.begin("kvm", false);
      preferences.putString("sta_ssid", ssid);
      preferences.putString("sta_pass", pass);
      preferences.putString("mode", "STA");
      preferences.end();
      inviaBLE("WIFI:CONNECTING " + ssid + "\n");
      delay(100); ESP.restart();
    } else { inviaBLE("ERR:WIFI_INVALID\n"); }
    return;
  }
  if (cmd == "WIFI_AP")  { salvaModalita("AP");  inviaBLE("WIFI:AP_MODE\n");  delay(100); ESP.restart(); return; }
  if (cmd == "WIFI_STA") { salvaModalita("STA"); inviaBLE("WIFI:STA_MODE\n"); delay(100); ESP.restart(); return; }
  if (cmd == "WIFI_BLE") { salvaModalita("BLE"); inviaBLE("WIFI:BLE_MODE\n"); delay(100); ESP.restart(); return; }
  if (cmd == "WIFI_NETWORKS") {
    String json = "[";
    for (int i = 0; i < (int)savedNetworks.size(); i++) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + savedNetworks[i].ssid + "\"}";
    }
    json += "]";
    inviaBLE("WIFI_NETWORKS:" + json + "\n");
    return;
  }
  if (cmd.startsWith("WIFI_DELETE:")) {
    deleteNetwork(cmd.substring(12));
    inviaBLE("WIFI_DELETED\n");
    return;
  }
}

// ============================================================
// COMBINAZIONI TASTI
// ============================================================
void eseguiCombinazione(String combo) {
  combo.trim();
  String C = combo; C.toUpperCase();

  bool ctrl  = C.indexOf("CTRL")  >= 0;
  bool alt   = C.indexOf("ALT")   >= 0;
  bool shift = C.indexOf("SHIFT") >= 0;
  bool win   = C.indexOf("WIN")   >= 0;

  String finale = combo;
  int ult = combo.lastIndexOf('+');
  if (ult >= 0) finale = combo.substring(ult + 1);
  finale.trim(); finale.toUpperCase();

  int kc = 0;
  if      (finale=="ENTER"||finale=="RETURN") kc=KEY_RETURN;
  else if (finale=="TAB")                      kc=KEY_TAB;
  else if (finale=="ESC"||finale=="ESCAPE")    kc=KEY_ESC;
  else if (finale=="DEL"||finale=="DELETE")    kc=KEY_DELETE;
  else if (finale=="BS"||finale=="BACKSPACE")  kc=KEY_BACKSPACE;
  else if (finale=="SPACE")                    kc=' ';
  else if (finale=="UP")                       kc=KEY_UP_ARROW;
  else if (finale=="DOWN")                     kc=KEY_DOWN_ARROW;
  else if (finale=="LEFT")                     kc=KEY_LEFT_ARROW;
  else if (finale=="RIGHT")                    kc=KEY_RIGHT_ARROW;
  else if (finale=="HOME")                     kc=KEY_HOME;
  else if (finale=="END")                      kc=KEY_END;
  else if (finale=="PGUP")                     kc=KEY_PAGE_UP;
  else if (finale=="PGDN")                     kc=KEY_PAGE_DOWN;
  else if (finale=="INSERT")                   kc=KEY_INSERT;
  else if (finale=="PRTSC")                    kc=KEY_PRINT_SCREEN;
  else if (finale=="F1")  kc=KEY_F1;  else if (finale=="F2")  kc=KEY_F2;
  else if (finale=="F3")  kc=KEY_F3;  else if (finale=="F4")  kc=KEY_F4;
  else if (finale=="F5")  kc=KEY_F5;  else if (finale=="F6")  kc=KEY_F6;
  else if (finale=="F7")  kc=KEY_F7;  else if (finale=="F8")  kc=KEY_F8;
  else if (finale=="F9")  kc=KEY_F9;  else if (finale=="F10") kc=KEY_F10;
  else if (finale=="F11") kc=KEY_F11; else if (finale=="F12") kc=KEY_F12;
  else if (finale.length()==1) {
    char ch = finale.charAt(0);
    if (ch>='A'&&ch<='Z')      kc = ch + 32;
    else if (ch>='0'&&ch<='9') kc = ch;
  }

  if (holdActive) {
    if (ctrl)  Keyboard.press(KEY_LEFT_CTRL);
    if (alt)   Keyboard.press(KEY_LEFT_ALT);
    if (shift) Keyboard.press(KEY_LEFT_SHIFT);
    if (win)   Keyboard.press(KEY_LEFT_GUI);
    if (kc)    Keyboard.press(kc);
    return;
  }

  if (ctrl)  Keyboard.press(KEY_LEFT_CTRL);
  if (alt)   Keyboard.press(KEY_LEFT_ALT);
  if (shift) Keyboard.press(KEY_LEFT_SHIFT);
  if (win)   Keyboard.press(KEY_LEFT_GUI);
  if (kc)  { Keyboard.press(kc); delay(30); }
  delay(30);
  Keyboard.releaseAll(); delay(30);

  // Ripristina hold modificatori globali se attivi
  if (holdModCtrl)  Keyboard.press(KEY_LEFT_CTRL);
  if (holdModAlt)   Keyboard.press(KEY_LEFT_ALT);
  if (holdModShift) Keyboard.press(KEY_LEFT_SHIFT);
  if (holdModWin)   Keyboard.press(KEY_LEFT_GUI);
}

// ============================================================
// SEQUENZE CON [TOKEN]
// ============================================================
void eseguiSequenza(String seq) {
  int pos = 0;
  while (pos < (int)seq.length()) {
    if (seq.charAt(pos) == '[') {
      int fine = seq.indexOf(']', pos);
      if (fine > pos) {
        String token = seq.substring(pos + 1, fine); token.trim();
        // FIX v7.1: [WAIT]/[PAUSE] ora e' davvero case-insensitive.
        // Prima si confrontava "token == WAIT" senza normalizzare il
        // maiuscolo/minuscolo — un fix descritto altrove ma mai
        // realmente scritto nel codice. [wait] minuscolo cadeva nel
        // ramo sbagliato e veniva interpretato come combinazione tasti.
        String tokenUpper = token; tokenUpper.toUpperCase();
        if (tokenUpper == "WAIT" || tokenUpper == "PAUSE") {
          delay(600);
        }
        // NUOVO v7.1: [ALT+Y] e [ALT+N] per rispondere ai dialoghi UAC
        // di Windows ("Vuoi consentire a questa app di apportare
        // modifiche al dispositivo?"), utili nelle macro admin.
        else if (tokenUpper == "ALT+Y") {
          Keyboard.press(KEY_LEFT_ALT); Keyboard.press('y');
          delay(30); Keyboard.releaseAll();
        }
        else if (tokenUpper == "ALT+N") {
          Keyboard.press(KEY_LEFT_ALT); Keyboard.press('n');
          delay(30); Keyboard.releaseAll();
        }
        else eseguiCombinazione(token);
        pos = fine + 1;
      } else pos++;
    } else {
      int fine = seq.indexOf('[', pos);
      if (fine < 0) fine = seq.length();
      String chunk = seq.substring(pos, fine);
      if (chunk.length() > 0) inviaTesto(chunk);
      pos = fine;
    }
  }
}

void eseguiMacro(String id) {
  id.trim();
  for (size_t i = 0; i < NUM_MACRO; i++) {
    if (String(macroList[i].id) == id) {
      eseguiSequenza(String(macroList[i].sequenza)); return;
    }
  }
  inviaBLE("ERR:MACRO_NOT_FOUND\n");
}

// ============================================================
// INVIO TESTO CON FIX ACCENTI (v6.0) - CORRETTO CON EURO
// ============================================================
void inviaCarattereAccentato(uint16_t codepoint) {
  for (size_t i = 0; i < NUM_ACCENTI; i++) {
    if (accentTable[i].codepoint == codepoint) {
      if (modoCorrente == MODO_ALT) {
        String altStr = String(accentTable[i].altCode);
        inviaSequenzaALT("ALT+" + altStr);
      } else {
        // FIX: stesso ritardo del ramo caratteri semplici, stesso motivo
        Keyboard.print(accentTable[i].utf8);
        delay(5);
      }
      return;
    }
  }
  Keyboard.print("?");
  delay(5);
}

void inviaTesto(String testo) {
  int i = 0;
  int len = (int)testo.length();
  while (i < len) {
    // v6.6d FIX: delimitatori cambiati da '[' ']' a caratteri di
    // controllo \x01/\x02 — le parentesi quadre letterali nel testo
    // (array, JSON, codice) si confondevano con i delimitatori
    // sintetici usati per i tag ALT+xxxx, corrompendo il parsing
    // di tutto il testo successivo al primo conflitto.
    if (testo.charAt(i) == '\x01') {
      int fine = testo.indexOf('\x02', i);
      if (fine > i) {
        String token = testo.substring(i + 1, fine); token.trim();
        if (token.startsWith("ALT+")) inviaSequenzaALT(token);
        else eseguiCombinazione(token);
        i = fine + 1; continue;
      }
    }

    uint8_t b0 = (uint8_t)testo.charAt(i);

    if (b0 < 0x80) {
      char c = (char)b0;
      // NUOVO v7.1: simboli che sul layout italiano richiedono AltGr
      // o Shift — verificati contro la disposizione fisica IT reale.
      // Nota: ogni caso costa un piccolo delay(10) per garantire che
      // il sistema operativo host registri la combinazione prima del
      // rilascio; su testi con molti di questi simboli (es. comandi
      // con parentesi/graffe) questo si somma e rallenta l'invio.
      // i case AltGr+codici raw valgono SOLO per layout IT; con US '\x7F'
      // non matcha nulla -> default -> Keyboard.print(c), gia' corretto.
      switch (layoutCorrente == LAYOUT_IT ? c : '\x7F') {
        case '\'': Keyboard.print("'"); break;
        case '"':  Keyboard.print("\""); break;
        // ★★★ FIX v7.2: HID corretti per layout tastiera IT ★★★
        // Su layout IT:
        //   | = AltGr + tasto 1  (HID 0x1E)
        //   ~ = AltGr + tasto 5  (HID 0x22)
        //   [ = AltGr + tasto è  (HID 0x08)
        //   ] = AltGr + tasto +  (HID 0x2E)
        //   \ = AltGr + tasto ù  (HID 0x31)
        //   { = AltGr+Shift + è  (HID 0x08)
        //   } = AltGr+Shift + +  (HID 0x2E)
        // AltGr + tasto
        case '|':
          Keyboard.press(KEY_RIGHT_ALT); Keyboard.pressRaw(0x1E);  // FIX: tasto 1
          delay(10); Keyboard.releaseAll(); break;
        case '~':
          Keyboard.press(KEY_RIGHT_ALT); Keyboard.pressRaw(0x22);  // FIX: tasto 5
          delay(10); Keyboard.releaseAll(); break;
        case '[':
          Keyboard.press(KEY_RIGHT_ALT); Keyboard.pressRaw(0x08);  // OK: tasto è
          delay(10); Keyboard.releaseAll(); break;
        case ']':
          Keyboard.press(KEY_RIGHT_ALT); Keyboard.pressRaw(0x2E);  // OK: tasto +
          delay(10); Keyboard.releaseAll(); break;
        case '\\':
          Keyboard.press(KEY_RIGHT_ALT); Keyboard.pressRaw(0x31);  // OK: tasto ù
          delay(10); Keyboard.releaseAll(); break;
        // AltGr + Shift + tasto
        case '{':
          Keyboard.press(KEY_RIGHT_ALT); Keyboard.press(KEY_LEFT_SHIFT);
          Keyboard.pressRaw(0x08); delay(10); Keyboard.releaseAll(); break;
        case '}':
          Keyboard.press(KEY_RIGHT_ALT); Keyboard.press(KEY_LEFT_SHIFT);
          Keyboard.pressRaw(0x2E); delay(10); Keyboard.releaseAll(); break;
        // RIPRISTINATO: & * ( ) _ + < > NON hanno mai avuto bisogno di
        // ricostruzione manuale Shift+cifra — era basata su un'assunzione
        // sbagliata della disposizione della tastiera italiana (es.
        // Shift+0 sulla IT reale produce '=', non ')'). Sono caratteri
        // ASCII standard che Keyboard.print() gestisce già correttamente
        // da solo tramite il layout IT caricato — cadono nel ramo
        // 'default' qui sotto, come apostrofo e virgolette.
        default:
          // FIX: ritardo tra un carattere e il successivo — senza,
          // l'invio troppo rapido di tasti USB HID può far perdere o
          // mischiare lettere al sistema operativo di destinazione
          // (sintomo: "parole scritte male o mischiate"). 5ms è un
          // compromesso tra affidabilità e velocità di digitazione.
          Keyboard.print(String(c));
          delay(5);
          break;
      }
      i++;
      continue;
    }

    if (b0 >= 0xC0 && b0 <= 0xDF && i + 1 < len) {
      uint8_t b1 = (uint8_t)testo.charAt(i + 1);
      if (b1 >= 0x80 && b1 <= 0xBF) {
        uint16_t cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
        inviaCarattereAccentato(cp);
        i += 2; continue;
      }
    }

    // v6.3 FIX: i caratteri UTF-8 a 3 byte (0xE0-0xEF) venivano
    // SALTATI senza scrivere nulla — bug che "inghiottiva" l'euro
    // (€ = 0xE2 0x82 0xAC) e qualsiasi altro simbolo a 3 byte
    // (virgolette tipografiche “ ”, trattino lungo —, ecc. spesso
    // inseriti dall'autocorrezione del telefono). Ora decodifichiamo
    // il vero codepoint Unicode e gestiamo almeno l'euro via ALT+0128;
    // gli altri simboli a 3 byte non hanno un ALT-code universale
    // affidabile su tutti i sistemi, quindi mostriamo '?' invece di
    // sparire silenziosamente (più facile da notare e correggere).
    if (b0 >= 0xE0 && b0 <= 0xEF && i + 2 < len) {
      uint8_t b1 = (uint8_t)testo.charAt(i + 1);
      uint8_t b2 = (uint8_t)testo.charAt(i + 2);
      if (b1 >= 0x80 && b1 <= 0xBF && b2 >= 0x80 && b2 <= 0xBF) {
        uint32_t cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(b1 & 0x3F) << 6) | (b2 & 0x3F);
        if (cp == 0x20AC) {
          // ★★★ EURO — METODO UNIVERSALE: ALT+0128 ★★★
          Keyboard.press(KEY_LEFT_ALT);
          delay(30);
          Keyboard.press(KEY_KP_0); delay(10); Keyboard.release(KEY_KP_0);
          Keyboard.press(KEY_KP_1); delay(10); Keyboard.release(KEY_KP_1);
          Keyboard.press(KEY_KP_2); delay(10); Keyboard.release(KEY_KP_2);
          Keyboard.press(KEY_KP_8); delay(10); Keyboard.release(KEY_KP_8);
          delay(30);
          Keyboard.release(KEY_LEFT_ALT);
        } else {
          Keyboard.print("?");
        }
      }
      i += 3; continue;
    }
    i++;
  }
}

void inviaSequenzaALT(String seq) {
  seq.replace("ALT+", "");
  seq.trim();
  Keyboard.press(KEY_LEFT_ALT);
  delay(50);
  for (int i = 0; i < (int)seq.length(); i++) {
    uint8_t kp = 0;
    switch (seq.charAt(i)) {
      case '0': kp=KEY_KP_0; break; case '1': kp=KEY_KP_1; break;
      case '2': kp=KEY_KP_2; break; case '3': kp=KEY_KP_3; break;
      case '4': kp=KEY_KP_4; break; case '5': kp=KEY_KP_5; break;
      case '6': kp=KEY_KP_6; break; case '7': kp=KEY_KP_7; break;
      case '8': kp=KEY_KP_8; break; case '9': kp=KEY_KP_9; break;
    }
    if (kp) { Keyboard.press(kp); delay(20); Keyboard.release(kp); delay(10); }
  }
  delay(30);
  Keyboard.release(KEY_LEFT_ALT);
  delay(30);
}

void inviaBLE(String msg) {
  if (!deviceConnected || !pTX) return;
  pTX->setValue((uint8_t*)msg.c_str(), msg.length());
  pTX->notify();
}

void resetTutto() {
  Keyboard.releaseAll();
  Mouse.release(MOUSE_LEFT);
  Mouse.release(MOUSE_RIGHT);
  holdModCtrl = holdModAlt = holdModShift = holdModWin = false;
  holdActive = false;
  mouseSmoothX = mouseSmoothY = 0;
}

// Layout tastiera: cambia davvero la tabella HID e viene salvato in flash.
// SUPPORTATI solo IT e US (unici nomi costante verificati). UK/FR/DE/ES/PT
// NON vengono finti: rispondono ERR. Per aggiungerli verifica i nomi in
// USBHIDKeyboard.h del core. (Idea: sessione parallela v7.2 - persistenza
// in flash, Keyboard.end() prima di begin(), query LAYOUT?.)
void applicaLayoutHID() {
  Keyboard.end();
  if (layoutCorrente == LAYOUT_US) Keyboard.begin(KeyboardLayout_en_US);
  else                             Keyboard.begin(KeyboardLayout_it_IT);
}

void setLayout(String l) {
  l.toUpperCase();
  if (l == "IT")      layoutCorrente = LAYOUT_IT;
  else if (l == "US") layoutCorrente = LAYOUT_US;
  else {
    inviaBLE("ERR:LAYOUT_NON_SUPPORTATO:" + l + "\n");
    return;
  }
  applicaLayoutHID();
  Preferences pLay;
  pLay.begin("kvm", false);
  pLay.putString("layout", l);
  pLay.end();
  inviaBLE("LAYOUT:" + l + "\n");
}

void setModoInvio(String m) {
  m.toUpperCase();
  if      (m=="ALT")     modoCorrente=MODO_ALT;
  else if (m=="UNICODE") modoCorrente=MODO_UNICODE;
  else if (m=="ASCII")   modoCorrente=MODO_ASCII;
  else                   modoCorrente=MODO_AUTO;
  inviaBLE("MODE:" + m + "\n");
}
