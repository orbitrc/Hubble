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
#include <libgen.h>
#include <ctype.h>
#include <time.h>
#include <assert.h>

// C++
#include <vector>

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

#include "weston-desktop-shell-client-protocol.h"

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

struct output;

//=============
// Desktop
//=============

class Desktop
{
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
    }

    int is_painted() const;

    void parse_panel_position(struct weston_config_section *s);

    void parse_clock_format(struct weston_config_section *s);

public:
    struct display *display;
    struct weston_desktop_shell *shell;
    struct unlock_dialog *unlock_dialog;
    struct task unlock_task;
//    struct wl_list outputs;
    pr::Vector<struct output*> outputs;

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

struct output;

//=========
// Panel
//=========

struct panel {
	struct surface base;

	struct output *owner;

	struct window *window;
	struct widget *widget;
	struct wl_list launcher_list;
	struct panel_clock *clock;
	int painted;
	enum weston_desktop_shell_panel_position panel_position;
    ClockFormat clock_format;
	uint32_t color;
};

//===============
// Background
//===============

struct background {
	struct surface base;

	struct output *owner;

	struct window *window;
	struct widget *widget;
	int painted;

	char *image;
	int type;
	uint32_t color;
};

//===============
// Output
//===============

struct output {
    struct wl_output *output;
    uint32_t server_output_id;
    struct wl_list link;

    int x;
    int y;
    struct panel *panel;
    struct background *background;
};

struct panel_launcher {
	struct widget *widget;
	struct panel *panel;
	cairo_surface_t *icon;
	int focused, pressed;
	char *path;
	struct wl_list link;
	struct wl_array envp;
	struct wl_array argv;
};

struct panel_clock {
	struct widget *widget;
	struct panel *panel;
	struct toytimer timer;
	char *format_string;
	time_t refresh_timer;
};

struct unlock_dialog {
	struct window *window;
	struct widget *widget;
	struct widget *button;
	int button_focused;
	int closing;
    Desktop *desktop;
};

//=====================
// Desktop Methods
//=====================

