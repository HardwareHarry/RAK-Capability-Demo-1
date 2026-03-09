// =============================================================================
// main.cpp — WisBlock Sensor Hub Entry Point
// Boot sequence, FreeRTOS task creation, and supervisor loop
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LittleFS.h>

#include "config.h"
#include "types.h"

#include "storage/ring_buffer.h"
#include "sensors/sensor_manager.h"
#include "network/web_server.h"
#include "network/lora_manager.h"
#include "network/captive_portal.h"
#include "display/display_factory.h"
#include "ota_manager.h"
#include "location/geocoder.h"

// Shared FRAM instance — created by sensorTask, used by webServer and loraTask
FramRingBuffer* g_framPtr = nullptr;

// Task function forward declarations
void sensorTaskFn(void* param);
void loraTaskFn(void* param);
void webServerTaskFn(void* param);
void displayTaskFn(void* param);
void supervisorTaskFn(void* param);

// =============================================================================
// Global shared state
// =============================================================================
SystemHealth     g_health;
LiveDataCache    g_liveData;
EventGroupHandle_t g_events;

// Feature managers (initialized after boot)
OTAManager*      g_otaManager = nullptr;
Geocoder*        g_geocoder = nullptr;

// Task handles (for supervisor monitoring and potential restart)
TaskHandle_t     g_sensorTaskHandle    = nullptr;
TaskHandle_t     g_loraTaskHandle      = nullptr;
TaskHandle_t     g_webServerTaskHandle = nullptr;
TaskHandle_t     g_displayTaskHandle   = nullptr;
TaskHandle_t     g_supervisorHandle    = nullptr;

// =============================================================================
// Boot Phase 1: Hardware Initialisation
// =============================================================================

static bool initHardware() {
    Serial.begin(115200);
    delay(500);  // Allow USB-CDC to enumerate

    Serial.println();
    Serial.println(F("╔══════════════════════════════════════════╗"));
    Serial.printf( "║  %s v%s\n", FW_NAME, FW_VERSION);
    Serial.printf( "║  Built: %s %s\n", FW_BUILD_DATE, FW_BUILD_TIME);
    Serial.println(F("╚══════════════════════════════════════════╝"));
    Serial.println();

    // --- Enable 3V3_S switched sensor rail (stays on permanently) -----------
    pinMode(PIN_3V3_S_EN, OUTPUT);
    digitalWrite(PIN_3V3_S_EN, HIGH);
    Serial.println(F("[BOOT] 3V3_S sensor rail enabled (permanent)"));
    delay(100);  // Let rail stabilise

    // --- I2C Bus ------------------------------------------------------------
    Wire.begin(PIN_WIRE_SDA, PIN_WIRE_SCL);
    Wire.setClock(400000);  // 400 kHz fast mode
    Serial.println(F("[BOOT] I2C bus initialised (400 kHz)"));

    // --- SPI Bus (for FRAM and Display) -------------------------------------
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
    Serial.println(F("[BOOT] SPI bus initialised"));

    // --- UART1 for GNSS (Slot D) --------------------------------------------
    Serial1.begin(GNSS_BAUD, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
    Serial.println(F("[BOOT] UART1 initialised for GNSS"));

    // --- LittleFS for web assets --------------------------------------------
    if (!LittleFS.begin(true)) {
        Serial.println(F("[BOOT] WARNING: LittleFS mount failed"));
        // Non-fatal: web server will serve 404s but system keeps running
    } else {
        Serial.println(F("[BOOT] LittleFS mounted"));
    }

    // --- I2C Bus Scan (diagnostic) ------------------------------------------
    Serial.println(F("[BOOT] I2C scan:"));
    uint8_t deviceCount = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  0x%02X", addr);
            deviceCount++;
            // Identify known devices
            if (addr == BME680_I2C_ADDR)    Serial.print(" (BME680)");
            if (addr == VEML7700_I2C_ADDR)  Serial.print(" (VEML7700)");
            if (addr == PMSA003I_I2C_ADDR)  Serial.print(" (PMSA003I)");
            if (addr == RV3028_I2C_ADDR)    Serial.print(" (RV-3028 RTC)");
            if (addr == IO_EXPANDER_I2C_ADDR) Serial.print(" (MCP23017)");
            if (addr == 0x38)    Serial.print(" (FT6336 Touch)");  // Can't use original FT6336 if the display is set to TFT.  This should work either way.
            Serial.println();
        }
    }
    Serial.printf("[BOOT] Found %d I2C devices\n\n", deviceCount);

    return true;
}

