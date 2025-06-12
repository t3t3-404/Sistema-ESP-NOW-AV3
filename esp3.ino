#include <WiFi.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "esp_wifi.h"   // Para usar WiFi.channel()

// ----------------------------------------------------
// 1) CONFIGURAÇÕES DE REDE & AIO (MQTT)
// ----------------------------------------------------
#define WLAN_SSID       "Zezin"
#define WLAN_PASS       "83279727"

#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME  "gunterxd"
#define AIO_KEY       "aio_RCbO76ahvWrMo9X0KfOWvrSNl52t"

// Constrói o cliente MQTT
WiFiClient    wifiClient;
Adafruit_MQTT_Client mqtt(&wifiClient, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// Feeds de publicação (temperatura e umidade)
Adafruit_MQTT_Publish temperaturaFeed = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/temperatura");
Adafruit_MQTT_Publish umidadeFeed     = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/umidade");

// Feeds de assinatura (para receber comandos de LED)
Adafruit_MQTT_Subscribe led1Feed = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/sala");
Adafruit_MQTT_Subscribe led2Feed = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/cozinha");
Adafruit_MQTT_Subscribe led3Feed = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/banheiro");
Adafruit_MQTT_Subscribe led4Feed = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/quarto");


// ----------------------------------------------------
// 2) CONFIGURAÇÕES ESP-NOW
// ----------------------------------------------------
// Estrutura de comando para controlar LEDs (ESP3 ➜ ESP2)
typedef struct struct_comando {
  int led;      // 0..3
  int estado;   // 0 = OFF, 1 = ON
} struct_comando;

// MAC real do ESP2 (substitua pelo MAC exato do seu ESP2)
uint8_t enderecoMacESP2[] = { 0x08, 0xA6, 0xF7, 0xB0, 0x75, 0x50 };

// Variável para armazenar o comando a ser enviado ao ESP2
struct_comando comandoParaEnviar;

// ----------------------------------------------------
// 3) PROTÓTIPOS
// ----------------------------------------------------
void MQTT_connect();
void processLedCommand(int ledIndex, const char *payload);
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len);
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);

// ----------------------------------------------------
// 4) SETUP
// ----------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(10);

  // --- 4.1)  Conecta ao Wi-Fi (para usar MQTT) ---
  Serial.println();
  Serial.println("Conectando ao WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi conectado! IP: ");
  Serial.println(WiFi.localIP());

  // 4.1.1) Lê o canal da rede Wi-Fi à qual estamos conectados
  int canalAP = WiFi.channel();
  Serial.print("ESP3: Conectado no WiFi com canal = ");
  Serial.println(canalAP);

  // --- 4.2) Inicializa ESP-NOW ---
  //    Aqui, o “home channel” do ESP3 será o mesmo canal do seu AP
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW (ESP3)");
    while (true) { delay(1000); }
  }
  Serial.printf("ESP-NOW inicializado no canal %d.\n", canalAP);

  // Registra callbacks de recepção e envio
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  // 4.2.1) Adiciona o peer ESP2 usando exatamente o mesmo canal da AP
  esp_now_peer_info_t peerInfo = {};
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, enderecoMacESP2, 6);
  peerInfo.channel = canalAP;      // MESMO canal do AP (WiFi.channel())
  peerInfo.encrypt = false;        // Sem criptografia
  peerInfo.ifidx = WIFI_IF_STA;    // Usa interface STA

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Falha ao adicionar peer ESP2 em ESP3");
    // Continua mesmo que falhe, para ainda tentar receber do ESP2
  } else {
    Serial.printf("Peer ESP2 adicionado em canal %d com sucesso.\n", canalAP);
  }

  Serial.println("ESP3 (Gateway) pronto para receber ESP-NOW do ESP2.");

  // --- 4.3) Inicializa MQTT/Adafruit IO ---
  mqtt.subscribe(&led1Feed);
  mqtt.subscribe(&led2Feed);
  mqtt.subscribe(&led3Feed);
  mqtt.subscribe(&led4Feed);
  Serial.println("Assinaturas MQTT configuradas (Sala, Cozinha, Banheiro, Quarto).");
  Serial.println("Setup concluído.\n");
}

