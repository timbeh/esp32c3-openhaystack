#include <Arduino.h>
#include <NimBLEDevice.h>
#include <nvs_flash.h>
#include <esp_pm.h>
#include <esp_partition.h>
#include <esp_log.h>
#include <esp_random.h>
#include <USB.h>
#include <USBCDC.h>
#include <esp_sleep.h>

#define DELAY_IN_S 10
#define REUSE_CYCLES 30
#define ADV_DURATION_MS 20  // Reduced from 1000ms to 20ms
#define MIN_ADV_INTERVAL 0x0800  // 1.28s
#define MAX_ADV_INTERVAL 0x0800  // 1.28s
#define DEBUG_ENABLED false  // Set to true for debugging, false for production

// Debug logging macro
#if DEBUG_ENABLED
#define DEBUG_LOG(...) Serial.printf(__VA_ARGS__)
#define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define DEBUG_LOG(...)
#define DEBUG_PRINT(...)
#define DEBUG_PRINTLN(...)
#endif

// Error logging (always enabled)
#define ERROR_LOG(...) Serial.printf(__VA_ARGS__)
#define ERROR_PRINT(...) Serial.print(__VA_ARGS__)
#define ERROR_PRINTLN(...) Serial.println(__VA_ARGS__)

/** Random device address */
static uint8_t rnd_addr[6] = {0xFF, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

/** Advertisement payload */
static uint8_t adv_data[31] = {
    0x1e,       /* Length (30) */
    0xff,       /* Manufacturer Specific Data (type 0xff) */
    0x4c, 0x00, /* Company ID (Apple) */
    0x12, 0x19, /* Offline Finding type and length */
    0x00,       /* State */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, /* First two bits */
    0x00  /* Hint (0x00) */
};

static uint8_t public_key[28];
static uint8_t key_index = 0;
static uint8_t cycle = 0;
static uint8_t key_count = 0;

int load_bytes_from_partition(uint8_t *dst, size_t size, int offset) {
    const esp_partition_t *keypart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, "key");
    if (keypart == NULL) {
        ERROR_PRINTLN("ERROR: Could not find key partition");
        return ESP_FAIL;
    }
    DEBUG_LOG("Found key partition: name=%s, address=0x%x, size=%d\n", 
             keypart->label, keypart->address, keypart->size);
    
    esp_err_t status = esp_partition_read(keypart, offset, dst, size);
    if (status != ESP_OK) {
        ERROR_LOG("ERROR: Could not read key from partition: %s\n", esp_err_to_name(status));
    } else {
        DEBUG_LOG("Successfully read %d bytes from partition at offset %d\n", size, offset);
    }
    return status;
}

uint8_t get_key_count() {
    uint8_t keyCount[1];
    if (load_bytes_from_partition(keyCount, sizeof(keyCount), 0) != ESP_OK) {
        ERROR_PRINTLN("ERROR: Could not read the key count, stopping.");
        return 0;
    }
    DEBUG_LOG("Found %i keys in key partition\n", keyCount[0]);
    return keyCount[0];
}

void set_addr_from_key(uint8_t *addr, uint8_t *public_key) {
    addr[0] = public_key[0] | 0b11000000;
    addr[1] = public_key[1];
    addr[2] = public_key[2];
    addr[3] = public_key[3];
    addr[4] = public_key[4];
    addr[5] = public_key[5];
}

void set_payload_from_key(uint8_t *payload, uint8_t *public_key) {
    // Ensure we don't exceed the payload length
    if (sizeof(adv_data) < 31) {
        ERROR_PRINTLN("ERROR: Advertisement data buffer too small");
        return;
    }
    
    /* copy last 22 bytes */
    memcpy(&payload[7], &public_key[6], 22);
    /* append two bits of public key */
    payload[29] = public_key[0] >> 6;
    
    // Verify the payload length
    DEBUG_LOG("Advertisement payload length: %d bytes\n", payload[0] + 1);
}

// Function to set custom random address for NimBLE
bool set_random_address(uint8_t *address) {
    if (address == NULL) {
        ERROR_PRINTLN("ERROR: Invalid address pointer");
        return false;
    }
    
    // Create a NimBLEAddress from the byte array
    NimBLEAddress nimbleAddr(address, 1); // 1 = RANDOM address type
    
    // Use the internal NimBLE API to set the address
    if (BLE_HS_ECONTROLLER == ble_hs_id_set_rnd(const_cast<uint8_t*>(nimbleAddr.getNative()))) {
        ERROR_PRINTLN("ERROR: Failed to set random address");
        return false;
    }
    
    DEBUG_LOG("Successfully set random address: %s\n", nimbleAddr.toString().c_str());
    return true;
}

