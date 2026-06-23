/*
 * KVM Bridge T-Dongle S3 - v5.19
 *
 * NOVITÀ rispetto v5.18:
 *   - LittleFS: macro custom e credenziali salvate sul dispositivo
 *     (non più nel browser, funziona da qualsiasi telefono/PC)
 *   - Comandi BLE: MACRO_SAVE, MACRO_DEL, MACRO_LIST,
 *                  CRED_SAVE, CRED_DEL, CRED_LIST
 *   - Al boot carica dati da flash e li invia al client connesso
 *   - Bug fix: delay(1) → delay(5) nel loop
 *
 * IMPOSTAZIONI IDE:
 *   Board:       LilyGo T-Dongle S3
 *   Partition:   Huge APP (3MB/1MB LittleFS) ← OBBLIGATORIO per LittleFS
 *   USB Mode:    USB-OTG (TinyUSB)
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

USBHIDKeyboard Keyboard;
USBHIDMouse Mouse;

#define SERVICE_UUID      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLECharacteristic *pTX;
BLECharacteristic *pRX;
bool deviceConnected = false;
String bleBuffer = "";

float mouseSmoothX = 0, mouseSmoothY = 0;
const float SMOOTH = 0.20;

bool holdModCtrl = false, holdModAlt = false;
bool holdModShift = false, holdModWin = false;
bool holdActive = false;
bool capsLockActive = false;

// ── Layout e modalità ────────────────────────────────────────
enum LayoutTastiera { LAYOUT_IT, LAYOUT_US, LAYOUT_UK, LAYOUT_FR,
                      LAYOUT_DE, LAYOUT_ES, LAYOUT_PT, LAYOUT_ASCII };
enum ModoInvio      { MODO_AUTO, MODO_ALT, MODO_UNICODE, MODO_ASCII };

LayoutTastiera layoutCorrente = LAYOUT_IT;
ModoInvio      modoCorrente   = MODO_AUTO;

// ── Mappa accenti ─────────────────────────────────────────────
struct AccentMap { String seq; String unicode; String base; };
AccentMap accentMap[] = {
  {"0224","à","a"}, {"0232","è","e"}, {"0233","é","e"},
  {"0236","ì","i"}, {"0242","ò","o"}, {"0249","ù","u"},
  {"0192","À","A"}, {"0200","È","E"}, {"0201","É","E"},
  {"0204","Ì","I"}, {"0210","Ò","O"}, {"0217","Ù","U"},
};
#define NUM_ACCENTI (sizeof(accentMap)/sizeof(accentMap[0]))

// ── Macro predefinite (in flash, non modificabili) ────────────
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
  {"lnx_term",     "Terminale Linux",    "[CTRL+ALT+T]"},
  {"lnx_update",   "apt update",         "sudo apt update[ENTER]"},
  {"lnx_upgrade",  "apt upgrade",        "sudo apt upgrade -y[ENTER]"},
  {"lnx_df",       "df -h",              "df -h[ENTER]"},
  {"lnx_top",      "top",                "top[ENTER]"},
  {"lnx_lsblk",   "lsblk",              "lsblk[ENTER]"},
  {"lnx_dmesg",    "dmesg tail",         "dmesg -T | tail -30[ENTER]"},
  {"lnx_ss",       "ss -tulpn",          "ss -tulpn[ENTER]"},
  {"bios_f2",      "BIOS F2",            "[F2]"},
  {"bios_f10",     "BIOS F10",           "[F10]"},
  {"bios_f12",     "Boot Menu F12",      "[F12]"},
  {"bios_del",     "BIOS DEL",           "[DELETE]"},
};
#define NUM_MACRO (sizeof(macroList)/sizeof(macroList[0]))

// ── Prototipi ─────────────────────────────────────────────────
void eseguiComando(String cmd);
void eseguiCombinazione(String combo);
void eseguiSequenza(String seq);
void eseguiMacro(String id);
void inviaTesto(String testo);
void inviaAccentato(String seq);
void inviaSequenzaALT(String seq);
void inviaBLE(String msg);
void inviaChunkedBLE(String msg);
void resetTutto();
void setModoInvio(String modo);
void setLayout(String layout);

// ── Storage LittleFS ─────────────────────────────────────────
#define FILE_MACRO "/macro_custom.json"
#define FILE_CRED  "/credenziali.json"

void initStorage() {
  if (!LittleFS.begin(true)) {
    Serial.println("❌ LittleFS mount FAIL");
    return;
  }
  Serial.println("✅ LittleFS OK");
  // Crea file vuoti se non esistono
  if (!LittleFS.exists(FILE_MACRO)) {
    File f = LittleFS.open(FILE_MACRO, "w");
    f.print("[]"); f.close();
  }
  if (!LittleFS.exists(FILE_CRED)) {
    File f = LittleFS.open(FILE_CRED, "w");
    f.print("[]"); f.close();
  }
}

// Invia JSON via BLE in chunks da 180 byte
// (MTU BLE default 20b, negoziato fino a 512b, usiamo 180 per sicurezza)
void inviaFileBLE(String prefix, const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) { inviaBLE("ERR:FILE\n"); return; }
  String content = f.readString();
  f.close();

  // Invia in blocchi con header e sequenza
  // Formato: PREFIX_START:<totale>\n  PREFIX_DATA:<chunk>\n  PREFIX_END\n
  int total = content.length();
  inviaBLE(prefix + "_START:" + String(total) + "\n");
  delay(30);

  const int CHUNK = 180;
  for (int i = 0; i < total; i += CHUNK) {
    String chunk = content.substring(i, min(i + CHUNK, total));
    inviaBLE(prefix + "_DATA:" + chunk + "\n");
    delay(50); // Lascia tempo al client BLE di ricevere
  }
  inviaBLE(prefix + "_END\n");
}

bool salvaSuFile(const char* path, String jsonContent) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  f.print(jsonContent);
  f.close();
  return true;
}

String leggiFile(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return "[]";
  String s = f.readString();
  f.close();
  return s;
}

// ── Aggiunge una macro custom ─────────────────────────────────
// Formato comando BLE: MACRO_SAVE:{"n":"nome","s":"sequenza"}
void salvaMacroCustom(String jsonItem) {
  String existing = leggiFile(FILE_MACRO);
  DynamicJsonDocument docList(8192);
  deserializeJson(docList, existing);
  JsonArray arr = docList.as<JsonArray>();

  DynamicJsonDocument docItem(512);
  DeserializationError err = deserializeJson(docItem, jsonItem);
  if (err) { inviaBLE("ERR:JSON_MACRO\n"); return; }

  // Controlla se esiste già (aggiorna)
  const char* nome = docItem["n"];
  for (int i = 0; i < (int)arr.size(); i++) {
    if (String(arr[i]["n"].as<const char*>()) == String(nome)) {
      arr[i]["s"] = docItem["s"].as<const char*>();
      String out; serializeJson(docList, out);
      salvaSuFile(FILE_MACRO, out);
      inviaBLE("MACRO_SAVED:" + String(nome) + "\n");
      return;
    }
  }

  // Aggiunge nuovo
  JsonObject newItem = arr.createNestedObject();
  newItem["n"] = docItem["n"].as<const char*>();
  newItem["s"] = docItem["s"].as<const char*>();
  String out; serializeJson(docList, out);
  salvaSuFile(FILE_MACRO, out);
  inviaBLE("MACRO_SAVED:" + String(nome) + "\n");
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
      salvaSuFile(FILE_MACRO, out);
      inviaBLE("MACRO_DELETED:" + nome + "\n");
      return;
    }
  }
  inviaBLE("ERR:MACRO_NOT_FOUND\n");
}

// ── Aggiunge una credenziale ──────────────────────────────────
// Formato: CRED_SAVE:{"n":"nome","u":"user","p":"pass"}
void salvaCredenziale(String jsonItem) {
  String existing = leggiFile(FILE_CRED);
  DynamicJsonDocument docList(8192);
  deserializeJson(docList, existing);
  JsonArray arr = docList.as<JsonArray>();

  DynamicJsonDocument docItem(512);
  DeserializationError err = deserializeJson(docItem, jsonItem);
  if (err) { inviaBLE("ERR:JSON_CRED\n"); return; }

  const char* nome = docItem["n"];
  for (int i = 0; i < (int)arr.size(); i++) {
    if (String(arr[i]["n"].as<const char*>()) == String(nome)) {
      arr[i]["u"] = docItem["u"].as<const char*>();
      arr[i]["p"] = docItem["p"].as<const char*>();
      String out; serializeJson(docList, out);
      salvaSuFile(FILE_CRED, out);
      inviaBLE("CRED_SAVED:" + String(nome) + "\n");
      return;
    }
  }

  JsonObject newItem = arr.createNestedObject();
  newItem["n"] = docItem["n"].as<const char*>();
  newItem["u"] = docItem["u"].as<const char*>();
  newItem["p"] = docItem["p"].as<const char*>();
  String out; serializeJson(docList, out);
  salvaSuFile(FILE_CRED, out);
  inviaBLE("CRED_SAVED:" + String(nome) + "\n");
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
      salvaSuFile(FILE_CRED, out);
      inviaBLE("CRED_DELETED:" + nome + "\n");
      return;
    }
  }
  inviaBLE("ERR:CRED_NOT_FOUND\n");
}

// Esegue una credenziale direttamente dal dispositivo
void eseguiCredenziale(String nome) {
  String existing = leggiFile(FILE_CRED);
  DynamicJsonDocument doc(8192);
  deserializeJson(doc, existing);
  JsonArray arr = doc.as<JsonArray>();

  for (int i = 0; i < (int)arr.size(); i++) {
    if (String(arr[i]["n"].as<const char*>()) == nome) {
      String user = arr[i]["u"].as<String>();
      String pass = arr[i]["p"].as<String>();
      if (user.length() > 0) {
        inviaTesto(user); delay(80);
        Keyboard.write(KEY_TAB); delay(80);
      }
      inviaTesto(pass); delay(50);
      Keyboard.write(KEY_RETURN);
      inviaBLE("OK:CRED\n");
      return;
    }
  }
  inviaBLE("ERR:CRED_NOT_FOUND\n");
}

// ── BLE Callbacks ─────────────────────────────────────────────
class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer* s) {
    deviceConnected = true;
    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("✅ BLE Connesso");
    // Al connect: invia automaticamente macro e credenziali
    delay(500);
    inviaFileBLE("MACRO", FILE_MACRO);
    delay(200);
    inviaFileBLE("CRED", FILE_CRED);
  }
  void onDisconnect(BLEServer* s) {
    deviceConnected = false;
    digitalWrite(LED_BUILTIN, LOW);
    Serial.println("⚠️ BLE Disconnesso");
    s->startAdvertising();
    resetTutto();
  }
};

class CharCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pC) {
    uint8_t* data = pC->getData();
    size_t len = pC->getLength();
    if (len > 0) {
      for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n' || c == '\r') {
          if (bleBuffer.length() > 0) {
            eseguiComando(bleBuffer);
            bleBuffer = "";
          }
        } else {
          bleBuffer += c;
          if (bleBuffer.length() > 512) bleBuffer = "";
        }
      }
    }
  }
};

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  USB.begin();
  Serial.begin(115200);
  delay(500);
  Serial.println("========================================");
  Serial.println("KVM Bridge T-Dongle S3 v5.19");
  Serial.println("LittleFS + Memoria Persistente");
  Serial.println("========================================");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  initStorage();

  Keyboard.begin(KeyboardLayout_it_IT);
  Mouse.begin();

  BLEDevice::init("KVM-S3");
  BLEDevice::setMTU(512); // Negozia MTU alto per chunk grandi
  BLEServer *srv = BLEDevice::createServer();
  srv->setCallbacks(new ServerCB());
  BLEService *svc = srv->createService(SERVICE_UUID);
  pTX = svc->createCharacteristic(CHARACTERISTIC_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pRX = svc->createCharacteristic(CHARACTERISTIC_RX,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pRX->setCallbacks(new CharCB());
  svc->start();
  BLEAdvertising *adv = srv->getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->start();

  Serial.println("✅ Pronto! BLE: KVM-S3");
}

void loop() {
  delay(5); // 5ms: stabile per BLE e watchdog
}

// ── Parsing comandi BLE ───────────────────────────────────────
void eseguiComando(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.println("CMD: " + cmd);

  // ── Storage commands ──────────────────────────────────────
  if (cmd == "MACRO_LIST") {
    inviaFileBLE("MACRO", FILE_MACRO); return;
  }
  if (cmd == "CRED_LIST") {
    inviaFileBLE("CRED", FILE_CRED); return;
  }
  if (cmd.startsWith("MACRO_SAVE:")) {
    salvaMacroCustom(cmd.substring(11)); return;
  }
  if (cmd.startsWith("MACRO_DEL:")) {
    eliminaMacroCustom(cmd.substring(10)); return;
  }
  if (cmd.startsWith("CRED_SAVE:")) {
    salvaCredenziale(cmd.substring(10)); return;
  }
  if (cmd.startsWith("CRED_DEL:")) {
    eliminaCredenziale(cmd.substring(9)); return;
  }
  if (cmd.startsWith("CRED_USE:")) {
    eseguiCredenziale(cmd.substring(9)); return;
  }

  // ── HID commands ──────────────────────────────────────────
  if (cmd == "PING") { inviaBLE("PONG\n"); return; }
  if (cmd == "RESET") { resetTutto(); inviaBLE("OK:RESET\n"); return; }
  if (cmd == "HOLD_ON")  { holdActive = true;  inviaBLE("HOLD:ON\n");  return; }
  if (cmd == "HOLD_OFF") { holdActive = false;
    Keyboard.releaseAll(); inviaBLE("HOLD:OFF\n"); return; }
  if (cmd == "CAPS_TOGGLE") {
    capsLockActive = !capsLockActive;
    inviaBLE("CAPS:" + String(capsLockActive?"ON":"OFF") + "\n"); return; }

  if (cmd.startsWith("MODE:"))   { setModoInvio(cmd.substring(5)); return; }
  if (cmd.startsWith("LAYOUT:")) { setLayout(cmd.substring(7));    return; }

  if (cmd.startsWith("T:")) {
    inviaTesto(cmd.substring(2));
    inviaBLE("OK:T\n"); return;
  }
  if (cmd.startsWith("K:")) {
    eseguiCombinazione(cmd.substring(2));
    inviaBLE("OK:K\n"); return;
  }
  if (cmd.startsWith("M:")) {
    int v = cmd.indexOf(',', 2);
    if (v > 2) {
      float rx = cmd.substring(2, v).toInt();
      float ry = cmd.substring(v + 1).toInt();
      mouseSmoothX = mouseSmoothX * SMOOTH + rx * (1 - SMOOTH);
      mouseSmoothY = mouseSmoothY * SMOOTH + ry * (1 - SMOOTH);
      int fx = round(mouseSmoothX), fy = round(mouseSmoothY);
      if (fx != 0 || fy != 0) {
        Mouse.move(fx, fy);
        mouseSmoothX = mouseSmoothY = 0;
      }
    }
    return; // Nessun ACK per M: — troppo frequente
  }
  if (cmd.startsWith("SC:")) {
    Mouse.move(0, 0, -cmd.substring(3).toInt()); return;
  }
  if (cmd.startsWith("C:")) {
    char tipo = cmd.charAt(2);
    String act = (cmd.length() > 4) ? cmd.substring(4) : "";
    char btn = (tipo == 'L') ? MOUSE_LEFT : MOUSE_RIGHT;
    if      (act == "down") Mouse.press(btn);
    else if (act == "up")   Mouse.release(btn);
    else                    Mouse.click(btn);
    inviaBLE("OK:C\n"); return;
  }
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
  if (cmd.startsWith("MACRO:")) {
    eseguiMacro(cmd.substring(6));
    inviaBLE("OK:MACRO\n"); return;
  }
  if (cmd.startsWith("MACRO_RUN:")) {
    // Esegue una macro custom per nome leggendo da LittleFS
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

// ── Combinazioni tasti ────────────────────────────────────────
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
  if      (finale=="ENTER"||finale=="RETURN")  kc=KEY_RETURN;
  else if (finale=="TAB")                       kc=KEY_TAB;
  else if (finale=="ESC"||finale=="ESCAPE")     kc=KEY_ESC;
  else if (finale=="DEL"||finale=="DELETE")     kc=KEY_DELETE;
  else if (finale=="BS"||finale=="BACKSPACE")   kc=KEY_BACKSPACE;
  else if (finale=="SPACE")                     kc=' ';
  else if (finale=="UP")                        kc=KEY_UP_ARROW;
  else if (finale=="DOWN")                      kc=KEY_DOWN_ARROW;
  else if (finale=="LEFT")                      kc=KEY_LEFT_ARROW;
  else if (finale=="RIGHT")                     kc=KEY_RIGHT_ARROW;
  else if (finale=="HOME")                      kc=KEY_HOME;
  else if (finale=="END")                       kc=KEY_END;
  else if (finale=="PGUP")                      kc=KEY_PAGE_UP;
  else if (finale=="PGDN")                      kc=KEY_PAGE_DOWN;
  else if (finale=="INSERT")                    kc=KEY_INSERT;
  else if (finale=="PRTSC")                     kc=KEY_PRINT_SCREEN;
  else if (finale=="F1")  kc=KEY_F1;  else if (finale=="F2")  kc=KEY_F2;
  else if (finale=="F3")  kc=KEY_F3;  else if (finale=="F4")  kc=KEY_F4;
  else if (finale=="F5")  kc=KEY_F5;  else if (finale=="F6")  kc=KEY_F6;
  else if (finale=="F7")  kc=KEY_F7;  else if (finale=="F8")  kc=KEY_F8;
  else if (finale=="F9")  kc=KEY_F9;  else if (finale=="F10") kc=KEY_F10;
  else if (finale=="F11") kc=KEY_F11; else if (finale=="F12") kc=KEY_F12;
  else if (finale.length()==1) {
    char ch = finale.charAt(0);
    if      (ch>='A'&&ch<='Z') kc = ch + 32;
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
  Keyboard.releaseAll();
  delay(30);
  // Ripristina hold attivi dopo releaseAll
  if (holdModCtrl)  Keyboard.press(KEY_LEFT_CTRL);
  if (holdModAlt)   Keyboard.press(KEY_LEFT_ALT);
  if (holdModShift) Keyboard.press(KEY_LEFT_SHIFT);
  if (holdModWin)   Keyboard.press(KEY_LEFT_GUI);
}

// ── Sequenze con [TOKEN] ──────────────────────────────────────
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
  for (int i = 0; i < (int)NUM_MACRO; i++) {
    if (String(macroList[i].id) == id) {
      eseguiSequenza(String(macroList[i].sequenza)); return;
    }
  }
  inviaBLE("ERR:MACRO_NOT_FOUND\n");
}

// ── Invio testo con gestione accenti ─────────────────────────
void inviaTesto(String testo) {
  int i = 0;
  while (i < (int)testo.length()) {
    // Cerca [ALT+XXXX] nel testo
    if (testo.charAt(i) == '[') {
      int fine = testo.indexOf(']', i);
      if (fine > i) {
        String token = testo.substring(i + 1, fine); token.trim();
        if (token.startsWith("ALT+")) inviaSequenzaALT(token);
        else eseguiCombinazione(token);
        i = fine + 1;
        continue;
      }
    }
    // Carattere normale
    Keyboard.print(String(testo.charAt(i)));
    i++;
  }
}

// ── Sequenza ALT numerica (Numpad) ────────────────────────────
// FIX v5.15: release(kp) invece di releaseAll()
// FIX v5.16: mantiene lo zero iniziale (non usa toInt())
void inviaSequenzaALT(String seq) {
  seq.replace("ALT+", "");
  seq.trim();
  Serial.print("ALT+"); Serial.println(seq);

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
    if (kp) {
      Keyboard.press(kp);
      delay(20);
      Keyboard.release(kp); // Solo kp — ALT rimane premuto!
      delay(10);
    }
  }

  delay(30);
  Keyboard.release(KEY_LEFT_ALT);
  delay(30);
}

// ── BLE invia ─────────────────────────────────────────────────
void inviaBLE(String msg) {
  if (!deviceConnected) return;
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

// ── Layout e modalità ─────────────────────────────────────────
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
