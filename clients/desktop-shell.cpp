/*
 * Copyright © 2011 Kristian Høgsberg
 * Copyright © 2011 Collabora, Ltd.
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

#include "config.h"

// C
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/wait.h>
#include <ctype.h>
#include <time.h>
#include <assert.h>

// C++

// Linux
#include <linux/input.h>

// Cairo
#include <cairo.h>

// Primer
#include <primer/string.h>
#include <primer/vector.h>

#include <wayland-client.h>
#include "window.h"
#include "shared/cairo-util.h"
#include <libweston/config-parser.h>
#include "shared/helpers.h"
#include "shared/xalloc.h"
#include <libweston/zalloc.h>
#include "shared/file-util.h"
#include "shared/timespec-util.h"

#include <wayland-protocols/weston/weston-desktop-shell-client-protocol.h>

#define DEFAULT_SPACING 10

extern char **environ; /* defined by libc */

enum class ClockFormat {
    Minutes,
    Seconds,
    Minutes24h,
    Seconds24h,
    Iso,
    None,
};

class Output;
struct panel_launcher;

//=============
// Desktop
//=============

class Desktop *desktop_singleton;

class Desktop
{
public:
    //=========================
    // Desktop Unlock Dialog
    //=========================
    class UnlockDialog
    {
    public:
        UnlockDialog(Desktop *desktop);
        ~UnlockDialog();

        struct window* window();

        struct widget* widget();

        struct widget* button();

        bool button_focused() const;

        /// Set button_focused as true.
        void focus_button();

        /// Set button_focused as false.
        void unfocus_button();

        bool closing() const;

        /// Set closing as true.
        void mark_as_closing();

    private:
        struct window *_window;
        struct widget *_widget;
        struct widget *_button;
        int _button_focused;
        int _closing;
        Desktop *_desktop;
    };

public:
    Desktop()
    {
        this->display = nullptr;
        this->shell = nullptr;
        this->unlock_dialog = nullptr;
//         this->outputs = ;

        this->want_panel = 0;
        this->panel_position = WESTON_DESKTOP_SHELL_PANEL_POSITION_TOP;
        this->clock_format = ClockFormat::Minutes;

        this->grab_window = nullptr;
        this->grab_widget = nullptr;

        this->config = nullptr;
        this->locking = false;

        this->grab_cursor = CURSOR_BLANK;

        this->painted = 0;

        desktop_singleton = this;
    }

    ~Desktop();

    int is_painted() const;

    void parse_panel_position(struct weston_config_section *s);

    void parse_clock_format(struct weston_config_section *s);

    void remove_output(Output *output);

    /// Get singleton instance.
    static Desktop* instance();

public:
    struct display *display;
    struct weston_desktop_shell *shell;
    Desktop::UnlockDialog *unlock_dialog;
    struct task unlock_task;
//    struct wl_list outputs;
    pr::Vector<Output*> outputs;

    int want_panel;
    enum weston_desktop_shell_panel_position panel_position;
    ClockFormat clock_format;

    struct window *grab_window;
    struct widget *grab_widget;

    struct weston_config *config;
    bool locking;

    enum cursor_type grab_cursor;

    int painted;
};

//===========
// Surface
//===========

struct surface {
    void (*configure)(void *data,
        struct weston_desktop_shell *desktop_shell,
        uint32_t edges, struct window *window,
        int32_t width, int32_t height);
};

static void panel_configure(void *data,
        struct weston_desktop_shell *desktop_shell,
        uint32_t edges, struct window *window,
        int32_t width, int32_t height);

//=========
// Panel
//=========

static void panel_redraw_handler(struct widget *widget, void *data);

static void panel_resize_handler(struct widget *widget,
        int32_t width, int32_t height, void *data);

class Panel
{
public:
    //==============
    // Panel Clock
    //==============
    class Clock
    {
        friend Panel;
    public:
        ~Clock();

        int timer_reset();

        struct widget* widget();

        Panel* panel();

        struct toytimer& timer();

        const pr::String& format_string() const;

    private:
        struct widget *_widget;
        Panel *_panel;
        struct toytimer _timer;
        pr::String _format_string;
        time_t _refresh_timer;
    };

public:
    Panel(Output *output);
    ~Panel();

    void add_clock();

    void add_launchers();

    Output* owner();

    void set_owner(Output *output);

    struct window* window();

    struct widget* widget();

    pr::Vector<struct panel_launcher*>& launchers();

    Panel::Clock* clock();

    int painted() const;

    void set_painted(int value);

    enum weston_desktop_shell_panel_position position() const;

    ClockFormat clock_format() const;

    uint32_t color() const;

    void set_color(uint32_t color);

public:
    struct surface base;

private:
    Output *_owner;

    struct window *_window;
    struct widget *_widget;
    pr::Vector<struct panel_launcher*> _launchers;
    Panel::Clock *_clock;
    int _painted;
    enum weston_desktop_shell_panel_position _panel_position;
    ClockFormat _clock_format;
    uint32_t _color;
};

static void clock_func(struct toytimer *tt);

static void panel_clock_redraw_handler(struct widget *widget, void *data);

static void panel_add_launcher(Panel *panel,
        const char *icon, const char *path);

//===============
// Background
//===============

class Background {
public:
    enum class Type {
        Scale,
        ScaleCrop,
        Tile,
        Centered,
        Invalid,
    };

public:
	struct surface base;

    Output *owner;

	struct window *window;
	struct widget *widget;
	int painted;

	char *image;
    Background::Type type;
	uint32_t color;
};

static Background* background_create(Desktop *desktop, Output *output);

static void background_destroy(Background *background);

//===============
// Output
//===============

static void output_handle_geometry(void *data,
        struct wl_output *wl_output,
        int x, int y,
        int physical_width,
        int physical_height,
        int subpixel,
        const char *make,
        const char *model,
        int transform);

static void output_handle_mode(void *data,
        struct wl_output *wl_output,
        uint32_t flags,
        int width,
        int height,
        int refresh);

static void output_handle_done(void *data,
        struct wl_output *wl_output);

static void output_handle_scale(void *data,
        struct wl_output *wl_output,
        int32_t scale);

static const struct wl_output_listener output_listener = {
    output_handle_geometry,
    output_handle_mode,
    output_handle_done,
    output_handle_scale
};

class Output
{
public:
    Output(uint32_t server_output_id);
    ~Output();

    void init();

    uint32_t server_output_id() const;

    int x() const;

    void set_x(int x);

    int y() const;

    void set_y(int y);

    Panel* panel();

    void set_panel(Panel *panel);

    Background* background();

