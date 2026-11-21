// ===== Virtuelna pametna sigurnosna kamera - FAZA 2 (v2, sa dijagnostikom) =====
// Faza 1 + Wi-Fi + MQTT:
//  - pokret se šalje veb-interfejsu preko MQTT-a
//  - komande "levo"/"desno" iz interfejsa okreću servo

#include <WiFi.h>          // Wi-Fi veza
#include <PubSubClient.h>  // MQTT klijent
#include <ESP32Servo.h>    // servo biblioteka za ESP32

// ---- Wi-Fi (Wokwi ima gotovu mrežu bez lozinke) ----
const char* WIFI_SSID  = "Wokwi-GUEST";
const char* WIFI_PASS  = "";
const int   WIFI_KANAL = 6;

// ---- MQTT broker i teme ----
const char* MQTT_BROKER = "broker.hivemq.com"; // javni broker, bez naloga
const int   MQTT_PORT   = 1883;                // običan MQTT port (za ESP32)
#define PREFIKS "pmf-iot/marko-7k2x/"          // isti prefiks mora biti i u veb-interfejsu
const char* TEMA_POKRET       = PREFIKS "motion";       // ESP32 -> svet: događaj pokreta
const char* TEMA_STATUS       = PREFIKS "status";       // ESP32 -> svet: online/offline
const char* TEMA_SERVO_SET    = PREFIKS "servo/set";    // svet -> ESP32: komanda levo/desno
const char* TEMA_SERVO_STANJE = PREFIKS "servo/state";  // ESP32 -> svet: trenutni ugao

// ---- Konfiguracija pinova ----
const int PIN_PIR       = 13;  // izlaz PIR senzora
const int PIN_SERVO     = 18;  // PWM signal za servo
const int PIN_LED       = 26;  // LED dioda (preko otpornika 220 oma)
const int PIN_BUZZER    = 27;  // buzzer
const int PIN_BTN_LEVO  = 32;  // lokalno dugme LEVO (za testiranje)
const int PIN_BTN_DESNO = 33;  // lokalno dugme DESNO (za testiranje)

// ---- Podešavanja servo motora ----
const int UGAO_START = 90;
const int UGAO_MIN   = 0;
const int UGAO_MAX   = 180;
const int KORAK      = 15;     // stepeni po jednoj komandi

WiFiClient   wifiKlijent;          // TCP veza ka internetu
PubSubClient mqtt(wifiKlijent);    // MQTT klijent koji koristi tu vezu
Servo kameraServo;

int ugao = UGAO_START;
bool prethodnoStanje = false;
bool prethodnoLevo = false;
bool prethodnoDesno = false;
bool bioPovezan = false;             // da li je MQTT veza bila uspostavljena (za dijagnostiku)
unsigned long brojDogadjaja = 0;
unsigned long poslednjiPokusaj = 0;  // kada smo poslednji put pokušali MQTT vezu

// Pošalji trenutni ugao kao "retained" poruku: ko se naknadno pretplati odmah dobije poslednji ugao.
void posaljiUgao() {
  if (!mqtt.connected()) return;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", ugao);
  mqtt.publish(TEMA_SERVO_STANJE, buf, true);
}

// Pomeri servo za delta stepeni (negativno = levo, pozitivno = desno).
// Poziva se i iz lokalnog dugmeta i iz MQTT komande.
void pomeriServo(int delta) {
  ugao = constrain(ugao + delta, UGAO_MIN, UGAO_MAX);
  kameraServo.write(ugao);
  Serial.printf("Servo: %d stepeni\n", ugao);
  posaljiUgao();
}

// Kratak zvučni signal (bez tone(), da nema sukoba sa servo bibliotekom)
void beep(unsigned long trajanjeMs) {
  unsigned long kraj = millis() + trajanjeMs;
  while (millis() < kraj) {
    digitalWrite(PIN_BUZZER, HIGH);
    delayMicroseconds(250);
    digitalWrite(PIN_BUZZER, LOW);
    delayMicroseconds(250);
  }
}

// Poziva se automatski kad stigne poruka na temu na koju smo pretplaćeni
void naPoruku(char* tema, byte* payload, unsigned int duzina) {
  String poruka;
  for (unsigned int i = 0; i < duzina; i++) poruka += (char)payload[i];
  poruka.trim();
  poruka.toLowerCase();
  Serial.printf("MQTT primljeno [%s]: %s\n", tema, poruka.c_str());

  if (poruka == "levo")       pomeriServo(-KORAK);
  else if (poruka == "desno") pomeriServo(+KORAK);
  else Serial.println("Nepoznata komanda (ocekujem: levo / desno)");
}

