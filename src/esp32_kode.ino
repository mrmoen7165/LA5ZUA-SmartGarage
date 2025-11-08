/************************************************************
 *  BalleGarage_Final_FULL.ino
 *  ESP32 port- og lysstyring med webgrensesnitt
 *  © Balle’s Man Cave – komplett versjon
 ************************************************************/

#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <DHT.h>

/*************** WiFi og NTP ***************/
const char* WIFI_SSID      = "Moen";
const char* WIFI_PASSWORD  = "93285393";
const char* TZ_EUROPE_OSLO = "CET-1CEST,M3.5.0,M10.5.0/3";
const char* NTP_SERVER_1   = "pool.ntp.org";
const char* NTP_SERVER_2   = "time.nist.gov";

/*************** Pinner ***************/
#define RELAY_OPEN_PIN     26
#define RELAY_CLOSE_PIN    27
#define RELAY_LIGHT_PIN    33
#define RELAY_ACTIVE_LOW   true

#define LM393_PIN          19
#define LM393_LOW_IS_DARK  true

#define DHT_PIN            21
#define DHT_TYPE           DHT22
DHT dht(DHT_PIN, DHT_TYPE);

#define CLIPPER_PIN        17
#define SOIL_PIN           32

/*************** Tilstander ***************/
enum GateState : uint8_t { OPENED, CLOSED, OPENING, CLOSING, WAITING_CLOSE, UNKNOWN };
volatile GateState gateState = CLOSED;
enum GateMode  : uint8_t { GATE_AUTO=0, GATE_MANUAL=1 };
volatile GateMode  gateMode  = GATE_AUTO;
enum LightMode : uint8_t { LIGHT_AUTO=0, LIGHT_FORCE_ON=1, LIGHT_FORCE_OFF=2 };
volatile LightMode lightMode = LIGHT_AUTO;

/*************** Konstanter ***************/
#define OPEN_RUN_TIME       7000
#define CLOSE_RUN_TIME      7000
#define DIR_DEAD_TIME        200

/*************** Lysstyring ***************/
const unsigned long LIGHT_DEBOUNCE_MS      = 2000;
const unsigned long LIGHT_MIN_INTERVAL_MS  = 15000;
bool lightOn = false;
int  lastLmRaw = -1;
unsigned long lastLmChange = 0;
unsigned long lastLightSwitch = 0;

/*************** Sensorverdier ***************/
unsigned long nextDht  = 0;
float lastTempC = NAN, lastHum = NAN;
unsigned long nextSoil = 0;
int lastSoilRaw = -1, lastSoilPct = -1;
int SOIL_DRY = 4095;
int SOIL_WET = 1800;

/*************** Portkontroll ***************/
unsigned long actionStart = 0;

/*************** Webserver ***************/
WebServer server(80);
unsigned long bootMillis;

/*************** Hjelpefunksjoner ***************/
inline void setRelayRaw(uint8_t pin, bool on) {
  // Standard: port-releer er aktiv LOW
  bool activeLow = true;

  // Lysreleet er motsatt logikk – aktiv HIGH
  if (pin == RELAY_LIGHT_PIN) activeLow = false;

  digitalWrite(pin, activeLow ? (on ? LOW : HIGH)
                              : (on ? HIGH : LOW));
}

void allStop(){
  setRelayRaw(RELAY_OPEN_PIN,false);
  setRelayRaw(RELAY_CLOSE_PIN,false);
}

void runOpen(){
  setRelayRaw(RELAY_CLOSE_PIN,false);
  delay(DIR_DEAD_TIME);
  setRelayRaw(RELAY_OPEN_PIN,true);
}

void runClose(){
  setRelayRaw(RELAY_OPEN_PIN,false);
  delay(DIR_DEAD_TIME);
  setRelayRaw(RELAY_CLOSE_PIN,true);
}

/*************** WiFi og tid ***************/
void connectWiFi(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi: kobler til");
  unsigned long start = millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-start<20000){
    Serial.print(".");
    delay(300);
  }
  if(WiFi.status()==WL_CONNECTED)
    Serial.printf(" OK  IP: %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println(" FEIL (fortsetter offline)");
}

void initTime(){
  setenv("TZ", TZ_EUROPE_OSLO, 1);
  tzset();
  configTime(0,0,NTP_SERVER_1,NTP_SERVER_2);
}