    void set_background(Background *background);

private:
    struct wl_output *_wl_output;
    uint32_t _server_output_id;
    int _x;
    int _y;
    Panel *_panel;
    Background *_background;
};

//==================
// Panel Launcher
//==================

struct panel_launcher {
	struct widget *widget;
    Panel *panel;
	cairo_surface_t *icon;
	int focused, pressed;
	char *path;
	struct wl_list link;
	struct wl_array envp;
	struct wl_array argv;
};

static void panel_destroy_launcher(struct panel_launcher *launcher);


//=====================
// Desktop Methods
//=====================

Desktop::~Desktop()
{
    fprintf(stderr, " [DEBUG] BEGIN ~Desktop()\n");

    // Destroy grab surface.
    widget_destroy(this->grab_widget);
    window_destroy(this->grab_window);

    // Destroy outputs.
    for (auto& output: this->outputs) {
        this->remove_output(output);
        fprintf(stderr, "   - ~Desktop() - remove_output() done.\n");
    }
    if (this->unlock_dialog != nullptr) {
        delete this->unlock_dialog;
    }
    weston_desktop_shell_destroy(this->shell);
    display_destroy(this->display);
    weston_config_destroy(this->config);

    fprintf(stderr, " [DEBUG] END ~Desktop()\n");
}

int Desktop::is_painted() const
{
    for (const auto& output: this->outputs) {
        if (output->panel() && !output->panel()->painted()) {
            return 0;
        }
        if (output->background() && !output->background()->painted) {
            return 0;
        }
    }

    return 1;
}

void Desktop::parse_panel_position(struct weston_config_section *s)
{
    char *position_buf;
    pr::String position;

    this->want_panel = 1;

    weston_config_section_get_string(s, "panel-position", &position_buf, "top");
    position = position_buf;
    free(position_buf);
    if (position == "top") {
        this->panel_position = WESTON_DESKTOP_SHELL_PANEL_POSITION_TOP;
    } else if (position == "bottom") {
        this->panel_position = WESTON_DESKTOP_SHELL_PANEL_POSITION_BOTTOM;
    } else if (position == "left") {
        this->panel_position = WESTON_DESKTOP_SHELL_PANEL_POSITION_LEFT;
    } else if (position == "right") {
        this->panel_position = WESTON_DESKTOP_SHELL_PANEL_POSITION_RIGHT;
    } else {
        // 'none' is valid here.
        if (position == "none") {
            fprintf(stderr, "Wrong panel position: %s\n", "none");
        }
        this->want_panel = 0;
    }
}

void Desktop::parse_clock_format(struct weston_config_section *s)
{
    char *clock_format;

    weston_config_section_get_string(s, "clock-format", &clock_format, "");
    if (strcmp(clock_format, "minutes") == 0)
        this->clock_format = ClockFormat::Minutes;
    else if (strcmp(clock_format, "seconds") == 0)
        this->clock_format = ClockFormat::Seconds;
    else if (strcmp(clock_format, "minutes-24h") == 0)
        this->clock_format = ClockFormat::Minutes24h;
    else if (strcmp(clock_format, "seconds-24h") == 0)
        this->clock_format = ClockFormat::Seconds24h;
    else if (strcmp(clock_format, "none") == 0)
        this->clock_format = ClockFormat::None;
    else
        this->clock_format = ClockFormat::Iso;
    free(clock_format);
}

void Desktop::remove_output(Output *output)
{
    Output *rep = nullptr;

    if (!output->background()) {
        delete output;
        return;
    }

    // Find a wl_output that is a clone of the removed wl_output.
    // We don't want to leave the clone without a background or panel.
    for (auto& cur: this->outputs) {
        if (cur == output) {
            continue;
        }

        // XXX: Assumes size matches.
        if (cur->x() == output->x() && cur->y() == output->y()) {
            rep = cur;
            break;
        }
    }

    if (rep) {
        /* If found and it does not already have a background or panel,
         * hand over the background and panel so they don't get
         * destroyed.
         *
         * We never create multiple backgrounds or panels for clones,
         * but if the compositor moves outputs, a pair of wl_outputs
         * might become "clones". This may happen temporarily when
         * an output is about to be removed and the rest are reflowed.
         * In this case it is correct to let the background/panel be
         * destroyed.
         */

        if (!rep->background()) {
            rep->set_background(output->background());
            output->set_background(nullptr);
            rep->background()->owner = rep;
        }

        if (!rep->panel()) {
            rep->set_panel(output->panel());
            output->set_panel(nullptr);
            if (rep->panel()) {
                rep->panel()->set_owner(rep);
            }
        }
    }

    delete output;
}

Desktop* Desktop::instance()
{
    return desktop_singleton;
}

//==================
// Output Methods
//==================
Output::Output(uint32_t server_output_id)
{
    Desktop *desktop = Desktop::instance();

    this->_wl_output = static_cast<struct wl_output*>(
        display_bind(desktop->display, server_output_id, &wl_output_interface, 2));
    this->_server_output_id = server_output_id;

    this->_panel = nullptr;
    this->_background = nullptr;

    wl_output_add_listener(this->_wl_output,
        &output_listener,
        static_cast<void*>(this));

    desktop->outputs.push(this);

    /* On start up we may process an output global before the shell global
     * in which case we can't create the panel and background just yet */
    if (desktop->shell) {
//        output_init(output, desktop);
        this->init();
    }
}

Output::~Output()
{
    if (this->_background) {
        background_destroy(this->_background);
    }
    if (this->_panel) {
        delete this->_panel;
    }
    wl_output_destroy(this->_wl_output);
}

void Output::init()
{
    struct wl_surface *surface;
    Desktop *desktop = Desktop::instance();

    if (desktop->want_panel) {
        this->_panel = new Panel(this);
        surface = window_get_wl_surface(this->_panel->window());
        weston_desktop_shell_set_panel(desktop->shell,
            this->_wl_output, surface);
    }

    this->_background = background_create(desktop, this);
    surface = window_get_wl_surface(this->_background->window);
    weston_desktop_shell_set_background(desktop->shell,
        this->_wl_output, surface);
}

uint32_t Output::server_output_id() const
{
    return this->_server_output_id;
}

int Output::x() const
{
    return this->_x;
}

void Output::set_x(int x)
{
    if (this->_x != x) {
        this->_x = x;
    }
}

int Output::y() const
{
    return this->_y;
}

void Output::set_y(int y)
{
    if (this->_y != y) {
        this->_y = y;
    }
}

Panel* Output::panel()
{
    return this->_panel;
}

void Output::set_panel(Panel *panel)
{
    this->_panel = panel;
}

Background* Output::background()
{
    return this->_background;
}

void Output::set_background(Background *background)
{
    this->_background = background;
}

