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

import $ from "../../ts/jq";

import * as util from "../../ts/util";
import * as API from "../../ts/api";

import { h, render, Fragment } from "preact";
import { __ } from "../../ts/translation";

import { Switch } from "../../ts/components/switch";
import { ConfigComponent } from "src/ts/components/config_component";
import { ConfigForm } from "src/ts/components/config_form";
import { FormRow } from "src/ts/components/form_row";

import timezones from "./timezones";
import { InputSelect } from "src/ts/components/input_select";
import { Button } from "react-bootstrap";
import { InputText } from "src/ts/components/input_text";

type NTPConfig = API.getType['ntp/config'];

export class NTP extends ConfigComponent<'ntp/config'> {
    constructor() {
        super('ntp/config',
              __("ntp.script.save_failed"),
              __("ntp.script.reboot_content_changed"));
    }

    updateTimezone(s: string, i: number) {
        let splt = this.state.timezone.split("/");
        splt[i] = s;
        if (i == 0)
            splt[1] = Object.keys(timezones[splt[0]])[0];
        else if (i == 1) {
            if (timezones[splt[0]][splt[1]] != null)
                splt[2] = Object.keys(timezones[splt[0]][splt[1]])[0];
            else
                splt = [splt[0], splt[1]];
        }

        this.setState({timezone: splt.join("/")});
    }

    render(props: {}, state: Readonly<NTPConfig>) {
        if (!state || Object.keys(state).length == 0)
            return (<></>);

        let splt = state.timezone.split("/");

        return (
            <>
                <ConfigForm id="ntp_config_form"
                            title={__("ntp.content.ntp")}
                            onSave={this.save}
                            onDirtyChange={(d) => this.ignore_updates = d}>
                    <FormRow label={__("ntp.content.enable")}>
                        <Switch desc={__("ntp.content.enable_desc")}
                                checked={state.enable}
                                onClick={this.toggle('enable')}/>
                    </FormRow>

                    <FormRow label={__("ntp.content.use_dhcp")}>
                        <Switch desc={__("ntp.content.use_dhcp_desc")}
                                checked={state.use_dhcp}
                                onClick={this.toggle('use_dhcp')}/>
                    </FormRow>

                    <FormRow label={__("ntp.content.timezone")}>
                        <div class="input-group">
                            <InputSelect
                                required
                                value={splt[0]}
                                onValue={(v) => this.updateTimezone(v, 0)}
                                items={
                                    Object.keys(timezones).map(t => [t, t.replace(/_/g, " ")])
                                }
                            />
                            <InputSelect
                                required
                                value={splt[1]}
                                onValue={(v) => this.updateTimezone(v, 1)}
                                items={
                                    Object.keys(timezones[splt[0]]).map(t => [t, t.replace(/_/g, " ")])
                                }
                            />
                            {
                                timezones[splt[0]][splt[1]] == null ? "" :
                                <InputSelect
                                    required
                                    value={splt[2]}
                                    onValue={(v) => this.updateTimezone(v, 2)}
                                    items={
                                        Object.keys(timezones[splt[0]][splt[1]]).map(t => [t, t.replace(/_/g, " ")])
                                    }
                                />
                            }
                        </div>
                        <br/>
                        <Button variant="primary" className="form-control" onClick={() => this.setState({timezone: Intl.DateTimeFormat().resolvedOptions().timeZone})}>{__("ntp.content.use_browser_timezone")}</Button>
                    </FormRow>

                    <FormRow label={__("ntp.content.server")}>
                        <InputText required
                                   maxLength={64}
                                   value={state.server}
                                   onValue={this.set("server")}/>
                    </FormRow>

                    <FormRow label={__("ntp.content.server2")}>
                        <InputText required
                                   maxLength={64}
                                   value={state.server2}
                                   onValue={this.set("server2")}/>
                    </FormRow>
                </ConfigForm>
            </>
        );
    }
}

render(<NTP/>, $('#ntp')[0])

function update_state() {
    let state = API.get('ntp/state');
    $('#ntp_state_time').html(util.timestamp_min_to_date(state.time, ""));
    util.update_button_group('ntp_state_synced_group', !API.get('ntp/config').enable ? 0 : (state.synced ? 2 : 1))
}

export function init() {

}

export function add_event_listeners(source: API.APIEventTarget) {
    source.addEventListener('ntp/state', update_state);
}

export function update_sidebar_state(module_init: any) {
    $('#sidebar-ntp').prop('hidden', !module_init.ntp);
}