// =============================================================================
// setup() — Arduino entry point
// =============================================================================

void setup() {
    // --- Phase 1: Hardware Init ---------------------------------------------
    if (!initHardware()) {
        Serial.println(F("[BOOT] FATAL: Hardware init failed. Halting."));
        while (true) { delay(1000); }
    }

    // --- Initialise shared state --------------------------------------------
    g_health.init();
    g_liveData.init();
    g_events = xEventGroupCreate();

    Serial.println(F("[BOOT] Shared state initialised"));
    Serial.println(F("[BOOT] Creating FreeRTOS tasks..."));
    Serial.println();

    // --- Create Supervisor first (manages all others) -----------------------
    xTaskCreatePinnedToCore(
        supervisorTaskFn,
        "Supervisor",
        STACK_SUPERVISOR,
        nullptr,
        PRIORITY_SUPERVISOR,
        &g_supervisorHandle,
        CORE_SUPERVISOR
    );

    // --- Create worker tasks ------------------------------------------------
    xTaskCreatePinnedToCore(
        sensorTaskFn,
        "Sensors",
        STACK_SENSOR,
        nullptr,
        PRIORITY_SENSOR,
        &g_sensorTaskHandle,
        CORE_SENSOR
    );

    xTaskCreatePinnedToCore(
        loraTaskFn,
        "LoRa",
        STACK_LORA,
        nullptr,
        PRIORITY_LORA,
        &g_loraTaskHandle,
        CORE_LORA
    );

    xTaskCreatePinnedToCore(
        webServerTaskFn,
        "WebServer",
        STACK_WEBSERVER,
        nullptr,
        PRIORITY_WEBSERVER,
        &g_webServerTaskHandle,
        CORE_WEBSERVER
    );

    xTaskCreatePinnedToCore(
        displayTaskFn,
        "Display",
        STACK_DISPLAY,
        nullptr,
        PRIORITY_DISPLAY,
        &g_displayTaskHandle,
        CORE_DISPLAY
    );

    Serial.println(F("[BOOT] All tasks created. Handing off to FreeRTOS scheduler."));
    Serial.println();
}

// =============================================================================
// loop() — Arduino main loop (effectively idle; all work is in tasks)
// =============================================================================

void loop() {
    // The Arduino loop task runs on Core 1 at priority 1.
    // All real work happens in our FreeRTOS tasks.
    // This loop just yields to prevent the watchdog from triggering.
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// =============================================================================
// Supervisor Task — system watchdog and health monitor
// =============================================================================

void supervisorTaskFn(void* param) {
    Serial.println(F("[SUPERVISOR] Starting supervisor task"));
    g_health.systemState = SystemState::WARMING_UP;

    // Feed the ESP32 hardware watchdog
    // esp_task_wdt_init() can be called here for TWDT if desired

    uint32_t lastTempCheck = 0;  // Track temperature check timing

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));  // Check every 2 seconds

        uint32_t now = millis();

        // --- Temperature Monitoring (every 5 seconds) ---
        if (now - lastTempCheck >= 5000) {
            lastTempCheck = now;
            g_health.updateChipTemperature();

            uint8_t thermalState = g_health.getThermalState();
            if (thermalState > 0) {
                int16_t temp = g_health.getChipTemperatureC();
                const char* stateStr = (thermalState == 1) ? "WARM" : "CRITICAL";
                Serial.printf("[SUPERVISOR] Thermal: %d°C - %s\n", temp, stateStr);
            }
        }

        // --- Task Watchdog Monitoring ---
        bool allRunning = true;
        bool anyError = false;

        for (uint8_t i = 0; i < static_cast<uint8_t>(TaskId::TASK_COUNT); i++) {
            TaskId id = static_cast<TaskId>(i);
            auto& t = g_health.tasks[i];

            // ...existing code...

            // Skip tasks that haven't started yet
            if (t.state == TaskState::NOT_STARTED) {
                allRunning = false;
                continue;
            }

            // Check for heartbeat timeout
            uint32_t elapsed = now - t.lastHeartbeat;
            if (elapsed > t.timeoutMs) {
                t.consecutiveMisses++;

                if (t.consecutiveMisses == 1) {
                    Serial.printf("[SUPERVISOR] WARNING: %s missed heartbeat "
                                  "(elapsed %lu ms, timeout %lu ms)\n",
                                  taskIdName(id), elapsed, t.timeoutMs);
                }

                // Graduated recovery
                if (t.consecutiveMisses >= WDT_HARD_RESTART_AFTER) {
                    Serial.printf("[SUPERVISOR] CRITICAL: %s unresponsive "
                                  "after %d misses. Restarting system.\n",
                                  taskIdName(id), t.consecutiveMisses);
                    // TODO: Log to FRAM crash record before restart
                    delay(100);
                    ESP.restart();
                }
                else if (t.consecutiveMisses >= WDT_SOFT_RECOVERY_AFTER) {
                    Serial.printf("[SUPERVISOR] RECOVERY: Attempting soft "
                                  "recovery for %s\n", taskIdName(id));
                    t.state = TaskState::RECOVERING;
                    // TODO: Per-task recovery actions (I2C bus reset, etc.)
                }

                anyError = true;
            }

            // Track whether all tasks are in a healthy state
            if (t.state != TaskState::RUNNING) {
                allRunning = false;
            }
            if (t.state == TaskState::ERROR) {
                anyError = true;
            }
        }

        // Update system state
        SystemState newState;
        if (anyError) {
            newState = SystemState::DEGRADED;
        } else if (allRunning) {
            newState = SystemState::READY;
        } else {
            newState = SystemState::WARMING_UP;
        }

        if (newState != g_health.systemState) {
            Serial.printf("[SUPERVISOR] System state: %d -> %d\n",
                          static_cast<int>(g_health.systemState),
                          static_cast<int>(newState));
            g_health.systemState = newState;
            xEventGroupSetBits(g_events, EVT_STATE_CHANGE | EVT_DISPLAY_REFRESH);
        }
    }
}