//=================
// Panel Methods
//=================
Panel::Panel(Output *output)
{
    struct weston_config_section *s;
    Desktop *desktop = Desktop::instance();

    this->_clock = nullptr;

    this->_owner = output;
    this->base.configure = panel_configure;
    this->_window = window_create_custom(desktop->display);
    this->_widget = window_add_widget(this->_window,
        static_cast<void*>(this));

    window_set_title(this->_window, "panel");
    window_set_user_data(this->_window,
        static_cast<void*>(this));

    widget_set_redraw_handler(this->_widget, panel_redraw_handler);
    widget_set_resize_handler(this->_widget, panel_resize_handler);

    this->_panel_position = desktop->panel_position;
    this->_clock_format = desktop->clock_format;
    if (this->_clock_format != ClockFormat::None) {
        this->add_clock();
    }

    s = weston_config_get_section(desktop->config, "shell", NULL, NULL);
    weston_config_section_get_color(s, "panel-color",
        &this->_color, 0xaa000000);

    this->add_launchers();
}

Panel::~Panel()
{
    if (this->_clock) {
        delete this->_clock;
        this->_clock = nullptr;
    }

    for (auto& launcher: this->_launchers) {
        panel_destroy_launcher(launcher);
    }

    widget_destroy(this->_widget);
    window_destroy(this->_window);
}

void Panel::add_clock()
{
    this->_clock = new Panel::Clock();
    Panel::Clock *clock = this->_clock;

    clock->_panel = this;
    this->_clock = clock;

    switch (this->_clock_format) {
    case ClockFormat::Iso:
        clock->_format_string = "%Y-%m-%dT%H:%M:%S";
        clock->_refresh_timer = 1;
        break;
    case ClockFormat::Minutes:
        clock->_format_string = "%a %b %d, %I:%M %p";
        clock->_refresh_timer = 60;
        break;
    case ClockFormat::Seconds:
        clock->_format_string = "%a %b %d, %I:%M:%S %p";
        clock->_refresh_timer = 1;
        break;
    case ClockFormat::Minutes24h:
        clock->_format_string = "%a %b %d, %H:%M";
        clock->_refresh_timer = 60;
        break;
    case ClockFormat::Seconds24h:
        clock->_format_string = "%a %b %d, %H:%M:%S";
        clock->_refresh_timer = 1;
        break;
    case ClockFormat::None:
        assert(!"not reached");
    }

    toytimer_init(&clock->_timer, CLOCK_MONOTONIC,
        window_get_display(this->_window), clock_func);
    clock->timer_reset();

    clock->_widget = widget_add_widget(this->_widget, clock);
    widget_set_redraw_handler(clock->_widget, panel_clock_redraw_handler);
}

void Panel::add_launchers()
{
    struct weston_config_section *s;
    char *icon, *path;
    const char *name;
    int count;
    Desktop *desktop = Desktop::instance();

    count = 0;
    s = NULL;
    while (weston_config_next_section(desktop->config, &s, &name)) {
        if (strcmp(name, "launcher") != 0)
            continue;

        weston_config_section_get_string(s, "icon", &icon, NULL);
        weston_config_section_get_string(s, "path", &path, NULL);

        if (icon != NULL && path != NULL) {
            panel_add_launcher(this, icon, path);
            // Create launchers and add.
            //
            count++;
        } else {
            fprintf(stderr, "invalid launcher section\n");
        }

        free(icon);
        free(path);
    }

    if (count == 0) {
        char *name = file_name_with_datadir("terminal.png");

        /* add default launcher */
        panel_add_launcher(this,
                   name,
                   BINDIR "/weston-terminal");
        free(name);
    }
}

Output* Panel::owner()
{
    return this->_owner;
}

void Panel::set_owner(Output *output)
{
    this->_owner = output;
}

struct window* Panel::window()
{
    return this->_window;
}

struct widget* Panel::widget()
{
    return this->_widget;
}

pr::Vector<struct panel_launcher*>& Panel::launchers()
{
    return this->_launchers;
}

Panel::Clock* Panel::clock()
{
    return this->_clock;
}

int Panel::painted() const
{
    return this->_painted;
}

void Panel::set_painted(int value)
{
    this->_painted = value;
}

enum weston_desktop_shell_panel_position Panel::position() const
{
    return this->_panel_position;
}

ClockFormat Panel::clock_format() const
{
    return this->_clock_format;
}

uint32_t Panel::color() const
{
    return this->_color;
}

void Panel::set_color(uint32_t color)
{
    this->_color = color;
}

//=======================
// Panel Clock Methods
//=======================
Panel::Clock::~Clock()
{
    widget_destroy(this->_widget);
    toytimer_fini(&this->_timer);
}

int Panel::Clock::timer_reset()
{
    struct itimerspec its;
    struct timespec ts;
    struct tm *tm;

    clock_gettime(CLOCK_REALTIME, &ts);
    tm = localtime(&ts.tv_sec);

    its.it_interval.tv_sec = this->_refresh_timer;
    its.it_interval.tv_nsec = 0;
    its.it_value.tv_sec =
        this->_refresh_timer - tm->tm_sec % this->_refresh_timer;
    its.it_value.tv_nsec = 10000000; /* 10 ms late to ensure the clock digit has actually changed */
    timespec_add_nsec(&its.it_value, &its.it_value, -ts.tv_nsec);

    toytimer_arm(&this->_timer, &its);
    return 0;
}

struct widget* Panel::Clock::widget()
{
    return this->_widget;
}

Panel* Panel::Clock::panel()
{
    return this->_panel;
}

struct toytimer& Panel::Clock::timer()
{
    return this->_timer;
}

const pr::String& Panel::Clock::format_string() const
{
    return this->_format_string;
}


//===================
// C Functions
//===================

static void
sigchild_handler(int s)
{
    (void)s;
	int status;
	pid_t pid;

	while (pid = waitpid(-1, &status, WNOHANG), pid > 0)
		fprintf(stderr, "child %d exited\n", pid);
}

static void
check_desktop_ready(struct window *window)
{
	struct display *display;
    Desktop *desktop;

    display = window_get_display(window);
    desktop = static_cast<Desktop*>(display_get_user_data(display));

    if (!desktop->painted && desktop->is_painted()) {
        desktop->painted = 1;

		weston_desktop_shell_desktop_ready(desktop->shell);
	}
}

static void
panel_launcher_activate(struct panel_launcher *widget)
{
    char **argv;
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        return;
    }

    if (pid) {
        return;
    }

    argv = static_cast<char**>(widget->argv.data);

    if (setsid() == -1)
        exit(EXIT_FAILURE);

    char* const* envp_data = static_cast<char* const*>(widget->envp.data);
    if (execve(argv[0], argv, envp_data) < 0) {
        fprintf(stderr, "execl '%s' failed: %s\n", argv[0],
            strerror(errno));
        exit(1);
    }
}

