WAYLAND_PROTOCOLS_STABLE_DIR=/usr/share/wayland-protocols/stable

default:
	meson build/ --prefix=$(PWD)/build
	ninja -C build/ install

protocols:
	wayland-scanner client-header $(WAYLAND_PROTOCOLS_STABLE_DIR)/xdg-shell/xdg-shell.xml wayland-protocols/stable/xdg-shell.h
	wayland-scanner private-code $(WAYLAND_PROTOCOLS_STABLE_DIR)/xdg-shell/xdg-shell.xml wayland-protocols/stable/xdg-shell.c
	wayland-scanner client-header protocol/weston-desktop-shell.xml wayland-protocols/weston/weston-desktop-shell-client-protocol.h
	wayland-scanner server-header protocol/weston-desktop-shell.xml wayland-protocols/weston/weston-desktop-shell-server-protocol.h

clean:
	rm -r build/
