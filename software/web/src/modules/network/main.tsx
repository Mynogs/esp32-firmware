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
import { InputText } from "src/ts/components/input_text";

type NetworkConfig = API.getType['network/config'];

export class Network extends ConfigComponent<'network/config'> {
    constructor() {
        super('network/config',
              __("network.script.save_failed"),
              __("network.script.reboot_content_changed"));
    }

    render(props: {}, state: Readonly<NetworkConfig>) {
        if (!state)
            return (<></>);

        return (
            <>
                <ConfigForm id="network_config_form"
                            title={__("network.content.network")}
                            onSave={this.save}
                            onDirtyChange={(d) => this.ignore_updates = d}>
                    <FormRow label={__("network.content.hostname")}>
                        <InputText maxLength={32}
                                   pattern="[a-zA-Z0-9\-]*"
                                   required
                                   value={state.hostname}
                                   onValue={this.set("hostname")}
                                   invalidFeedback={__("network.content.hostname_invalid")}
                                   />
                    </FormRow>

                    <FormRow label={__("network.content.enable_mdns")}>
                        <Switch desc={__("network.content.enable_mdns_desc")}
                                checked={state.enable_mdns}
                                onClick={this.toggle('enable_mdns')}/>
                    </FormRow>
                </ConfigForm>
            </>
        );
    }
}

render(<Network/>, $('#network')[0])

export function init() {}

export function add_event_listeners(source: API.APIEventTarget) {}

export function update_sidebar_state(module_init: any) {
    $('#sidebar-network').prop('hidden', !module_init.network);
}
