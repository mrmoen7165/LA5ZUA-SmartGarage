# 🧠 LA5ZUA-SmartGarage – Tekniske notater

## Systemoversikt
ESP32 styrer tre releer:
1. Åpne-port (GPIO26)
2. Lukke-port (GPIO27)
3. Utelys (GPIO33)

Sensorer og innganger:
- LM393 lyssensor (GPIO19)
- DHT22 temperatur/fukt (GPIO21)
- Clipper-bryter (GPIO17)
- Soil sensor (GPIO32, analog)

## Relelogikk
- Aktiv LOW på alle releer (`RELAY_ACTIVE_LOW = true`)
- `DIR_DEAD_TIME = 200 ms` mellom retninger
- `CLOSE_DELAY_TIME = 15s` før automatisk lukking
- Relestyring håndtert via `setRelayRaw()` og `runOpen()/runClose()`

## Lysstyring
- LM393_LOW_IS_DARK = true → sensor gir LOW når det er mørkt
- Automatisk nattmodus fra 16:00–08:00
- Kan styres manuelt via webgrensesnitt

## Sensorer
- DHT22 bruker 3.3V og 10kΩ pull-up på data
- LM393 kjører 3.3V logikk, DO går direkte til ESP32
- Soil sensor kan leses analogt for fuktstatus (GPIO32)

## Feilsøking
- Hvis DHT gir "NaN" → sjekk at VCC = 3.3V
- Hvis lys ikke aktiveres → sjekk LM393-potmeter og logikknivå
- Hvis releer klikker tilfeldig → bekreft felles GND
- ESP32 rebooter? → strømforsyning må levere nok strøm (min. 2A)

## Planlagte utvidelser
- MQTT-integrasjon for Home Assistant
- Logging av temperatur og lysnivå til webserver
- Automatisk OTA-sjekk daglig kl. 03:00
- Ny webside med statusikoner og sanntidsoppdatering

---

📅 Sist oppdatert: 09.11.2025  
📖 Versjon: v1.1.8  
© 2025 LA5ZUA Tech DIY Series – SmartGarage