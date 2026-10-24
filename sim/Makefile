# Host build of the UI + LVGL, for screenshots. LVGL v8.4: see tools/sim.sh
LVGL ?= ../.sim-deps/lvgl
SRCS := $(shell find $(LVGL)/src -name '*.c') ../firmware/main/ui/ui.c ../firmware/main/ui/ui_settings.c ../firmware/main/ui/ui_camera.c ../firmware/main/ui/ui_shell.c ../firmware/main/ui/ui_kb.c ../firmware/main/ui/assets.c render.c
CFLAGS := -O1 -w -I. -I$(LVGL) -I../firmware/main/ui -DLV_CONF_INCLUDE_SIMPLE
render: $(SRCS)
	@gcc $(CFLAGS) -o $@ $(SRCS) -lm
clean:
	rm -f render
