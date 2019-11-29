#include <math.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

DynamicJsonDocument victronDefs(32768);
//StaticJsonDocument<2048> currentData;

#define MAX_LINE 200
char readLine[MAX_LINE];
int pos = 0;

typedef struct {
  char key[16];
  char value[48];
} VicPair;

#define MAX_VIC_PAIR 128
VicPair currentData[MAX_VIC_PAIR];

void vicInit() {
  for (int i = 0; i < MAX_VIC_PAIR; i++) {
    currentData[i].key[0] = '\0';
    currentData[i].value[0] = '\0';
  }
}

VicPair* vicFindKey(const char* key) {
  for (int i = 0; i < MAX_VIC_PAIR; i++) {
    if (strcmp(key, currentData[i].key) == 0) {
      return (&(currentData[i]));
    }
  }

  return (0);
}

VicPair* vicFindEmptyPair() {
  for (int i = 0; i < MAX_VIC_PAIR; i++) {
    if (currentData[i].key[0] == '\0') {
      return (&(currentData[i]));
    }
  }

  return (0);
}

void updateCurrentData(DynamicJsonDocument& updates, 
                       char* fieldKey, 
                       char* fieldValue,
                       char* unitsKey,
                       char* unitsValue) {
  int fieldChanged = 0;
  int unitsChanged = 0;

  VicPair* fieldKeyPair = vicFindKey(fieldKey);
  if (fieldKeyPair != 0) {
    if (strcmp(fieldValue, fieldKeyPair->value) != 0) {
      // Value for field key changed
      fieldChanged = 1;

      // Update current data and updates
      strcpy(fieldKeyPair->value, fieldValue);
      updates[fieldKey] = fieldValue;
    }
  } else {
    // Field key/value added
    fieldChanged = 1;

    // Add to current data and updates
    fieldKeyPair = vicFindEmptyPair();
    if (fieldKeyPair != 0) {
      strcpy(fieldKeyPair->key, fieldKey);
      strcpy(fieldKeyPair->value, fieldValue);
    }
    updates[fieldKey] = fieldValue;
  }

  VicPair* unitsKeyPair = vicFindKey(unitsKey);
  if (unitsKeyPair != 0) {
    if (strcmp(unitsValue, unitsKeyPair->value) != 0) {
      // Value for units key changed
      unitsChanged = 1;

      // If the field key/value aren't already in the update, add them
      if (!fieldChanged) {
        updates[fieldKey] = fieldValue;
      }

      // Add units key/value to currentData and updates
      strcpy(unitsKeyPair->value, unitsValue);
      updates[unitsKey] = unitsValue;
    }
  } else {
    // Units key/value added
    unitsChanged = 1;

    // If the field key/value aren't already in the update, add them
    if (!fieldChanged) {
      updates[fieldKey] = fieldValue;
    }

    // Add units key/value to currentData and updates
    unitsKeyPair = vicFindEmptyPair();
    if (unitsKeyPair != 0) {
      strcpy(unitsKeyPair->key, unitsKey);
      strcpy(unitsKeyPair->value, unitsValue);
    }
    updates[unitsKey] = unitsValue;
  }

  // If the field changed but the units didn't, incllude the units
  // in the update anyway
  if (fieldChanged && (!unitsChanged)) {
    updates[unitsKey] = unitsValue;
  }

  // Create/update derived panel current
  if (fieldChanged || unitsChanged) {
    if (strcmp(fieldKey, "vpv") == 0) {
      VicPair* ppv = vicFindKey("ppv");
      if (ppv != 0) {
        float watts = (float)atoi(ppv->value);
        float volts = atof(fieldValue);
        if (strcmp(unitsValue, "mV") == 0) {
          volts /= 1000.0;
        }
        float amps = watts / volts;

        const char* units = "A";
        char tmp[50];
        if (amps < 1.0) {
          amps *= 10.0;
          int roundAmps = roundf(amps);
          roundAmps *= 100;
          if (roundAmps == 1000) {
            units = "A";
            sprintf(tmp, "%.1f", 1.0);
          } else {
            units = "mA";
            sprintf(tmp, "%d", roundAmps);
          }
        } else {
          sprintf(tmp, "%.1f", amps);
        }

        updateCurrentData(updates,
                          (char*)"ipv", tmp,
                          (char*)"ipv_units", (char*)units);
      }
    }

    if (strcmp(fieldKey, "ppv") == 0) {
      VicPair* vpv = vicFindKey("vpv");
      VicPair* vpv_units = vicFindKey("vpv_units");
      if ((vpv != 0) && (vpv_units != 0)) {
        float volts = atof(vpv->value);
        if (strcmp(vpv_units->value, "mV") == 0) {
          volts /= 1000.0;
        }
        float watts = (float)atoi(fieldValue);
        float amps = watts / volts;

        const char* units = "A";
        char tmp[50];
        if (amps < 1.0) {
          amps *= 10.0;
          int roundAmps = roundf(amps);
          roundAmps *= 100;
          if (roundAmps == 1000) {
            units = "A";
            sprintf(tmp, "%.1f", 1.0);
          } else {
            units = "mA";
            sprintf(tmp, "%d", roundAmps);
          }
        } else {
          sprintf(tmp, "%.1f", amps);
        }

        updateCurrentData(updates,
                          (char*)"ipv", tmp,
                          (char*)"ipv_units", (char*)units);
      }
    }
  }
}

