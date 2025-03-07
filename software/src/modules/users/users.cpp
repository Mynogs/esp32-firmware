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
#include "users.h"

#include "LittleFS.h"

#include "task_scheduler.h"

#include "modules.h"
#include "tools.h"

#include "digest_auth.h"
#include <cmath>

#include <memory>

#define USERNAME_LENGTH 32
#define DISPLAY_NAME_LENGTH 32
#define USERNAME_ENTRY_LENGTH (USERNAME_LENGTH + DISPLAY_NAME_LENGTH)
#define MAX_PASSIVE_USERS 256
#define USERNAME_FILE "/users/all_usernames"

#if MODULE_ESP32_ETHERNET_BRICK_AVAILABLE()
#define MAX_ACTIVE_USERS 16
#else
#define MAX_ACTIVE_USERS 10
#endif

extern TaskScheduler task_scheduler;

// We have to do access the evse/evse_v2 configs manually
// because a lot of the code runs in setup(), i.e. before APIs
// are registered.
void set_data_storage(uint8_t *buf)
{
#if MODULE_EVSE_AVAILABLE()
    tf_evse_set_data_storage(&evse.device, DATA_STORE_PAGE_CHARGE_TRACKER, buf);
#elif MODULE_EVSE_V2_AVAILABLE()
    tf_evse_v2_set_data_storage(&evse_v2.device, DATA_STORE_PAGE_CHARGE_TRACKER, buf);
#endif
}

void get_data_storage(uint8_t *buf)
{
#if MODULE_EVSE_AVAILABLE()
    tf_evse_get_data_storage(&evse.device, DATA_STORE_PAGE_CHARGE_TRACKER, buf);
#elif MODULE_EVSE_V2_AVAILABLE()
    tf_evse_v2_get_data_storage(&evse_v2.device, DATA_STORE_PAGE_CHARGE_TRACKER, buf);
#endif
}

void zero_user_slot_info()
{
    uint8_t buf[63] = {0};
    set_data_storage(buf);
}

uint8_t get_iec_state()
{
#if MODULE_EVSE_AVAILABLE()
    return evse.evse_state.get("iec61851_state")->asUint();
#elif MODULE_EVSE_V2_AVAILABLE()
    return evse_v2.evse_state.get("iec61851_state")->asUint();
#endif
    return 0;
}

uint8_t get_charger_state()
{
#if MODULE_EVSE_AVAILABLE()
    return evse.evse_state.get("charger_state")->asUint();
#elif MODULE_EVSE_V2_AVAILABLE()
    return evse_v2.evse_state.get("charger_state")->asUint();
#endif
    return 0;
}

Config *get_user_slot()
{
#if MODULE_EVSE_AVAILABLE()
    return (Config *)evse.evse_slots.get(CHARGING_SLOT_USER);
#elif MODULE_EVSE_V2_AVAILABLE()
    return (Config *)evse_v2.evse_slots.get(CHARGING_SLOT_USER);
#endif
    return nullptr;
}

Config *get_low_level_state()
{
#if MODULE_EVSE_AVAILABLE()
    return &evse.evse_low_level_state;
#elif MODULE_EVSE_V2_AVAILABLE()
    return &evse_v2.evse_low_level_state;
#endif
    return nullptr;
}

void set_user_current(uint16_t current)
{
#if MODULE_EVSE_AVAILABLE()
    evse.set_user_current(current);
#elif MODULE_EVSE_V2_AVAILABLE()
    evse_v2.set_user_current(current);
#endif
}

float get_energy()
{
    bool meter_avail = meter.state.get("state")->asUint() == 2;
    // If for some reason we decide to use energy_rel here, also update the energy_this_charge calculation in modbus_tcp.cpp
    return !meter_avail ? NAN : meter.values.get("energy_abs")->asFloat();
}

