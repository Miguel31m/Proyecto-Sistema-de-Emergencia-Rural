/*
 * PROYECTO: Sistema de Alerta LoRa (433 MHz) con ESP32 (Bidireccional con Confirmación)
 * INTEGRANTES: Jacobo Ospina, Juan Pablo Gonzales, Miguel Ángel Montoya
 * MÓDULO: Nodo Emisor (Botón de Pánico Rural - Sin WiFi, Bajo Consumo)
 * FORMATO: C++ estándar (.cpp)
 *
 * CONEXIONES DIRECTAS EN EL ESP32 (30 PINES EN PROTOBOARD, SIN SHIELD):
 *   - LoRa Ra-01: 3.3V->3V3, GND->GND, SCK->D18, MISO->D19, MOSI->D23, NSS->D5, RST->D14, DIO0->D2
 *   - Pulsador: Un pin a D33 y el otro a GND
 *   - LED Rojo (dura 3 seg prendido al enviar): Pata larga (+) -> Resistencia 220 ohm -> D25 | Pata corta (-) -> GND
 *   - LED Verde (dura 3 seg prendido al llegar a Telegram): Pata larga (+) -> Resistencia 220 ohm -> D26 | Pata corta (-) -> GND
 */

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <esp_bt.h>

// Pines del módulo LoRa Ra-01 (433 MHz)
#define LORA_SCK  18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_SS   5
#define LORA_RST  14
#define LORA_DIO0 2

// Pines del Pulsador y los 2 LEDs
#define PIN_BOTON      33  // Pulsador conectado entre D33 y GND
#define PIN_LED_ROJO   25  // LED Rojo: Dura 3 segundos prendido al enviar la señal
#define PIN_LED_VERDE  26  // LED Verde: Dura 3 segundos prendido cuando llega a Telegram

// Parámetros de Radiofrecuencia LoRa (433 MHz)
#define LORA_FREQ 433E6
const byte SYNC_WORD = 0xF3;

const String ID_NODO      = "NODO_VEREDA_01";
const String CLAVE_ALERTA = "SOS_EMERGENCIA_2026";
const String CLAVE_ACK    = "ACK_TELEGRAM_OK";

unsigned long contadorMensajes = 0;

// Prototipos de funciones (C++ estándar)
void enviarAlertaConConfirmacion();
bool esperarConfirmacionACK(unsigned long tiempoEsperaMs);

void setup() {
  Serial.begin(115200);

  // Apagar WiFi y Bluetooth en el emisor para ahorrar batería
  WiFi.mode(WIFI_OFF);
  btStop();

  pinMode(PIN_BOTON, INPUT_PULLUP);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);

  digitalWrite(PIN_LED_ROJO, LOW);
  digitalWrite(PIN_LED_VERDE, LOW);

  Serial.println("Iniciando Nodo Emisor LoRa...");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("Error: No se detectó el módulo LoRa Ra-01. Revise los cables.");
    while (1) {
      digitalWrite(PIN_LED_ROJO, !digitalRead(PIN_LED_ROJO));
      delay(200);
    }
  }

  // Configuración para largo alcance rural
  LoRa.setTxPower(20);             // Máxima potencia (20 dBm)
  LoRa.setSpreadingFactor(12);     // SF12: Máximo alcance
  LoRa.setSignalBandwidth(125E3);  // Ancho de banda 125 kHz
  LoRa.setCodingRate4(8);          // Máxima protección contra errores
  LoRa.setSyncWord(SYNC_WORD);
  LoRa.enableCrc();

  Serial.println("Emisor listo. Presione el pulsador para pedir ayuda.");

  // Destello rápido de prueba al encender
  digitalWrite(PIN_LED_ROJO, HIGH);
  delay(250);
  digitalWrite(PIN_LED_ROJO, LOW);
  digitalWrite(PIN_LED_VERDE, HIGH);
  delay(250);
  digitalWrite(PIN_LED_VERDE, LOW);
}

void loop() {
  // Si se presiona el pulsador (conecta D33 con GND)
  if (digitalRead(PIN_BOTON) == LOW) {
    delay(50); // Evita falsas pulsaciones
    if (digitalRead(PIN_BOTON) == LOW) {
      enviarAlertaConConfirmacion();

      // Esperar a que suelten el botón antes de permitir otro envío
      while (digitalRead(PIN_BOTON) == LOW) {
        delay(10);
      }
    }
  }
  delay(20);
}

void enviarAlertaConConfirmacion() {
  contadorMensajes++;
  String paquete = ID_NODO + "|" + CLAVE_ALERTA + "|" + String(contadorMensajes);

  // Asegurar que el LED verde esté apagado antes de iniciar
  digitalWrite(PIN_LED_VERDE, LOW);

  Serial.print("Enviando señal de auxilio: ");
  Serial.println(paquete);

  // 1. Enviar el paquete por LoRa y encender el LED ROJO durante exactamente 3 segundos
  digitalWrite(PIN_LED_ROJO, HIGH);

  LoRa.beginPacket();
  LoRa.print(paquete);
  LoRa.endPacket();

  // Durante estos 3 segundos mientras el LED rojo está prendido, también escuchamos por si el ACK llega rápido
  bool confirmado = esperarConfirmacionACK(3000);
  digitalWrite(PIN_LED_ROJO, LOW); // Apagar LED rojo tras cumplir los 3 segundos

  // 2. Si aún no ha llegado la confirmación (porque Telegram tarda ~2-4 seg en responder), esperar hasta 7 seg más
  if (!confirmado) {
    Serial.println("Esperando confirmación de entrega a Telegram desde el Receptor...");
    confirmado = esperarConfirmacionACK(7000);
  }

  // 3. Si el Receptor confirmó que el mensaje llegó a Telegram, prender LED VERDE por 3 segundos
  if (confirmado) {
    Serial.println("¡CONFIRMADO! El mensaje llegó a Telegram. Encendiendo LED Verde 3 segundos...");
    digitalWrite(PIN_LED_VERDE, HIGH);
    delay(3000);
    digitalWrite(PIN_LED_VERDE, LOW);
  } else {
    Serial.println("Aviso: No se recibió confirmación del Receptor.");
    // Parpadeo corto del LED rojo avisando que no hubo respuesta
    for (int i = 0; i < 4; i++) {
      digitalWrite(PIN_LED_ROJO, HIGH);
      delay(150);
      digitalWrite(PIN_LED_ROJO, LOW);
      delay(150);
    }
  }
}

bool esperarConfirmacionACK(unsigned long tiempoEsperaMs) {
  unsigned long inicio = millis();

  while (millis() - inicio < tiempoEsperaMs) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String respuesta = "";
      while (LoRa.available()) {
        respuesta += (char)LoRa.read();
      }

      Serial.print("Respuesta recibida: ");
      Serial.println(respuesta);

      if (respuesta.indexOf(CLAVE_ACK) != -1 && respuesta.indexOf(ID_NODO) != -1) {
        return true;
      }
    }
    delay(10);
  }
  return false;
}