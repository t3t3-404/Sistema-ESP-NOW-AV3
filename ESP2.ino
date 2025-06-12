#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"   // Para usar esp_wifi_set_channel()

// MACs dos peers
uint8_t enderecoMacESP1[] = { 0x10, 0x06, 0x1C, 0x86, 0xBF, 0x00 };
uint8_t enderecoMacESP3[] = { 0xF4, 0x65, 0x0B, 0x47, 0x31, 0xBC };

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2],
           mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print("Envio para ");
  Serial.print(macStr);
  Serial.print(" -> ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sucesso" : "Falha");
}

void OnDataRecv(const esp_now_recv_info_t *info,
                const uint8_t *incomingData, int len) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           info->src_addr[0], info->src_addr[1], info->src_addr[2],
           info->src_addr[3], info->src_addr[4], info->src_addr[5]);

  Serial.print("Mensagem recebida de ");
  Serial.print(macStr);
  Serial.print(" | Tamanho: ");
  Serial.println(len);

  bool veioDoESP1 = (memcmp(info->src_addr, enderecoMacESP1, 6) == 0);
  bool veioDoESP3 = (memcmp(info->src_addr, enderecoMacESP3, 6) == 0);

  if (veioDoESP1) {
    Serial.println("  -> Retransmitindo para ESP3...");
    esp_err_t res = esp_now_send(enderecoMacESP3, incomingData, len);
    if (res != ESP_OK) {
      Serial.print("  ! Erro ao enviar para ESP3: ");
      Serial.println(res);
    }
  }
  else if (veioDoESP3) {
    Serial.println("  -> Retransmitindo para ESP1...");
    esp_err_t res = esp_now_send(enderecoMacESP1, incomingData, len);
    if (res != ESP_OK) {
      Serial.print("  ! Erro ao enviar para ESP1: ");
      Serial.println(res);
    }
  }
  else {
    Serial.println("  ! MAC de origem desconhecido.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // 1) Modo STA e desconecta de qualquer Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // 2) Inicializa ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW (ESP2)");
    return;
  }

  // 3) Força canal 6 para ESP-NOW (mesmo canal do ESP3)
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  Serial.println("ESP2: Canal forçado a 6 para ESP-NOW.");

  // 4) Registra callbacks
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  // 5) Adiciona peer ESP1 em canal 6
  esp_now_peer_info_t peerInfo1;
  memset(&peerInfo1, 0, sizeof(peerInfo1));
  memcpy(peerInfo1.peer_addr, enderecoMacESP1, 6);
  peerInfo1.channel = 6;
  peerInfo1.encrypt = false;
  if (esp_now_add_peer(&peerInfo1) != ESP_OK) {
    Serial.println("Falha ao adicionar peer ESP1");
  }

  // 6) Adiciona peer ESP3 em canal 6
  esp_now_peer_info_t peerInfo3;
  memset(&peerInfo3, 0, sizeof(peerInfo3));
  memcpy(peerInfo3.peer_addr, enderecoMacESP3, 6);
  peerInfo3.channel = 6;
  peerInfo3.encrypt = false;
  if (esp_now_add_peer(&peerInfo3) != ESP_OK) {
    Serial.println("Falha ao adicionar peer ESP3");
  }

  Serial.println("ESP2 configurado como repetidor no canal 6.");
}

void loop() {
  // Toda a retransmissão ocorre em OnDataRecv()
  delay(1000);
}