#define USER_SLOT_INFO_VERSION 1
struct UserSlotInfo {
    uint16_t checksum;
    uint8_t version;
    uint8_t user_id;
    uint32_t evse_uptime_on_start;
    uint32_t timestamp_minutes;
    float meter_start;
};

uint16_t calc_checksum(UserSlotInfo info)
{
    uint32_t float_buf = 0;
    memcpy(&float_buf, &info.meter_start, sizeof(float_buf));

    uint32_t checksum = info.checksum
                      + ((((uint32_t)info.version) << 8) | info.user_id)
                      + (info.evse_uptime_on_start >> 16)
                      + (info.evse_uptime_on_start & 0xFF)
                      + (info.timestamp_minutes >> 16)
                      + (info.timestamp_minutes & 0xFF)
                      + (float_buf >> 16)
                      + (float_buf & 0xFF);

    uint32_t carry = checksum >> 16;
    checksum = (checksum & 0xFFFF) + carry;
    checksum = ~checksum;
    return checksum;
}

void write_user_slot_info(uint8_t user_id, uint32_t evse_uptime, uint32_t timestamp_minutes, float meter_start)
{
    UserSlotInfo info;
    info.checksum = 0;
    info.version = USER_SLOT_INFO_VERSION;
    info.user_id = user_id;
    info.evse_uptime_on_start = evse_uptime;
    info.timestamp_minutes = timestamp_minutes;
    info.meter_start = meter_start;

    info.checksum = calc_checksum(info);

    uint8_t buf[63] = {0};
    memcpy(buf, &info, sizeof(info));
    set_data_storage(buf);
}

bool read_user_slot_info(UserSlotInfo *result)
{
    uint8_t buf[63] = {0};
    get_data_storage(buf);

    memcpy(result, buf, sizeof(UserSlotInfo));
    if (calc_checksum(*result) != 0) {
        logger.printfln("Checksum mismatch!");
        return false;
    }

    if (result->version != USER_SLOT_INFO_VERSION)
        logger.printfln("Version mismatch!");

    return result->version == USER_SLOT_INFO_VERSION;
}

volatile bool user_api_blocked = false;