static void
panel_launcher_redraw_handler(struct widget *widget, void *data)
{
    struct panel_launcher *launcher = static_cast<struct panel_launcher*>(data);
	struct rectangle allocation;
	cairo_t *cr;

    cr = widget_cairo_create(launcher->panel->widget());

	widget_get_allocation(widget, &allocation);
	allocation.x += allocation.width / 2 -
		cairo_image_surface_get_width(launcher->icon) / 2;
	if (allocation.width > allocation.height)
		allocation.x += allocation.width / 2 - allocation.height / 2;
	allocation.y += allocation.height / 2 -
		cairo_image_surface_get_height(launcher->icon) / 2;
	if (allocation.height > allocation.width)
		allocation.y += allocation.height / 2 - allocation.width / 2;
	if (launcher->pressed) {
		allocation.x++;
		allocation.y++;
	}

	cairo_set_source_surface(cr, launcher->icon,
				 allocation.x, allocation.y);
	cairo_paint(cr);

	if (launcher->focused) {
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.4);
		cairo_mask_surface(cr, launcher->icon,
				   allocation.x, allocation.y);
	}

	cairo_destroy(cr);
}

static int
panel_launcher_motion_handler(struct widget *widget, struct input *input,
        uint32_t time, float x, float y, void *data)
{
    (void)input;
    (void)time;
    struct panel_launcher *launcher = static_cast<struct panel_launcher*>(data);

    widget_set_tooltip(widget, basename((char *)launcher->path), x, y);

    return CURSOR_LEFT_PTR;
}

static void
set_hex_color(cairo_t *cr, uint32_t color)
{
	cairo_set_source_rgba(cr,
			      ((color >> 16) & 0xff) / 255.0,
			      ((color >>  8) & 0xff) / 255.0,
			      ((color >>  0) & 0xff) / 255.0,
			      ((color >> 24) & 0xff) / 255.0);
}

static void panel_redraw_handler(struct widget *widget, void *data)
{
    (void)widget;

	cairo_surface_t *surface;
	cairo_t *cr;
    Panel *panel = static_cast<Panel*>(data);

    cr = widget_cairo_create(panel->widget());
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    set_hex_color(cr, panel->color());
	cairo_paint(cr);

	cairo_destroy(cr);
    surface = window_get_surface(panel->window());
	cairo_surface_destroy(surface);
    panel->set_painted(1);
    check_desktop_ready(panel->window());
}

static int panel_launcher_enter_handler(struct widget *widget,
        struct input *input, float x, float y, void *data)
{
    (void)input;
    (void)x;
    (void)y;
	struct panel_launcher *launcher = static_cast<struct panel_launcher*>(data);

	launcher->focused = 1;
	widget_schedule_redraw(widget);

	return CURSOR_LEFT_PTR;
}

static void panel_launcher_leave_handler(struct widget *widget,
        struct input *input, void *data)
{
    (void)input;
	struct panel_launcher *launcher = static_cast<struct panel_launcher*>(data);

	launcher->focused = 0;
	widget_destroy_tooltip(widget);
	widget_schedule_redraw(widget);
}

static void panel_launcher_button_handler(struct widget *widget,
        struct input *input, uint32_t time,
        uint32_t button,
        enum wl_pointer_button_state state, void *data)
{
    (void)input;
    (void)time;
    (void)button;
    (void)data;
	struct panel_launcher *launcher;

	launcher = (struct panel_launcher*)widget_get_user_data(widget);
	widget_schedule_redraw(widget);
	if (state == WL_POINTER_BUTTON_STATE_RELEASED)
		panel_launcher_activate(launcher);

}

static void panel_launcher_touch_down_handler(struct widget *widget,
        struct input *input,
        uint32_t serial, uint32_t time, int32_t id,
        float x, float y, void *data)
{
    (void)input;
    (void)serial;
    (void)time;
    (void)id;
    (void)x;
    (void)y;
    (void)data;
	struct panel_launcher *launcher;

	launcher = (struct panel_launcher*)widget_get_user_data(widget);
	launcher->focused = 1;
	widget_schedule_redraw(widget);
}

static void panel_launcher_touch_up_handler(struct widget *widget,
        struct input *input,
        uint32_t serial, uint32_t time, int32_t id,
        void *data)
{
    (void)input;
    (void)serial;
    (void)time;
    (void)id;
    (void)data;
	struct panel_launcher *launcher;

	launcher = (struct panel_launcher*)widget_get_user_data(widget);
	launcher->focused = 0;
	widget_schedule_redraw(widget);
	panel_launcher_activate(launcher);
}

static void clock_func(struct toytimer *tt)
{
    Panel::Clock *clock = nullptr;
    Desktop *desktop = Desktop::instance();

    for (auto& output: desktop->outputs) {
        if (&output->panel()->clock()->timer() == tt) {
            clock = output->panel()->clock();
            break;
        }
    }

    assert(clock != nullptr);

    widget_schedule_redraw(clock->widget());
}

static void
panel_clock_redraw_handler(struct widget *widget, void *data)
{
    Panel::Clock *clock = static_cast<Panel::Clock*>(data);
	cairo_t *cr;
	struct rectangle allocation;
	cairo_text_extents_t extents;
	time_t rawtime;
	struct tm * timeinfo;
	char string[128];

	time(&rawtime);
	timeinfo = localtime(&rawtime);
    strftime(string, sizeof string, clock->format_string().c_str(), timeinfo);

	widget_get_allocation(widget, &allocation);
	if (allocation.width == 0)
		return;

    cr = widget_cairo_create(clock->panel()->widget());
	cairo_set_font_size(cr, 14);
	cairo_text_extents(cr, string, &extents);
	if (allocation.x > 0)
		allocation.x +=
			allocation.width - DEFAULT_SPACING * 1.5 - extents.width;
	else
		allocation.x +=
			allocation.width / 2 - extents.width / 2;
	allocation.y += allocation.height / 2 - 1 + extents.height / 2;
	cairo_move_to(cr, allocation.x + 1, allocation.y + 1);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.85);
	cairo_show_text(cr, string);
	cairo_move_to(cr, allocation.x, allocation.y);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
	cairo_show_text(cr, string);
	cairo_destroy(cr);
}

