/*
 * PROYECTO: Sistema de Alerta LoRa (433 MHz) con ESP32 + Telegram
 * INTEGRANTES: Jacobo Ospina, Juan Pablo Gonzales, Miguel Ángel Montoya
 * MÓDULO: Nodo Receptor Central (Con WiFi + Telegram + Buzzer + LED Rojo Titilante 10s + ACK)
 * FORMATO: C++ estándar (.cpp)
 *
 * LIBRERÍAS NECESARIAS:
 *   1. LoRa (Sandeep Mistry)
 *   2. UniversalTelegramBot (Brian Lough)
 *   3. ArduinoJson (Benoit Blanchon)
 *
 * CONEXIONES DIRECTAS EN EL ESP32 (30 PINES EN PROTOBOARD, SIN SHIELD):
 *   - LoRa Ra-01: 3.3V->3V3, GND->GND, SCK->D18, MISO->D19, MOSI->D23, NSS->D5, RST->D14, DIO0->D2
 *   - Buzzer Activo: Pata positiva (+) -> D26 | Pata negativa (-) -> GND
 *   - LED Rojo de Alerta (titila 10 segundos): Pata larga (+) -> Resistencia 220 ohm -> D25 | Pata corta (-) -> GND
 */

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <esp_bt.h>

// ========================================================
// 1. PON AQUÍ EL NOMBRE Y CONTRASEÑA DE TU WIFI (2.4 GHz)
// ========================================================
const char* WIFI_SSID     = "TU_RED_WIFI";
const char* WIFI_PASSWORD = "TU_CONTRASENA_WIFI";

// Credenciales ya configuradas para tu grupo de Telegram
#define BOT_TOKEN "8700900678:AAFtuvl45r6hUcS2kBR2Jd6tt4fhQMJm6wI"
#define CHAT_ID   "-1004323066022"

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// ========================================================
// 2. PINES DEL LORA, BUZZER Y LED ROJO DE ALERTA
// ========================================================
#define LORA_SCK  18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_SS   5
#define LORA_RST  14
#define LORA_DIO0 2

#define PIN_BUZZER    26  // Pata positiva (+) del Buzzer en D26, pata negativa (-) en GND
#define PIN_LED_ROJO  25  // LED Rojo: Pata larga (+) -> Resistencia 220 ohm -> D25, pata corta (-) -> GND

// ========================================================
// 3. PARÁMETROS DE RADIO LORA (433 MHz)
// ========================================================
#define LORA_FREQ 433E6
const byte SYNC_WORD = 0xF3;

const String CLAVE_ESPERADA = "SOS_EMERGENCIA_2026";
const String CLAVE_ACK      = "ACK_TELEGRAM_OK";

bool alarmaActiva = false;
unsigned long inicioAlarma = 0;
const unsigned long DURACION_ALERTA_MS = 10000; // El LED rojo titila y el buzzer suena por 10 segundos exactos
const unsigned long COOLDOWN_TELEGRAM  = 12000; // Evita mensajes duplicados
unsigned long ultimoEnvioTelegram = 0;

// Prototipos de funciones (C++ estándar)
void conectarWiFi();
void enviarConfirmacionACK(String nombreNodo);
bool enviarAlertaTelegram(String nombreNodo, int rssi);

void setup() {
  Serial.begin(115200);

  // Apagar Bluetooth para dejar memoria libre para Telegram
  btStop();

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_ROJO, LOW);

  Serial.println("Iniciando Nodo Receptor Central LoRa + Telegram...");

  // 1. Iniciar módulo LoRa Ra-01
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("Error: No se detectó el módulo LoRa Ra-01. Revise los cables.");
    while (1) {
      digitalWrite(PIN_LED_ROJO, !digitalRead(PIN_LED_ROJO));
      delay(200);
    }
  }

  LoRa.setTxPower(20);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);
  LoRa.setSyncWord(SYNC_WORD);
  LoRa.enableCrc();

  // 2. Conectar al WiFi y preparar Telegram
  conectarWiFi();
  secured_client.setInsecure();

  // Pitido y destello corto avisando que todo arrancó bien
  digitalWrite(PIN_BUZZER, HIGH);
  digitalWrite(PIN_LED_ROJO, HIGH);
  delay(200);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_ROJO, LOW);

  Serial.println("Receptor listo y escuchando en 433 MHz...");
  bot.sendMessage(CHAT_ID, "✅ *Central de Emergencias Rural conectada*\nSistema LoRa 433 MHz activo y escuchando.", "Markdown");
}

