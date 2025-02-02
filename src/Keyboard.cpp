#include "Server.h"

bool Keyboard::handle_keybinding(xkb_keysym_t sym) {
    /*
     * Here we handle compositor keybindings. This is when the compositor is
     * processing keys, rather than passing them on to the client for its own
     * processing.
     *
     * This function assumes Alt is held down.
     */

    // exit compositor
    if (sym == XKB_KEY_Escape) {
        wl_display_terminate(server->wl_display);
        return true;
    }

    Output *output = server->output_at(server->cursor->x, server->cursor->y);
    if (output == NULL)
        return false;

    // switch to workspace n, 1-9 inclusive
    if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9)
        return output->set_workspace(sym - XKB_KEY_1);

    // match remaining syms
    switch (sym) {
    case XKB_KEY_Left:
        wlr_log(WLR_DEBUG, "TODO: move left");
        break;
    case XKB_KEY_Right:
        wlr_log(WLR_DEBUG, "TODO: move right");
        break;
    case XKB_KEY_o: // focus the previous toplevel in the active workspace
        output->get_active()->focus_prev();
        break;
    case XKB_KEY_p: // focus the next toplevel in the active workspace
        output->get_active()->focus_next();
        break;
    case XKB_KEY_t: // set workspace to tile
        output->get_active()->tile();
        break;
    case XKB_KEY_space: // open rofi
        if (fork() == 0)
            execl("/bin/sh", "/bin/sh", "-c", "rofi -show drun", nullptr);
        break;
    case XKB_KEY_c:
        wlr_log(WLR_DEBUG, "PrintScreen activated");
        if (fork() == 0)
            execl("/bin/sh", "/bin/sh", "-c",
                  "grim -g \"$(slurp)\" - | swappy -f -", nullptr);
        break;
    default:
        return false;
    }
    return true;
}

bool Keyboard::handle_shift_keybinding(uint32_t keycode, xkb_keysym_t sym) {
    Output *output = server->output_at(server->cursor->x, server->cursor->y);
    if (output == NULL)
        return false;

    // move active toplevel to workspace n, 1-9 inclusive
    // keycode for 1 is 2 which corresponds to workspace 0
    if (keycode > 1 && keycode < 11) {
        Workspace *current = output->get_active();
        Workspace *target = output->get_workspace(keycode - 2);

        if (target == nullptr)
            return false;

        current->move_to(current->active_toplevel, target);
        return true;
    }

    switch (sym) {
    default:
        return false;
    }
    return true;
}

Keyboard::Keyboard(struct Server *server, struct wlr_input_device *device) {
    this->server = server;
    this->wlr_keyboard = wlr_keyboard_from_input_device(device);

    /* We need to prepare an XKB keymap and assign it to the keyboard. This
     * assumes the defaults (e.g. layout = "us"). */
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

    /* Here we set up listeners for keyboard events. */

    // handle_modifiers
    modifiers.notify = [](struct wl_listener *listener, void *data) {
        /* This event is raised when a modifier key, such as shift or alt,
         * is pressed. We simply communicate this to the client. */
        struct Keyboard *keyboard =
            wl_container_of(listener, keyboard, modifiers);
        /*
         * A seat can only have one keyboard, but this is a limitation of
         * the Wayland protocol - not wlroots. We assign all connected
         * keyboards to the same seat. You can swap out the underlying
         * wlr_keyboard like this and wlr_seat handles this transparently.
         */
        wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
        /* Send modifiers to the client. */
        wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
                                           &keyboard->wlr_keyboard->modifiers);
    };
    wl_signal_add(&wlr_keyboard->events.modifiers, &modifiers);

    // handle_key (keyboard)
    key.notify = [](struct wl_listener *listener, void *data) {
        /* This event is raised when a key is pressed or released. */
        struct Keyboard *keyboard = wl_container_of(listener, keyboard, key);
        struct Server *server = keyboard->server;
        struct wlr_keyboard_key_event *event = (wlr_keyboard_key_event *)data;
        struct wlr_seat *seat = server->seat;

        /* Translate libinput keycode -> xkbcommon */
        uint32_t keycode = event->keycode + 8;
        /* Get a list of keysyms based on the keymap for this keyboard */
        const xkb_keysym_t *syms;
        int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state,
                                           keycode, &syms);

        bool handled = false;
        uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
        if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            // Alt modifier
            if (modifiers & WLR_MODIFIER_ALT) {
                // Alt + Shift
                if (modifiers & WLR_MODIFIER_SHIFT)
                    for (int i = 0; i < nsyms; i++)
                        handled = keyboard->handle_shift_keybinding(
                            event->keycode, syms[i]);
                else
                    for (int i = 0; i < nsyms; i++)
                        handled = keyboard->handle_keybinding(syms[i]);
            }
        }

        if (!handled) {
            /* Otherwise, we pass it along to the client. */
            wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
            wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode,
                                         event->state);
        }
    };
    wl_signal_add(&wlr_keyboard->events.key, &key);

    // handle_destroy (keyboard)
    destroy.notify = [](struct wl_listener *listener, void *data) {
        /* This event is raised by the keyboard base wlr_input_device to
         * signal the destruction of the wlr_keyboard. It will no longer
         * receive events and should be destroyed.
         */
        struct Keyboard *keyboard =
            wl_container_of(listener, keyboard, destroy);
        delete keyboard;
    };
    wl_signal_add(&device->events.destroy, &destroy);
}

Keyboard::~Keyboard() {
    wl_list_remove(&modifiers.link);
    wl_list_remove(&key.link);
    wl_list_remove(&destroy.link);
    wl_list_remove(&link);
}