void spojiWiFi() {
  Serial.print("Wi-Fi: povezivanje");
  WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_KANAL);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println(" OK, IP: " + WiFi.localIP().toString());
}

// Jedan pokušaj MQTT veze. Last Will: ako ESP32 nestane, broker sam objavi "offline".
void spojiMQTT() {
  // ID mora biti jedinstven: dva klijenta sa istim ID-jem izbacuju jedan drugog.
  // Kombinujemo slučajan broj i trenutno vreme da dve simulacije ne dobiju isti ID.
  String id = "marko-cam-" + String(random(0xffff), HEX) + String(micros() & 0xfff, HEX);
  Serial.printf("MQTT: povezivanje (ID %s)... ", id.c_str());

  if (mqtt.connect(id.c_str(), nullptr, nullptr, TEMA_STATUS, 0, true, "offline")) {
    Serial.println("povezan");
    bioPovezan = true;
    mqtt.publish(TEMA_STATUS, "online", true);   // javi da smo online
    bool ok = mqtt.subscribe(TEMA_SERVO_SET);    // slušaj komande za servo
    Serial.printf("MQTT: pretplata na %s -> %s\n", TEMA_SERVO_SET, ok ? "OK" : "NEUSPESNO");
    posaljiUgao();
  } else {
    Serial.printf("greska (rc=%d), pokusavam ponovo\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BTN_LEVO, INPUT_PULLUP);
  pinMode(PIN_BTN_DESNO, INPUT_PULLUP);

  kameraServo.attach(PIN_SERVO, 500, 2400);
  kameraServo.write(ugao);

  spojiWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setKeepAlive(30);        // šaljemo "ping" na svakih 30 s da broker zna da smo živi
  mqtt.setCallback(naPoruku);   // koja funkcija obrađuje dolazne poruke
  spojiMQTT();

  Serial.println("Sistem spreman. Cekam pokret...");
}

void loop() {
  // ---------- 0) Održavanje veze ----------
  if (!mqtt.connected()) {
    if (bioPovezan) {            // veza je upravo pukla: ispiši razlog (-4 = timeout, -3 = veza izgubljena)
      Serial.printf("MQTT: veza prekinuta, rc=%d\n", mqtt.state());
      bioPovezan = false;
    }
    if (millis() - poslednjiPokusaj > 5000) {   // pokušaj ponovo na svakih 5 s
      poslednjiPokusaj = millis();
      if (WiFi.status() != WL_CONNECTED) spojiWiFi();
      spojiMQTT();
    }
  } else {
    mqtt.loop();   // obrađuje dolazne poruke i održava vezu živom
  }

  // ---------- 1) PIR senzor ----------
  bool pokret = (digitalRead(PIN_PIR) == HIGH);

  if (pokret && !prethodnoStanje) {      // pokret POČEO
    brojDogadjaja++;
    Serial.printf("[%lu ms] POKRET detektovan (dogadjaj #%lu)\n", millis(), brojDogadjaja);
    digitalWrite(PIN_LED, HIGH);
    beep(300);

    char poruka[64];
    snprintf(poruka, sizeof(poruka), "{\"stanje\":\"pokret\",\"broj\":%lu}", brojDogadjaja);
    mqtt.publish(TEMA_POKRET, poruka);   // obaveštenje za veb-interfejs
    Serial.println("OBAVESTENJE: poslato preko MQTT-a.");
  }

  if (!pokret && prethodnoStanje) {      // pokret PRESTAO
    digitalWrite(PIN_LED, LOW);
    mqtt.publish(TEMA_POKRET, "{\"stanje\":\"mirno\"}");
    Serial.println("Pokret prestao.");
  }
  prethodnoStanje = pokret;

  // ---------- 2) Lokalna dugmad (za testiranje bez veb-interfejsa) ----------
  bool levo  = (digitalRead(PIN_BTN_LEVO)  == LOW);
  bool desno = (digitalRead(PIN_BTN_DESNO) == LOW);
  if (levo  && !prethodnoLevo)  pomeriServo(-KORAK);
  if (desno && !prethodnoDesno) pomeriServo(+KORAK);
  prethodnoLevo  = levo;
  prethodnoDesno = desno;

  delay(20);
}