void Users::pre_setup()
{
    user_config = Config::Object({
        {"users", Config::Array(
            {
                Config::Object({
                    {"id", Config::Uint8(0)},
                    {"roles", Config::Uint32(0xFFFFFFFF)},
                    {"current", Config::Uint16(32000)},
                    {"display_name", Config::Str("Anonymous", 0, USERNAME_LENGTH)},
                    {"username", Config::Str("anonymous", 0, USERNAME_LENGTH)},
                    {"digest_hash", Config::Str("", 0, 32)}
                })
            },
            new Config(Config::Object({
                {"id", Config::Uint8(0)},
                {"roles", Config::Uint32(0)},
                {"current", Config::Uint16(32000)},
                {"display_name", Config::Str("", 0, USERNAME_LENGTH)},
                {"username", Config::Str("", 0, USERNAME_LENGTH)},
                {"digest_hash", Config::Str("", 0, 32)},
            })),
            0, MAX_ACTIVE_USERS,
            Config::type_id<Config::ConfObject>()
        )},
        {"next_user_id", Config::Uint8(0)},
        {"http_auth_enabled", Config::Bool(false)}
    });

    add = ConfigRoot(Config::Object({
        {"id", Config::Uint8(0)},
        {"roles", Config::Uint32(0)},
        {"current", Config::Uint16(32000)},
        {"display_name", Config::Str("", 0, USERNAME_LENGTH)},
        {"username", Config::Str("", 0, USERNAME_LENGTH)},
        {"digest_hash", Config::Str("", 0, 32)},
    }), [this](Config &add) -> String {
        if (user_api_blocked) {
            for(int i = 0; i < 50; ++i) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                if (!user_api_blocked)
                    break;
            }
            if (user_api_blocked)
                return "Still applying the last operation. Please retry.";
        }

        if (user_config.get("next_user_id")->asUint() == 0)
            return "Can't add user. All user IDs in use.";

        if (add.get("id")->asUint() != user_config.get("next_user_id")->asUint())
            return "Can't add user. Wrong next user ID";

        if (user_config.get("users")->count() == MAX_ACTIVE_USERS)
            return "Can't add user. Already have the maximum number of active users.";

        for(int i = 0; i < user_config.get("users")->count(); ++i)
            if (user_config.get("users")->get(i)->get("username")->asString() == add.get("username")->asString())
                return "Can't add user. A user with this username already exists.";

        {
            char username[33] = {0};
            File f = LittleFS.open(USERNAME_FILE, "r");
            for(size_t i = 0; i < f.size(); i += USERNAME_ENTRY_LENGTH) {
                f.seek(i);
                f.read((uint8_t *) username, USERNAME_LENGTH);
                if (add.get("username")->asString() == username)
                    return "Can't add user. A user with this username already has tracked charges.";
            }
        }

        user_api_blocked = true;
        return "";
    });
    add.permit_null_updates = false;

    remove = ConfigRoot(Config::Object({
        {"id", Config::Uint8(0)}
    }), [this](Config &remove) -> String {
        if (user_api_blocked) {
            for (int i = 0; i < 50; ++i) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                if (!user_api_blocked)
                    break;
            }
            if (user_api_blocked)
                return "Still applying the last operation. Please retry.";
        }

        if (remove.get("id")->asUint() == 0)
            return "The anonymous user can't be removed.";

        for (int i = 0; i < user_config.get("users")->count(); ++i) {
            if (user_config.get("users")->get(i)->get("id")->asUint() == remove.get("id")->asUint()) {
                user_api_blocked = true;
                return "";
            }
        }

        return "Can't remove user. User with this ID not found.";
    });

    http_auth_update = ConfigRoot(Config::Object({
        {"enabled", Config::Bool(false)}
    }), [this](Config &update) -> String {
        if (!update.get("enabled")->asBool())
            return String("");

        for (int i = 0; i < user_config.get("users")->count(); ++i) {
            if (user_config.get("users")->get(i)->get("digest_hash")->asString() != "")
                return String("");
        }

        return String("Can't enable HTTP authentication if not at least one user with a password is configured!");
    });
}

void create_username_file()
{
    logger.printfln("Recreating users file");
    File f = LittleFS.open(USERNAME_FILE, "w", true);
    const uint8_t buf[512] = {};

    for (int i = 0; i < MAX_PASSIVE_USERS * USERNAME_ENTRY_LENGTH; i += sizeof(buf))
        f.write(buf, sizeof(buf));
}