void loop() {
  // Si se cae el WiFi, intenta reconectar
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }

  // 1. Revisar si llegó una señal por LoRa
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String mensajeRecibido = "";
    while (LoRa.available()) {
      mensajeRecibido += (char)LoRa.read();
    }

    int rssi = LoRa.packetRssi();
    Serial.print("Señal LoRa recibida: ");
    Serial.print(mensajeRecibido);
    Serial.print(" | Potencia RSSI: ");
    Serial.print(rssi);
    Serial.println(" dBm");

    // Verificar si es nuestra señal de emergencia
    if (mensajeRecibido.indexOf(CLAVE_ESPERADA) != -1) {
      int separador = mensajeRecibido.indexOf('|');
      String nombreNodo = (separador != -1) ? mensajeRecibido.substring(0, separador) : "NODO_VEREDA_01";

      Serial.println("¡ALERTA RECIBIDA! Activando LED rojo titilante (10s), Buzzer y enviando a Telegram...");

      // PASO 1: Iniciar alarma local (LED Rojo titilando 10s + Buzzer)
      alarmaActiva = true;
      inicioAlarma = millis();
      digitalWrite(PIN_LED_ROJO, HIGH);
      digitalWrite(PIN_BUZZER, HIGH);

      // PASO 2: Enviar la alerta a Telegram
      bool telegramEnviado = false;
      if (millis() - ultimoEnvioTelegram > COOLDOWN_TELEGRAM || ultimoEnvioTelegram == 0) {
        ultimoEnvioTelegram = millis();
        telegramEnviado = enviarAlertaTelegram(nombreNodo, rssi);
      } else {
        telegramEnviado = true;
      }

      // PASO 3: Apenas se envía el mensaje a Telegram, avisar al Emisor para que prenda su LED Verde 3s
      if (telegramEnviado) {
        enviarConfirmacionACK(nombreNodo);
      }
    }
  }

  // 2. Hacer titilar el LED Rojo y sonar el Buzzer durante 10 segundos exactos
  if (alarmaActiva) {
    if (millis() - inicioAlarma >= DURACION_ALERTA_MS) {
      alarmaActiva = false;
      digitalWrite(PIN_LED_ROJO, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      Serial.println("Alerta local de 10 segundos finalizada.");
    } else {
      // Titila rápido (250 ms prendido / 250 ms apagado)
      unsigned long fase = (millis() / 250) % 2;
      if (fase == 0) {
        digitalWrite(PIN_LED_ROJO, HIGH);
        digitalWrite(PIN_BUZZER, HIGH);
      } else {
        digitalWrite(PIN_LED_ROJO, LOW);
        digitalWrite(PIN_BUZZER, LOW);
      }
    }
  }
}

void conectarWiFi() {
  Serial.print("Conectando a WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 30) {
    delay(500);
    Serial.print(".");
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nNo se conectó al WiFi. El LED rojo y Buzzer seguirán funcionando localmente.");
  }
}

void enviarConfirmacionACK(String nombreNodo) {
  String paqueteACK = CLAVE_ACK + "|" + nombreNodo;

  Serial.print("Devolviendo confirmación ACK al Emisor (para encender LED verde 3s): ");
  Serial.println(paqueteACK);

  for (int i = 0; i < 2; i++) {
    LoRa.beginPacket();
    LoRa.print(paqueteACK);
    LoRa.endPacket();
    delay(120);
  }
}

bool enviarAlertaTelegram(String nombreNodo, int rssi) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sin WiFi para enviar el mensaje a Telegram.");
    return false;
  }

  String texto = "🚨 *PELIGRO, SE HA ENVIADO UNA SEÑAL DE AYUDA* 🚨\n\n";
  texto += "📍 *Origen:* " + nombreNodo + "\n";
  texto += "📡 *Intensidad de señal:* " + String(rssi) + " dBm\n";
  texto += "⚠️ *Aviso:* Acudir de inmediato a verificar la emergencia.";

  Serial.println("Enviando alerta a Telegram...");
  bool enviado = bot.sendMessage(CHAT_ID, texto, "Markdown");

  if (enviado) {
    Serial.println("¡Alerta enviada a Telegram con éxito!");
    return true;
  } else {
    Serial.println("Error al enviar a Telegram. Revise conexión a internet.");
    return false;
  }
}