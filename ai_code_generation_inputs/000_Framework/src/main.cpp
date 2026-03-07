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
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);  // 400 kHz fast mode
    Serial.println(F("[BOOT] I2C bus initialised (400 kHz)"));

    // --- SPI Bus (for FRAM and Display) -------------------------------------
    SPI.begin(PIN_SPI_CLK, PIN_SPI_MISO, PIN_SPI_MOSI);
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
            if (addr == FT6336_I2C_ADDR)    Serial.print(" (FT6336 Touch)");
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

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));  // Check every 2 seconds

        uint32_t now = millis();
        bool allRunning = true;
        bool anyError = false;

        for (uint8_t i = 0; i < static_cast<uint8_t>(TaskId::TASK_COUNT); i++) {
            TaskId id = static_cast<TaskId>(i);
            auto& t = g_health.tasks[i];

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
    g_health.heartbeat(TaskId::SENSOR, TaskState::INITIALISING, "Starting sensors...");
    Serial.println(F("[SENSOR] Task starting"));

    // TODO: Initialise BME680, VEML7700, PMSA003I, GNSS, RTC, FRAM
    // Each sensor init should be wrapped in error handling:
    //   if (!bme680.begin()) { log error; mark gas as unavailable; }
    //   but continue with other sensors

    g_health.heartbeat(TaskId::SENSOR, TaskState::WARMING_UP, "Sensors warming up...");

    uint32_t warmupStart = millis();
    uint32_t lastCollectionMs = 0;

    while (true) {
        uint32_t now = millis();

        // --- Continuous sensor reads (every loop iteration) -----------------
        // TODO: Parse GNSS NMEA from Serial1
        // TODO: Read PMSA003I if data available
        // TODO: Run BME680 forced measurement cycle

        // --- Check warm-up status -------------------------------------------
        bool pmReady = (now - warmupStart) >= WARMUP_PMSA003I_MS;
        // bool gnssReady = gnss.fixValid;
        // bool gasStable = (now - warmupStart) >= WARMUP_BME680_GAS_MS;

        // Update status message for supervisor/display
        if (!pmReady) {
            uint32_t remaining = (WARMUP_PMSA003I_MS - (now - warmupStart)) / 1000;
            char msg[48];
            snprintf(msg, sizeof(msg), "PM warming up (%lus)", remaining);
            g_health.heartbeat(TaskId::SENSOR, TaskState::WARMING_UP, msg);
        } else {
            g_health.heartbeat(TaskId::SENSOR, TaskState::RUNNING, "All sensors nominal");
        }

        // --- Collection cycle (every SENSOR_INTERVAL_MS) --------------------
        if (now - lastCollectionMs >= SENSOR_INTERVAL_MS) {
            lastCollectionMs = now;

            // TODO: Read RTC timestamp
            // TODO: Snapshot all cached readings into SensorRecord
            // TODO: Write record to FRAM ring buffer
            // TODO: Update LiveDataCache (g_liveData.update(...))

            // Signal other tasks
            xEventGroupSetBits(g_events, EVT_NEW_RECORD);

            Serial.println(F("[SENSOR] Collection cycle complete"));
        }

        vTaskDelay(pdMS_TO_TICKS(250));  // ~4 Hz inner loop
    }
}

void loraTaskFn(void* param) {
    g_health.heartbeat(TaskId::LORA, TaskState::INITIALISING, "LoRa init...");
    Serial.println(F("[LORA] Task starting"));

    // TODO: Initialise SX1262 via LMIC
    // TODO: OTAA join attempt
    // TODO: Update heartbeat with join status

    g_health.heartbeat(TaskId::LORA, TaskState::WARMING_UP, "Joining network...");

    while (true) {
        // Wait for new data or timeout
        EventBits_t bits = xEventGroupWaitBits(
            g_events, EVT_NEW_RECORD,
            pdTRUE,     // clear bits on exit
            pdFALSE,    // any bit
            pdMS_TO_TICKS(30000)  // check in at least every 30s
        );

        g_health.heartbeat(TaskId::LORA, TaskState::RUNNING, "Idle");

        // TODO: Check if enough time has elapsed since last TX
        // TODO: If so, read FRAM header to find lastLoRaSendIndex
        // TODO: Calculate how many records have been written since then
        // TODO: Pack current record as primary payload
        // TODO: If missed records exist, pack up to LORAWAN_MAX_BACKFILL
        //       as additional payloads (or a single larger payload with
        //       a backfill indicator byte)
        // TODO: Transmit via LMIC
        // TODO: On TX_COMPLETE callback, update FRAM header with new
        //       lastLoRaSendIndex and lastLoRaSendTime

        // Backfill strategy:
        //   1. Read FRAM header → lastLoRaSendIndex
        //   2. Read FRAM header → current writeIndex
        //   3. Calculate gap: missed = (writeIndex - lastLoRaSendIndex) % FRAM_MAX_RECORDS
        //   4. If missed > 0, send current + min(missed, LORAWAN_MAX_BACKFILL)
        //   5. Each TX carries a sequence counter so the backend knows ordering
        //   6. On successful ACK, advance lastLoRaSendIndex by records sent

        Serial.println(F("[LORA] Heartbeat"));
    }
}