static void panel_resize_handler(struct widget *widget,
        int32_t width, int32_t height, void *data)
{
    (void)widget;
    Panel *panel = static_cast<Panel*>(data);
	int x = 0;
	int y = 0;
	int w = height > width ? width : height;
	int h = w;
    int horizontal = panel->position() == WESTON_DESKTOP_SHELL_PANEL_POSITION_TOP || panel->position() == WESTON_DESKTOP_SHELL_PANEL_POSITION_BOTTOM;
	int first_pad_h = horizontal ? 0 : DEFAULT_SPACING / 2;
	int first_pad_w = horizontal ? DEFAULT_SPACING / 2 : 0;

    for (auto& launcher: panel->launchers()) {
        //
        widget_set_allocation(launcher->widget, x, y,
            w + first_pad_w + 1, h + first_pad_h + 1);
        if (horizontal) {
            x += w + first_pad_w;
        } else {
            y += h + first_pad_h;
        }
        first_pad_h = first_pad_w = 0;
    }

    if (panel->clock_format() == ClockFormat::Seconds) {
        w = 170;
    } else { /* CLOCK_FORMAT_MINUTES and 24H versions */
        w = 150;
    }

	if (horizontal)
		x = width - w;
	else
		y = height - (h = DEFAULT_SPACING * 3);

    if (panel->clock()) {
        widget_set_allocation(panel->clock()->widget(),
            x, y, w + 1, h + 1);
    }
}

static void
panel_destroy_launcher(struct panel_launcher *launcher)
{
	wl_array_release(&launcher->argv);
	wl_array_release(&launcher->envp);

	free(launcher->path);

	cairo_surface_destroy(launcher->icon);

	widget_destroy(launcher->widget);
	wl_list_remove(&launcher->link);

	free(launcher);
}

static cairo_surface_t *
load_icon_or_fallback(const char *icon)
{
	cairo_surface_t *surface = cairo_image_surface_create_from_png(icon);
	cairo_status_t status;
	cairo_t *cr;

	status = cairo_surface_status(surface);
	if (status == CAIRO_STATUS_SUCCESS)
		return surface;

	cairo_surface_destroy(surface);
	fprintf(stderr, "ERROR loading icon from file '%s', error: '%s'\n",
		icon, cairo_status_to_string(status));

	/* draw fallback icon */
	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
					     20, 20);
	cr = cairo_create(surface);

	cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 1);
	cairo_paint(cr);

	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_rectangle(cr, 0, 0, 20, 20);
	cairo_move_to(cr, 4, 4);
	cairo_line_to(cr, 16, 16);
	cairo_move_to(cr, 4, 16);
	cairo_line_to(cr, 16, 4);
	cairo_stroke(cr);

	cairo_destroy(cr);

	return surface;
}

static void panel_add_launcher(Panel *panel, const char *icon, const char *path)
{
    struct panel_launcher *launcher;
    char *start, *p, *eq, **ps;
    int i, j, k;

    launcher = (struct panel_launcher*)xzalloc(sizeof *launcher);
    launcher->icon = load_icon_or_fallback(icon);
    launcher->path = (char*)xstrdup(path);

    wl_array_init(&launcher->envp);
    wl_array_init(&launcher->argv);
    for (i = 0; environ[i]; i++) {
        ps = (char**)wl_array_add(&launcher->envp, sizeof *ps);
        *ps = environ[i];
    }
    j = 0;

    start = launcher->path;
    while (*start) {
        for (p = start, eq = NULL; *p && !isspace(*p); p++) {
            if (*p == '=') {
                eq = p;
            }
        }

		if (eq && j == 0) {
			ps = (char**)launcher->envp.data;
			for (k = 0; k < i; k++)
				if (strncmp(ps[k], start, eq - start) == 0) {
					ps[k] = start;
					break;
				}
			if (k == i) {
				ps = (char**)wl_array_add(&launcher->envp, sizeof *ps);
				*ps = start;
				i++;
			}
		} else {
			ps = (char**)wl_array_add(&launcher->argv, sizeof *ps);
			*ps = start;
			j++;
		}

        while (*p && isspace(*p)) {
            *p++ = '\0';
        }

        start = p;
    }

	ps = (char**)wl_array_add(&launcher->envp, sizeof *ps);
	*ps = NULL;
	ps = (char**)wl_array_add(&launcher->argv, sizeof *ps);
	*ps = NULL;

	launcher->panel = panel;
//	wl_list_insert(panel->launcher_list.prev, &launcher->link);
    panel->launchers().push(launcher);

    launcher->widget = widget_add_widget(panel->widget(), launcher);
	widget_set_enter_handler(launcher->widget,
				 panel_launcher_enter_handler);
	widget_set_leave_handler(launcher->widget,
				   panel_launcher_leave_handler);
	widget_set_button_handler(launcher->widget,
				    panel_launcher_button_handler);
	widget_set_touch_down_handler(launcher->widget,
				      panel_launcher_touch_down_handler);
	widget_set_touch_up_handler(launcher->widget,
				    panel_launcher_touch_up_handler);
	widget_set_redraw_handler(launcher->widget,
				  panel_launcher_redraw_handler);
	widget_set_motion_handler(launcher->widget,
				  panel_launcher_motion_handler);
}

static void background_draw(struct widget *widget, void *data)
{
    Background *background = static_cast<Background*>(data);
    cairo_surface_t *surface, *image;
    cairo_pattern_t *pattern;
    cairo_matrix_t matrix;
    cairo_t *cr;
    double im_w, im_h;
    double sx, sy, s;
    double tx, ty;
    struct rectangle allocation;

	surface = window_get_surface(background->window);

	cr = widget_cairo_create(background->widget);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	if (background->color == 0)
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.2, 1.0);
	else
		set_hex_color(cr, background->color);
	cairo_paint(cr);

	widget_get_allocation(widget, &allocation);
	image = NULL;
	if (background->image)
		image = load_cairo_surface(background->image);
	else if (background->color == 0) {
		char *name = file_name_with_datadir("pattern.png");

		image = load_cairo_surface(name);
		free(name);
	}

    if (image && background->type != Background::Type::Invalid) {
		im_w = cairo_image_surface_get_width(image);
		im_h = cairo_image_surface_get_height(image);
		sx = im_w / allocation.width;
		sy = im_h / allocation.height;

		pattern = cairo_pattern_create_for_surface(image);

		switch (background->type) {
        case Background::Type::Invalid:
            break;
        case Background::Type::Scale:
			cairo_matrix_init_scale(&matrix, sx, sy);
			cairo_pattern_set_matrix(pattern, &matrix);
			cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);
			break;
        case Background::Type::ScaleCrop:
			s = (sx < sy) ? sx : sy;
			/* align center */
			tx = (im_w - s * allocation.width) * 0.5;
			ty = (im_h - s * allocation.height) * 0.5;
			cairo_matrix_init_translate(&matrix, tx, ty);
			cairo_matrix_scale(&matrix, s, s);
			cairo_pattern_set_matrix(pattern, &matrix);
			cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);
			break;
        case Background::Type::Tile:
			cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
			break;
        case Background::Type::Centered:
			s = (sx < sy) ? sx : sy;
			if (s < 1.0)
				s = 1.0;

			/* align center */
			tx = (im_w - s * allocation.width) * 0.5;
			ty = (im_h - s * allocation.height) * 0.5;

			cairo_matrix_init_translate(&matrix, tx, ty);
			cairo_matrix_scale(&matrix, s, s);
			cairo_pattern_set_matrix(pattern, &matrix);
			break;
		}

		cairo_set_source(cr, pattern);
		cairo_pattern_destroy (pattern);
		cairo_surface_destroy(image);
		cairo_mask(cr, pattern);
	}

	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	background->painted = 1;
	check_desktop_ready(background->window);
}