String fmtTime(){
  time_t now=time(nullptr);
  if(now<1000) return "-";
  struct tm t; localtime_r(&now,&t);
  char buf[32]; strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",&t);
  return String(buf);
}

/*************** Portstyring ***************/
void startOpen(){
  Serial.println("PORT: Åpner...");
  allStop();
  runOpen();
  gateState = OPENING;
  actionStart = millis();
}

void startClose(){
  Serial.println("PORT: Lukker...");
  allStop();
  runClose();
  gateState = CLOSING;
  actionStart = millis();
}

/*************** Sensoroppdatering ***************/
void updateLightFromLm393() {
  int raw = digitalRead(LM393_PIN);
  bool dark;

  // Tolk signalet konsekvent: LOW = mørkt
  dark = (raw == LOW);

  unsigned long now = millis();

  // Oppdater bare i AUTO-modus
  if (lightMode == LIGHT_AUTO) {
    if (lastLmRaw == -1) {
      lastLmRaw = raw;
      lastLmChange = now;
      lightOn = dark; // mørkt = slå på
      setRelayRaw(RELAY_LIGHT_PIN, lightOn);
    }

    if (raw != lastLmRaw) {
      lastLmRaw = raw;
      lastLmChange = now;
    }

    if (now - lastLmChange >= LIGHT_DEBOUNCE_MS &&
        now - lastLightSwitch >= LIGHT_MIN_INTERVAL_MS) {

      bool want = dark; // mørkt = slå på
      if (want != lightOn) {
        lightOn = want;
        setRelayRaw(RELAY_LIGHT_PIN, lightOn);
        lastLightSwitch = now;
        Serial.println(lightOn ? "AUTO: Utelys PÅ" : "AUTO: Utelys AV");
      }
    }
  }
}



/*************** Trygg DHT-lesing ***************/
void updateDht() {
  static unsigned long lastDhtRead = 0;
  const unsigned long DHT_INTERVAL = 3000; // hvert 3. sekund
  if (millis() - lastDhtRead < DHT_INTERVAL) return;
  lastDhtRead = millis();

  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println("⚠️ DHT-feil hopper over lesing");
    return;  // unngå crash ved dårlig lesing
  }

  // Oppdater bare hvis verdiene faktisk er gyldige
  lastHum = h;
  lastTempC = t;

  Serial.printf("🌡️ DHT OK: %.1f°C / %.0f%%\n", t, h);
}


int soilPercentFromRaw(int raw){
  float p=100.0*((float)(SOIL_DRY-raw)/(SOIL_DRY-SOIL_WET));
  if(p<0)p=0; if(p>100)p=100; return (int)(p+0.5);
}

void updateSoil(){
  if(millis()<nextSoil) return;
  nextSoil=millis()+10000;
  long sum=0; for(int i=0;i<16;i++){ sum+=analogRead(SOIL_PIN); delay(2); }
  int raw=sum/16;
  lastSoilRaw=raw; lastSoilPct=soilPercentFromRaw(raw);
}

