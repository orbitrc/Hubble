/*
 * Copyright © 2010-2012 Intel Corporation
 * Copyright © 2011-2012 Collabora, Ltd.
 * Copyright © 2013 Raspberry Pi Foundation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <libweston/libweston.h>
#include <libweston/xwayland-api.h>

#include <primer/string.h>
#include <primer/vector.h>

#include <wayland-protocols/weston/weston-desktop-shell-server-protocol.h>

enum class AnimationType {
    None,
    Zoom,
    Fade,
    DimLayer,
};

enum class FadeType {
    FadeIn,
    FadeOut,
};

enum exposay_target_state {
	EXPOSAY_TARGET_OVERVIEW, /* show all windows */
	EXPOSAY_TARGET_CANCEL, /* return to normal, same focus */
	EXPOSAY_TARGET_SWITCH, /* return to normal, switch focus */
};

enum exposay_layout_state {
	EXPOSAY_LAYOUT_INACTIVE = 0, /* normal desktop */
	EXPOSAY_LAYOUT_ANIMATE_TO_INACTIVE, /* in transition to normal */
	EXPOSAY_LAYOUT_OVERVIEW, /* show all windows */
	EXPOSAY_LAYOUT_ANIMATE_TO_OVERVIEW, /* in transition to all windows */
};

struct exposay_output {
	int num_surfaces;
	int grid_size;
	int surface_size;
	int padding_inner;
};

namespace hb {
class Workspace;
} // namespace hb

struct weston_desktop;
struct text_backend;
struct shell_output;
struct shell_seat;

struct exposay {
	/* XXX: Make these exposay_surfaces. */
	struct weston_view *focus_prev;
	struct weston_view *focus_current;
	struct weston_view *clicked;
    hb::Workspace *workspace;
	struct weston_seat *seat;

	struct wl_list surface_list;

	struct weston_keyboard_grab grab_kbd;
	struct weston_pointer_grab grab_ptr;

	enum exposay_target_state state_target;
	enum exposay_layout_state state_cur;
	int in_flight; /* number of animations still running */

	int row_current;
	int column_current;
	struct exposay_output *cur_output;

	bool mod_pressed;
	bool mod_invalid;
};

struct desktop_shell;
struct input_panel_surface;

namespace hb {

class FocusState;

//================
// Focus Surface
//================
class FocusSurface
{
public:
    FocusSurface(struct weston_compositor *weston_compositor,
            struct weston_output *output);
    ~FocusSurface();

    struct weston_surface* surface();
    void set_surface(struct weston_surface *surface);

    struct weston_view* view();
    void set_view(struct weston_view *view);

    struct weston_transform workspace_transform();
    struct weston_transform* workspace_transform_ptr();
    void set_workspace_transform(struct weston_transform transform);

private:
    struct weston_surface *_weston_surface;
    struct weston_view *_weston_view;
    struct weston_transform _workspace_transform;
};

//==============
// Workspace
//==============
class Workspace
{
public:
    Workspace(struct desktop_shell *shell);
    ~Workspace();

    bool is_empty() const;

    struct weston_layer* layer();

    pr::Vector<FocusState*>& focus_list();

    FocusSurface* focus_surface_front();
    void set_focus_surface_front(FocusSurface *front);

    FocusSurface* focus_surface_back();
    void set_focus_surface_back(FocusSurface *back);

    struct weston_view_animation* focus_animation();
    void set_focus_animation(struct weston_view_animation* anim);

    bool has_only(struct weston_surface *surface);

    void view_translate(struct weston_view *view, double d);

    void translate_out(double fraction);
    void translate_in(double fraction);

    void deactivate_transforms();

public:
    struct wl_listener seat_destroyed_listener;

private:
    struct weston_layer _layer;

    pr::Vector<FocusState*> _focus_list;

    hb::FocusSurface *_fsurf_front;
    hb::FocusSurface *_fsurf_back;
    struct weston_view_animation *_focus_animation;
};

//==============
// Focus State
//==============
class FocusState
{
public:
    FocusState(struct desktop_shell *shell,
               struct weston_seat *seat,
               hb::Workspace *ws);
    ~FocusState();

    void set_focus(struct weston_surface *surface);