//========================================
// Struct Surface Configure Functions
//========================================

static void panel_configure(void *data,
        struct weston_desktop_shell *desktop_shell,
        uint32_t edges, struct window *window,
        int32_t width, int32_t height)
{
    (void)desktop_shell;
    (void)edges;
    Desktop *desktop = static_cast<Desktop*>(data);
    struct surface *surface = (struct surface*)window_get_user_data(window);
    Panel *panel = container_of(surface, Panel, base);
    Output *owner;

    if (width < 1 || height < 1) {
        /* Shell plugin configures 0x0 for redundant panel. */
        owner = panel->owner();
        delete panel;
        owner->set_panel(nullptr);
        return;
    }

    switch (desktop->panel_position) {
    case WESTON_DESKTOP_SHELL_PANEL_POSITION_TOP:
    case WESTON_DESKTOP_SHELL_PANEL_POSITION_BOTTOM:
        height = 32;
        break;
    case WESTON_DESKTOP_SHELL_PANEL_POSITION_LEFT:
    case WESTON_DESKTOP_SHELL_PANEL_POSITION_RIGHT:
        switch (desktop->clock_format) {
        case ClockFormat::Iso:
        case ClockFormat::None:
            width = 32;
            break;
        case ClockFormat::Minutes:
        case ClockFormat::Minutes24h:
        case ClockFormat::Seconds24h:
            width = 150;
            break;
        case ClockFormat::Seconds:
            width = 170;
            break;
        }
        break;
    }
    window_schedule_resize(panel->window(), width, height);
}

static void background_configure(void *data,
        struct weston_desktop_shell *desktop_shell,
        uint32_t edges, struct window *window,
        int32_t width, int32_t height)
{
    (void)data;
    (void)desktop_shell;
    (void)edges;
    Output *owner;
    Background *background =
        (Background*) window_get_user_data(window);

	if (width < 1 || height < 1) {
		/* Shell plugin configures 0x0 for redundant background. */
		owner = background->owner;
        background_destroy(background);
        owner->set_background(nullptr);
		return;
	}

	if (!background->image && background->color) {
		widget_set_viewport_destination(background->widget, width, height);
		width = 1;
		height = 1;
	}

	widget_schedule_resize(background->widget, width, height);
}


//=================================
// Desktop Unlock Dialog Handlers
//=================================

static void unlock_dialog_redraw_handler(struct widget *widget, void *data)
{
    Desktop::UnlockDialog *dialog = static_cast<Desktop::UnlockDialog*>(data);
	struct rectangle allocation;
	cairo_surface_t *surface;
	cairo_t *cr;
	cairo_pattern_t *pat;
	double cx, cy, r, f;

	cr = widget_cairo_create(widget);

    widget_get_allocation(dialog->widget(), &allocation);
	cairo_rectangle(cr, allocation.x, allocation.y,
			allocation.width, allocation.height);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.6);
	cairo_fill(cr);

	cairo_translate(cr, allocation.x, allocation.y);
    if (dialog->button_focused()) {
        f = 1.0;
    } else {
        f = 0.7;
    }

	cx = allocation.width / 2.0;
	cy = allocation.height / 2.0;
	r = (cx < cy ? cx : cy) * 0.4;
	pat = cairo_pattern_create_radial(cx, cy, r * 0.7, cx, cy, r);
	cairo_pattern_add_color_stop_rgb(pat, 0.0, 0, 0.86 * f, 0);
	cairo_pattern_add_color_stop_rgb(pat, 0.85, 0.2 * f, f, 0.2 * f);
	cairo_pattern_add_color_stop_rgb(pat, 1.0, 0, 0.86 * f, 0);
	cairo_set_source(cr, pat);
	cairo_pattern_destroy(pat);
	cairo_arc(cr, cx, cy, r, 0.0, 2.0 * M_PI);
	cairo_fill(cr);

    widget_set_allocation(dialog->button(),
			      allocation.x + cx - r,
			      allocation.y + cy - r, 2 * r, 2 * r);

	cairo_destroy(cr);

    surface = window_get_surface(dialog->window());
    cairo_surface_destroy(surface);
}

static void unlock_dialog_button_handler(struct widget *widget,
        struct input *input, uint32_t time,
        uint32_t button,
        enum wl_pointer_button_state state, void *data)
{
    (void)widget;
    (void)input;
    (void)time;
    Desktop::UnlockDialog *dialog = static_cast<Desktop::UnlockDialog*>(data);
    Desktop *desktop = Desktop::instance();

    assert(desktop != nullptr);

    if (button == BTN_LEFT) {
        if (state == WL_POINTER_BUTTON_STATE_RELEASED &&
                !dialog->closing()) {
            display_defer(desktop->display, &desktop->unlock_task);
            dialog->mark_as_closing();
        }
    }
}

static void unlock_dialog_touch_down_handler(struct widget *widget,
        struct input *input,
        uint32_t serial, uint32_t time, int32_t id,
        float x, float y, void *data)
{
    (void)input;
    (void)serial;
    (void)time;
    (void)id;
    (void)x;
    (void)y;
    Desktop::UnlockDialog *dialog = static_cast<Desktop::UnlockDialog*>(data);

    dialog->focus_button();
    widget_schedule_redraw(widget);
}

static void unlock_dialog_touch_up_handler(struct widget *widget,
        struct input *input,
        uint32_t serial, uint32_t time, int32_t id,
        void *data)
{
    (void)input;
    (void)serial;
    (void)time;
    (void)id;
    Desktop::UnlockDialog *dialog = static_cast<Desktop::UnlockDialog*>(data);
    Desktop *desktop = Desktop::instance();

    assert(desktop != nullptr);

    dialog->unfocus_button();
	widget_schedule_redraw(widget);
	display_defer(desktop->display, &desktop->unlock_task);
    dialog->mark_as_closing();
}