int Desktop::is_painted() const
{
    for (const auto& output: this->outputs) {
        if (output->panel && !output->panel->painted) {
            return 0;
        }
        if (output->background && !output->background->painted) {
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


static void panel_add_launchers(struct panel *panel, Desktop *desktop);

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

	cr = widget_cairo_create(launcher->panel->widget);

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
	struct panel *panel = static_cast<struct panel*>(data);

	cr = widget_cairo_create(panel->widget);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	set_hex_color(cr, panel->color);
	cairo_paint(cr);

	cairo_destroy(cr);
	surface = window_get_surface(panel->window);
	cairo_surface_destroy(surface);
	panel->painted = 1;
	check_desktop_ready(panel->window);
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

static void
clock_func(struct toytimer *tt)
{
	struct panel_clock *clock = container_of(tt, struct panel_clock, timer);

	widget_schedule_redraw(clock->widget);
}

static void
panel_clock_redraw_handler(struct widget *widget, void *data)
{
	struct panel_clock *clock = static_cast<struct panel_clock*>(data);
	cairo_t *cr;
	struct rectangle allocation;
	cairo_text_extents_t extents;
	time_t rawtime;
	struct tm * timeinfo;
	char string[128];

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(string, sizeof string, clock->format_string, timeinfo);

	widget_get_allocation(widget, &allocation);
	if (allocation.width == 0)
		return;

	cr = widget_cairo_create(clock->panel->widget);
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

static int
clock_timer_reset(struct panel_clock *clock)
{
	struct itimerspec its;
	struct timespec ts;
	struct tm *tm;

	clock_gettime(CLOCK_REALTIME, &ts);
	tm = localtime(&ts.tv_sec);

	its.it_interval.tv_sec = clock->refresh_timer;
	its.it_interval.tv_nsec = 0;
	its.it_value.tv_sec = clock->refresh_timer - tm->tm_sec % clock->refresh_timer;
	its.it_value.tv_nsec = 10000000; /* 10 ms late to ensure the clock digit has actually changed */
	timespec_add_nsec(&its.it_value, &its.it_value, -ts.tv_nsec);

	toytimer_arm(&clock->timer, &its);
	return 0;
}

static void
panel_destroy_clock(struct panel_clock *clock)
{
	widget_destroy(clock->widget);
	toytimer_fini(&clock->timer);
	free(clock);
}

static void panel_add_clock(struct panel *panel)
{
	struct panel_clock *clock;

	clock = (struct panel_clock*)xzalloc(sizeof *clock);
	clock->panel = panel;
	panel->clock = clock;

	switch (panel->clock_format) {
    case ClockFormat::Iso:
        clock->format_string = "%Y-%m-%dT%H:%M:%S";
        clock->refresh_timer = 1;
        break;
    case ClockFormat::Minutes:
		clock->format_string = "%a %b %d, %I:%M %p";
		clock->refresh_timer = 60;
		break;
    case ClockFormat::Seconds:
		clock->format_string = "%a %b %d, %I:%M:%S %p";
		clock->refresh_timer = 1;
		break;
    case ClockFormat::Minutes24h:
		clock->format_string = "%a %b %d, %H:%M";
		clock->refresh_timer = 60;
		break;
    case ClockFormat::Seconds24h:
		clock->format_string = "%a %b %d, %H:%M:%S";
		clock->refresh_timer = 1;
		break;
    case ClockFormat::None:
		assert(!"not reached");
	}

	toytimer_init(&clock->timer, CLOCK_MONOTONIC,
		      window_get_display(panel->window), clock_func);
	clock_timer_reset(clock);

	clock->widget = widget_add_widget(panel->widget, clock);
	widget_set_redraw_handler(clock->widget, panel_clock_redraw_handler);
}

static void panel_resize_handler(struct widget *widget,
        int32_t width, int32_t height, void *data)
{
    (void)widget;
	struct panel_launcher *launcher;
	struct panel *panel = static_cast<struct panel*>(data);
	int x = 0;
	int y = 0;
	int w = height > width ? width : height;
	int h = w;
	int horizontal = panel->panel_position == WESTON_DESKTOP_SHELL_PANEL_POSITION_TOP || panel->panel_position == WESTON_DESKTOP_SHELL_PANEL_POSITION_BOTTOM;
	int first_pad_h = horizontal ? 0 : DEFAULT_SPACING / 2;
	int first_pad_w = horizontal ? DEFAULT_SPACING / 2 : 0;

	wl_list_for_each(launcher, &panel->launcher_list, link) {
		widget_set_allocation(launcher->widget, x, y,
				      w + first_pad_w + 1, h + first_pad_h + 1);
		if (horizontal)
			x += w + first_pad_w;
		else
			y += h + first_pad_h;
		first_pad_h = first_pad_w = 0;
	}

    if (panel->clock_format == ClockFormat::Seconds)
		w = 170;
	else /* CLOCK_FORMAT_MINUTES and 24H versions */
		w = 150;

	if (horizontal)
		x = width - w;
	else
		y = height - (h = DEFAULT_SPACING * 3);

	if (panel->clock)
		widget_set_allocation(panel->clock->widget,
				      x, y, w + 1, h + 1);
}

static void panel_destroy(struct panel *panel);

static void panel_configure(void *data,
        struct weston_desktop_shell *desktop_shell,
        uint32_t edges, struct window *window,
        int32_t width, int32_t height)
{
    (void)desktop_shell;
    (void)edges;
    Desktop *desktop = static_cast<Desktop*>(data);
	struct surface *surface = (struct surface*)window_get_user_data(window);
	struct panel *panel = container_of(surface, struct panel, base);
	struct output *owner;

	if (width < 1 || height < 1) {
		/* Shell plugin configures 0x0 for redundant panel. */
		owner = panel->owner;
		panel_destroy(panel);
		owner->panel = NULL;
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
	window_schedule_resize(panel->window, width, height);
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

static void
panel_destroy(struct panel *panel)
{
	struct panel_launcher *tmp;
	struct panel_launcher *launcher;

	if (panel->clock)
		panel_destroy_clock(panel->clock);

	wl_list_for_each_safe(launcher, tmp, &panel->launcher_list, link)
		panel_destroy_launcher(launcher);

	widget_destroy(panel->widget);
	window_destroy(panel->window);

	free(panel);
}

static struct panel* panel_create(Desktop *desktop, struct output *output)
{
	struct panel *panel;
	struct weston_config_section *s;

	panel = (struct panel*)xzalloc(sizeof *panel);

	panel->owner = output;
	panel->base.configure = panel_configure;
	panel->window = window_create_custom(desktop->display);
	panel->widget = window_add_widget(panel->window, panel);
	wl_list_init(&panel->launcher_list);

	window_set_title(panel->window, "panel");
	window_set_user_data(panel->window, panel);

	widget_set_redraw_handler(panel->widget, panel_redraw_handler);
	widget_set_resize_handler(panel->widget, panel_resize_handler);

	panel->panel_position = desktop->panel_position;
	panel->clock_format = desktop->clock_format;
    if (panel->clock_format != ClockFormat::None) {
        panel_add_clock(panel);
    }

	s = weston_config_get_section(desktop->config, "shell", NULL, NULL);
	weston_config_section_get_color(s, "panel-color",
					&panel->color, 0xaa000000);

	panel_add_launchers(panel, desktop);

	return panel;
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

static void
panel_add_launcher(struct panel *panel, const char *icon, const char *path)
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
		for (p = start, eq = NULL; *p && !isspace(*p); p++)
			if (*p == '=')
				eq = p;

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

		while (*p && isspace(*p))
			*p++ = '\0';

		start = p;
	}

	ps = (char**)wl_array_add(&launcher->envp, sizeof *ps);
	*ps = NULL;
	ps = (char**)wl_array_add(&launcher->argv, sizeof *ps);
	*ps = NULL;

	launcher->panel = panel;
	wl_list_insert(panel->launcher_list.prev, &launcher->link);

	launcher->widget = widget_add_widget(panel->widget, launcher);
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

enum {
	BACKGROUND_SCALE,
	BACKGROUND_SCALE_CROP,
	BACKGROUND_TILE,
	BACKGROUND_CENTERED
};

static void
background_draw(struct widget *widget, void *data)
{
	struct background *background = static_cast<struct background*>(data);
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

	if (image && background->type != -1) {
		im_w = cairo_image_surface_get_width(image);
		im_h = cairo_image_surface_get_height(image);
		sx = im_w / allocation.width;
		sy = im_h / allocation.height;

		pattern = cairo_pattern_create_for_surface(image);

		switch (background->type) {
		case BACKGROUND_SCALE:
			cairo_matrix_init_scale(&matrix, sx, sy);
			cairo_pattern_set_matrix(pattern, &matrix);
			cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);
			break;
		case BACKGROUND_SCALE_CROP:
			s = (sx < sy) ? sx : sy;
			/* align center */
			tx = (im_w - s * allocation.width) * 0.5;
			ty = (im_h - s * allocation.height) * 0.5;
			cairo_matrix_init_translate(&matrix, tx, ty);
			cairo_matrix_scale(&matrix, s, s);
			cairo_pattern_set_matrix(pattern, &matrix);
			cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);
			break;
		case BACKGROUND_TILE:
			cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
			break;
		case BACKGROUND_CENTERED:
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

static void
background_destroy(struct background *background);

static void background_configure(void *data,
        struct weston_desktop_shell *desktop_shell,
        uint32_t edges, struct window *window,
        int32_t width, int32_t height)
{
    (void)data;
    (void)desktop_shell;
    (void)edges;
	struct output *owner;
	struct background *background =
		(struct background *) window_get_user_data(window);

	if (width < 1 || height < 1) {
		/* Shell plugin configures 0x0 for redundant background. */
		owner = background->owner;
		background_destroy(background);
		owner->background = NULL;
		return;
	}

	if (!background->image && background->color) {
		widget_set_viewport_destination(background->widget, width, height);
		width = 1;
		height = 1;
	}

	widget_schedule_resize(background->widget, width, height);
}

static void
unlock_dialog_redraw_handler(struct widget *widget, void *data)
{
	struct unlock_dialog *dialog = static_cast<struct unlock_dialog*>(data);
	struct rectangle allocation;
	cairo_surface_t *surface;
	cairo_t *cr;
	cairo_pattern_t *pat;
	double cx, cy, r, f;

	cr = widget_cairo_create(widget);

	widget_get_allocation(dialog->widget, &allocation);
	cairo_rectangle(cr, allocation.x, allocation.y,
			allocation.width, allocation.height);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.6);
	cairo_fill(cr);

	cairo_translate(cr, allocation.x, allocation.y);
	if (dialog->button_focused)
		f = 1.0;
	else
		f = 0.7;

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

	widget_set_allocation(dialog->button,
			      allocation.x + cx - r,
			      allocation.y + cy - r, 2 * r, 2 * r);

	cairo_destroy(cr);

	surface = window_get_surface(dialog->window);
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
	struct unlock_dialog *dialog = static_cast<struct unlock_dialog*>(data);
    Desktop *desktop = dialog->desktop;

	if (button == BTN_LEFT) {
		if (state == WL_POINTER_BUTTON_STATE_RELEASED &&
		    !dialog->closing) {
			display_defer(desktop->display, &desktop->unlock_task);
			dialog->closing = 1;
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
	struct unlock_dialog *dialog = static_cast<struct unlock_dialog*>(data);

	dialog->button_focused = 1;
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
    struct unlock_dialog *dialog = static_cast<struct unlock_dialog*>(data);
    Desktop *desktop = dialog->desktop;

	dialog->button_focused = 0;
	widget_schedule_redraw(widget);
	display_defer(desktop->display, &desktop->unlock_task);
	dialog->closing = 1;
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
	struct unlock_dialog *dialog = static_cast<struct unlock_dialog*>(data);

	dialog->button_focused = 1;
	widget_schedule_redraw(widget);

	return CURSOR_LEFT_PTR;
}

static void
unlock_dialog_widget_leave_handler(struct widget *widget,
        struct input *input, void *data)
{
    (void)input;
    struct unlock_dialog *dialog = static_cast<struct unlock_dialog*>(data);

	dialog->button_focused = 0;
	widget_schedule_redraw(widget);
}

static struct unlock_dialog* unlock_dialog_create(Desktop *desktop)
{
	struct display *display = desktop->display;
	struct unlock_dialog *dialog;
	struct wl_surface *surface;

	dialog = (struct unlock_dialog*)xzalloc(sizeof *dialog);

	dialog->window = window_create_custom(display);
	dialog->widget = window_frame_create(dialog->window, dialog);
	window_set_title(dialog->window, "Unlock your desktop");

	window_set_user_data(dialog->window, dialog);
	window_set_keyboard_focus_handler(dialog->window,
					  unlock_dialog_keyboard_focus_handler);
	dialog->button = widget_add_widget(dialog->widget, dialog);
	widget_set_redraw_handler(dialog->widget,
				  unlock_dialog_redraw_handler);
	widget_set_enter_handler(dialog->button,
				 unlock_dialog_widget_enter_handler);
	widget_set_leave_handler(dialog->button,
				 unlock_dialog_widget_leave_handler);
	widget_set_button_handler(dialog->button,
				  unlock_dialog_button_handler);
	widget_set_touch_down_handler(dialog->button,
				      unlock_dialog_touch_down_handler);
	widget_set_touch_up_handler(dialog->button,
				      unlock_dialog_touch_up_handler);

	surface = window_get_wl_surface(dialog->window);
	weston_desktop_shell_set_lock_surface(desktop->shell, surface);

	window_schedule_resize(dialog->window, 260, 230);

	return dialog;
}

static void
unlock_dialog_destroy(struct unlock_dialog *dialog)
{
	window_destroy(dialog->window);
	free(dialog);
}

static void
unlock_dialog_finish(struct task *task, uint32_t events)
{
    (void)events;
    Desktop *desktop = container_of(task, Desktop, unlock_task);

	weston_desktop_shell_unlock(desktop->shell);
	unlock_dialog_destroy(desktop->unlock_dialog);
	desktop->unlock_dialog = NULL;
}

static void
desktop_shell_configure(void *data,
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
		desktop->unlock_dialog = unlock_dialog_create(desktop);
		desktop->unlock_dialog->desktop = desktop;
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

static const struct weston_desktop_shell_listener listener = {
	desktop_shell_configure,
	desktop_shell_prepare_lock_surface,
	desktop_shell_grab_cursor
};

static void
background_destroy(struct background *background)
{
	widget_destroy(background->widget);
	window_destroy(background->window);

	free(background->image);
	free(background);
}

static struct background* background_create(Desktop *desktop,
        struct output *output)
{
	struct background *background;
	struct weston_config_section *s;
	char *type;

	background = (struct background*)xzalloc(sizeof *background);
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
		background->type = BACKGROUND_SCALE;
	} else if (strcmp(type, "scale-crop") == 0) {
		background->type = BACKGROUND_SCALE_CROP;
	} else if (strcmp(type, "tile") == 0) {
		background->type = BACKGROUND_TILE;
	} else if (strcmp(type, "centered") == 0) {
		background->type = BACKGROUND_CENTERED;
	} else {
		background->type = -1;
		fprintf(stderr, "invalid background-type: %s\n",
			type);
	}

	free(type);

	return background;
}

static int grab_surface_enter_handler(struct widget *widget,
        struct input *input, float x, float y, void *data)
{
    Desktop *desktop = static_cast<Desktop*>(data);

    return desktop->grab_cursor;
}

static void grab_surface_destroy(Desktop *desktop)
{
	widget_destroy(desktop->grab_widget);
	window_destroy(desktop->grab_window);
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

static void
output_destroy(struct output *output)
{
	if (output->background)
		background_destroy(output->background);
	if (output->panel)
		panel_destroy(output->panel);
	wl_output_destroy(output->output);
	wl_list_remove(&output->link);

	free(output);
}

static void desktop_destroy_outputs(Desktop *desktop)
{
    for (auto& output: desktop->outputs) {
        output_destroy(output);
    }
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
	struct output *output = static_cast<struct output*>(data);

	output->x = x;
	output->y = y;

	if (output->panel)
		window_set_buffer_transform(output->panel->window, (enum wl_output_transform)transform);
	if (output->background)
		window_set_buffer_transform(output->background->window, (enum wl_output_transform)transform);
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
    struct output *output = static_cast<struct output*>(data);

	if (output->panel)
		window_set_buffer_scale(output->panel->window, scale);
	if (output->background)
		window_set_buffer_scale(output->background->window, scale);
}

static const struct wl_output_listener output_listener = {
	output_handle_geometry,
	output_handle_mode,
	output_handle_done,
	output_handle_scale
};

static void output_init(struct output *output, Desktop *desktop)
{
	struct wl_surface *surface;

	if (desktop->want_panel) {
		output->panel = panel_create(desktop, output);
		surface = window_get_wl_surface(output->panel->window);
		weston_desktop_shell_set_panel(desktop->shell,
					       output->output, surface);
	}

	output->background = background_create(desktop, output);
	surface = window_get_wl_surface(output->background->window);
	weston_desktop_shell_set_background(desktop->shell,
					    output->output, surface);
}

static void create_output(Desktop *desktop, uint32_t id)
{
	struct output *output;

    output = static_cast<struct output*>(zalloc(sizeof *output));
    if (!output) {
        return;
    }

	output->output =
        static_cast<struct wl_output*>(display_bind(desktop->display, id, &wl_output_interface, 2));
	output->server_output_id = id;

	wl_output_add_listener(output->output, &output_listener, output);

    desktop->outputs.push(output);

	/* On start up we may process an output global before the shell global
	 * in which case we can't create the panel and background just yet */
	if (desktop->shell)
		output_init(output, desktop);
}

static void output_remove(Desktop *desktop, struct output *output)
{
	struct output *rep = NULL;

	if (!output->background) {
		output_destroy(output);
		return;
	}

    // Find a wl_output that is a clone of the removed wl_output.
    // We don't want to leave the clone without a background or panel.
    for (auto& cur: desktop->outputs) {
        if (cur == output) {
            continue;
        }

        // XXX: Assumes size matches.
        if (cur->x == output->x && cur->y == output->y) {
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

		if (!rep->background) {
			rep->background = output->background;
			output->background = NULL;
			rep->background->owner = rep;
		}

		if (!rep->panel) {
			rep->panel = output->panel;
			output->panel = NULL;
			if (rep->panel)
				rep->panel->owner = rep;
		}
	}

	output_destroy(output);
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
						  &listener,
						  desktop);
    } else if (!strcmp(interface, "wl_output")) {
        create_output(desktop, id);
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
            if (output->server_output_id == id) {
                output_remove(desktop, output);
                break;
            }
        }
	}
}

static void panel_add_launchers(struct panel *panel, Desktop *desktop)
{
	struct weston_config_section *s;
	char *icon, *path;
	const char *name;
	int count;

	count = 0;
	s = NULL;
	while (weston_config_next_section(desktop->config, &s, &name)) {
		if (strcmp(name, "launcher") != 0)
			continue;

		weston_config_section_get_string(s, "icon", &icon, NULL);
		weston_config_section_get_string(s, "path", &path, NULL);

		if (icon != NULL && path != NULL) {
			panel_add_launcher(panel, icon, path);
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
		panel_add_launcher(panel,
				   name,
				   BINDIR "/weston-terminal");
		free(name);
	}
}


int main(int argc, char *argv[])
{
    Desktop desktop;
//    memset(&desktop, 0, sizeof(struct desktop));

	struct output *output;
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

	display_set_user_data(desktop.display, &desktop);
	display_set_global_handler(desktop.display, global_handler);
	display_set_global_handler_remove(desktop.display, global_handler_remove);

	/* Create panel and background for outputs processed before the shell
	 * global interface was processed */
    if (desktop.want_panel) {
        weston_desktop_shell_set_panel_position(desktop.shell, desktop.panel_position);
    }

    for (auto& output: desktop.outputs) {
        if (!output->panel) {
            output_init(output, &desktop);
        }
    }

	grab_surface_create(&desktop);

	signal(SIGCHLD, sigchild_handler);

	display_run(desktop.display);

	/* Cleanup */
	grab_surface_destroy(&desktop);
	desktop_destroy_outputs(&desktop);
	if (desktop.unlock_dialog)
		unlock_dialog_destroy(desktop.unlock_dialog);
	weston_desktop_shell_destroy(desktop.shell);
	display_destroy(desktop.display);
	weston_config_destroy(desktop.config);

	return 0;
}
