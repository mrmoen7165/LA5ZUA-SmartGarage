# 🔄 Firmware Oppdatering – LA5ZUA-SmartGarage

Denne mappen inneholder de nødvendige filene for **OTA (Over-The-Air)** oppdatering av ESP32-prosjektet.

ESP32-en sjekker automatisk disse filene på GitHub for å finne ny firmware.

---

## 📂 Filstruktur
firmware/
├── latest.bin ← selve firmware-filen (kompilert fra Arduino)
├── version.txt ← versjonsnummer for OTA-sjekk
└── README.md ← denne forklaringen


## ⚙️ Hvordan oppdatere firmware

### 1️⃣ Bygg ny `.bin`-fil i Arduino IDE
- Åpne `esp32_kode.ino`
- Velg: **Sketch → Export compiled Binary**
- Åpne prosjektmappen med: **Sketch → Show Sketch Folder**
- Finn filen `esp32_kode.ino.esp32.bin`  
- Kopiér den inn hit (`firmware/`) og gi den navnet:
latest.bin

yaml
Kopier kode

---

### 2️⃣ Oppdater `version.txt`
- Åpne `version.txt` i Notisblokk eller VS Code  
- Øk versjonsnummeret, f.eks.:
1.0.0 → 1.0.1


ESP32 sammenligner dette tallet med `currentVersion` i koden.
Hvis de er forskjellige, laster den automatisk ned den nye `latest.bin`.


### 3️⃣ Push til GitHub
Når `latest.bin` og `version.txt` er klare:

```bash
git add latest.bin version.txt
git commit -m "Oppdatert firmware v1.0.1"
git push
🌐 OTA URL-er
Disse URL-ene brukes i koden for å laste ned filene direkte:



const char* versionURL = "https://raw.githubusercontent.com/mrmoen7165/LA5ZUA-SmartGarage/main/firmware/version.txt";
const char* binURL     = "https://raw.githubusercontent.com/mrmoen7165/LA5ZUA-SmartGarage/main/firmware/latest.bin";
🧠 Tips
Hold version.txt og latest.bin synkronisert – samme versjon gjelder alltid.

Endre currentVersion i ESP-koden for å samsvare med den du laster opp.

Unngå mellomrom eller ekstra tekst i version.txt.

Du kan legge til en changelog.txt her hvis du vil dokumentere endringer per versjon.

📡 LA5ZUA Tech DIY Series – SmartGarage OTA

Denne README-fila gir deg alt du trenger i `firmware/`-mappa — ryddig og profesjonelt 👌  





