#include "Server.h"

Workspace::Workspace(struct Output *output) {
    this->output = output;
    wl_list_init(&toplevels);
}

Workspace::~Workspace() {}

// add a toplevel to the workspace
void Workspace::add_toplevel(struct Toplevel *toplevel) {
    wl_list_insert(&toplevels, &toplevel->link);
    active_toplevel = toplevel;
    active_toplevel->focus();
}

// returns true if the workspace contains the passed toplevel
bool Workspace::contains(struct Toplevel *toplevel) {
    if (wl_list_empty(&toplevels))
        return false;

    Toplevel *current, *tmp;
    wl_list_for_each_safe(current, tmp, &toplevels,
                          link) if (current == toplevel) return true;

    return false;
}

// move a toplevel to another workspace
bool Workspace::move_to(struct Toplevel *toplevel,
                        struct Workspace *workspace) {
    if (workspace == this)
        return false;

    if (contains(toplevel)) {
        wl_list_remove(&toplevel->link);
        workspace->add_toplevel(toplevel);
        return true;
    }

    return false;
}

// get the nth toplevel
struct Toplevel *Workspace::get_toplevel(uint32_t n) {
    Toplevel *toplevel, *tmp;
    uint32_t current = 0;
    wl_list_for_each_safe(toplevel, tmp, &toplevels, link) {
        if (current == n)
            return toplevel;
        ++n;
    }

    return nullptr;
}

// move the nth toplevel to another workspace
bool Workspace::move_to(uint32_t n, struct Workspace *workspace) {
    if (wl_list_empty(&toplevels))
        return false;

    Toplevel *toplevel = get_toplevel(n);

    if (toplevel == nullptr)
        return false;

    return move_to(toplevel, workspace);
}

// set the workspace's visibility
void Workspace::set_hidden(bool hidden) {
    Toplevel *toplevel, *tmp;
    wl_list_for_each_safe(toplevel, tmp, &toplevels, link)
        toplevel->set_hidden(hidden);
}

// focus the workspace
void Workspace::focus() {
    set_hidden(false);

    if (!wl_list_empty(&toplevels)) {
        Toplevel *toplevel = wl_container_of(toplevels.prev, toplevel, link);
        active_toplevel = toplevel;
        active_toplevel->focus();
    }
}

// focus the toplevel following the active one, looping around to the start
void Workspace::focus_next() {
    if (wl_list_length(&toplevels) < 2 || wl_list_empty(&toplevels))
        return;

    if (!active_toplevel) {
        active_toplevel =
            wl_container_of(toplevels.next, active_toplevel, link);
        active_toplevel->focus();

        return;
    }

    struct wl_list *next = active_toplevel->link.next;
    if (next == &toplevels)
        next = toplevels.next;

    active_toplevel = wl_container_of(next, active_toplevel, link);
    active_toplevel->focus();
}

// focus the toplevel preceding the active one, looping around to the end
void Workspace::focus_prev() {
    if (wl_list_length(&toplevels) < 2 || wl_list_empty(&toplevels))
        return;

    if (!active_toplevel) {
        active_toplevel =
            wl_container_of(toplevels.prev, active_toplevel, link);
        active_toplevel->focus();
        return;
    }

    struct wl_list *prev = active_toplevel->link.prev;
    if (prev == &toplevels)
        prev = toplevels.prev;

    active_toplevel = wl_container_of(prev, active_toplevel, link);
    active_toplevel->focus();
}

// auto-tile the toplevels of a workspace, not currently reversible or
// any kind of special state
void Workspace::tile() {
    if (wl_list_empty(&toplevels))
        return;

    struct wlr_box box;
    wlr_output_layout_get_box(output->server->output_layout, output->wlr_output,
                              &box);

    int toplevel_count = wl_list_length(&toplevels);

    int cols = std::round(std::sqrt(toplevel_count));
    int rows = (toplevel_count + cols - 1) / cols;

    int width = box.width / cols;
    int height = box.height / rows;

    int i = 0;
    Toplevel *toplevel, *tmp;
    wl_list_for_each_safe(toplevel, tmp, &toplevels, link) {
        int row = i / cols;
        int col = i % cols;
        int x = box.x + (col * width);
        int y = box.y + (row * height);

        toplevel->set_position_size(x, y, width, height);
        ++i;
    }
}
