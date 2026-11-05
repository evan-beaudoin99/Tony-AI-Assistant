#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h is missing: copy include/secrets.example.h to include/secrets.h and fill it in."
#include "secrets.example.h"
#endif

// How Tony's body talks to the server. Flip this and re-upload:
//   false  WiFi: the networks and server addresses in secrets.h.
//   true   USB: over the cable on the USB-UART port. On the laptop, run `python -m tony` as usual and
//          `python -m tony.usb_bridge` in a second terminal. WiFi stays off.
#define TONY_USE_USB true
#define TONY_USB_BAUD 921600  // must match the bridge's --baud

// Optional backups, tried when the main WiFi or server can't be reached. Leave a name empty ("") to skip it.
#ifndef WIFI_SSID_BACKUP
#define WIFI_SSID_BACKUP ""
#endif
#ifndef WIFI_PASSWORD_BACKUP
#define WIFI_PASSWORD_BACKUP ""
#endif
#ifndef TONY_SERVER_HOST_BACKUP
#define TONY_SERVER_HOST_BACKUP ""
#endif
#ifndef TONY_SERVER_PORT_BACKUP
#define TONY_SERVER_PORT_BACKUP TONY_SERVER_PORT
#endif

#define TONY_FW_VERSION "1.0.0"
