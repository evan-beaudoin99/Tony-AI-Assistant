#pragma once

// Copy this file to secrets.h (same folder) and fill it in. secrets.h is git-ignored.

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// The computer running the Tony server. `python -m tony` prints this address when it starts.
#define TONY_SERVER_HOST "192.168.1.50"
#define TONY_SERVER_PORT 8765

// Must match DEVICE_TOKEN in server/.env. Letters and digits only.
#define TONY_DEVICE_TOKEN "change-me"