void setup() {
    // Initialize serial with reduced baud rate for power saving
    Serial.begin(9600);
    while (!Serial) {
        delay(10);
    }
    
    DEBUG_PRINTLN("\n\nESP32C3 OpenHaystack starting...");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    DEBUG_PRINTLN("NVS initialized");

    // Configure power management
    esp_pm_config_esp32c3_t pm_config = {
        .max_freq_mhz = 160,  // Maximum CPU frequency
        .min_freq_mhz = 10,   // Minimum CPU frequency
        .light_sleep_enable = true  // Enable light sleep
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
    DEBUG_PRINTLN("Power management configured");

    // Initialize NimBLE with reduced power
    NimBLEDevice::init("ESP32C3");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    DEBUG_PRINTLN("NimBLE initialized");
    
    // This must be set to use a custom random address
    NimBLEDevice::setOwnAddrType(BLE_ADDR_RANDOM);
    DEBUG_PRINTLN("BLE address type set to random");

    // Get key count and start with random index
    key_count = get_key_count();
    if (key_count > 0) {
        DEBUG_LOG("Found %d keys\n", key_count);
        key_index = esp_random() % key_count;
        cycle = 0;
    } else {
        ERROR_PRINTLN("ERROR: No keys found in partition!");
    }
}

void loop() {
    if (key_count == 0) {
        ERROR_PRINTLN("ERROR: No keys available, sleeping...");
        esp_sleep_enable_timer_wakeup(5000000);  // 5 seconds
        esp_light_sleep_start();
        return;
    }

    // Load the key
    int address = 1 + (key_index * sizeof(public_key));
    if (load_bytes_from_partition(public_key, sizeof(public_key), address) != ESP_OK) {
        ERROR_PRINTLN("ERROR: Could not read the key, retrying...");
        esp_sleep_enable_timer_wakeup(1000000);  // 1 second
        esp_light_sleep_start();
        return;
    }

    // Update address and payload with the key
    set_addr_from_key(rnd_addr, public_key);
    set_payload_from_key(adv_data, public_key);
    
    // Set the custom random address for BLE
    if (!set_random_address(rnd_addr)) {
        ERROR_PRINTLN("ERROR: Failed to set custom random address");
        esp_sleep_enable_timer_wakeup(1000000);  // 1 second
        esp_light_sleep_start();
        return;
    }

    // Get advertising instance directly from NimBLEDevice
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();

    // Create the advertisement data
    NimBLEAdvertisementData advData;
    
    // Create manufacturer data
    std::string mfgData;
    mfgData.push_back(0x4c);  // Apple company ID (LSB)
    mfgData.push_back(0x00);  // Apple company ID (MSB)
    mfgData.push_back(0x12);  // Offline Finding type
    mfgData.push_back(0x19);  // Offline Finding length
    mfgData.push_back(0x00);  // State
    
    // Add the public key
    for (int i = 6; i < 28; i++) {
        mfgData.push_back(public_key[i]);
    }
    
    // Add only the first two bits (remove the hint byte)
    mfgData.push_back(public_key[0] >> 6);
    
    // Set the manufacturer data
    advData.setManufacturerData(mfgData);
    
    // Set the advertisement data
    pAdvertising->setAdvertisementData(advData);
    
    // Configure advertisement parameters
    pAdvertising->setMinInterval(MIN_ADV_INTERVAL);
    pAdvertising->setMaxInterval(MAX_ADV_INTERVAL);
    
    // Configure as non-connectable
    pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
    
    // Enable scan response
    pAdvertising->setScanResponse(true);
    
    // Start advertising
    pAdvertising->start();
    delay(ADV_DURATION_MS);  // Advertise for a short duration
    pAdvertising->stop();

    // Update key cycling
    if (cycle >= REUSE_CYCLES) {
        key_index = (key_index + 1) % key_count;
        cycle = 0;
    } else {
        cycle++;
    }
    
    // Use light sleep instead of delay
    esp_sleep_enable_timer_wakeup(DELAY_IN_S * 1000000);  // Convert seconds to microseconds
    esp_light_sleep_start();
} 