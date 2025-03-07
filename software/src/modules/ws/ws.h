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

#pragma once

#include "api.h"
#include "web_sockets.h"

class WS : public IAPIBackend
{
public:
    WS() : web_sockets() {}
    void pre_setup();
    void setup();
    void register_urls();
    void loop();

    // IAPIBackend implementation
    void addCommand(size_t commandIdx, const CommandRegistration &reg) override;
    void addState(size_t stateIdx, const StateRegistration &reg) override;
    void addRawCommand(size_t rawCommandIdx, const RawCommandRegistration &reg) override;
    bool pushStateUpdate(size_t stateIdx, String payload, String path) override;
    void pushRawStateUpdate(String payload, String path) override;
    void wifiAvailable() override;

    bool initialized = false;

    WebSockets web_sockets;
};
