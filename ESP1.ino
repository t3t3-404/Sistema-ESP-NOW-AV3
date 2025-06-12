// === ESP-1 (sensor DHT + envia JSON) ===
#include <WiFi.h>
#include <esp_now.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

typedef struct struct_comando {
  int led;
  int estado;
} struct_comando;

uint8_t enderecoMacESP2[] = { 0x08, 0xA6, 0xF7, 0xB0, 0x75, 0x50 };
struct_comando comandoRecebido;

#define DHTPIN 27
#define DHTTYPE DHT11
int ledPins[] = { 12, 14, 25, 26 };
DHT dht(DHTPIN, DHTTYPE);
unsigned long contadorMsg = 0;
String nodeID = "TURMA-1";

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nStatus do último envio: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Entregue com Sucesso" : "Falha na Entrega");
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  // Se chegar um struct_comando binário:
  if (len == sizeof(struct_comando)) {
    memcpy(&comandoRecebido, data, len);
    Serial.print("Comando binário recebido: LED ");
    Serial.print(comandoRecebido.led);
    Serial.print(" -> ");
    Serial.println(comandoRecebido.estado ? "ON" : "OFF");
    if (comandoRecebido.led >= 0 && comandoRecebido.led < 4) {
      digitalWrite(ledPins[comandoRecebido.led], comandoRecebido.estado ? HIGH : LOW);
    }
  }
  else {
    // Senão, chegou JSON (por exemplo, eco do gateway)
    char buf[200];
    memcpy(buf, data, len);
    buf[len] = '\0';
    Serial.print("JSON inesperado recebido: ");
    Serial.println(buf);
  }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 4; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }
  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  if (esp_now_init() != ESP_OK) { Serial.println("Erro ESP-NOW"); return; }
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, enderecoMacESP2, 6);
  peer.channel = 6;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) Serial.println("Falha add peer");
  Serial.println("ESP1 pronto no canal 6");
}

void loop() {
  delay(10000);
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) { Serial.println("Erro DHT"); return; }
  contadorMsg++;
  StaticJsonDocument<200> doc;
  doc["id"] = nodeID;
  doc["contador"] = contadorMsg;
  doc["temperatura"] = t;
  doc["umidade"] = h;
  char buf[200];
  serializeJson(doc, buf);
  esp_err_t res = esp_now_send(enderecoMacESP2, (uint8_t*)buf, strlen(buf));
  if (res == ESP_OK) Serial.println("Dados enviados com sucesso");
  else {
    Serial.print("Erro esp_now_send: "); Serial.println(res);
  }
}