// =============================================================================
// Stub Task Implementations — replace with real implementations
// =============================================================================
// Each stub demonstrates the heartbeat pattern and basic lifecycle.

void sensorTaskFn(void* param) {
    static FramRingBuffer fram(PIN_FRAM_CS);
    fram.init();
    g_framPtr = &fram;  // expose to other tasks

    static SensorManager sensors(fram, g_liveData, g_health, g_events);
    sensors.init();

    while (true) {
        sensors.tick();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void loraTaskFn(void* param) {
    g_health.heartbeat(TaskId::LORA, TaskState::INITIALISING, "Waiting for FRAM...");

    // Wait for sensor task to initialise FRAM
    while (g_framPtr == nullptr) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    static LoRaManager lora(*g_framPtr, g_liveData, g_health, g_events);

    // Initialize radio
    if (!lora.init()) {
        Serial.println(F("[LORA] FATAL: Radio initialization failed"));
        g_health.heartbeat(TaskId::LORA, TaskState::ERROR, "Init failed");
        vTaskDelete(nullptr);  // Kill this task
        return;
    }

    // Attempt OTAA join
    g_health.heartbeat(TaskId::LORA, TaskState::WARMING_UP, "Joining...");

    uint8_t joinAttempts = 0;
    const uint8_t MAX_JOIN_ATTEMPTS = 3;

    while (!lora.isJoined() && joinAttempts < MAX_JOIN_ATTEMPTS) {
        joinAttempts++;
        Serial.printf("[LORA] Join attempt %d/%d\n", joinAttempts, MAX_JOIN_ATTEMPTS);

        if (lora.join(60000)) {
            break;  // Join successful
        }

        // Wait before retry
        if (joinAttempts < MAX_JOIN_ATTEMPTS) {
            Serial.println(F("[LORA] Join failed, retrying in 30 seconds..."));
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
    }

    if (!lora.isJoined()) {
        Serial.println(F("[LORA] WARNING: Could not join network"));
        Serial.println(F("[LORA] Will continue attempting in background"));
        g_health.heartbeat(TaskId::LORA, TaskState::DEGRADED, "Not joined");
    }

    uint32_t lastJoinAttemptMs = millis();
    const uint32_t REJOIN_INTERVAL_MS = 300000;  // Retry join every 5 minutes if not joined

    while (true) {
        // If not joined, periodically retry
        if (!lora.isJoined()) {
            if (millis() - lastJoinAttemptMs >= REJOIN_INTERVAL_MS) {
                Serial.println(F("[LORA] Attempting rejoin..."));
                lora.join(60000);
                lastJoinAttemptMs = millis();
            }

            // Heartbeat even when not joined
            g_health.heartbeat(TaskId::LORA, TaskState::DEGRADED, "Not joined");
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }

        // Wait for new data event or timeout
        EventBits_t bits = xEventGroupWaitBits(
            g_events, EVT_NEW_RECORD,
            pdTRUE,     // clear bits on exit
            pdFALSE,    // any bit
            pdMS_TO_TICKS(LORA_TX_INTERVAL_MS / 10)  // wake up periodically
        );

        // Attempt transmission (will check internal timer)
        if (lora.transmit()) {
            Serial.println(F("[LORA] Transmission successful"));
        }

        // Heartbeat
        g_health.heartbeat(TaskId::LORA, TaskState::RUNNING, "Idle");
    }
}

void webServerTaskFn(void* param) {
    g_health.heartbeat(TaskId::WEBSERVER, TaskState::INITIALISING, "Waiting for FRAM...");

    // Wait for sensor task to initialise FRAM
    while (g_framPtr == nullptr) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Initialize feature managers
    if (!g_otaManager) {
        g_otaManager = new OTAManager(g_health, nullptr);  // TODO: pass display driver
        g_otaManager->init();
        Serial.println(F("[OTA] Manager initialized"));
    }

    if (!g_geocoder) {
        g_geocoder = new Geocoder();
        Serial.println(F("[LOCATION] Geocoder initialized"));
    }

    static WebServerManager web(g_liveData, g_health, *g_framPtr, g_events);
    web.init();

    while (true) {
        web.tick();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void displayTaskFn(void* param) {
    g_health.heartbeat(TaskId::DISPLAY_TASK, TaskState::INITIALISING, "Display init...");
    Serial.println(F("[DISPLAY] Task starting"));

    // Instantiate the display driver based on compile-time selection
    #if defined(DISPLAY_EINK) && DISPLAY_EINK == 1
        Serial.println(F("[DISPLAY] Using E-Ink driver (RAK14000)"));
    #elif defined(DISPLAY_TFT) && DISPLAY_TFT == 1
        Serial.println(F("[DISPLAY] Using TFT driver (RAK14014)"));
    #endif

    DisplayDriver* driver = createDisplay();

    if (!driver || !driver->init()) {
        g_health.heartbeat(TaskId::DISPLAY_TASK, TaskState::ERROR, "Init failed");
        Serial.println(F("[DISPLAY] FATAL: Display initialization failed"));
        vTaskDelete(nullptr);
        return;
    }

    // --- Boot Phase: Show initialization progress ---
    Serial.println(F("[DISPLAY] Boot screen phase"));
    g_health.heartbeat(TaskId::DISPLAY_TASK, TaskState::INITIALISING, "Boot screen");

    while (g_health.systemState == SystemState::BOOTING ||
           g_health.systemState == SystemState::WARMING_UP) {

        // Show boot screen with current system health
        driver->showBootScreen(g_health);

        // Wait for state change or periodic refresh
        uint32_t refreshMs = driver->isPersistent() ? 60000 : 1000;  // E-Ink: 60s, TFT: 1s
        xEventGroupWaitBits(
            g_events, EVT_STATE_CHANGE | EVT_DISPLAY_REFRESH,
            pdTRUE, pdFALSE,
            pdMS_TO_TICKS(refreshMs)
        );
    }

    // --- Dashboard Phase: Live monitoring ---
    Serial.println(F("[DISPLAY] Transitioning to live dashboard"));
    g_health.heartbeat(TaskId::DISPLAY_TASK, TaskState::RUNNING, "Dashboard");

    while (true) {
        LiveData data = g_liveData.snapshot();
        driver->showDashboard(data, g_health);

        // Update heartbeat
        g_health.heartbeat(TaskId::DISPLAY_TASK, TaskState::RUNNING, "Dashboard");

        // Wait for new data or periodic refresh
        // E-Ink: refresh every 5 minutes (low power priority)
        // TFT: refresh every 2 seconds (responsive feel)
        uint32_t refreshMs = driver->isPersistent() ? DISPLAY_REFRESH_INTERVAL_MS : 2000;
        xEventGroupWaitBits(
            g_events, EVT_DISPLAY_REFRESH | EVT_NEW_RECORD,
            pdTRUE, pdFALSE,
            pdMS_TO_TICKS(refreshMs)
        );
    }
}
