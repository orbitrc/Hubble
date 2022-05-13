default:
	meson build/ --prefix=$(PWD)/build
	ninja -C build/ install
