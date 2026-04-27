// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "module.h"
#include "usb_main.h"
#include "transport.h"

#ifndef USB_POWER_DOWN_DELAY
#    define USB_POWER_DOWN_DELAY 3000
#endif

/* Tunables for the USB stuck-state recovery FSM in usb_remote_wakeup():
 *   USB_STUCK_RECOVERY_DELAY         grace period (ms) before the first attempt
 *   USB_STUCK_RECOVERY_DISCONNECT_MS D+ off-time (ms) so the host registers a removal
 *   USB_STUCK_RECOVERY_INTERVAL      cooldown (ms) between successive attempts
 *   USB_STUCK_RECOVERY_MAX_ATTEMPTS  retry bound; stop after N to avoid cycling
 *                                    the bus indefinitely when no host is present
 */
#ifndef USB_STUCK_RECOVERY_DELAY
#    define USB_STUCK_RECOVERY_DELAY 2000
#endif

#ifndef USB_STUCK_RECOVERY_DISCONNECT_MS
#    define USB_STUCK_RECOVERY_DISCONNECT_MS 1000
#endif

#ifndef USB_STUCK_RECOVERY_INTERVAL
#    define USB_STUCK_RECOVERY_INTERVAL 3000
#endif

#ifndef USB_STUCK_RECOVERY_MAX_ATTEMPTS
#    define USB_STUCK_RECOVERY_MAX_ATTEMPTS 20
#endif

enum {
    USB_RECOVERY_IDLE = 0,
    USB_RECOVERY_DISCONNECTING,
    USB_RECOVERY_COOLDOWN,
    USB_RECOVERY_GAVE_UP,
};

extern host_driver_t chibios_driver;
extern host_driver_t wireless_driver;

static transport_t transport = TRANSPORT_USB;

void wls_transport_enable(bool enable) __attribute__((weak));
void wls_transport_enable(bool enable) {

    if (enable) {
        if (host_get_driver() != &wireless_driver) {
            host_set_driver(&wireless_driver);
            keyboard_protocol = true; // default with true
        }
    } else {
        if (*md_getp_state() == MD_STATE_CONNECTED) {
            wireless_driver.send_keyboard(NULL);
            wireless_driver.send_nkro(NULL);
        }
    }
}

/* Control USB device connection and disconnection by
 * controlling the power supply of the USB DP pull-up resistor.
 * Overwrite these two functions. */
void usb_power_connect(void) __attribute__((weak));
void usb_power_connect(void) {}

void usb_power_disconnect(void) __attribute__((weak));
void usb_power_disconnect(void) {}

void usb_transport_enable(bool enable) __attribute__((weak));
void usb_transport_enable(bool enable) {

    if (enable) {
        host_driver_t *current = host_get_driver();

        /* Restart USB only on a real wireless->USB transition. At cold boot
         * host_get_driver() is NULL (protocol_post_init runs later), so the
         * naive `!= &chibios_driver` check would race init_usb_driver. */
        if (current == &wireless_driver) {
            extern bool last_suspend_state;

            /* This flag is not set to 1 with probability after usb restart */
            last_suspend_state = true;
#if !defined(KEEP_USB_CONNECTION_IN_WIRELESS_MODE)
            usb_power_connect();
            restart_usb_driver(&USBD1);
#endif
        }

        if (current != &chibios_driver) {
            host_set_driver(&chibios_driver);
        }
    } else {
        if (USB_DRIVER.state == USB_ACTIVE) {
            report_keyboard_t empty_report = {0};
            report_nkro_t empty_nkro_report = {0};
            host_keyboard_send(&empty_report);
            host_nkro_send(&empty_nkro_report);
        }

#if !defined(KEEP_USB_CONNECTION_IN_WIRELESS_MODE)
        usbStop(&USBD1);
        usbDisconnectBus(&USBD1);
        usb_power_disconnect();
#endif
    }
}

void set_transport(transport_t new_transport) {

    transport = new_transport;

    switch (transport) {
        case TRANSPORT_USB: {
            usb_transport_enable(true);
            wls_transport_enable(false);
        } break;
        case TRANSPORT_WLS: {
            wls_transport_enable(true);
            usb_transport_enable(false);
        } break;
        default:
            break;
    }
}

transport_t get_transport(void) {

    return transport;
}

