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

interface InputDateProps extends Omit<JSXInternal.HTMLAttributes<HTMLInputElement>, "value" | "class" | "id" | "type" | "onInput"> {
    idContext?: Context<string>
    date: Date
    onDate?: (value: Date) => void
}

export function InputDate(props: InputDateProps) {
    let id = props.idContext === undefined ? "" : useContext(props.idContext);
    return (
        <input class={"form-control " + props.className}
               id={id}
               type="date"
               onInput={props.onDate ? (e) => props.onDate(new Date((e.target as HTMLInputElement).value)) : undefined}
               disabled={!props.onDate}
               {...{valueAsDate: props.date}}/> //valueAsDate is not recognized for some reason? https://github.com/facebook/react/issues/11369 (no issue found for preact)
    );
}
