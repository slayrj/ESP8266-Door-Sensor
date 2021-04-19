#include <Arduino.h>

#include <FS.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
 
char mqttServer[40] = "192.168.1.250";
char mqttPort[6] = "1883";
char mqttUser[15] = "user";
char mqttPassword[15] = "123456";

// Setup do botao de reset
const int setupBtn = 4;
int buttonState = 0;

WiFiClient espClient;
PubSubClient client(espClient);
void callback(char* topic, byte* payload, unsigned int length);

// Variaveis e configuracoes de bateria
ADC_MODE(ADC_VCC);
bool replaceBatt = false;

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup()  {
  // Setup botao de reset
  pinMode(setupBtn, INPUT);
  buttonState = digitalRead(setupBtn);

  // inicializando porta Serial
  Serial.begin(115200);
  Serial.println();

  // Formatar FS, para teste
  //SPIFFS.format();

  // INICIANDO FS
  //
  //
  //

  // Lendo configuracoes do JSON no FileSistem
  Serial.println("montando FS...");

  if (SPIFFS.begin()) {
    Serial.println("FS montado com sucesso");
    if (SPIFFS.exists("/config.json")) {
      // O arquivo existe, lendo e carregando
      Serial.println("Lendo arquivo de configuracao");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("Arquivo de configuracao aberto com sucesso");
        size_t size = configFile.size();
        // Allocando buffer para alocar o conteudo do arquivo.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqttServer, json["mqttServer"]);
          strcpy(mqttPort, json["mqttPort"]);
          strcpy(mqttUser, json["mqttUser"]);
          strcpy(mqttPassword, json["mqttPassword"]);

        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  // Fim da leitura


  // Customizacao de Parametros extras na configuracao do WifiManager
  WiFiManagerParameter custom_mqtt_server("server", "Servidor MQTT", mqttServer, 40);
  WiFiManagerParameter custom_mqtt_port("port", "Porta MQTT", mqttPort, 6);
  WiFiManagerParameter custom_mqtt_user("user", "Usuario MQTT", mqttUser, 15);
  WiFiManagerParameter custom_mqtt_password("password", "Senha MQTT", mqttPassword, 15);

  // Inicializando WiFiManager
  WiFiManager wifiManager;

  // Verificando se o botao de reset está pressionado
  if (digitalRead(setupBtn) == HIGH) {
  Serial.println("Resetando Configuracoes Wifi e formatando o SPIFFS");
  wifiManager.resetSettings();
  SPIFFS.format();
  ESP.restart();
  }

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  // Parametros para exibir no WifiManager
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  
  //wifiManager.setTimeout(120);

  // Tentando reconectar a Wifi configurada
  if (!wifiManager.autoConnect("Sensors-AP")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //Se conectar com sucesso a rede
  Serial.println("Conexao de rede estabelecida");

  //Ler variaveis atualizadas
  strcpy(mqttServer, custom_mqtt_server.getValue());
  strcpy(mqttPort, custom_mqtt_port.getValue());
  strcpy(mqttUser, custom_mqtt_user.getValue());
  strcpy(mqttPassword, custom_mqtt_password.getValue());

  //Salvar as variaveis para o FileSistem
  if (shouldSaveConfig) {
    Serial.println("Salvando Configuracao");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqttServer"] = mqttServer;
    json["mqttPort"] = mqttPort;
    json["mqttUser"] = mqttUser;
    json["mqttPassword"] = mqttPassword;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
   }
  // Fim do salvamento

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  // FINALIZANDO TAREFAS DO FS
  //
  // 
  //

  // Conectando o MQTT Server
  client.setServer(mqttServer, atoi(mqttPort));
  client.setCallback(callback);
 
  while (!client.connected()) {
    Serial.println("Conectando ao MQTT...");
 
    if (client.connect("back_door", mqttUser, mqttPassword )) {
 
      Serial.println("Conectado com sucesso ao MQTT!");  
 
    } else {
 
      Serial.println("Falha ao conectar no MQTT!!!");
      Serial.print(client.state());
      delay(2000);
 
    }
  }

  Serial.println("Reed switch acionado!");

  // Obtendo medindo e formatando a VCC da bateria
  int vdd_crude = ESP.getVcc();
  String vdd_str_crude = String(vdd_crude);

  if (vdd_crude <= 2744) {
    replaceBatt = true;
  }
  else {
    replaceBatt = false;
  }
   
  // Prepare a JSON payload string
  String payload = "{";
  payload += "\"batt\":"; payload += replaceBatt; payload += ",";
  payload += "\"stat\":"; payload += "\"Activated\""; payload += ",";
  payload += "\"batt_crude\":"; payload += vdd_str_crude;
  payload += "}";

  char attributes[100];
  payload.toCharArray( attributes, 100 );
  
  // Enviando dados
  Serial.println( "Alarme da Porta da Cozinha Acionado!" );
  Serial.print("JSON enviado ao MQTT: ");
  Serial.println(attributes);
  client.publish("alarm/backdoor/status", attributes);
  }

void callback(char* topic, byte* payload, unsigned int length) {
 
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
 
  Serial.print("Message:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
 
  Serial.println();
  Serial.println("-----------------------");
 
}
 
void loop() {
  client.loop();
  
  Serial.println("Entrando no modo DeepSleep...");
  ESP.deepSleep(0);
}
