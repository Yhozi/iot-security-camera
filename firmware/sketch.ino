// ===== Virtuelna pametna sigurnosna kamera - FAZA 2 (v3) =====
// PIR pokret -> LED + buzzer + kamera se UKLJUCI + MQTT obaveštenje
// Dugme KAMERA (ili klizač u veb-interfejsu) -> uključuje/isključuje kameru kad god želiš
// Dugmad LEVO/DESNO (ili dugmad u veb-interfejsu) -> okreću servo motor

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
const char* TEMA_POKRET        = PREFIKS "motion";        // ESP32 -> svet: događaj pokreta
const char* TEMA_STATUS        = PREFIKS "status";        // ESP32 -> svet: online/offline
const char* TEMA_SERVO_SET     = PREFIKS "servo/set";     // svet -> ESP32: levo/desno
const char* TEMA_SERVO_STANJE  = PREFIKS "servo/state";   // ESP32 -> svet: trenutni ugao
const char* TEMA_KAMERA_SET    = PREFIKS "camera/set";    // svet -> ESP32: on/off/toggle
const char* TEMA_KAMERA_STANJE = PREFIKS "camera/state";  // ESP32 -> svet: on/off

// ---- Konfiguracija pinova ----
const int PIN_PIR        = 13;  // izlaz PIR senzora
const int PIN_SERVO      = 18;  // PWM signal za servo
const int PIN_LED        = 26;  // LED dioda (preko otpornika 220 oma)
const int PIN_BUZZER     = 27;  // buzzer
const int PIN_BTN_LEVO   = 32;  // lokalno dugme LEVO
const int PIN_BTN_DESNO  = 33;  // lokalno dugme DESNO
const int PIN_BTN_KAMERA = 14;  // lokalno dugme KAMERA (uključi/isključi)

// ---- Podešavanja servo motora ----
const int UGAO_START = 90;
const int UGAO_MIN   = 0;
const int UGAO_MAX   = 180;
const int KORAK      = 15;     // stepeni po jednoj komandi

const unsigned long PAUZA_PONOVNOG_POVEZIVANJA = 1000; // posle prekida veze pokušaj ponovo za 1 s

WiFiClient   wifiKlijent;          // TCP veza ka internetu
PubSubClient mqtt(wifiKlijent);    // MQTT klijent koji koristi tu vezu
Servo kameraServo;

String idKlijenta;                 // ID se pravi JEDNOM pri pokretanju (isti ID pri ponovnom povezivanju)
int ugao = UGAO_START;
bool kameraUkljucena = false;      // da li je (virtuelna) kamera uključena
bool prethodnoStanje = false;
bool prethodnoLevo = false;
bool prethodnoDesno = false;
bool prethodnoKamera = false;
bool bioPovezan = false;           // da li je MQTT veza bila uspostavljena (za dijagnostiku)
unsigned long brojDogadjaja = 0;
unsigned long poslednjiPokusaj = 0;

// Pošalji trenutni ugao kao "retained" poruku: ko se naknadno pretplati odmah dobije poslednji ugao.
void posaljiUgao() {
  if (!mqtt.connected()) return;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", ugao);
  mqtt.publish(TEMA_SERVO_STANJE, buf, true);
}

// Pošalji stanje kamere (retained), da veb-interfejs uvek zna da li je kamera uključena.
void posaljiStanjeKamere() {
  if (!mqtt.connected()) return;
  mqtt.publish(TEMA_KAMERA_STANJE, kameraUkljucena ? "on" : "off", true);
}

// Uključi/isključi kameru. Poziva se iz dugmeta, iz MQTT komande i pri detekciji pokreta.
void postaviKameru(bool ukljuci) {
  kameraUkljucena = ukljuci;
  Serial.printf("KAMERA: %s\n", ukljuci ? "uključena" : "isključena");
  posaljiStanjeKamere();   // uvek javi novo stanje (i kad se nije promenilo, kao potvrdu komande)
}