void webServerTaskFn(void* param) {
    g_health.heartbeat(TaskId::WEBSERVER, TaskState::INITIALISING, "WiFi init...");
    Serial.println(F("[WEBSERVER] Task starting"));

    // TODO: Try STA mode first using stored credentials from NVS
    // TODO: If STA fails after WIFI_AP_FALLBACK_MS, start AP mode
    // TODO: Create AsyncWebServer on WEB_SERVER_PORT
    // TODO: Register routes:
    //   GET /           → serve index.html from LittleFS
    //   GET /api/live   → g_liveData.snapshot() as JSON
    //   GET /api/history?hours=N → read FRAM, downsample, return JSON
    //   GET /api/status → system health as JSON
    //   GET /api/config → current config as JSON
    //   POST /api/config → update config, save to NVS
    // TODO: Start captive portal DNS in AP mode

    g_health.heartbeat(TaskId::WEBSERVER, TaskState::WARMING_UP, "Connecting WiFi...");

    while (true) {
        // The AsyncWebServer handles requests via callbacks on Core 1.
        // This loop just monitors WiFi health and heartbeats.

        // TODO: Check WiFi.status(), reconnect if dropped
        // TODO: Update heartbeat with connection info

        g_health.heartbeat(TaskId::WEBSERVER, TaskState::RUNNING, "WiFi connected");

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void displayTaskFn(void* param) {
    g_health.heartbeat(TaskId::DISPLAY, TaskState::INITIALISING, "Display init...");
    Serial.println(F("[DISPLAY] Task starting"));

    // TODO: Instantiate the correct DisplayDriver based on build flag:
    //   #if DISPLAY_EINK
    //       EInkDisplay driver;
    //   #elif DISPLAY_TFT
    //       TftDisplay driver;
    //   #endif
    //   driver.init();

    // --- Boot screen phase ---
    // While system is BOOTING or WARMING_UP, repeatedly update boot screen
    while (g_health.systemState == SystemState::BOOTING ||
           g_health.systemState == SystemState::WARMING_UP) {

        // TODO: driver.showBootScreen(g_health);
        Serial.println(F("[DISPLAY] Boot screen update"));

        g_health.heartbeat(TaskId::DISPLAY, TaskState::WARMING_UP, "Boot screen");

        // Wait for state change or periodic refresh
        xEventGroupWaitBits(
            g_events, EVT_STATE_CHANGE | EVT_DISPLAY_REFRESH,
            pdTRUE, pdFALSE,
            pdMS_TO_TICKS(2000)  // refresh boot screen every 2s
        );
    }

    // --- Live dashboard phase ---
    Serial.println(F("[DISPLAY] Transitioning to live dashboard"));

    while (true) {
        LiveData data = g_liveData.snapshot();

        // TODO: driver.showDashboard(data, g_health);

        g_health.heartbeat(TaskId::DISPLAY, TaskState::RUNNING, "Dashboard");

        // Wait for event or periodic refresh
        // E-Ink: refresh every 5 minutes (slow, persistent)
        // TFT: refresh every 2-5 seconds (fast, volatile)
        xEventGroupWaitBits(
            g_events, EVT_DISPLAY_REFRESH | EVT_NEW_RECORD,
            pdTRUE, pdFALSE,
            pdMS_TO_TICKS(DISPLAY_REFRESH_INTERVAL_MS)
        );
    }
}