/****************** Web UI **********************/
String indexHtml(){
  return R"HTML(
<!doctype html><html><head>
<meta charset="utf-8"/><meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Balle’s Man Cave — Control Center</title>
<style>
:root{
  --bg:#0c1210; --panel:#111a16; --ink:#e7efe9;
  --grid:#26372f; --line:#1a2923;
  --green:#22c55e; --green-d:#15803d;
  --blue:#2563eb;  --blue-d:#1e40af;
  --red:#dc2626;   --red-d:#991b1b;
  --amber:#f59e0b; --amber-d:#b45309;
  --chip:#183a28;  --chip-b:#2c8a44;
}
*{box-sizing:border-box}
body{margin:0;background:#0c1210;color:var(--ink);font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu,Arial}
h1{max-width:1180px;margin:20px auto 8px;padding:0 12px}
.wrap{max-width:1180px;margin:0 auto;padding:0 12px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:14px;box-shadow:0 1px 0 #0a100e,0 0 24px #09120f inset;padding:14px;margin:12px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}
.kv{display:flex;justify-content:space-between;border-bottom:1px dashed var(--grid);padding:6px 0}
.pill{padding:2px 10px;border-radius:999px;border:1px solid #2b3d34;background:#0f1a16}
.pill.good{background:#0e2817;border-color:#2e7a42}
.pill.bad{background:#281313;border-color:#7a2e2e}
.btns{display:flex;gap:8px;flex-wrap:wrap}
.btn{padding:10px 14px;border-radius:10px;text-decoration:none;color:#fff;display:inline-block;border:1px solid transparent;transition:filter .12s,transform .02s}
.btn:active{transform:translateY(1px)}
/* handling-knapper */
.btn-open{background:var(--green);border-color:var(--green-d)}
.btn-close{background:var(--blue); border-color:var(--blue-d)}
.btn-stop{background:var(--red);  border-color:var(--red-d)}
/* dynamiske tilstander: gul=progress, grønn=done */
.state-progress{background:var(--amber)!important;border-color:var(--amber-d)!important;color:#000!important}
.state-done{background:var(--green)!important;border-color:var(--green-d)!important}

/* ===== Mer synlige chip-knapper (modus) ===== */
.btn-chip{
  --bd:#34d399; --glow:#22c55e;
  color:#eafff2; background:linear-gradient(#113322,#0c2419);
  border:2px solid var(--bd); border-radius:999px;
  padding:10px 16px; font-weight:700; letter-spacing:.2px;
  box-shadow:0 0 0 1px #07140d inset, 0 1px 0 #0008, 0 0 12px rgba(34,197,94,.25);
  text-decoration:none; transition:filter .12s, transform .06s, box-shadow .12s;
}
.btn-chip:hover{ filter:brightness(1.15); transform:translateY(-1px); }
.btn-chip.active{
  background:linear-gradient(#0f3a22,#0d2f1d);
  border-color:#4ade80;
  box-shadow:0 0 0 2px rgba(255,255,255,.08) inset, 0 0 18px var(--glow);
}
/* ============================================ */

.layout{display:grid;grid-template-columns:1fr 260px;gap:14px}
.ledpanel{background:#0f1513;border:1px solid var(--line);border-radius:12px;padding:10px}
.ledrow{display:flex;align-items:center;justify-content:space-between;border-bottom:1px dashed var(--grid);padding:7px 4px}
.led{width:14px;height:14px;border-radius:50%;box-shadow:0 0 0 1px #000 inset, 0 0 12px #000}
.led.g{background:#32f28a;box-shadow:0 0 0 1px #124 inset,0 0 12px #32f28a}
.led.r{background:#ff5252;box-shadow:0 0 0 1px #411 inset,0 0 12px #ff5252}
.led.y{background:#ffd45a;box-shadow:0 0 0 1px #442 inset,0 0 12px #ffd45a}
.led.o{opacity:.22}
@media(max-width:1100px){ .layout{grid-template-columns:1fr} }
</style>
</head>
<body>
  <h1>Balle’s Man Cave — Control Center</h1>
  <div class="wrap">
    <div class="card">
      <div class="grid">
        <div class="kv"><b>Tid</b><span id="time">-</span></div>
        <div class="kv"><b>Wi-Fi</b><span id="wifi" class="pill">-</span></div>
        <div class="kv"><b>IP</b><span id="ip">-</span></div>
        <div class="kv"><b>RSSI</b><span id="rssi">-</span></div>
        <div class="kv"><b>Uptime</b><span id="uptime">-</span></div>
      </div>
    </div>

    <div class="layout">
      <div>
        <div class="card">
          <div class="grid">
            <div class="kv"><b>Port</b><span id="gate" class="pill">-</span></div>
            <div class="kv"><b>Balle gjær d du itj gidd</b><span id="out" class="pill">—</span></div>
            <div class="kv"><b>Balle kvile sæ (18)</b><span id="in"  class="pill">—</span></div>
            <div class="kv"><b>Utelys</b><span id="light" class="pill">-</span></div>
            <div class="kv"><b>LM393 lys føler</b><span id="lmraw" class="pill">-</span></div>
          </div>

          <div style="height:10px"></div>

          <div class="btns">
            <div class="btns" style="margin-right:16px">
              <b style="align-self:center">Port-modus:</b>
              <a id="btnGateAuto"   class="btn-chip" href="/action/gate?mode=auto">Auto</a>
              <a id="btnGateManual" class="btn-chip" href="/action/gate?mode=manual">Manuell</a>
            </div>

            <a id="btnOpen"  class="btn btn-open"  href="/action/open">Åpne</a>
            <a id="btnClose" class="btn btn-close" href="/action/close">Lukk</a>
            <a               class="btn btn-stop"  href="/action/stop">Stopp</a>

            <div style="width:18px"></div>

            <div class="btns">
              <b style="align-self:center">Lys-modus:</b>
            <a id="btnLightAuto" class="btn-chip" href="/action/light?mode=auto">Auto</a>
            <a id="btnLightOn"   class="btn-chip" href="/action/light?mode=off">På</a>
            <a id="btnLightOff"  class="btn-chip" href="/action/light?mode=on">Av</a>

            </div>
          </div>
        </div>

        <div class="card">
          <div class="grid">
            <div class="kv"><b>Tempratur måler DHT22</b><span id="temp" class="pill">-</span></div>
            <div class="kv"><b>Luftfukt</b><span id="hum" class="pill">-</span></div>
            <div class="kv"><b>Jordfukt Måler</b><span id="soilraw" class="pill">-</span></div>
            <div class="kv"><b>Jordfuktighet (%)</b><span id="soilpct" class="pill">-</span></div>
          </div>
        </div>
      </div>

      <!-- LED-panel til høyre -->
      <div class="ledpanel">
        <div class="ledrow"><span>Strøm på enhet</span><span class="led g" id="ledPower"></span></div>
        <div class="ledrow"><span>Port Åpen</span>  <span class="led o" id="ledOpen"></span></div>
        <div class="ledrow"><span>Port Stengt</span><span class="led o" id="ledClosed"></span></div>
        <div class="ledrow"><span>Klipper Ute</span><span class="led o" id="ledOut"></span></div>
        <div class="ledrow"><span>Klipper Inne</span><span class="led o" id="ledIn"></span></div>
        <div class="ledrow"><span>Utelys AV</span> <span class="led o" id="ledLightOff"></span></div>
        <div class="ledrow"><span>Utelys PÅ</span> <span class="led o" id="ledLightOn"></span></div>
        <div class="ledrow"><span>Div ERROR</span> <span class="led o" id="ledErr"></span></div>
      </div>
    </div>
  </div>

<script>
const E=id=>document.getElementById(id);
const S=(id,v)=>E(id).innerText=v;
function setActive(id,on){ const el=E(id); if(!el) return; el.classList.toggle('active',!!on); }
function pill(el,good){ el.classList.remove('good','bad'); if(good===true) el.classList.add('good'); if(good===false) el.classList.add('bad'); }
function setLED(id,color){ E(id).className='led '+(color||'o'); }

/* Gi Åpne/Lukk-knappene gul/grønn basert på state */
function reflectGateButtons(stateTxt){
  const opening=/ÅPNER/i.test(stateTxt);
  const closing=/LUKKER/i.test(stateTxt);
  const waiting=/VENTER/i.test(stateTxt);
  const openOk=/^ÅPEN$/i.test(stateTxt);
  const closedOk=/^STENGT$/i.test(stateTxt);
  const bOpen=E('btnOpen'), bClose=E('btnClose');

  [bOpen,bClose].forEach(b=>{ b.classList.remove('state-progress','state-done'); });

  if (opening){ bOpen.classList.add('state-progress'); }
  else if (openOk){ bOpen.classList.add('state-done'); }

  if (closing || waiting){ bClose.classList.add('state-progress'); }
  else if (closedOk){ bClose.classList.add('state-done'); }
}

function reflectGateState(txt){
  const isOpen   = /^ÅPEN$/i.test(txt);
  const isClosed = /^STENGT$/i.test(txt);
  const isWait   = /VENTER/i.test(txt);
  const isUnk    = /UKJENT/i.test(txt);

  const g=E('gate'); g.innerText=txt; pill(g, isOpen?true:isClosed?false:undefined);
  reflectGateButtons(txt);

  setLED('ledOpen',   isOpen   ? 'g' : isWait ? 'y' : 'o');
  setLED('ledClosed', isClosed ? 'g' : 'o');
  setLED('ledErr',    isUnk    ? 'r' : 'o');
}

async function refresh(){
  try{
    const j = await (await fetch('/api/status')).json();

    S('time', j.time);
    S('ip', j.ip || '-'); S('rssi', j.rssi + ' dBm'); S('uptime', j.uptime);

    const wifi=E('wifi'); wifi.innerText = j.wifi_connected ? 'Tilkoblet' : 'Frakoblet';
    pill(wifi, j.wifi_connected); setLED('ledPower','g');

    reflectGateState(j.gate_state);

    const out=E('out'), inn=E('in');
    out.innerText = j.clipper_out ? 'AKTIV' : '—';
    inn.innerText = j.clipper_in  ? 'AKTIV' : '—';
    pill(out, j.clipper_out); pill(inn, j.clipper_in);
    setLED('ledOut', j.clipper_out ? 'g' : 'o');
    setLED('ledIn',  j.clipper_in  ? 'g' : 'o');

    // inverter visningen slik at nettsiden samsvarer med faktisk rele-status
    const lightOn = !j.light_on;
    const L = E('light'); L.innerText = lightOn ? 'PÅ' : 'AV';
    pill(L, lightOn);
    setLED('ledLightOn',  lightOn ? 'g' : 'o');
    setLED('ledLightOff', lightOn ? 'o' : 'g');
    
    S('lmraw', j.lm393_raw);
    S('temp', j.temp_c!==null ? j.temp_c.toFixed(1)+' °C':'-');
    S('hum',  j.hum!==null    ? Math.round(j.hum)+' %':'-');
    S('soilraw', j.soil_raw);
    S('soilpct', j.soil_pct+' %');

    setActive('btnGateAuto',   j.gate_mode===0);
    setActive('btnGateManual', j.gate_mode===1);

    // 🔄 inverter logikken for lys-knappene
    setActive('btnLightAuto',  j.light_mode===0);
    setActive('btnLightOn',    j.light_mode===2);
    setActive('btnLightOff',   j.light_mode===1);

  }catch(e){}
}
refresh(); setInterval(refresh, 1000);
</script>
</body></html>
)HTML";
}

void handleRoot(){ server.send(200, "text/html; charset=utf-8", indexHtml()); }

/*************** API for status ***************/
void handleApiStatus(){
  bool clipperIn  = (digitalRead(CLIPPER_PIN) == LOW);
  bool clipperOut = !clipperIn;

  unsigned long uptimeSec = (millis() - bootMillis)/1000;
  int hrs = uptimeSec / 3600, mins = (uptimeSec % 3600)/60, secs = uptimeSec % 60;
  char uptimeStr[16]; sprintf(uptimeStr,"%02d:%02d:%02d",hrs,mins,secs);

  bool wifiOK = WiFi.isConnected();
  String ipStr = wifiOK ? WiFi.localIP().toString() : "-";
  int rssi = wifiOK ? WiFi.RSSI() : 0;

  String json="{";
  json += "\"time\":\""+fmtTime()+"\",";
  json += "\"wifi_connected\":" + String(wifiOK?"true":"false") + ",";
  json += "\"ip\":\""+ipStr+"\",";
  json += "\"rssi\":"+String(rssi)+",";
  json += "\"uptime\":\""+String(uptimeStr)+"\",";
  json += "\"gate_state\":\""+String(
     gateState==OPENED?"ÅPEN":gateState==CLOSED?"STENGT":
     gateState==OPENING?"ÅPNER":gateState==CLOSING?"LUKKER":
     gateState==WAITING_CLOSE?"VENTER PÅ LUKKING":"UKJENT")+"\",";
  json += "\"clipper_in\":"+String(clipperIn?"true":"false")+",";
  json += "\"clipper_out\":"+String(clipperOut?"true":"false")+",";
  json += "\"light_on\":"+String(lightOn?"true":"false")+",";
  json += "\"lm393_raw\":\""+String(lastLmRaw==LOW?"LOW":"HIGH")+"\",";
  json += "\"temp_c\":"+String(isnan(lastTempC)?"null":String(lastTempC,1))+",";
  json += "\"hum\":"+String(isnan(lastHum)?"null":String(lastHum,0))+",";
  json += "\"soil_raw\":"+String(lastSoilRaw<0?0:lastSoilRaw)+",";
  json += "\"soil_pct\":"+String(lastSoilPct<0?0:lastSoilPct)+",";
  json += "\"gate_mode\":"+String((int)gateMode)+",";
  json += "\"light_mode\":"+String((int)lightMode);
  json += "}";
  server.send(200,"application/json; charset=utf-8",json);
}

/*************** SETUP ***************/
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Starter Balle’s Man Cave ===");

  // --- Pin-konfig ---
  pinMode(RELAY_OPEN_PIN, OUTPUT);
  pinMode(RELAY_CLOSE_PIN, OUTPUT);
  pinMode(RELAY_LIGHT_PIN, OUTPUT);
  pinMode(LM393_PIN, INPUT);
  pinMode(CLIPPER_PIN, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(SOIL_PIN, ADC_11db);

  allStop();
  setRelayRaw(RELAY_LIGHT_PIN, false);

  // --- Start sensorer ---
  Serial.println("Starter DHT og sensorer...");
  dht.begin();

  // Gi sensorer tid til å stabilisere seg
  for (int i = 0; i < 10; i++) {
    delay(100);
    digitalRead(LM393_PIN);
  }
  Serial.println("Sensorer stabilisert.");

  // --- Start nettverk ---
  connectWiFi();
  initTime();
  bootMillis = millis();

  // --- Webserver-ruter ---
  server.on("/", []() { server.send(200, "text/html; charset=utf-8", indexHtml()); });
  server.on("/api/status", handleApiStatus);

  server.on("/action/open", []() {
    startOpen();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/action/close", []() {
    startClose();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/action/stop", []() {
    allStop();
    gateState = UNKNOWN;
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/action/gate", []() {
    if (server.hasArg("mode")) {
      String m = server.arg("mode");
      if (m == "auto") {
        gateMode = GATE_AUTO;
        Serial.println("Modus: PORT AUTO");
      }
      if (m == "manual") {
        gateMode = GATE_MANUAL;
        Serial.println("Modus: PORT MANUELL");
      }
    }
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/action/light", []() {
    if (server.hasArg("mode")) {
      String m = server.arg("mode");
      if (m == "auto") {
        lightMode = LIGHT_AUTO;
        Serial.println("Lysmodus: AUTO");
      }
      if (m == "on") {
        lightMode = LIGHT_FORCE_ON;
        setRelayRaw(RELAY_LIGHT_PIN, true);
        lightOn = true;
        Serial.println("Lysmodus: PÅ");
      }
      if (m == "off") {
        lightMode = LIGHT_FORCE_OFF;
        setRelayRaw(RELAY_LIGHT_PIN, false);
        lightOn = false;
        Serial.println("Lysmodus: AV");
      }
    }
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.begin();
  Serial.println("Webserver startet OK.\n");
}

/*************** LOOP ***************/
void loop() {
  server.handleClient();
  updateLightFromLm393();
  updateDht();
  updateSoil();

  // --- Manuell styring av lys ---
  if (lightMode == LIGHT_FORCE_ON) {
    if (!lightOn) {
      lightOn = true;
      setRelayRaw(RELAY_LIGHT_PIN, true);
      Serial.println("MANUELL: Utelys PÅ");
    }
  }
  else if (lightMode == LIGHT_FORCE_OFF) {
    if (lightOn) {
      lightOn = false;
      setRelayRaw(RELAY_LIGHT_PIN, false);
      Serial.println("MANUELL: Utelys AV");
    }
  }

  // --- Automatikk fra portbryter ---
  static bool lastStable = HIGH;
  static unsigned long lastChange = 0;
  const unsigned long debounceDelay = 50;
  bool reading = digitalRead(CLIPPER_PIN);

  if (reading != lastStable && millis() - lastChange > debounceDelay) {
    lastStable = reading;
    lastChange = millis();
    if (gateMode == GATE_AUTO) {
      bool pressed = (lastStable == LOW);
      if (pressed) {
        Serial.println("AUTO: Bryter trykket → lukker port");
        startClose();
      } else {
        Serial.println("AUTO: Bryter sluppet → åpner port");
        startOpen();
      }
    }
  }

  if (gateState == OPENING && millis() - actionStart > OPEN_RUN_TIME) {
    Serial.println("PORT: ferdig åpnet → stopp");
    allStop();
    gateState = OPENED;
  }
  if (gateState == CLOSING && millis() - actionStart > CLOSE_RUN_TIME) {
    Serial.println("PORT: ferdig lukket → stopp");
    allStop();
    gateState = CLOSED;
  }

  // --- Periodisk debug (hver 2 sek) ---
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 2000) {
    lastDebug = millis();
    Serial.printf("LM393: %s  |  Temp: %.1f°C  |  Fukt: %.0f%%  |  Lys: %s\n",
      digitalRead(LM393_PIN) == LOW ? "LOW (mørkt)" : "HIGH (lyst)",
      isnan(lastTempC) ? 0 : lastTempC,
      isnan(lastHum) ? 0 : lastHum,
      lightOn ? "PÅ" : "AV");
  }

  delay(50); // litt roligere loop
}