void Users::setup()
{
    api.restorePersistentConfig("users/config", &user_config);

    if (!LittleFS.exists(USERNAME_FILE)) {
        logger.printfln("Username list does not exist! Recreating now.");
        create_username_file();
        for (int i = 0; i < user_config.get("users")->count(); ++i) {
            Config *user = (Config *)user_config.get("users")->get(i);
            this->rename_user(user->get("id")->asUint(), user->get("username")->asCStr(), user->get("display_name")->asCStr());
        }
    }

    // Next user id is 0 if there is no free user left.
    // After a reboot maybe tracked charges were removed.
    if (user_config.get("next_user_id")->asUint() == 0)
        search_next_free_user();

    bool charge_start_tracked = charge_tracker.currentlyCharging();
    bool charging = get_charger_state() == 2 || get_charger_state() == 3;

    if (charge_start_tracked && !charging) {
        this->stop_charging(0, true);
    }

    if (charging) {
        // If the EVSE is already charging, read back the user slot info, in case the ESP just power cycled.
        UserSlotInfo info;
        bool success = read_user_slot_info(&info);
        if (success) {
            if (!charge_start_tracked) {
                charge_tracker.startCharge(info.timestamp_minutes, info.meter_start, info.user_id, info.evse_uptime_on_start, CHARGE_TRACKER_AUTH_TYPE_LOST, Config::ConfVariant{});
            } else {
                // Don't track a start, but restore the current_charge API anyway.
                charge_tracker.current_charge.get("user_id")->updateInt(info.user_id);
                charge_tracker.current_charge.get("meter_start")->updateFloat(info.meter_start);
                charge_tracker.current_charge.get("evse_uptime_start")->updateUint(info.evse_uptime_on_start);
                charge_tracker.current_charge.get("timestamp_minutes")->updateUint(info.timestamp_minutes);
                charge_tracker.current_charge.get("authorization_type")->updateUint(CHARGE_TRACKER_AUTH_TYPE_LOST);
            }
        } else if (!charge_start_tracked)
            this->start_charging(0, 32000, CHARGE_TRACKER_AUTH_TYPE_NONE, Config::ConfVariant{});
    }

    task_scheduler.scheduleWithFixedDelay([this](){
        static uint8_t last_charger_state = get_charger_state();

        uint8_t charger_state = get_charger_state();
        if (charger_state == last_charger_state)
            return;

        logger.printfln("Charger state changed from %u to %u", last_charger_state, charger_state);
        last_charger_state = charger_state;

        // stop_charging and start_charging will check
        // if a start/stop was already tracked, so it is safe
        // to call those methods more often than needed.
        switch(charger_state) {
            case CHARGER_STATE_NOT_PLUGGED_IN:
                this->stop_charging(0, true);
                break;
            case CHARGER_STATE_WAITING_FOR_RELEASE:
                break;
            case CHARGER_STATE_READY_TO_CHARGE:
            case CHARGER_STATE_CHARGING:
                if (!get_user_slot()->get("active")->asBool())
                    this->start_charging(0, 32000, CHARGE_TRACKER_AUTH_TYPE_NONE, Config::ConfVariant{});
                break;
            case CHARGER_STATE_ERROR:
                break;
        }
    }, 1000, 1000);

    initialized = true;

    if (user_config.get("http_auth_enabled")->asBool()) {
        bool user_with_password_found = false;
        for (int i = 0; i < user_config.get("users")->count(); ++i) {
            if (user_config.get("users")->get(i)->get("digest_hash")->asString() != "") {
                user_with_password_found = true;
                break;
            }
        }

        if (!user_with_password_found) {
            logger.printfln("Web interface authentication can not be enabled: No user with set password found.");
            return;
        }

        server.setAuthentication([this](WebServerRequest req) -> bool {
            String auth = req.header("Authorization");
            if (auth == "") {
                return false;
            }

            if (!auth.startsWith("Digest ")) {
                return false;
            }

            auth = auth.substring(7);
            AuthFields fields = parseDigestAuth(auth.c_str());

            for (int i = 0; i < user_config.get("users")->count(); ++i) {
                if (user_config.get("users")->get(i)->get("username")->asString().equals(fields.username))
                    return checkDigestAuthentication(fields, req.methodString(), fields.username.c_str(), user_config.get("users")->get(i)->get("digest_hash")->asCStr(), nullptr, true, nullptr, nullptr, nullptr);
            }

            return false;
        });

        logger.printfln("Web interface authentication enabled.");
    }
}

void Users::search_next_free_user() {
    uint8_t user_id = user_config.get("next_user_id")->asUint();
    uint8_t start_uid = user_id;
    user_id++;
    {
        File f = LittleFS.open(USERNAME_FILE, "r+");
        while(start_uid != user_id) {
            if (user_id == 0)
                user_id++;
            f.seek(user_id * USERNAME_ENTRY_LENGTH, SeekMode::SeekSet);
            char user_name_byte = 0;
            f.readBytes(&user_name_byte, 1);
            if (user_name_byte == '\0')
                break;
            user_id++;
        };
    }
    if (user_id == start_uid)
        user_id = 0;

    user_config.get("next_user_id")->updateUint(user_id);
}

