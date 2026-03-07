# TODO

Working list of items that are in progress or need to be started.

## BACKLOG

Secrets, credentials, and other sensitive information should be stored in a secure vault, not in this file.
- [ ] Secrets and credentials manager
    - [ ] Create a secrets header file which is excluded from version control and add it to the .gitignore file.
    - [ ] Add WiFi credentials to the secrets header and into the WiFi connection manager code.
    - [ ] Add LoRaWAN keys to the secrets header and into the LoRaWAN connection manager code.
- [ ] Implement sensor reading and storing logic]
    - [ ] Add code to read data from connected sensors and store it in FRAM.
    - [ ] Implement a timestamping mechanism for each sensor reading.
- [ ] Implement LoRaWAN transmission logic]
    - [ ] Add code to transmit sensor data via LoRaWAN at regular intervals.
    - [ ] Implement a backfill strategy to transmit missed records when connectivity is restored.
- [ ] Add WebUI for local configuration and monitoring]
    - [ ] Create a simple web interface to display sensor data and system status.
    - [ ] Add configuration options for WiFi and LoRaWAN settings through the WebUI.

## DONE
- [x] Initial project setup with PlatformIO and CLion
- [x] Add RAK3312 board definition
- [x] Add display selection flags
- [x] Add LoRaWAN region selection flags
- [x] Implement build and upload instructions in the README.md file