/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef H_BLE_HCI_SCHED_
#define H_BLE_HCI_SCHED_

typedef int ble_hci_sched_tx_fn(void *arg);

int ble_hci_sched_enqueue(ble_hci_sched_tx_fn *tx_cb, void *tx_cb_arg);
void ble_hci_sched_wakeup(void);
void ble_hci_sched_command_complete(void);
int ble_hci_sched_init(void);

#endif