void Users::register_urls()
{
    api.addRawCommand("users/modify", [this](char *c, size_t s) -> String {
        if (user_api_blocked) {
            for(int i = 0; i < 50; ++i) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                if (!user_api_blocked)
                    break;
            }
            if (user_api_blocked)
                return "Still applying the last operation. Please retry.";
        }
        user_api_blocked = true;

        StaticJsonDocument<96> doc;

        DeserializationError error = deserializeJson(doc, c, s);

        if (error) {
            return String("Failed to deserialize string: ") + String(error.c_str());
        }

        if (doc["id"] == nullptr)
            return String("Can't modify user. User ID is null or missing.");

        uint8_t id = doc["id"].as<uint8_t>();
        if (id == 0) {
            if (doc["username"] != nullptr)
                return String("Username needs to be empty.");
            if (doc["roles"] != nullptr)
                return String("Roles need to be empty.");
            if (doc["current"] != nullptr)
                return String("Current needs to be empty.");
            if (doc["digest_hash"] != nullptr)
                return String("Digest_hash needs to be empty.");
        }

        Config *user = nullptr;
        for(int i = 0; i < user_config.get("users")->count(); ++i) {
            if (user_config.get("users")->get(i)->get("id")->asUint() == id) {
                user = (Config *)user_config.get("users")->get(i);
                break;
            }
        }

        if (user == nullptr) {
            return "Can't modify user. User with this ID not found.";
        }

        for(int i = 0; i < user_config.get("users")->count(); ++i) {
            if (user_config.get("users")->get(i)->get("id")->asUint() == id)
                continue;

            if (user_config.get("users")->get(i)->get("username")->asString() == doc["username"]) {
                return "Can't modify user. Another user with the same username already exists.";
            }
        }

        char username[33] = {0};
        File f = LittleFS.open(USERNAME_FILE, "r");
        for(size_t i = 0; i < f.size(); i += USERNAME_ENTRY_LENGTH) {
            if ((i / USERNAME_ENTRY_LENGTH) == id)
                continue;

            f.seek(i);
            f.read((uint8_t *) username, USERNAME_LENGTH);
            if (doc["username"].as<String>() == username)
                return "Can't modify user. A user with this username already has tracked charges.";
        }

        if (doc["roles"] != nullptr)
            user->get("roles")->updateUint((uint32_t) doc["roles"]);

        bool display_name_changed = false;
        if (doc["display_name"] != nullptr)
            display_name_changed = user->get("display_name")->updateString(doc["display_name"]);

        bool username_changed = false;
        if (doc["username"] != nullptr)
            username_changed = user->get("username")->updateString(doc["username"]);

        if (doc["current"] != nullptr)
            user->get("current")->updateUint((uint32_t) doc["current"]);

        if (doc["digest_hash"] != nullptr)
            user->get("digest_hash")->updateString(doc["digest_hash"]);

        String err = this->user_config.validate();
        if (err != "")
            return err;

        task_scheduler.scheduleOnce([this, display_name_changed, username_changed, user](){
            API::writeConfig("users/config", &user_config);

            if (display_name_changed || username_changed)
                this->rename_user(user->get("id")->asUint(), user->get("username")->asCStr(), user->get("display_name")->asCStr());

            user_api_blocked = false;
        }, 0);

        return "";
    }, true);

    api.addState("users/config", &user_config, {"digest_hash"}, 1000);
    api.addCommand("users/add", &add, {"digest_hash"}, [this](){
        user_config.get("users")->add();
        Config *user = (Config *)user_config.get("users")->get(user_config.get("users")->count() - 1);

        user->get("id")->updateUint(add.get("id")->asUint());
        user->get("roles")->updateUint(add.get("roles")->asUint());
        user->get("current")->updateUint(add.get("current")->asUint());
        user->get("display_name")->updateString(add.get("display_name")->asString());
        user->get("username")->updateString(add.get("username")->asString());
        user->get("digest_hash")->updateString(add.get("digest_hash")->asCStr());

        search_next_free_user();

        API::writeConfig("users/config", &user_config);
        this->rename_user(user->get("id")->asUint(), user->get("username")->asCStr(), user->get("display_name")->asCStr());
        user_api_blocked = false;
    }, true);

    api.addCommand("users/remove", &remove, {}, [this](){
        int idx = -1;
        for(int i = 0; i < user_config.get("users")->count(); ++i) {
            if (user_config.get("users")->get(i)->get("id")->asUint() == remove.get("id")->asUint()) {
                idx = i;
                break;
            }
        }

        if (idx < 0) {
            logger.printfln("Can't remove user. User with this ID not found.");
            return;
        }

        user_config.get("users")->remove(idx);
        API::writeConfig("users/config", &user_config);

        Config *tags = (Config *)nfc.config.get("authorized_tags");

        for(int i = 0; i < tags->count(); ++i) {
            if(tags->get(i)->get("user_id")->asUint() == remove.get("id")->asUint())
                tags->get(i)->get("user_id")->updateUint(0);
        }
        API::writeConfig("nfc/config", &nfc.config);

        if (!charge_tracker.is_user_tracked(remove.get("id")->asUint()))
        {
            this->rename_user(remove.get("id")->asUint(), "", "");
            if (user_config.get("next_user_id")->asUint() == 0)
            {
                user_config.get("next_user_id")->updateUint(remove.get("id")->asUint());
                API::writeConfig("users/config", &user_config);
            }
        }

        user_api_blocked = false;
    }, true);


    api.addCommand("users/http_auth_update", &http_auth_update, {}, [this](){
        bool enable = http_auth_update.get("enabled")->asBool();
        if (!enable)
            server.setAuthentication([](WebServerRequest req){return true;});

        user_config.get("http_auth_enabled")->updateBool(enable);
        API::writeConfig("users/config", &user_config);
    }, false);

    server.on("/users/all_usernames", HTTP_GET, [this](WebServerRequest request) {
        //std::lock_guard<std::mutex> lock{records_mutex};
        size_t len = MAX_PASSIVE_USERS * USERNAME_ENTRY_LENGTH;
        auto buf = std::unique_ptr<char[]>(new char[len]);
        if (buf == nullptr) {
            return request.send(507);
        }

        File f = LittleFS.open(USERNAME_FILE, "r");

        size_t read = f.read((uint8_t *)buf.get(), len);
        return request.send(200, "application/octet-stream", buf.get(), read);
    });
}