static void unlock_dialog_keyboard_focus_handler(struct window *window,
        struct input *device, void *data)
{
    (void)device;
    (void)data;
    window_schedule_redraw(window);
}

static int unlock_dialog_widget_enter_handler(struct widget *widget,
        struct input *input,
        float x, float y, void *data)
{
    (void)input;
    (void)x;
    (void)y;
    Desktop::UnlockDialog *dialog = static_cast<Desktop::UnlockDialog*>(data);

    dialog->focus_button();
	widget_schedule_redraw(widget);

	return CURSOR_LEFT_PTR;
}

static void unlock_dialog_widget_leave_handler(struct widget *widget,
        struct input *input, void *data)
{
    (void)input;
    Desktop::UnlockDialog *dialog = static_cast<Desktop::UnlockDialog*>(data);

    dialog->unfocus_button();
	widget_schedule_redraw(widget);
}

//==================================
// Desktop Unlock Dialog Methods
//==================================
Desktop::UnlockDialog::UnlockDialog(Desktop *desktop)
{
    struct display *display = desktop->display;
    struct wl_surface *surface;

    this->_desktop = desktop;

    this->_window = window_create_custom(display);
    this->_widget = window_frame_create(this->_window,
        static_cast<void*>(this));
    window_set_title(this->_window, "Unlock your desktop");

    // Set handlers.
    window_set_user_data(this->_window, static_cast<void*>(this));
    window_set_keyboard_focus_handler(this->_window,
        unlock_dialog_keyboard_focus_handler);
    this->_button = widget_add_widget(this->_widget,
        static_cast<void*>(this));
    widget_set_redraw_handler(this->_widget,
        unlock_dialog_redraw_handler);
    widget_set_enter_handler(this->_button,
        unlock_dialog_widget_enter_handler);
    widget_set_leave_handler(this->_button,
        unlock_dialog_widget_leave_handler);
    widget_set_button_handler(this->_button,
        unlock_dialog_button_handler);
    widget_set_touch_down_handler(this->_button,
        unlock_dialog_touch_down_handler);
    widget_set_touch_up_handler(this->_button,
        unlock_dialog_touch_up_handler);

    surface = window_get_wl_surface(this->_window);
	weston_desktop_shell_set_lock_surface(desktop->shell, surface);

    window_schedule_resize(this->_window, 260, 230);
}

Desktop::UnlockDialog::~UnlockDialog()
{
    window_destroy(this->_window);
}

struct window* Desktop::UnlockDialog::window()
{
    return this->_window;
}

struct widget* Desktop::UnlockDialog::widget()
{
    return this->_widget;
}

struct widget* Desktop::UnlockDialog::button()
{
    return this->_button;
}

bool Desktop::UnlockDialog::button_focused() const
{
    return this->_button_focused == 1;
}

void Desktop::UnlockDialog::focus_button()
{
    this->_button_focused = 1;
}

void Desktop::UnlockDialog::unfocus_button()
{
    this->_button_focused = 0;
}

bool Desktop::UnlockDialog::closing() const
{
    return this->_closing;
}

void Desktop::UnlockDialog::mark_as_closing()
{
    this->_closing = 1;
}


static void unlock_dialog_finish(struct task *task, uint32_t events)
{
    (void)task;
    (void)events;
    Desktop *desktop = Desktop::instance();

    assert(desktop != nullptr);

    weston_desktop_shell_unlock(desktop->shell);
    delete desktop->unlock_dialog;
    desktop->unlock_dialog = nullptr;
}

//===========================================
// weston_desktop_shell_listener handlers
//===========================================

static void desktop_shell_configure(void *data,
        struct weston_desktop_shell *desktop_shell,
        uint32_t edges,
        struct wl_surface *surface,
        int32_t width, int32_t height)
{
	struct window *window = (struct window*)wl_surface_get_user_data(surface);
	struct surface *s = (struct surface*)window_get_user_data(window);

	s->configure(data, desktop_shell, edges, window, width, height);
}

static void desktop_shell_prepare_lock_surface(void *data,
        struct weston_desktop_shell *desktop_shell)
{
    (void)desktop_shell;
    Desktop *desktop = static_cast<Desktop*>(data);

	if (!desktop->locking) {
		weston_desktop_shell_unlock(desktop->shell);
		return;
	}

    if (!desktop->unlock_dialog) {
        desktop->unlock_dialog = new Desktop::UnlockDialog(desktop);
    }
}

static void desktop_shell_grab_cursor(void *data,
        struct weston_desktop_shell *desktop_shell, uint32_t cursor)
{
    (void)desktop_shell;
    Desktop *desktop = static_cast<Desktop*>(data);

	switch (cursor) {
	case WESTON_DESKTOP_SHELL_CURSOR_NONE:
		desktop->grab_cursor = CURSOR_BLANK;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_BUSY:
		desktop->grab_cursor = CURSOR_WATCH;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_MOVE:
		desktop->grab_cursor = CURSOR_DRAGGING;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_TOP:
		desktop->grab_cursor = CURSOR_TOP;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_BOTTOM:
		desktop->grab_cursor = CURSOR_BOTTOM;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_LEFT:
		desktop->grab_cursor = CURSOR_LEFT;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_RIGHT:
		desktop->grab_cursor = CURSOR_RIGHT;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_TOP_LEFT:
		desktop->grab_cursor = CURSOR_TOP_LEFT;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_TOP_RIGHT:
		desktop->grab_cursor = CURSOR_TOP_RIGHT;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_BOTTOM_LEFT:
		desktop->grab_cursor = CURSOR_BOTTOM_LEFT;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_RESIZE_BOTTOM_RIGHT:
		desktop->grab_cursor = CURSOR_BOTTOM_RIGHT;
		break;
	case WESTON_DESKTOP_SHELL_CURSOR_ARROW:
	default:
		desktop->grab_cursor = CURSOR_LEFT_PTR;
	}
}

static const struct weston_desktop_shell_listener desktop_shell_listener = {
    desktop_shell_configure,
    desktop_shell_prepare_lock_surface,
    desktop_shell_grab_cursor,
};


static void background_destroy(Background *background)
{
	widget_destroy(background->widget);
	window_destroy(background->window);

	free(background->image);
	free(background);
}

