# LINK DE TELEGRAM PARA RECIBIR LOS AVISOS DEL BOT
https://t.me/+GBWS72TCFQthNjQx



# Software C++: Sistema de Alerta LoRa (433 MHz) + Telegram (Sin Shield)

## Tiempos y Comportamiento Programados
1. **Nodo Emisor (`nodo_emisor/nodo_emisor.cpp`):**
   - Al presionar el pulsador (`D33`), envía la señal por LoRa y enciende el **LED Rojo (`D25`) durante 3 segundos**.
   - Apenas el Receptor confirma que el mensaje sí llegó al grupo de Telegram, el Emisor enciende el **LED Verde (`D26`) durante 3 segundos**.

2. **Nodo Receptor (`nodo_receptor/nodo_receptor.cpp`):**
   - Al recibir la señal de auxilio, pone a titilar el **LED Rojo (`D25`) durante 10 segundos** junto con el **Buzzer (`D26`)**, envía por WiFi el mensaje `"PELIGRO, SE HA ENVIADO UNA SEÑAL DE AYUDA"` al grupo de Telegram y le devuelve la confirmación al Emisor.

## Paso único antes de subir al Receptor
En `nodo_receptor/nodo_receptor.cpp` (líneas 25 y 26), escriban el nombre y contraseña de su red WiFi de 2.4 GHz:
```cpp
const char* WIFI_SSID     = "TU_RED_WIFI";
const char* WIFI_PASSWORD = "TU_CONTRASENA_WIFI";
```
*(El `BOT_TOKEN` y el `CHAT_ID` de su grupo ya están configurados).*