void usb_remote_wakeup(void) {

#ifdef USB_REMOTE_USE_QMK
    if (USB_DRIVER.state == USB_SUSPENDED) {
        dprintln("suspending keyboard");
        while (USB_DRIVER.state == USB_SUSPENDED) {
            /* Do this in the suspended state */
            suspend_power_down(); // on AVR this deep sleeps for 15ms
            /* Remote wakeup */
            if ((USB_DRIVER.status & 2U) && suspend_wakeup_condition()) {
                usbWakeupHost(&USB_DRIVER);
#    if USB_SUSPEND_WAKEUP_DELAY > 0
                // Some hubs, kvm switches, and monitors do
                // weird things, with USB device state bouncing
                // around wildly on wakeup, yielding race
                // conditions that can corrupt the keyboard state.
                //
                // Pause for a while to let things settle...
                wait_ms(USB_SUSPEND_WAKEUP_DELAY);
#    endif
            }
        }
        /* Woken up */
    }
#else
    static uint32_t suspend_timer     = 0x00;
    static uint32_t recovery_ts       = 0x00;
    static uint8_t  recovery_state    = USB_RECOVERY_IDLE;
    static uint8_t  recovery_attempts = 0;

    /* Soft-suspend while the host is suspended. */
    if ((USB_DRIVER.state == USB_SUSPENDED)) {
        if (!suspend_timer) suspend_timer = sync_timer_read32();
        if (sync_timer_elapsed32(suspend_timer) >= USB_POWER_DOWN_DELAY) {
            suspend_timer = 0x00;
            suspend_power_down();
        }
    } else {
        suspend_timer = 0x00;
    }

    /* Non-blocking auto-recovery when USB never reaches USB_ACTIVE.
     * Mirrors the manual mode-slider workaround. */
    if (USB_DRIVER.state == USB_ACTIVE) {
        recovery_state    = USB_RECOVERY_IDLE;
        recovery_ts       = 0x00;
        recovery_attempts = 0;
        return;
    }

    switch (recovery_state) {
        case USB_RECOVERY_IDLE:
            if (!recovery_ts) {
                recovery_ts = sync_timer_read32();
            }
            if (sync_timer_elapsed32(recovery_ts) >= USB_STUCK_RECOVERY_DELAY) {
                if (recovery_attempts >= USB_STUCK_RECOVERY_MAX_ATTEMPTS) {
                    recovery_state = USB_RECOVERY_GAVE_UP;
                } else {
                    /* Phase 1: clean shutdown (mirrors usb_transport_enable(false)). */
                    usbStop(&USBD1);
                    usbDisconnectBus(&USBD1);
                    usb_power_disconnect();
                    recovery_ts    = sync_timer_read32();
                    recovery_state = USB_RECOVERY_DISCONNECTING;
                }
            }
            break;

        case USB_RECOVERY_DISCONNECTING:
            if (sync_timer_elapsed32(recovery_ts) >= USB_STUCK_RECOVERY_DISCONNECT_MS) {
                /* Phase 2: reconnect (mirrors usb_transport_enable(true)). */
                extern bool last_suspend_state;
                last_suspend_state = true;
                usb_power_connect();
                restart_usb_driver(&USBD1);
                host_set_driver(&chibios_driver);

                recovery_attempts++;
                recovery_ts    = sync_timer_read32();
                recovery_state = USB_RECOVERY_COOLDOWN;
            }
            break;

        case USB_RECOVERY_COOLDOWN:
            if (sync_timer_elapsed32(recovery_ts) >= USB_STUCK_RECOVERY_INTERVAL) {
                recovery_ts    = sync_timer_read32();
                recovery_state = USB_RECOVERY_IDLE;
            }
            break;

        case USB_RECOVERY_GAVE_UP:
            /* Reset only when USB_ACTIVE returns (handled at top of fn). */
            break;

        default:
            recovery_state = USB_RECOVERY_IDLE;
            break;
    }
#endif
}

#ifndef USB_REMOTE_USE_QMK
void usb_remote_host(void) {

    if (USB_DRIVER.state == USB_SUSPENDED) {
        if ((USB_DRIVER.status & 2U) && suspend_wakeup_condition()) {
            usbWakeupHost(&USB_DRIVER);
#    if USB_SUSPEND_WAKEUP_DELAY > 0
            // Some hubs, kvm switches, and monitors do
            // weird things, with USB device state bouncing
            // around wildly on wakeup, yielding race
            // conditions that can corrupt the keyboard state.
            //
            // Pause for a while to let things settle...
            wait_ms(USB_SUSPEND_WAKEUP_DELAY);
#    endif
        }
#    if !defined(USB_REMOTE_USE_QMK) && USB_POWER_DOWN_DELAY
        suspend_wakeup_init();
#    endif
    }
}

bool process_action_kb(keyrecord_t *record) {

    (void)record;
    if (get_transport() == TRANSPORT_USB){
        usb_remote_host();
    }

    return true;
}
#endif
