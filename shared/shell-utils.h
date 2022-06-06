/*
 * Copyright 2021 Collabora, Ltd.
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
 *
 */

#include "config.h"
#include "shared/helpers.h"
#include <libweston/libweston.h>

/* parameter for create_solid_color_surface() */
struct weston_solid_color_surface {
	int (*get_label)(struct weston_surface *es, char *buf, size_t len);
	void (*surface_committed)(struct weston_surface *es, int32_t sx, int32_t sy);
	void *surface_private;
	float r, g, b;
};

#ifdef __cplusplus
extern "C" {
#endif

struct weston_output *
get_default_output(struct weston_compositor *compositor);

struct weston_output *
get_focused_output(struct weston_compositor *compositor);

void
center_on_output(struct weston_view *view, struct weston_output *output);

void
surface_subsurfaces_boundingbox(struct weston_surface *surface, int32_t *x,
				int32_t *y, int32_t *w, int32_t *h);

int
surface_get_label(struct weston_surface *surface, char *buf, size_t len);

/* helper to create a view w/ a color
 */
struct weston_view *
create_solid_color_surface(struct weston_compositor *compositor,
			   struct weston_solid_color_surface *ss,
			   float x, float y, int w, int h);

#ifdef __cplusplus
}
#endif
