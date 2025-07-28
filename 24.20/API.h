#pragma once
#include "Utils.h"
#define CURL_STATICLIB
#include <curl.h>

class API
{
public:
    static size_t CallBack(void*, size_t, size_t, std::string*);
    static void SendGet(const std::string&);
    static std::string GetResponse(const std::string&);
};