    struct desktop_shell *shell;
    struct weston_seat *seat;
    hb::Workspace *ws;
    struct weston_surface *keyboard_focus;
    struct wl_list link;
    struct wl_listener seat_destroy_listener;
    struct wl_listener surface_destroy_listener;
private:
};

//================
// Shell Output
//================
class ShellOutput
{
    friend struct desktop_shell;
public:
    //=====================
    // Shell Output Fade
    //=====================
    class Fade
    {
    public:
        Fade();
        ~Fade();

        struct weston_view* view();
        void set_view(struct weston_view *view);

        struct weston_view_animation* animation();
        void set_animation(struct weston_view_animation* animation);

        FadeType type() const;
        void set_type(FadeType type);

        struct wl_event_source *startup_timer;

    private:
        struct weston_view *_view;
        struct weston_view_animation *_animation;
        FadeType _type;
    };

public:
    ShellOutput(struct weston_output *output);
    ~ShellOutput();

    struct desktop_shell  *shell;
    struct weston_output  *output;
    struct exposay_output eoutput;
    struct wl_listener    destroy_listener;
    struct wl_list        link;

    struct weston_surface *panel_surface;
    struct wl_listener panel_surface_listener;

    struct weston_surface *background_surface;
    struct wl_listener background_surface_listener;

    hb::ShellOutput::Fade fade;
};

//=================
// Desktop Shell
//=================
class DesktopShell
{
public:
    //===========
    // Child
    //===========
    class Child
    {
    public:
        struct wl_client *client;
        struct wl_resource *desktop_shell;
        struct wl_listener client_destroy_listener;

        unsigned deathcount;
        struct timespec deathstamp;
    };

    //===========
    // TextInput
    //===========
    class TextInput
    {
    public:
        struct weston_surface *surface;
        pixman_box32_t cursor_rectangle;
    };

    //=============
    // Workspaces
    //=============
    class Workspaces
    {
    public:
        pr::Vector<hb::Workspace*> array;
        unsigned int current;
        unsigned int num;

        struct weston_animation animation;
        struct wl_list anim_sticky_list;
        int anim_dir;
        struct timespec anim_timestamp;
        double anim_current;
        hb::Workspace *anim_from;
        hb::Workspace *anim_to;
    };

    //==============
    // InputPanel
    //==============
    class InputPanel
    {
    public:
        struct wl_resource *binding;
        pr::Vector<struct input_panel_surface*> surfaces;
    };

public:
    DesktopShell(struct weston_compositor *compositor);
    ~DesktopShell();

    struct weston_compositor* compositor();

    /// Get singleton instance.
    static DesktopShell* instance();

public:
    //=====================
    // Wayland Listeners
    //=====================
    struct wl_listener idle_listener;
    struct wl_listener wake_listener;
    struct wl_listener transform_listener;
    struct wl_listener resized_listener;
    struct wl_listener destroy_listener;
    struct wl_listener show_input_panel_listener;
    struct wl_listener hide_input_panel_listener;
    struct wl_listener update_input_panel_listener;

    struct wl_listener pointer_focus_listener;

    struct wl_listener lock_surface_listener;

    struct wl_listener seat_create_listener;
    struct wl_listener output_create_listener;
    struct wl_listener output_move_listener;

private:
    struct weston_compositor *_compositor;
    struct weston_desktop *_desktop;
    const struct weston_xwayland_surface_api *_xwayland_surface_api;

    struct weston_layer _fullscreen_layer;
    struct weston_layer _panel_layer;
    struct weston_layer _background_layer;
    struct weston_layer _lock_layer;
    struct weston_layer _input_panel_layer;

    struct weston_surface *_grab_surface;

    bool _locked;
    bool _showing_input_panels;
    bool _prepare_event_sent;

    struct text_backend *_text_backend;

    struct weston_surface *_lock_surface;

    bool _allow_zap;
    uint32_t _binding_modifier;
    uint32_t _exposay_modifier;
    AnimationType _win_animation_type;
    AnimationType _win_close_animation_type;
    AnimationType _startup_animation_type;
    AnimationType _focus_animation_type;

    struct weston_layer _minimized_layer;

    pr::Vector<hb::ShellOutput*> _output_list;
    pr::Vector<struct shell_seat*> _seat_list;

    enum weston_desktop_shell_panel_position _panel_position;