static Background* background_create(Desktop *desktop,
        Output *output)
{
    fprintf(stderr, " [DEBUG] BEGIN background_create()\n");

    Background *background;
	struct weston_config_section *s;
	char *type;

    background = (Background*)xzalloc(sizeof *background);
	background->owner = output;
	background->base.configure = background_configure;
	background->window = window_create_custom(desktop->display);
	background->widget = window_add_widget(background->window, background);
	window_set_user_data(background->window, background);
	widget_set_redraw_handler(background->widget, background_draw);
	widget_set_transparent(background->widget, 0);

	s = weston_config_get_section(desktop->config, "shell", NULL, NULL);
	weston_config_section_get_string(s, "background-image",
					 &background->image, NULL);
	weston_config_section_get_color(s, "background-color",
					&background->color, 0x00000000);

	weston_config_section_get_string(s, "background-type",
					 &type, "tile");
	if (type == NULL) {
		fprintf(stderr, "%s: out of memory\n", program_invocation_short_name);
		exit(EXIT_FAILURE);
	}

	if (strcmp(type, "scale") == 0) {
        background->type = Background::Type::Scale;
	} else if (strcmp(type, "scale-crop") == 0) {
        background->type = Background::Type::ScaleCrop;
	} else if (strcmp(type, "tile") == 0) {
        background->type = Background::Type::Tile;
	} else if (strcmp(type, "centered") == 0) {
        background->type = Background::Type::Centered;
	} else {
        background->type = Background::Type::Invalid;
        fprintf(stderr, "invalid background-type: %s\n", type);
	}

	free(type);

    fprintf(stderr, " [DEBUG] END background_create()\n");
	return background;
}

//========================
// Grab Surface Methods
//========================

static int grab_surface_enter_handler(struct widget *widget,
        struct input *input, float x, float y, void *data)
{
    (void)widget;
    (void)input;
    (void)x;
    (void)y;
    Desktop *desktop = static_cast<Desktop*>(data);

    return desktop->grab_cursor;
}

static void grab_surface_create(Desktop *desktop)
{
	struct wl_surface *s;

	desktop->grab_window = window_create_custom(desktop->display);
	window_set_user_data(desktop->grab_window, desktop);

	s = window_get_wl_surface(desktop->grab_window);
	weston_desktop_shell_set_grab_surface(desktop->shell, s);

	desktop->grab_widget =
		window_add_widget(desktop->grab_window, desktop);
	/* We set the allocation to 1x1 at 0,0 so the fake enter event
	 * at 0,0 will go to this widget. */
	widget_set_allocation(desktop->grab_widget, 0, 0, 1, 1);

	widget_set_enter_handler(desktop->grab_widget,
				 grab_surface_enter_handler);
}



static void output_handle_geometry(void *data,
        struct wl_output *wl_output,
        int x, int y,
        int physical_width,
        int physical_height,
        int subpixel,
        const char *make,
        const char *model,
        int transform)
{
    (void)wl_output;
    (void)physical_width;
    (void)physical_height;
    (void)subpixel;
    (void)make;
    (void)model;
    Output *output = static_cast<Output*>(data);

    output->set_x(x);
    output->set_y(y);

    if (output->panel()) {
        window_set_buffer_transform(output->panel()->window(),
            (enum wl_output_transform)transform);
    }
    if (output->background()) {
        window_set_buffer_transform(output->background()->window,
            (enum wl_output_transform)transform);
    }
}

static void output_handle_mode(void *data,
        struct wl_output *wl_output,
        uint32_t flags,
        int width,
        int height,
        int refresh)
{
    (void)data;
    (void)wl_output;
    (void)flags;
    (void)width;
    (void)height;
    (void)refresh;
}

static void output_handle_done(void *data,
        struct wl_output *wl_output)
{
    (void)data;
    (void)wl_output;
}

static void output_handle_scale(void *data,
        struct wl_output *wl_output,
        int32_t scale)
{
    (void)wl_output;
    Output *output = static_cast<Output*>(data);

    if (output->panel()) {
        window_set_buffer_scale(output->panel()->window(), scale);
    }
    if (output->background()) {
        window_set_buffer_scale(output->background()->window, scale);
    }
}

static void global_handler(struct display *display, uint32_t id,
        const char *interface, uint32_t version, void *data)
{
    (void)display;
    (void)version;
    Desktop *desktop = static_cast<Desktop*>(data);

    if (!strcmp(interface, "weston_desktop_shell")) {
        desktop->shell = static_cast<weston_desktop_shell*>(
            display_bind(desktop->display,
                id,
                &weston_desktop_shell_interface,
                1)
        );
        weston_desktop_shell_add_listener(desktop->shell,
            &desktop_shell_listener,
            desktop);
    } else if (!strcmp(interface, "wl_output")) {
        new Output(id);
    }
}

static void global_handler_remove(struct display *display, uint32_t id,
        const char *interface, uint32_t version, void *data)
{
    (void)display;
    (void)version;
    Desktop *desktop = static_cast<Desktop*>(data);

    if (!strcmp(interface, "wl_output")) {
        for (auto& output: desktop->outputs) {
            if (output->server_output_id() == id) {
                desktop->remove_output(output);
                break;
            }
        }
    }
}


int main(int argc, char *argv[])
{
    Desktop desktop;
//    memset(&desktop, 0, sizeof(struct desktop));

    struct weston_config_section *s;
    const char *config_file;

    fprintf(stderr, "== BEGIN desktop-shell.cpp main() ==\n");

    desktop.unlock_task.run = unlock_dialog_finish;
    // wl_list_init(&desktop.outputs);

	config_file = weston_config_get_name_from_env();
	desktop.config = weston_config_parse(config_file);
	s = weston_config_get_section(desktop.config, "shell", NULL, NULL);
	weston_config_section_get_bool(s, "locking", &desktop.locking, true);
    desktop.parse_panel_position(s);
    desktop.parse_clock_format(s);

    desktop.display = display_create(&argc, argv);
    if (desktop.display == NULL) {
        fprintf(stderr, "failed to create display: %s\n",
            strerror(errno));
        weston_config_destroy(desktop.config);
        return -1;
    }
    fprintf(stderr, "   - display created and configured.\n");

	display_set_user_data(desktop.display, &desktop);
	display_set_global_handler(desktop.display, global_handler);
	display_set_global_handler_remove(desktop.display, global_handler_remove);

	/* Create panel and background for outputs processed before the shell
	 * global interface was processed */
    if (desktop.want_panel) {
        weston_desktop_shell_set_panel_position(desktop.shell, desktop.panel_position);
    }

    for (auto& output: desktop.outputs) {
        if (!output->panel()) {
            output->init();
        }
    }

    grab_surface_create(&desktop);

	signal(SIGCHLD, sigchild_handler);

    fprintf(stderr, "   - DISPLAY RUN\n");
    display_run(desktop.display);

    // Cleanup.
    fprintf(stderr, " [DEBUG] desktop-shell clean up...\n");

    return 0;
}
