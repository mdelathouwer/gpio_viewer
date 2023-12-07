#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class GPIOMonitor
{
public:
    GPIOMonitor(const int *pins, int numPins, unsigned long samplingInterval = 50)
        : gpioPins(pins), numPins(numPins), samplingInterval(samplingInterval), server(80), ws("/ws")
    {
        lastPinStates = new int[numPins];
        for (int i = 0; i < numPins; i++)
        {
            lastPinStates[i] = -1; // Initialize with an invalid state
        }
    }

    ~GPIOMonitor()
    {
        delete[] lastPinStates;
    }

    void begin()
    {

        // Setup WebSocket
        ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                          AwsEventType type, void *arg, uint8_t *data, size_t len)
                   { onWebSocketEvent(server, client, type, arg, data, len); });
        server.addHandler(&ws);

        // Serve Web Page
        server.on("/", [this](AsyncWebServerRequest *request)
                  { request->send_P(200, "text/html", index_html); });

        server.begin();
        // Create a task for monitoring GPIOs
        xTaskCreate(&GPIOMonitor::monitorTaskStatic, "GPIO Monitor Task", 2048, this, 1, NULL);
    }
    // Create a task for monitoring GPIOs
    static void monitorTaskStatic(void *pvParameter)
    {
        static_cast<GPIOMonitor *>(pvParameter)->monitorTask();
    }

private:
    const int *gpioPins;
    int *lastPinStates;
    int numPins;
    unsigned long samplingInterval;
    AsyncWebServer server;
    AsyncWebSocket ws;

    const char *index_html = R"rawliteral(
        <!DOCTYPE HTML><html>
        <head>
          <title>ESP32 GPIO State</title>
          <script>
            var ws;
            function initWebSocket() {
              ws = new WebSocket('ws://' + window.location.hostname + '/ws');
              ws.onmessage = function(event) {
                var data = JSON.parse(event.data);
                document.getElementById("gpioState").innerHTML = "GPIO " + data.gpio + ": " + (data.state ? "HIGH" : "LOW");
              };
            }
            window.addEventListener('load', initWebSocket);
          </script>
        </head>
        <body>
          <h1>ESP32 GPIO Monitor</h1>
          <p>GPIO State: <span id="gpioState">Waiting for updates...</span></p>
        </body>
        </html>
    )rawliteral";

    void monitorTask()
    {
        while (1)
        {
            for (int i = 0; i < numPins; i++)
            {
                int currentState = readGPIORegister(gpioPins[i]);
                // Serial.printf("gpio #%d = %d\n", gpioPins[i], currentState);
                if (currentState != lastPinStates[i])
                {
                    sendGPIOState(gpioPins[i], currentState);
                    lastPinStates[i] = currentState;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(samplingInterval));
        }
    }

    int readGPIORegister(int gpioNum)
    {
        if (gpioNum < 32)
        {
            // GPIOs 0-31 are read from GPIO_IN_REG
            return (GPIO.in >> gpioNum) & 0x1;
        }
        else
        {
            // GPIOs 32-39 are read from GPIO_IN1_REG
            return (GPIO.in1.val >> (gpioNum - 32)) & 0x1;
        }
    }

    void sendGPIOState(int gpio, int state)
    {
        String message = "{\"gpio\": " + String(gpio) + ", \"state\": " + String(state) + "}";
        // Serial.printf("Sending %s\n", message.c_str());
        ws.textAll(message);
    }

    void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                          AwsEventType type, void *arg, uint8_t *data, size_t len)
    {
        if (type == WS_EVT_CONNECT)
        {
            Serial.println("WebSocket client connected");
        }
        else if (type == WS_EVT_DISCONNECT)
        {
            Serial.println("WebSocket client disconnected");
        }
    }
};