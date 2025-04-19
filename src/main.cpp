#include <Arduino.h>
#include <NimBLEDevice.h>
#include <nvs_flash.h>
#include <esp_pm.h>
#include <esp_partition.h>
#include <esp_log.h>
#include <esp_random.h>
#include <USB.h>
#include <USBCDC.h>

#define DELAY_IN_S 10
#define REUSE_CYCLES 30

static const char *LOG_TAG = "macless_haystack";

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
        Serial.println("ERROR: Could not find key partition");
        return ESP_FAIL;
    }
    Serial.printf("Found key partition: name=%s, address=0x%x, size=%d\n", 
             keypart->label, keypart->address, keypart->size);
    
    esp_err_t status = esp_partition_read(keypart, offset, dst, size);
    if (status != ESP_OK) {
        Serial.printf("ERROR: Could not read key from partition: %s\n", esp_err_to_name(status));
    } else {
        Serial.printf("Successfully read %d bytes from partition at offset %d\n", size, offset);
    }
    return status;
}

uint8_t get_key_count() {
    uint8_t keyCount[1];
    if (load_bytes_from_partition(keyCount, sizeof(keyCount), 0) != ESP_OK) {
        Serial.println("ERROR: Could not read the key count, stopping.");
        return 0;
    }
    Serial.printf("Found %i keys in key partition\n", keyCount[0]);
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
        Serial.println("ERROR: Advertisement data buffer too small");
        return;
    }
    
    /* copy last 22 bytes */
    memcpy(&payload[7], &public_key[6], 22);
    /* append two bits of public key */
    payload[29] = public_key[0] >> 6;
    
    // Verify the payload length
    Serial.printf("Advertisement payload length: %d bytes\n", payload[0] + 1);
}

// Function to set custom random address for NimBLE
bool set_random_address(uint8_t *address) {
    if (address == NULL) {
        Serial.println("ERROR: Invalid address pointer");
        return false;
    }
    
    // Create a NimBLEAddress from the byte array
    NimBLEAddress nimbleAddr(address, 1); // 1 = RANDOM address type
    
    // Use the internal NimBLE API to set the address
    if (BLE_HS_ECONTROLLER == ble_hs_id_set_rnd(const_cast<uint8_t*>(nimbleAddr.getNative()))) {
        Serial.println("ERROR: Failed to set random address");
        return false;
    }
    
    Serial.printf("Successfully set random address: %s\n", nimbleAddr.toString().c_str());
    return true;
}

void setup() {
    // Initialize serial
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    
    Serial.println("\n\nESP32C3 OpenHaystack starting...");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    Serial.println("NVS initialized");

    // Initialize NimBLE
    NimBLEDevice::init("ESP32C3");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    Serial.println("NimBLE initialized");
    
    // This must be set to use a custom random address
    NimBLEDevice::setOwnAddrType(BLE_ADDR_RANDOM);
    Serial.println("BLE address type set to random");

    // Get key count and start with random index
    key_count = get_key_count();
    if (key_count > 0) {
        Serial.printf("Found %d keys\n", key_count);
        key_index = esp_random() % key_count;
        cycle = 0;
    } else {
        Serial.println("ERROR: No keys found in partition!");
    }
}

void loop() {
    if (key_count == 0) {
        Serial.println("ERROR: No keys available, sleeping...");
        delay(5000);
        return;
    }

    // Load the key
    int address = 1 + (key_index * sizeof(public_key));
    Serial.printf("Loading key with index %d at address %d\n", key_index, address);
    if (load_bytes_from_partition(public_key, sizeof(public_key), address) != ESP_OK) {
        Serial.println("ERROR: Could not read the key, retrying...");
        delay(1000);
        return;
    }

    // Update address and payload with the key
    Serial.printf("Using key with start %02x %02x\n", public_key[0], public_key[1]);
    set_addr_from_key(rnd_addr, public_key);
    set_payload_from_key(adv_data, public_key);
    
    // Set the custom random address for BLE
    if (!set_random_address(rnd_addr)) {
        Serial.println("ERROR: Failed to set custom random address");
        delay(1000);
        return;
    }
    
    // Log the address we're using for debugging
    Serial.printf("Using device address: %02x:%02x:%02x:%02x:%02x:%02x\n", 
             rnd_addr[0], rnd_addr[1], rnd_addr[2], rnd_addr[3], rnd_addr[4], rnd_addr[5]);

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
    
    // Log the manufacturer data
    Serial.printf("Raw manufacturer data length: %d bytes\n", mfgData.length());
    Serial.print("Raw manufacturer data: ");
    for (size_t i = 0; i < mfgData.length(); i++) {
        Serial.printf("%02x ", (uint8_t)mfgData[i]);
    }
    Serial.println();
    
    // Set the manufacturer data
    advData.setManufacturerData(mfgData);
    
    // Log the complete advertisement data
    std::string advDataStr = advData.getPayload();
    Serial.printf("Complete advertisement data length: %d bytes\n", advDataStr.length());
    Serial.print("Complete advertisement data: ");
    for (size_t i = 0; i < advDataStr.length(); i++) {
        Serial.printf("%02x ", (uint8_t)advDataStr[i]);
        if (i > 0 && i % 16 == 0) Serial.println();  // New line every 16 bytes
    }
    Serial.println();
    
    // Log the structure of the advertisement data
    Serial.println("Advertisement data structure:");
    size_t pos = 0;
    while (pos < advDataStr.length()) {
        uint8_t length = advDataStr[pos];
        uint8_t type = advDataStr[pos + 1];
        Serial.printf("  AD Structure: length=%d, type=0x%02x\n", length, type);
        Serial.print("  Data: ");
        for (size_t i = 0; i < length; i++) {
            Serial.printf("%02x ", (uint8_t)advDataStr[pos + 2 + i]);
        }
        Serial.println();
        pos += length + 2;  // Move to next AD structure
    }
    
    // Set the advertisement data
    pAdvertising->setAdvertisementData(advData);
    
    // Configure advertisement parameters
    pAdvertising->setMinInterval(0x0800);  // 1.28s
    pAdvertising->setMaxInterval(0x0800);  // 1.28s
    
    // Configure as non-connectable
    pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
    
    // Enable scan response
    pAdvertising->setScanResponse(true);
    
    // Start advertising
    Serial.printf("Sending beacon (with key index %d)\n", key_index);
    pAdvertising->start();
    delay(1000);  // Advertise for 1 second
    pAdvertising->stop();

    Serial.println("Going to sleep");
    delay(10);
    
    // Update key cycling
    if (cycle >= REUSE_CYCLES) {
        Serial.printf("Max cycles %d are reached. Changing key\n", cycle);
        key_index = (key_index + 1) % key_count;
        cycle = 0;
    } else {
        Serial.printf("Current cycle is %d. Reusing key.\n", cycle);
        cycle++;
    }
    
    // Use delay instead of sleep
    Serial.println("Waiting for next cycle...");
    delay(DELAY_IN_S * 1000);  // Convert seconds to milliseconds
    
    Serial.println("Starting new cycle");
} 