# Virtuelna pametna sigurnosna kamera

IoT prototip: PIR senzor detektuje pokret, ESP32 reaguje (LED, buzzer, servo), komunikacija ide preko MQTT-a, a veb-interfejs prepoznaje objekte na slici (TensorFlow.js + COCO-SSD). Sve radi virtuelno: Wokwi simulator + javni MQTT broker + pregledač.

## Linkovi

- **Wokwi simulacija:** https://wokwi.com/projects/481344659910891521
- **Veb kontrole (GitHub Pages):** https://yhozi.github.io/iot-security-camera/

## Kako pokrenuti

1. Otvori Wokwi link i pokreni simulaciju (Play). Sačekaj da Serial ispiše `MQTT povezan`.
2. U drugom tabu otvori veb kontrole. Značke **Broker** i **ESP32** treba da budu zelene.
3. Klikni "Simulate motion" na PIR senzoru u Wokwi-ju ili koristi dugmad LEVO/DESNO i klizač kamere u veb-interfejsu.
4. Izaberi sliku i klikni "Prepoznaj objekte". Ako je na slici osoba, aktivira se alarm.

## Arhitektura

![Arhitektura](docs/arhitektura.png)

ESP32 i veb-interfejs ne komuniciraju direktno nego preko javnog MQTT brokera (`broker.hivemq.com`).

## Električna šema

![Šema](docs/sema.png)

| Komponenta | ESP32 pin |
|---|---|
| PIR senzor | GPIO 13 |
| Servo | GPIO 18 |
| LED (+ 220 Ω) | GPIO 26 |
| Buzzer | GPIO 27 |
| Dugme LEVO / DESNO / KAMERA | GPIO 32 / 33 / 14 |

## Struktura repozitorijuma

```
index.html        veb-interfejs (GitHub Pages)
firmware/         ESP32 program (sketch.ino) i Wokwi šema (diagram.json)
docs/             električna šema, arhitektura
README.md
```

## Tehnologije

ESP32 (Arduino C++), PubSubClient, ESP32Servo, MQTT (HiveMQ), MQTT.js, HTML/CSS/JavaScript, TensorFlow.js, COCO-SSD, Wokwi.

## Ograničenja

- Kamera je virtuelna: slika se bira ručno, nema live prenosa.
- Javni broker je samo za demonstraciju; teme imaju jedinstven prefiks, ali podaci nisu privatni.
- Na pravom uređaju (ESP32-CAM) menjaju se Wi-Fi podaci, broker i izvor slike, a MQTT teme i logika ostaju isti.

## Autor

Marko Popović