void Users::loop()
{
}

uint8_t Users::next_user_id()
{
    return this->user_config.get("next_user_id")->asUint();
}

void Users::rename_user(uint8_t user_id, const char *username, const char *display_name)
{
    char buf[USERNAME_ENTRY_LENGTH] = {0};
    snprintf(buf, USERNAME_LENGTH, "%s", username);
    snprintf(buf + USERNAME_LENGTH, DISPLAY_NAME_LENGTH, "%s", display_name);

    File f = LittleFS.open(USERNAME_FILE, "r+");
    f.seek(user_id * USERNAME_ENTRY_LENGTH, SeekMode::SeekSet);
    f.write((const uint8_t *)buf, USERNAME_ENTRY_LENGTH);
}

void Users::remove_from_username_file(uint8_t user_id)
{
    Config *users = (Config *)user_config.get("users");
    for (int i = 0; i < users->count(); ++i) {
        if (users->get(i)->get("id")->asUint() == user_id) {
            return;
        }
    }

    this->rename_user(user_id, "", "");
    if (user_config.get("next_user_id")->asUint() == 0)
    {
        user_config.get("next_user_id")->updateUint(user_id);
        API::writeConfig("users/config", &user_config);
    }
}

// Only returns true if the triggered action was a charge start.
bool Users::trigger_charge_action(uint8_t user_id, uint8_t auth_type, Config::ConfVariant auth_info, int action)
{
    bool user_enabled = get_user_slot()->get("active")->asBool();
    if (!user_enabled)
        return false;
    // This is called whenever a user wants to trigger a charge action.
    // I.e. when holding an NFC tag at the box or when calling the start_charging API

    uint16_t current_limit = 0;
    Config *users = (Config *)user_config.get("users");
    for (int i = 0; i < users->count(); ++i) {
        if (users->get(i)->get("id")->asUint() != user_id)
            continue;

        current_limit = users->get(i)->get("current")->asUint();
    }

    if (current_limit == 0) {
        logger.printfln("Unknown user with ID %u.", user_id);
        return false;
    }

    uint8_t iec_state = get_iec_state();
    uint32_t tscs = get_low_level_state()->get("time_since_state_change")->asUint();

    switch (iec_state) {
        case IEC_STATE_B: // State B: The user wants to start charging. If we already have a tracked charge, stop charging to allow switching to another user.
            if (charge_tracker.currentlyCharging()) {
                if (action == TRIGGER_CHARGE_ANY || action == TRIGGER_CHARGE_STOP)
                    this->stop_charging(user_id, false);
                return false;
            }
            if (action == TRIGGER_CHARGE_ANY || action == TRIGGER_CHARGE_START)
                return this->start_charging(user_id, current_limit, auth_type, auth_info);
            return false;
        case IEC_STATE_C: // State C: The user wants to stop charging.
            // Debounce here a bit, an impatient user can otherwise accidentially trigger a stop if a start_charging takes too long.
            if (tscs > 3000 && (action == TRIGGER_CHARGE_ANY || action == TRIGGER_CHARGE_STOP))
                this->stop_charging(user_id, false);
            return false;
        default: //Don't do anything in state A, D, and E/F
            break;
    }
    return false;
}