void handleLine(DynamicJsonDocument& updates, char* line) {
  char* field = strtok(line, "\t");
  char* value = strtok(0, "\r");

  if ((field != 0) && (value != 0)) {
    JsonArray fieldDefs = victronDefs["fields"];
    const char* vicType = "??";
    #define FT_LEN 100
    char formattedValue[FT_LEN];
    char formattedUnits[FT_LEN];
    snprintf(formattedValue, FT_LEN, "?(%s)", value);
    for (JsonVariant v : fieldDefs) {
      JsonObject fieldDef = v.as<JsonObject>();

      if (strcmp(field, fieldDef["name"]) == 0) {
        vicType = fieldDef["type"];
        formatVicValue(formattedValue, FT_LEN, 
                       formattedUnits, FT_LEN, 
                       value, vicType);

        char* tmp = field;
        while (*tmp != 0) {
          *tmp = tolower(*tmp);
          tmp++;
        }
        char unitsKey[50];
        sprintf(unitsKey, "%s_units", field);

        updateCurrentData(updates, 
                          field, formattedValue, 
                          unitsKey, formattedUnits);
      }
    }
  }
}

void formatVicValue(char* destValue, 
                    size_t sizeValue,
                    char* destUnits,
                    size_t sizeUnits, 
                    const char* value, 
                    const char* vicType) {
  if (strcmp(vicType, "%") == 0) {
    snprintf(destValue, sizeValue, "%s", value);
    snprintf(destUnits, sizeUnits, "%%");
  } else if (strcmp(vicType, "0.01 V") == 0) {
    float tmp = (float)atoi(value);
    snprintf(destValue, sizeValue, "%.2f", tmp / 100);
    snprintf(destUnits, sizeUnits, "V");
  } else if (strcmp(vicType, "0.01 kWh") == 0) {
    float tmp = (float)atoi(value);
    if (tmp < 100.0) {
      snprintf(destValue, sizeValue, "%d", (int)(tmp * 10));
      snprintf(destUnits, sizeUnits, "Wh");
    } else {
      snprintf(destValue, sizeValue, "%.2f", tmp / 100);
      snprintf(destUnits, sizeUnits, "kWh");
    }
  } else if (strcmp(vicType, "0.1 A") == 0) {
    float tmp = (float)atoi(value);
    snprintf(destValue, sizeValue, "%.1f", tmp / 10);
    snprintf(destUnits, sizeUnits, "A");
  } else if (strcmp(vicType, "W") == 0) {
    snprintf(destValue, sizeValue, "%s", value);
    snprintf(destUnits, sizeUnits, "W");
  } else if (strcmp(vicType, "count") == 0) {
    snprintf(destValue, sizeValue, "%s", value);
    destUnits[0] = '\0';
  } else if (strcmp(vicType, "deg_C") == 0) {
    snprintf(destValue, sizeValue, "%s", value);
    snprintf(destUnits, sizeUnits, "C");
  } else if (strcmp(vicType, "fw") == 0) {
    int candidate = 0;
    int offset = 0;
    if (value[0] == 'C') {
      candidate = 1;
      offset = 1;
    }
    char major = value[offset];
    char minor[10];
    strcpy(minor, value + offset + 1);
    if (candidate) {
      snprintf(destValue, sizeValue, "%c.%s (RC)", major, minor);
      destUnits[0] = '\0';
    } else {
      snprintf(destValue, sizeValue, "%c.%s", major, minor);
      destUnits[0] = '\0';
    }
  } else if (strcmp(vicType, "mA") == 0) {
    float tmp = (float)atoi(value);
    if (tmp < 1000) {
      snprintf(destValue, sizeValue, "%s", value);
      snprintf(destUnits, sizeUnits, "mA");
    } else {
      snprintf(destValue, sizeValue, "%.1f", tmp / 1000);
      snprintf(destUnits, sizeUnits, "A");
    }
  } else if (strcmp(vicType, "mAh") == 0) {
    float tmp = (float)atoi(value);
    if (tmp < 1000) {
      snprintf(destValue, sizeValue, "%s", value);
      snprintf(destUnits, sizeUnits, "mAh");
    } else {
      snprintf(destValue, sizeValue, "%.2f", tmp / 1000);
      snprintf(destUnits, sizeUnits, "Ah");
    }
  } else if (strcmp(vicType, "mV") == 0) {
    float tmp = (float)atoi(value);
    if (tmp < 1000) {
      snprintf(destValue, sizeValue, "%s", value);
      snprintf(destUnits, sizeUnits, "mV");
    } else {
      snprintf(destValue, sizeValue, "%.2f", tmp / 1000);
      snprintf(destUnits, sizeUnits, "V");
    }
  } else if (strcmp(vicType, "map_ar") == 0) {
    int reasons = atoi(value);
    if (reasons == 0) {
      snprintf(destValue, sizeValue, "No alarm");
    } else {
      JsonArray map = victronDefs["map_ar"];
      const char* format = "%s";
      for (JsonVariant v : map) {
        JsonObject mapEntry = v.as<JsonObject>();

        if ((((int)mapEntry["key"]) & reasons) != 0) {
          int charsWritten = snprintf(destValue, 
                                      sizeValue, 
                                      format, 
                                      (const char*)mapEntry["value"]);
          destValue += charsWritten;
          sizeValue -= charsWritten;
          format = " | %s";
        }
      }
    }
    destUnits[0] = '\0';
  } else if (strcmp(vicType, "map_cs") == 0) {
    int found = 0;
    int code = atoi(value);
    JsonArray map = victronDefs["map_cs"];
    for (JsonVariant v : map) {
      JsonObject mapEntry = v.as<JsonObject>();

      if (mapEntry["key"] == code) {
        found = 1;
        snprintf(destValue, sizeValue, "%s", (const char*)mapEntry["value"]);
        break;
      }
    }
    if (!found) {
      snprintf(destValue, sizeValue, "Unknown state (%s)", value);
    }
    destUnits[0] = '\0';
  } else if (strcmp(vicType, "map_err") == 0) {
    int found = 0;
    int code = atoi(value);
    JsonArray map = victronDefs["map_err"];
    for (JsonVariant v : map) {
      JsonObject mapEntry = v.as<JsonObject>();

      if (mapEntry["key"] == code) {
        found = 1;
        snprintf(destValue, sizeValue, "%s", (const char*)mapEntry["value"]);
        break;
      }
    }
    if (!found) {
      snprintf(destValue, sizeValue, "Unknown error (%s)", value);
    }
    destUnits[0] = '\0';
  } else if (strcmp(vicType, "map_mode") == 0) {
    int found = 0;
    int code = atoi(value);
    JsonArray map = victronDefs["map_mode"];
    for (JsonVariant v : map) {
      JsonObject mapEntry = v.as<JsonObject>();

      if (mapEntry["key"] == code) {
        found = 1;
        snprintf(destValue, sizeValue, "%s", (const char*)mapEntry["value"]);
        break;
      }
    }
    if (!found) {
      snprintf(destValue, sizeValue, "Unknown mode (%s)", value);
    }
    destUnits[0] = '\0';
  } else if (strcmp(vicType, "map_pid") == 0) {
    int found = 0;
    JsonArray map = victronDefs["map_pid"];
    for (JsonVariant v : map) {
      JsonObject mapEntry = v.as<JsonObject>();

      if (strcmp(mapEntry["key"], value) == 0) {
        found = 1;
        snprintf(destValue, sizeValue, "%s", (const char*)mapEntry["value"]);
        break;
      }
    }
    if (!found) {
      snprintf(destValue, sizeValue, "Unknown product (%s)", value);
    }
    destUnits[0] = '\0';
  } else if (strcmp(vicType, "min") == 0) {
    snprintf(destValue, sizeValue, "%s", value);
    snprintf(destUnits, sizeUnits, "min");
  } else if (strcmp(vicType, "onoff") == 0) {
    snprintf(destValue, sizeValue, "%s", value);
    destUnits[0] = '\0';
  } else if (strcmp(vicType, "range[0..364]") == 0) {
    snprintf(destValue, sizeValue, "%s", value);
    destUnits[0] = '\0';
  } else if (strcmp(vicType, "sec") == 0) {
    snprintf(destValue, sizeValue, "%s", value);
    snprintf(destUnits, sizeUnits, "sec");
  } else if (strcmp(vicType, "serial") == 0) {
    snprintf(destValue, sizeValue, "%s", value);
    destUnits[0] = '\0';
  } else if (strcmp(vicType, "string") == 0) {
    snprintf(destValue, sizeValue, "%s", value);
    destUnits[0] = '\0';
  }
}

