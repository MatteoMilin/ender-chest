#include <Arduino.h>

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <map>

#include <LittleFS.h>
#include <FS.h>

#define DEBUG  // Comment this line to disable debug messages
#define ANGLE 40

// Configs
const char* ssid = "ENDER CHEST";
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer webServer(80);
int can_open = 0;

Servo myservo;

#define BUZZER_PIN 25

// ------------ WEB Server structure -----------
struct Route {
    const char* method;
    const char* path;
    std::function<void()> handler;
};

std::map<std::string, HTTPMethod> methodMap = {
    {"GET", HTTP_GET},
    {"POST", HTTP_POST},
    {"PUT", HTTP_PUT},
    {"DELETE", HTTP_DELETE}
};

String getContentType(String filename) {
    if (filename.endsWith(".html")) return "text/html";
    if (filename.endsWith(".css")) return "text/css";
    if (filename.endsWith(".js")) return "application/javascript";
    if (filename.endsWith(".png")) return "image/png";
    if (filename.endsWith(".jpg")) return "image/jpeg";
    if (filename.endsWith(".ttf"))  return "font/ttf";
    return "text/plain";
}

String readPassword() {
    if (!LittleFS.exists("/password.txt")) return "passw0rd";
    File file = LittleFS.open("/password.txt", "r");
    String password = file.readStringUntil('\n');
    file.close();
    password.trim();
    return password;
}

void writePassword(const String& newPassword) {
    File file = LittleFS.open("/password.txt", "w");
    file.println(newPassword);
    file.close();
}


/**
 * @brief Handles file requests from the web server.
 *
 * @return int (Return 0 on success, non-zero on failure)
 */
int handleFileRequest() {
    String path = webServer.uri();

    if (LittleFS.exists(path)) {
        File file = LittleFS.open(path, "r");
        webServer.streamFile(file, getContentType(path));
        file.close();
        return 0;
    } else {
        return -1;
    }
}

int serveFile(const String& path) {
    if (LittleFS.exists(path)) {
        File file = LittleFS.open(path, "r");
        if (file) {
            webServer.streamFile(file, getContentType(path));
            file.close();
            return 0; // Success
        } else {
            Serial.printf("Failed to open file: %s\n", path.c_str());
            return -1; // File open error
        }
    } else {
        Serial.printf("File not found: %s\n", path.c_str());
        return -1; // File not found
    }
}

int redirectTo(const String& path) {
    webServer.sendHeader("Location", path, true);
    webServer.send(302, "text/plain", "");
    return 0; // Success
}
// -------- END OF WEB Server structure --------

// Web routes
Route routes[] = {
    {"GET", "/", []() {
        serveFile("/index.html");
    }},
    {"POST", "/open", []() {
        if (webServer.hasArg("password")) {
            String password = webServer.arg("password");

            if (password == readPassword()) {
                redirectTo("/success.html");
                if (can_open == 1)
                    can_open = 2;
                if (can_open == 0)
                    can_open = 1;
            } else {
                redirectTo("/fail.html");
            }
        } else {
            webServer.send(400, "text/plain", "Champ 'password' manquant");
        }
    }},
    {"POST", "/edit-passwd", []() {
        if (webServer.hasArg("oldPassword") && webServer.hasArg("newPassword") && webServer.hasArg("confirmation")) {
            String oldPassword = webServer.arg("oldPassword");
            String newPassword = webServer.arg("newPassword");
            String confirmation = webServer.arg("confirmation");
            String currentPassword = readPassword();

            if (oldPassword != currentPassword) {
                webServer.send(403, "text/plain", "Ancien mot de passe incorrect.");
                return;
            }
            if (newPassword != confirmation) {
                webServer.send(400, "text/plain", "Le nouveau mot de passe ne correspond pas à la confirmation.");
                return;
            }
            if (newPassword.length() < 4 || newPassword.length() > 32) {
                webServer.send(400, "text/plain", "Le mot de passe doit faire entre 4 et 32 caractères.");
                return;
            }
            writePassword(newPassword);
            webServer.send(200, "text/plain", "Mot de passe changé avec succès !");
        } else {
            webServer.send(400, "text/plain", "Champs manquants.");
        }
  }},
    {"POST", "/uid", []() {
        if (webServer.hasArg("plain")) {
            String data = webServer.arg("plain");
            if (data == "true" && can_open == 1)
                can_open = 2;
            if (data == "true" && can_open == 0)
                can_open = 1;
            webServer.send(200, "text/plain", "Message received");
        } else {
            webServer.send(400, "text/plain", "No data received");
        }
  }},
};

// Loading functions
// Each function is one step of the loading screen
std::vector<std::function<void()>> loadingFunctions = {
    []() {
        // WAIT FOR SERIAL

        #ifdef DEBUG // Wait for serial to connect
            while (!Serial.available()) delay(50);
        #endif
        Serial.println("Starting setup...");
    },
    []() {
        // INIT FILE SYSTEM

        Serial.println("Initialisation du système de fichiers...");

        if (!LittleFS.begin()) {
            Serial.println("Échec de l'initialisation du système de fichiers !");
            return;
        }
        Serial.println("Système de fichiers initialisé avec succès.");
    },
    []() {
        // INIT WIFI

        Serial.println("Initialisation du point d'accès...");

        WiFi.softAP(ssid);
        delay(100);  // Laisse le temps de monter
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    },
    []() {
        // INIT DNS

        dnsServer.start(DNS_PORT, "*", apIP);
    },
    []() {
        // BEGIN WEB SERVER
        for (const auto& r : routes) {
            auto it = methodMap.find(r.method);
            if (it != methodMap.end()) {
                webServer.on(r.path, it->second, r.handler);
            } else {
                Serial.printf("Méthode inconnue : %s\n", r.method);
            }
        }

        webServer.onNotFound([]() {
            Serial.printf("[debug] URI demandée: %s\n", webServer.uri().c_str());

            if (handleFileRequest() == 0) return; // Le fichier a déja été servi

            webServer.sendHeader("Location", String("http://") + apIP.toString(), true);
            webServer.send(302, "text/plain", "");
        });
        webServer.begin();
    },
};

void setup() {
    Serial.begin(115200);
    Serial.println("Démarrage ESP32...");

    for (const auto& func : loadingFunctions) {
        func();
    };

    myservo.attach(13); // Attache le servo au GPIO 13
    myservo.write(0);
    Serial.println("Finished loading");
}

void loop() {
    // Process DNS requests and handle web server requests
    dnsServer.processNextRequest();
    webServer.handleClient();
    if (can_open == 2) {
        myservo.write(ANGLE);
        can_open = 0;
    }
}