void Users::remove_username_file()
{
    if (LittleFS.exists(USERNAME_FILE))
        LittleFS.remove(USERNAME_FILE);
}

bool Users::start_charging(uint8_t user_id, uint16_t current_limit, uint8_t auth_type, Config::ConfVariant auth_info)
{
    if (charge_tracker.currentlyCharging())
        return false;

    uint32_t evse_uptime = get_low_level_state()->get("uptime")->asUint();
    float meter_start = get_energy();
    uint32_t timestamp = timestamp_minutes();

    write_user_slot_info(user_id, evse_uptime, timestamp, meter_start);
    charge_tracker.startCharge(timestamp, meter_start, user_id, evse_uptime, auth_type, auth_info);

    set_user_current(current_limit);

    return true;
}

bool Users::stop_charging(uint8_t user_id, bool force)
{
    if (charge_tracker.currentlyCharging()) {
        UserSlotInfo info;
        bool success = read_user_slot_info(&info);
        // If reading the user slot info failed, we don't know which user started this charge anymore.
        // This should only happen if the EVSE power-cycles, however on a power-cycle any running charge
        // should be aborted. It is safe to allow tracking a charge end in this case for any authorized card,
        // as this should never happen anyway.
        // Allow forcing the endCharge tracking. This is necessary in the case that the car was disconnected.
        // The user is then authorized at the other end of the charging cable.
        if (!force && success && info.user_id != user_id)
            return false;

        uint32_t charge_duration = 0;
        if (success) {
            uint32_t now_seconds = get_low_level_state()->get("uptime")->asUint() / 1000;
            uint32_t start_seconds = info.evse_uptime_on_start / 1000;
            if (now_seconds < start_seconds) {
                now_seconds += (0xFFFFFFFF / 1000);
            }
            charge_duration = now_seconds - start_seconds;
        }

        charge_tracker.endCharge(charge_duration, get_energy());
    }

    zero_user_slot_info();
    set_user_current(0);

    return true;
}
