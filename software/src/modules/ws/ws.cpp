/* esp32-firmware
 * Copyright (C) 2020-2021 Erik Fleckstein <erik@tinkerforge.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "ws.h"

#include <esp_http_server.h>

#include "task_scheduler.h"
#include "web_server.h"

extern TaskScheduler task_scheduler;
extern WebServer server;
extern API api;

void WS::pre_setup()
{
    api.registerBackend(this);
}

void WS::setup()
{
    initialized = true;
}

void WS::register_urls()
{
    web_sockets.onConnect([this](WebSocketsClient client) {
        String to_send = "";
        for (auto &reg : api.states) {
            to_send += String("{\"topic\":\"") + reg.path + String("\",\"payload\":") + reg.config->to_string_except(reg.keys_to_censor) + String("}\n");
        }
        client.send(to_send.c_str(), to_send.length());
    });

    web_sockets.start("/ws");

    task_scheduler.scheduleWithFixedDelay([this](){
        const char *payload = "{\"topic\": \"keep-alive\", \"payload\": \"null\"}\n";
        web_sockets.sendToAll(payload, strlen(payload));
    }, 1000, 1000);
}

void WS::loop()
{
}

void WS::addCommand(size_t commandIdx, const CommandRegistration &reg)
{
}

void WS::addState(size_t stateIdx, const StateRegistration &reg)
{
}

void WS::addRawCommand(size_t rawCommandIdx, const RawCommandRegistration &reg)
{
}

static const char *prefix = "{\"topic\":\"";
static const char *infix = "\",\"payload\":";
static const char *suffix = "}\n";
static size_t prefix_len = strlen(prefix);
static size_t infix_len = strlen(infix);
static size_t suffix_len = strlen(suffix);

bool WS::pushStateUpdate(size_t stateIdx, String payload, String path)
{
    if (!web_sockets.haveActiveClient())
        return true;
    //String to_send = String("{\"topic\":\"") + path + String("\",\"payload\":") + payload + String("}\n");
    size_t path_len = path.length();
    size_t payload_len = payload.length();

    size_t to_send_len = prefix_len + path_len + infix_len + payload_len + suffix_len;
    char *to_send = (char *)malloc(to_send_len);
    if (to_send == nullptr)
        return false;

    char *ptr = to_send;
    memcpy(ptr, prefix, prefix_len);
    ptr += prefix_len;

    memcpy(ptr, path.c_str(), path_len);
    ptr += path_len;

    memcpy(ptr, infix, infix_len);
    ptr += infix_len;

    memcpy(ptr, payload.c_str(), payload_len);
    ptr += payload_len;

    memcpy(ptr, suffix, suffix_len);
    ptr += suffix_len;

    web_sockets.sendToAllOwned(to_send, to_send_len);

    return true;
}

void WS::pushRawStateUpdate(String payload, String path)
{
    pushStateUpdate(0, payload, path);
}

void WS::wifiAvailable()
{
}