    pr::String _client;

    struct timespec _startup_time;

public:
    hb::DesktopShell::Child child;
    hb::DesktopShell::TextInput text_input;
    hb::DesktopShell::Workspaces workspaces;
    hb::DesktopShell::InputPanel input_panel;

    struct exposay exposay;
};

// Singleton object.
extern DesktopShell *desktop_shell_singleton;

} // namespace hb

struct desktop_shell {
    struct weston_compositor *compositor; //
    struct weston_desktop *desktop; //
    const struct weston_xwayland_surface_api *xwayland_surface_api; //

    struct wl_listener idle_listener; //
    struct wl_listener wake_listener; //
    struct wl_listener transform_listener; //
    struct wl_listener resized_listener; //
    struct wl_listener destroy_listener; //
    struct wl_listener show_input_panel_listener; //
    struct wl_listener hide_input_panel_listener; //
    struct wl_listener update_input_panel_listener; //

    struct weston_layer fullscreen_layer; //
    struct weston_layer panel_layer; //
    struct weston_layer background_layer; //
    struct weston_layer lock_layer; //
    struct weston_layer input_panel_layer; //

    struct wl_listener pointer_focus_listener; //
    struct weston_surface *grab_surface; //

	struct {
        struct wl_client *client;
        struct wl_resource *desktop_shell;
        struct wl_listener client_destroy_listener;

		unsigned deathcount;
		struct timespec deathstamp;
    } child; //

    bool locked; //
    bool showing_input_panels; //
    bool prepare_event_sent; //

    struct text_backend *text_backend; //

	struct {
		struct weston_surface *surface;
		pixman_box32_t cursor_rectangle;
    } text_input; //

    struct weston_surface *lock_surface; //
    struct wl_listener lock_surface_listener; //

	struct {
//		struct wl_array array;
        pr::Vector<hb::Workspace*> array;
        unsigned int current;
        unsigned int num;

//        struct wl_list client_list;

		struct weston_animation animation;
		struct wl_list anim_sticky_list;
		int anim_dir;
		struct timespec anim_timestamp;
		double anim_current;
        hb::Workspace *anim_from;
        hb::Workspace *anim_to;
    } workspaces; //

    hb::DesktopShell::InputPanel input_panel; //

    struct exposay exposay; //

    bool allow_zap; //
    uint32_t binding_modifier; //
    uint32_t exposay_modifier; //
    AnimationType win_animation_type; //
    AnimationType win_close_animation_type; //
    AnimationType startup_animation_type; //
    AnimationType focus_animation_type; //

    struct weston_layer minimized_layer; //

    struct wl_listener seat_create_listener; //
    struct wl_listener output_create_listener; //
    struct wl_listener output_move_listener; //
    pr::Vector<hb::ShellOutput*> output_list; //
    pr::Vector<struct shell_seat*> seat_list; //

    enum weston_desktop_shell_panel_position panel_position; //

    char *client; //

    struct timespec startup_time; //
};

#ifdef __cplusplus
extern "C" {
#endif

struct weston_output* get_default_output(struct weston_compositor *compositor);

struct weston_view* get_default_view(struct weston_surface *surface);

struct shell_surface* get_shell_surface(struct weston_surface *surface);

//============================
// Desktop Shell C Methods
//============================

void get_output_work_area(struct desktop_shell *shell,
        struct weston_output *output,
        pixman_rectangle32_t *area);

void lower_fullscreen_layer(struct desktop_shell *shell,
        struct weston_output *lowering_output);

void activate(struct desktop_shell *shell, struct weston_view *view,
        struct weston_seat *seat, uint32_t flags);

int input_panel_setup(struct desktop_shell *shell);

void input_panel_destroy(struct desktop_shell *shell);

typedef void (*shell_for_each_layer_func_t)(struct desktop_shell *,
        struct weston_layer *, void *);

void shell_for_each_layer(struct desktop_shell *shell,
        shell_for_each_layer_func_t func,
        void *data);


void exposay_binding(struct weston_keyboard *keyboard,
        enum weston_keyboard_modifier modifier,
        void *data);

struct weston_transform* view_get_transform(struct weston_view *view);

unsigned int get_output_height(struct weston_output *output);

#ifdef __cplusplus
}
#endif
