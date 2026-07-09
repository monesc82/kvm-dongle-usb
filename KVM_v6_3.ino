 * IMPOSTAZIONI IDE ARDUINO:
 *   Board:            ESP32S3 Dev Module (LilyGo T-Dongle S3)
 *   USB Mode:         USB-OTG (TinyUSB)
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
                    showToast('❌ Errore caricamento: ' + xhr.status);
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

  // ── /kvm/upload — carica la pagina KVM completa su LittleFS ──
  // FIX v6.1.2: scrittura binary-safe con File handle persistente
  // invece di String((char*)buf) che tronca al primo byte NUL.
  // Aggiunta verifica spazio libero e verifica dimensione finale.
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
        if (freeSpace < 20000) {
          Serial.println("❌ Spazio LittleFS insufficiente per upload KVM");
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
  }
  void onDisconnect(BLEServer* s) override {
    deviceConnected = false;
    pendingSync = false;
    digitalWrite(LED_BUILTIN, LOW);
    Serial.println("⚠️ BLE Disconnesso — riavvio advertising");
    s->startAdvertising();
    resetTutto();
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
  Serial.println("  KVM Dongle Unified
  Serial.println("========================================");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  // Inizializza storage LittleFS
  initStorage();

  // Carica reti salvate da LittleFS
  loadSavedNetworks();

  // Leggi modalita' da Preferences
  modalitaCorrente = leggiModalitaAvvio();

  // BOOT button tenuto premuto > 3s → forza AP WiFi (escape hatch)
  if (bootButtonPremuto()) {
    Serial.println("🔴 BOOT button rilevato — attesa 3s per conferma AP forzato...");
    delay(3000);
    if (bootButtonPremuto()) {
      Serial.println("✅ Forzato AP WiFi via BOOT button");
      modalitaCorrente = "AP";
      salvaModalita("AP");
    }
  }

  Serial.println("📌 Modalita': " + modalitaCorrente);

  // Inizializza USB HID (sempre, indipendente da modalita' wireless)
  Keyboard.begin(KeyboardLayout_it_IT);
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

  } else if (modalitaCorrente == "AP") {
    setupWiFiAP();
    setupWebServer();
    // 1 lampeggio lungo = AP
    digitalWrite(LED_BUILTIN, HIGH); delay(800); digitalWrite(LED_BUILTIN, LOW);

  } else if (modalitaCorrente == "STA") {
    if (sta_ssid.length() == 0) {
      Serial.println("⚠️ STA: nessun SSID — fallback AP");
      modalitaCorrente = "AP";
      salvaModalita("AP");
      setupWiFiAP();
    } else {
      setupWiFiSTA();
    }
    setupWebServer();
    // 3 lampeggi veloci = STA
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_BUILTIN, HIGH); delay(150);
      digitalWrite(LED_BUILTIN, LOW);  delay(150);
    }
  }
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  delay(5);
  if (webServerAttivo) {
    server.handleClient();
    dnsServer.processNextRequest();
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
    inviaBLE("CAPS:" + String(capsLockActive ? "ON" : "OFF") + "\n");
    return;
  }

  if (cmd.startsWith("MODE:"))   { setModoInvio(cmd.substring(5)); return; }
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
        if (token == "WAIT" || token == "PAUSE") delay(600);
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
        Keyboard.print(accentTable[i].utf8);
      }
      return;
    }
  }
  Keyboard.print("?");
}

void inviaTesto(String testo) {
  int i = 0;
  int len = (int)testo.length();
  while (i < len) {
    if (testo.charAt(i) == '[') {
      int fine = testo.indexOf(']', i);
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
      if (c == '\'') {
        Keyboard.print("'");
      } else if (c == '"') {
        Keyboard.print("\"");
      } else {
        Keyboard.print(String(c));
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

void setLayout(String l) {
  l.toUpperCase();
  if      (l=="IT") layoutCorrente=LAYOUT_IT;
  else if (l=="US") layoutCorrente=LAYOUT_US;
  else if (l=="UK") layoutCorrente=LAYOUT_UK;
  else if (l=="FR") layoutCorrente=LAYOUT_FR;
  else if (l=="DE") layoutCorrente=LAYOUT_DE;
  else if (l=="ES") layoutCorrente=LAYOUT_ES;
  else if (l=="PT") layoutCorrente=LAYOUT_PT;
  else              layoutCorrente=LAYOUT_ASCII;
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
