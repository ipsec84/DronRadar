#ifndef MESH_PROTOCOL_H
#define MESH_PROTOCOL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

#define MESH_UDP_PORT 4210
#define MAX_PEER_NODES 8

struct PeerNodeInfo {
    char nodeId[16];
    int alertTier;
    int confidence;
    float azimuth;
    float elevation;
    float f0;
    char droneType[24];
    char doppler[24];
    unsigned long lastSeenMs;
    IPAddress ip;
    bool active;
};

class DronRadarMesh {
private:
    WiFiUDP udp;
    char myNodeId[16];
    PeerNodeInfo peers[MAX_PEER_NODES];
    bool initialized;
    unsigned long lastBroadcastTime;

public:
    DronRadarMesh() : initialized(false), lastBroadcastTime(0) {
        strlcpy(myNodeId, "DR-01", sizeof(myNodeId));
        for (int i = 0; i < MAX_PEER_NODES; i++) {
            peers[i].active = false;
        }
    }

    void setNodeId(const char* id) {
        if (id && strlen(id) > 0) {
            strlcpy(myNodeId, id, sizeof(myNodeId));
        }
    }

    const char* getNodeId() const {
        return myNodeId;
    }

    bool begin() {
        for (int i = 0; i < MAX_PEER_NODES; i++) {
            peers[i].active = false;
        }
        if (udp.begin(MESH_UDP_PORT)) {
            initialized = true;
            Serial.printf("[MESH] Escuchando telemetría inter-nodo en puerto UDP %d\n", MESH_UDP_PORT);
            return true;
        }
        Serial.println("[MESH] Error al iniciar socket UDP de malla");
        return false;
    }

    void broadcastTelemetry(int tier, int confidence, float azimuth, float elevation, float f0, const char* droneType, const char* doppler) {
        if (!initialized || WiFi.status() != WL_CONNECTED) return;

        JsonDocument doc;
        doc["t"] = "telemetry";
        doc["node"] = myNodeId;
        doc["tier"] = tier;
        doc["conf"] = confidence;
        doc["az"] = roundf(azimuth * 10.0f) / 10.0f;
        doc["el"] = roundf(elevation * 10.0f) / 10.0f;
        doc["f0"] = roundf(f0 * 10.0f) / 10.0f;
        doc["type"] = droneType;
        doc["dop"] = doppler;
        doc["up"] = millis() / 1000;

        char buf[320];
        size_t len = serializeJson(doc, buf, sizeof(buf));

        // Enviar a broadcast de subred
        IPAddress broadcastIp = WiFi.broadcastIP();
        udp.beginPacket(broadcastIp, MESH_UDP_PORT);
        udp.write((const uint8_t*)buf, len);
        udp.endPacket();

        lastBroadcastTime = millis();
    }

    void update(unsigned long nowMs) {
        if (!initialized) return;

        // Limpiar nodos inactivos tras 15 segundos sin noticias
        for (int i = 0; i < MAX_PEER_NODES; i++) {
            if (peers[i].active && (nowMs - peers[i].lastSeenMs > 15000)) {
                peers[i].active = false;
            }
        }

        // Procesar paquetes UDP entrantes
        int packetSize = udp.parsePacket();
        if (packetSize > 0 && packetSize < 1024) {
            char incoming[1024];
            int len = udp.read(incoming, sizeof(incoming) - 1);
            if (len > 0) {
                incoming[len] = '\0';
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, incoming);
                if (!err) {
                    const char* fromNode = doc["node"] | "";
                    // Ignorar nuestros propios paquetes
                    if (strcmp(fromNode, myNodeId) != 0 && strlen(fromNode) > 0) {
                        recordPeer(fromNode, doc, udp.remoteIP(), nowMs);
                    }
                }
            }
        }
    }

    void recordPeer(const char* nodeId, const JsonDocument& doc, IPAddress ip, unsigned long nowMs) {
        int targetSlot = -1;
        // Buscar si ya existe
        for (int i = 0; i < MAX_PEER_NODES; i++) {
            if (peers[i].active && strcmp(peers[i].nodeId, nodeId) == 0) {
                targetSlot = i;
                break;
            }
        }
        // Si no existe, buscar un hueco libre
        if (targetSlot == -1) {
            for (int i = 0; i < MAX_PEER_NODES; i++) {
                if (!peers[i].active) {
                    targetSlot = i;
                    break;
                }
            }
        }
        if (targetSlot != -1) {
            strlcpy(peers[targetSlot].nodeId, nodeId, sizeof(peers[targetSlot].nodeId));
            peers[targetSlot].alertTier = doc["tier"] | 0;
            peers[targetSlot].confidence = doc["conf"] | 0;
            peers[targetSlot].azimuth = doc["az"] | 0.0f;
            peers[targetSlot].elevation = doc["el"] | 0.0f;
            peers[targetSlot].f0 = doc["f0"] | 0.0f;
            strlcpy(peers[targetSlot].droneType, doc["type"] | "N/A", sizeof(peers[targetSlot].droneType));
            strlcpy(peers[targetSlot].doppler, doc["dop"] | "N/A", sizeof(peers[targetSlot].doppler));
            peers[targetSlot].ip = ip;
            peers[targetSlot].lastSeenMs = nowMs;
            peers[targetSlot].active = true;
        }
    }

    int getActivePeerCount() const {
        int cnt = 0;
        for (int i = 0; i < MAX_PEER_NODES; i++) {
            if (peers[i].active) cnt++;
        }
        return cnt;
    }

    void populateJsonPeers(JsonArray& arr) const {
        for (int i = 0; i < MAX_PEER_NODES; i++) {
            if (peers[i].active) {
                JsonObject p = arr.add<JsonObject>();
                p["node"] = peers[i].nodeId;
                p["tier"] = peers[i].alertTier;
                p["conf"] = peers[i].confidence;
                p["az"] = peers[i].azimuth;
                p["el"] = peers[i].elevation;
                p["f0"] = peers[i].f0;
                p["type"] = peers[i].droneType;
                p["dop"] = peers[i].doppler;
                p["ip"] = peers[i].ip.toString();
                p["age_s"] = (millis() - peers[i].lastSeenMs) / 1000;
            }
        }
    }
};

#endif // MESH_PROTOCOL_H