void handleRoot(AsyncWebServerRequest* request) {
  request->send(SPIFFS, "/index.html", "text/html");
}

void onWSEvent(AsyncWebSocket* server,
               AsyncWebSocketClient* client,
               AwsEventType type,
               void* arg,
               uint8_t* data,
               size_t length) {
  switch (type) {
    case WS_EVT_CONNECT: 
      {
        Serial.println("WS client connect");

        // Send current data
        StaticJsonDocument<2048> doc;
        char json[4096];
        
        for (int i = 0; i < MAX_VIC_PAIR; i++) {
          if (currentData[i].key[0] != '\0') {
            doc[currentData[i].key] = currentData[i].value;
          }
        }

        if (!doc.isNull()) {
          serializeJsonPretty(doc, json);
          client->text(json);

          Serial.print("WS client connect, sending: ");
          Serial.println(json);
        }
      }
      break;
    case WS_EVT_DISCONNECT:
      Serial.println("WS client disconnect");
      break;
    case WS_EVT_DATA:
      {
        AwsFrameInfo* info = (AwsFrameInfo*)arg;
        if (info->final && (info->index == 0) && (info->len == length)) {
          if (info->opcode == WS_TEXT) {
            data[length] = 0;
            Serial.print("data is ");
            Serial.println((char*)data);
          } else {
            Serial.println("Received a ws message, but it isn't text");
          }
        } else {
          Serial.println("Received a ws message, but it didn't fit into one frame");
        }
      }
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(19200);
  Serial2.println("ESP32 Victron test");

  vicInit();

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  
  const char* ssid = "default";
  const char* key =  "default";
 
  File configFile = SPIFFS.open("/config.json", "r");
  DynamicJsonDocument doc(1024);
  if (!configFile) {
    Serial.println("Failed to open config.json for reading");
    return;
  } else {
    DeserializationError error = deserializeJson(doc, configFile);
    if (error) {
      Serial.print("Error parsing config.json [");
      Serial.print(error.c_str());
      Serial.println("]");
    }

    ssid = doc["ssid"];
    key = doc["key"];

    configFile.close();
  }

  File victronDDFile = SPIFFS.open("/victron_data_def.json", "r");
  if(!victronDDFile){
    Serial.println("Failed to open victron_data_def.json for reading");
    return;
  } else {
    DeserializationError error = deserializeJson(victronDefs, victronDDFile);
    if (error) {
      Serial.print("Error parsing victron_data_def.json [");
      Serial.print(error.c_str());
      Serial.println("]");
    }

    victronDDFile.close();
  }

  WiFi.begin(ssid, key);
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
 
  Serial.println(WiFi.localIP());

  if (!MDNS.begin("esp32")) {
    Serial.println("Error setting up mDNS responder");
  } else {
    Serial.println("mDNS responder started: esp32.local");
  }

  ws.onEvent(onWSEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, handleRoot);
   
  server.begin();

  MDNS.addService("http", "tcp", 80);
}

void loop() {
  DynamicJsonDocument updates(4096);
  while (Serial2.available()) {
    readLine[pos] = Serial2.read();
    if ((!(pos < MAX_LINE)) || (readLine[pos] == '\n')) {
      // Process line
      readLine[pos] = 0;

      handleLine(updates, readLine);

      pos = 0;
    } else {
      pos++;
    }
  }

  if (!updates.isNull()) {
    char json[4096];
    serializeJsonPretty(updates, json);
    ws.textAll(json);

    // Serial.print("Updates available, sending: ");
    // Serial.println(json);

    // serializeJsonPretty(currentData, json);
    // Serial.print("Current data: ");
    // Serial.println(json);
  }
}
