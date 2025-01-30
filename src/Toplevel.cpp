#include "Server.h"
#include "wlr.h"
#include <wayland-server-core.h>

Toplevel::Toplevel(struct Server *server,
                   struct wlr_xdg_toplevel *xdg_toplevel) {
    this->server = server;
    this->xdg_toplevel = xdg_toplevel;
    scene_tree =
        wlr_scene_xdg_surface_create(&server->scene->tree, xdg_toplevel->base);
    scene_tree->node.data = this;
    xdg_toplevel->base->data = scene_tree;

    /* Listen to the various events it can emit */

    // xdg_toplevel_map
    map.notify = [](struct wl_listener *listener, void *data) {
        /* Called when the surface is mapped, or ready to display on-screen. */
        struct Toplevel *toplevel = wl_container_of(listener, toplevel, map);

        wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

        toplevel->focus();
    };
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &map);

    // xdg_toplevel_unmap
    unmap.notify = [](struct wl_listener *listener, void *data) {
        /* Called when the surface is unmapped, and should no longer be shown.
         */
        struct Toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

        /* Reset the cursor mode if the grabbed toplevel was unmapped. */
        if (toplevel == toplevel->server->grabbed_toplevel)
            toplevel->server->reset_cursor_mode();

        wl_list_remove(&toplevel->link);
    };
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &unmap);

    // xdg_toplevel_commit
    commit.notify = [](struct wl_listener *listener, void *data) {
        /* Called when a new surface state is committed. */
        struct Toplevel *toplevel = wl_container_of(listener, toplevel, commit);

        if (toplevel->xdg_toplevel->base->initial_commit)
            /* When an xdg_surface performs an initial commit, the compositor
             * must reply with a configure so the client can map the surface.
             * tinywl configures the xdg_toplevel with 0,0 size to let the
             * client pick the dimensions itself. */
            wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
    };
    wl_signal_add(&xdg_toplevel->base->surface->events.commit, &commit);

    // xdg_toplevel_destroy
    destroy.notify = [](struct wl_listener *listener, void *data) {
        struct Toplevel *toplevel =
            wl_container_of(listener, toplevel, destroy);
        delete toplevel;
    };
    wl_signal_add(&xdg_toplevel->events.destroy, &destroy);

    // request_move
    request_move.notify = [](struct wl_listener *listener, void *data) {
        struct Toplevel *toplevel =
            wl_container_of(listener, toplevel, request_move);

        // start interactivity
        toplevel->begin_interactive(CURSORMODE_MOVE, 0);
    };
    wl_signal_add(&xdg_toplevel->events.request_move, &request_move);

    // request_resize
    request_resize.notify = [](struct wl_listener *listener, void *data) {
        /* This event is raised when a client would like to begin an interactive
         * resize, typically because the user clicked on their client-side
         * decorations. Note that a more sophisticated compositor should check
         * the provided serial against a list of button press serials sent to
         * this client, to prevent the client from requesting this whenever they
         * want. */
        struct wlr_xdg_toplevel_resize_event *event =
            (wlr_xdg_toplevel_resize_event *)data;
        struct Toplevel *toplevel =
            wl_container_of(listener, toplevel, request_resize);
        toplevel->begin_interactive(CURSORMODE_RESIZE, event->edges);
    };
    wl_signal_add(&xdg_toplevel->events.request_resize, &request_resize);

    // request_maximize
    request_maximize.notify = [](struct wl_listener *listener, void *data) {
        struct Toplevel *toplevel =
            wl_container_of(listener, toplevel, request_maximize);

        if (!toplevel->xdg_toplevel->base->initialized)
            return;

        double center_x = toplevel->scene_tree->node.x +
                          (toplevel->xdg_toplevel->current.width / 2.0);
        double center_y = toplevel->scene_tree->node.y +
                          (toplevel->xdg_toplevel->current.height / 2.0);

        struct wlr_output *wlr_output = wlr_output_layout_output_at(
            toplevel->server->output_layout, center_x, center_y);

        if (!wlr_output) {
            struct Output *first_output = toplevel->server->get_output(0);
            if (!first_output)
                return;
            wlr_output = first_output->wlr_output;
        }

        struct wlr_box output_box;
        wlr_output_layout_get_box(toplevel->server->output_layout, wlr_output,
                                  &output_box);

        if (toplevel->xdg_toplevel->requested.maximized) {
            toplevel->saved_geometry.x = toplevel->scene_tree->node.x;
            toplevel->saved_geometry.y = toplevel->scene_tree->node.y;
            toplevel->saved_geometry.width =
                toplevel->xdg_toplevel->current.width;
            toplevel->saved_geometry.height =
                toplevel->xdg_toplevel->current.height;

            float scale = wlr_output->scale;

            wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                        output_box.x, output_box.y);
            wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
                                      output_box.width / scale,
                                      output_box.height / scale);
        } else {
            wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                        toplevel->saved_geometry.x,
                                        toplevel->saved_geometry.y);
            wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
                                      toplevel->saved_geometry.width,
                                      toplevel->saved_geometry.height);
        }
        wlr_xdg_toplevel_set_maximized(
            toplevel->xdg_toplevel,
            toplevel->xdg_toplevel->requested.maximized);

        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
    };
    wl_signal_add(&xdg_toplevel->events.request_maximize, &request_maximize);

    // request_fullscreen
    request_fullscreen.notify = [](struct wl_listener *listener, void *data) {
        /* Just as with request_maximize, we must send a configure here. */
        struct Toplevel *toplevel =
            wl_container_of(listener, toplevel, request_fullscreen);
        if (toplevel->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
        }
    };
    wl_signal_add(&xdg_toplevel->events.request_fullscreen,
                  &request_fullscreen);
}

