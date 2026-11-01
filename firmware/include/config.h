#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h is missing: copy include/secrets.example.h to include/secrets.h and fill it in."
#include "secrets.example.h"
#endif

#define TONY_FW_VERSION "1.0.0"
