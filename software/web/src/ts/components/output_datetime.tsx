/* esp32-firmware
 * Copyright (C) 2022 Erik Fleckstein <erik@tinkerforge.com>
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

import { h, Context } from "preact";
import {useContext} from "preact/hooks";
import { JSXInternal } from "preact/src/jsx";

interface OutputDatetimeProps extends Omit<JSXInternal.HTMLAttributes<HTMLInputElement>, "value" | "class" | "id" | "type" | "onInput" | "disabled"> {
    idContext?: Context<string>
    date: Date
}

function toIsoString(date: Date) {
    var tzo = -date.getTimezoneOffset();
    const pad = function(num: number) {
        return (num < 10 ? '0' : '') + num;
    };

    return date.getFullYear() +
        '-' + pad(date.getMonth() + 1) +
        '-' + pad(date.getDate()) +
        'T' + pad(date.getHours()) +
        ':' + pad(date.getMinutes()) +
        ':' + pad(date.getSeconds());
  }

export function OutputDatetime(props: OutputDatetimeProps) {
    let id = props.idContext === undefined ? "" : useContext(props.idContext);
    return (
        <input class={"form-control " + props.className}
               id={id}
               type="datetime-local"
               step={1}
               disabled={true}
               value={toIsoString(props.date)}
               required
               />
    );
}