// ----------------------------------------------------
// 5) LOOP PRINCIPAL
// ----------------------------------------------------
void loop() {
  // 5.1) Garante que estamos conectados ao MQTT
  MQTT_connect();

  // 5.2) Lê novas mensagens dos feeds de LED (Sala, Cozinha, Banheiro, Quarto)
  Adafruit_MQTT_Subscribe *subscription;
  // Timeout de 5 s para receber dados do broker
  while ((subscription = mqtt.readSubscription(5000))) {
    if (subscription == &led1Feed) {
      processLedCommand(0, (char *)led1Feed.lastread);
    }
    else if (subscription == &led2Feed) {
      processLedCommand(1, (char *)led2Feed.lastread);
    }
    else if (subscription == &led3Feed) {
      processLedCommand(2, (char *)led3Feed.lastread);
    }
    else if (subscription == &led4Feed) {
      processLedCommand(3, (char *)led4Feed.lastread);
    }
  }

  // Breve delay para dar tempo de callbacks do ESP-NOW acontecerem
  delay(10);
}

// ----------------------------------------------------
// 6) FUNÇÃO PARA GARANTIR CONEXÃO MQTT
// ----------------------------------------------------
void MQTT_connect() {
  int8_t ret;

  // Retorna se já estiver conectado
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Conectando ao MQTT... ");
  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) {
    Serial.println(mqtt.connectErrorString(ret));
    Serial.println("Tentando novamente em 5 segundos...");
    mqtt.disconnect();
    delay(5000);
    retries--;
    if (retries == 0) {
      Serial.println("Não foi possível conectar ao MQTT. Travando aqui.");
      while (true) { delay(1000); }
    }
  }
  Serial.println("MQTT Conectado!");
}

// ----------------------------------------------------
// 7) PROCESSA COMANDO RECEBIDO PELO AIO (MQTT) E ENVIA VIA ESP-NOW AO ESP2
// ----------------------------------------------------
void processLedCommand(int ledIndex, const char *payload) {
  Serial.print("Comando recebido do Adafruit IO para LED ");
  Serial.print(ledIndex);
  Serial.print(": ");
  Serial.println(payload);

  comandoParaEnviar.led = ledIndex;
  comandoParaEnviar.estado = atoi(payload); // Ex: "0" ou "1"

  esp_err_t result = esp_now_send(enderecoMacESP2, (uint8_t *)&comandoParaEnviar, sizeof(comandoParaEnviar));
  if (result == ESP_OK) {
    Serial.println(" -> Comando ESP-NOW enviado para ESP2 com sucesso.");
  } else {
    Serial.print(" -> ERRO ao enviar comando ao ESP2 (codigo = ");
    Serial.print(result);
    Serial.println(")");
  }
}

// ----------------------------------------------------
// 8) CALLBACK DE RECEPÇÃO ESP-NOW (quando ESP2 mandar JSON)
// ----------------------------------------------------
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  // Copia os bytes recebidos para um buffer de string JSON
  if (len >= 200) len = 199;            // evita overflow
  char jsonBuffer[200] = {0};
  memcpy(jsonBuffer, incomingData, len);
  jsonBuffer[len] = '\0';

  // Desserializa o JSON
  StaticJsonDocument<200> jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, jsonBuffer);
  if (error) {
    Serial.print(F("Falha ao desserializar JSON: "));
    Serial.println(error.f_str());
    return;
  }

  // Extrai valores
  float temp = jsonDoc["temperatura"];
  float umid = jsonDoc["umidade"];

  Serial.print("JSON recebido do ESP2: Temp=");
  Serial.print(temp);
  Serial.print(" °C, Umid=");
  Serial.print(umid);
  Serial.println(" %");

  // Publica no Adafruit IO
  if (mqtt.connected()) {
    if (! temperaturaFeed.publish(temp)) {
      Serial.println(F("Falha ao publicar TEMPERATURA no AIO"));
    } else {
      Serial.println(F("Temperatura publicada!"));
    }
    if (! umidadeFeed.publish(umid)) {
      Serial.println(F("Falha ao publicar UMIDADE no AIO"));
    } else {
      Serial.println(F("Umidade publicada!"));
    }
  } else {
    Serial.println("MQTT não conectado, não foi possível publicar.");
  }
}

// ----------------------------------------------------
// 9) CALLBACK DE STATUS DE ENVIO ESP-NOW (opcional para debug)
// ----------------------------------------------------
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2],
           mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print("ENVIO ESP-NOW para ");
  Serial.print(macStr);
  Serial.print(" -> ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sucesso" : "Falha");
}
