# Host build of the UI + LVGL, for screenshots. LVGL v8.4: see tools/sim.sh
LVGL ?= ../.sim-deps/lvgl
SRCS := $(shell find $(LVGL)/src -name '*.c') ../firmware/main/ui/ui.c ../firmware/main/ui/ui_settings.c ../firmware/main/ui/ui_camera.c ../firmware/main/ui/ui_shell.c ../firmware/main/ui/ui_kb.c ../firmware/main/ui/assets.c ../firmware/main/ui/lv_quiet.c render.c
CFLAGS := -O1 -w -I. -I$(LVGL) -I../firmware/main/ui -DLV_CONF_INCLUDE_SIMPLE
# same setter wrappers as the firmware (firmware/main/CMakeLists.txt)
comma := ,
WRAP := $(addprefix -Wl$(comma)--wrap=,lv_obj_set_local_style_prop lv_label_set_text lv_label_set_text_fmt lv_obj_add_flag lv_obj_clear_flag lv_chart_set_value_by_id)
render: $(SRCS)
	@gcc $(CFLAGS) -o $@ $(SRCS) $(WRAP) -lm
clean:
	rm -f render