Toplevel::~Toplevel() {
    wl_list_remove(&map.link);
    wl_list_remove(&unmap.link);
    wl_list_remove(&commit.link);
    wl_list_remove(&destroy.link);
    wl_list_remove(&request_move.link);
    wl_list_remove(&request_resize.link);
    wl_list_remove(&request_maximize.link);
    wl_list_remove(&request_fullscreen.link);
}

void Toplevel::focus() {
    /* Note: this function only deals with keyboard focus. */
    if (xdg_toplevel == NULL)
        return;

    struct wlr_seat *seat = server->seat;
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    struct wlr_surface *surface = xdg_toplevel->base->surface;
    if (prev_surface == surface)
        /* Don't re-focus an already focused surface. */
        return;

    if (prev_surface) {
        /*
         * Deactivate the previously focused surface. This lets the client know
         * it no longer has focus and the client will repaint accordingly, e.g.
         * stop displaying a caret.
         */
        struct wlr_xdg_toplevel *prev_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel != NULL) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
    }
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    /* Move the toplevel to the front */
    wlr_scene_node_raise_to_top(&scene_tree->node);
    wl_list_remove(&link);
    wl_list_insert(&server->toplevels, &link);
    /* Activate the new surface */
    wlr_xdg_toplevel_set_activated(xdg_toplevel, true);
    /*
     * Tell the seat to have the keyboard enter this surface. wlroots will keep
     * track of this and automatically send key events to the appropriate
     * clients without additional work on your part.
     */
    if (keyboard != NULL)
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
                                       keyboard->num_keycodes,
                                       &keyboard->modifiers);
}

void Toplevel::begin_interactive(enum CursorMode mode, uint32_t edges) {
    /* This function sets up an interactive move or resize operation, where the
     * compositor stops propegating pointer events to clients and instead
     * consumes them itself, to move or resize windows. */
    server->grabbed_toplevel = this;
    server->cursor_mode = mode;

    if (mode == CURSORMODE_MOVE) {
        server->grab_x = server->cursor->x - scene_tree->node.x;
        server->grab_y = server->cursor->y - scene_tree->node.y;
    } else {
        struct wlr_box *geo_box = &xdg_toplevel->base->geometry;

        double border_x = (scene_tree->node.x + geo_box->x) +
                          ((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
        double border_y = (scene_tree->node.y + geo_box->y) +
                          ((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
        server->grab_x = server->cursor->x - border_x;
        server->grab_y = server->cursor->y - border_y;

        server->grab_geobox = *geo_box;
        server->grab_geobox.x += scene_tree->node.x;
        server->grab_geobox.y += scene_tree->node.y;

        server->resize_edges = edges;
    }
}