// Pomeri servo za delta stepeni (negativno = levo, pozitivno = desno).
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

  if (strcmp(tema, TEMA_SERVO_SET) == 0) {            // komande za servo
    if (poruka == "levo")       pomeriServo(-KORAK);
    else if (poruka == "desno") pomeriServo(+KORAK);
    else Serial.println("Nepoznata komanda (ocekujem: levo / desno)");

  } else if (strcmp(tema, TEMA_KAMERA_SET) == 0) {    // komande za kameru
    if (poruka == "on")          postaviKameru(true);
    else if (poruka == "off")    postaviKameru(false);
    else if (poruka == "toggle") postaviKameru(!kameraUkljucena);
    else Serial.println("Nepoznata komanda (ocekujem: on / off / toggle)");
  }
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
  Serial.printf("MQTT: povezivanje (ID %s)... ", idKlijenta.c_str());
  if (mqtt.connect(idKlijenta.c_str(), nullptr, nullptr, TEMA_STATUS, 0, true, "offline")) {
    Serial.println("povezan");
    bioPovezan = true;
    mqtt.publish(TEMA_STATUS, "online", true);   // javi da smo online
    bool ok1 = mqtt.subscribe(TEMA_SERVO_SET);   // slušaj komande za servo
    bool ok2 = mqtt.subscribe(TEMA_KAMERA_SET);  // slušaj komande za kameru
    Serial.printf("MQTT: pretplate -> %s\n", (ok1 && ok2) ? "OK" : "NEUSPESNO");
    posaljiUgao();
    posaljiStanjeKamere();
  } else {
    Serial.printf("greska (rc=%d), pokusavam ponovo\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BTN_LEVO, INPUT_PULLUP);     // dugme spojeno na GND: pritisnuto = LOW
  pinMode(PIN_BTN_DESNO, INPUT_PULLUP);
  pinMode(PIN_BTN_KAMERA, INPUT_PULLUP);

  kameraServo.attach(PIN_SERVO, 500, 2400);
  kameraServo.write(ugao);

  // ID mora biti jedinstven (dva klijenta sa istim ID-jem izbacuju jedan drugog).
  // Pravi se jednom: pri ponovnom povezivanju broker prepoznaje isti uređaj i redosled poruka je ispravan.
  idKlijenta = "marko-cam-" + String(random(0xffff), HEX) + String(micros() & 0xfff, HEX);

  spojiWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setKeepAlive(30);
  mqtt.setCallback(naPoruku);
  spojiMQTT();

  Serial.println("Sistem spreman. Cekam pokret...");
}

void loop() {
  // ---------- 0) Održavanje veze ----------
  if (!mqtt.connected()) {
    if (bioPovezan) {            // veza je upravo pukla (-4 = timeout, -3 = veza izgubljena)
      Serial.printf("MQTT: veza prekinuta, rc=%d\n", mqtt.state());
      bioPovezan = false;
      poslednjiPokusaj = 0;      // pokušaj odmah, bez čekanja
    }
    if (millis() - poslednjiPokusaj > PAUZA_PONOVNOG_POVEZIVANJA) {
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
    postaviKameru(true);                 // pokret uključuje kameru (isključuje je korisnik)

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

  // ---------- 2) Lokalna dugmad ----------
  bool levo   = (digitalRead(PIN_BTN_LEVO)   == LOW);  // LOW = pritisnuto
  bool desno  = (digitalRead(PIN_BTN_DESNO)  == LOW);
  bool kamera = (digitalRead(PIN_BTN_KAMERA) == LOW);

  if (levo   && !prethodnoLevo)   pomeriServo(-KORAK);          // reaguj samo na trenutak pritiska
  if (desno  && !prethodnoDesno)  pomeriServo(+KORAK);
  if (kamera && !prethodnoKamera) postaviKameru(!kameraUkljucena); // svaki pritisak menja stanje

  prethodnoLevo   = levo;
  prethodnoDesno  = desno;
  prethodnoKamera = kamera;

  delay(20);
}
