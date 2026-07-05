/*
 * xj380_gui.h — XJ380 GUI 后端 (SDL3)
 */
#ifndef XJ380_GUI_H
#define XJ380_GUI_H

#include "xj380_emu.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SDL3 初始化/清理 */
int  xj380_gui_init(void);
void xj380_gui_cleanup(void);
void xj380_gui_set_debug(bool enabled);

/* 窗口 */
void xj380_gui_create_window(xj380_emu_t *emu, uint64_t handle_ptr, uint64_t xwin_ptr);
void xj380_gui_close_window(xj380_emu_t *emu, uint64_t handle);
void xj380_gui_set_window_title(xj380_emu_t *emu, uint64_t handle, uint64_t title_ptr);
void xj380_gui_set_icon(xj380_emu_t *emu, uint64_t handle, uint64_t path_ptr);
void xj380_gui_get_window_size(xj380_emu_t *emu, uint64_t handle,
                               uint64_t width_ptr, uint64_t height_ptr);

/* 绘图 */
void xj380_gui_draw_point(xj380_emu_t *emu, uint64_t handle,
                          uint32_t x, uint32_t y, uint32_t rgba);
void xj380_gui_draw_line(xj380_emu_t *emu, uint64_t handle,
                         uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2,
                         uint32_t rgba);
void xj380_gui_draw_rect(xj380_emu_t *emu, uint64_t handle,
                         uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2,
                         uint32_t rgba, int fill);
void xj380_gui_draw_text(xj380_emu_t *emu, uint64_t handle,
                         uint32_t x, uint32_t y, uint64_t str_ptr,
                         uint32_t size, uint32_t rgba);
void xj380_gui_draw_text_l(xj380_emu_t *emu, uint64_t handle,
                           uint32_t x, uint32_t y, uint64_t str_ptr,
                           uint32_t size, uint32_t rgba, uint64_t width_ptr);
void xj380_gui_draw_sw_text(xj380_emu_t *emu, uint64_t handle,
                            uint32_t x, uint32_t y, uint64_t str_ptr,
                            uint32_t rgba);
uint64_t xj380_gui_calc_text_width(xj380_emu_t *emu, uint64_t str_ptr,
                                  uint32_t size);

/* 图片 */
void xj380_gui_draw_bmp(xj380_emu_t *emu, uint64_t handle,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint64_t path_ptr);
void xj380_gui_draw_png(xj380_emu_t *emu, uint64_t handle,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint64_t path_ptr);
void xj380_gui_draw_picture(xj380_emu_t *emu, uint64_t handle,
                            uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            uint64_t path_ptr);
void xj380_gui_get_pic_size(xj380_emu_t *emu, uint64_t path_ptr,
                            uint64_t width_ptr, uint64_t height_ptr);
void xj380_gui_draw_svg(xj380_emu_t *emu, uint64_t handle,
                        uint32_t x, uint32_t y, uint32_t width,
                        uint64_t svg_ptr, int enable_trans);
void xj380_gui_draw_fa(xj380_emu_t *emu, uint64_t handle,
                       uint32_t x, uint32_t y, uint32_t width,
                       uint64_t name_ptr, int enable_trans);

/* Framebuffer */
void xj380_gui_read_buffer(xj380_emu_t *emu, uint64_t handle,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint64_t buf_ptr);
void xj380_gui_write_buffer(xj380_emu_t *emu, uint64_t handle,
                            uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            uint64_t buf_ptr);
void xj380_gui_read_buffer_a(xj380_emu_t *emu, uint64_t handle,
                             uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint64_t buf_ptr);
void xj380_gui_write_buffer_a(xj380_emu_t *emu, uint64_t handle,
                              uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              uint64_t buf_ptr);
void xj380_gui_refresh_window(xj380_emu_t *emu, uint64_t handle);
void xj380_gui_refresh_part(xj380_emu_t *emu, uint64_t handle,
                            uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2);

/* 控件 */
void xj380_gui_button(xj380_emu_t *emu, uint64_t handle,
                      uint64_t crl_id, uint64_t x, uint64_t y, uint64_t text_ptr);
void xj380_gui_emp_button(xj380_emu_t *emu, uint64_t handle,
                          uint64_t crl_id, uint64_t x, uint64_t y, uint64_t text_ptr);
void xj380_gui_delete_button(xj380_emu_t *emu, uint64_t handle, uint64_t crl_id);
void xj380_gui_register_rbutton_menu(xj380_emu_t *emu, uint64_t handle,
                                     uint64_t items_ptr, uint64_t count);
void xj380_gui_delete_rbutton_menu(xj380_emu_t *emu, uint64_t handle);

/* 事件循环 (在 xj380_run 主循环中调用) */
int  xj380_gui_poll_events(xj380_emu_t *emu);
void xj380_gui_render_all(xj380_emu_t *emu);
int  xj380_gui_window_count(void);
void xj380_gui_flush_events(xj380_emu_t *emu);
bool xj380_gui_dispatch_queued_event(xj380_emu_t *emu, uint64_t return_addr);
void xj380_gui_finish_dispatched_event(xj380_emu_t *emu);
void xj380_gui_store_callback(xj380_emu_t *emu, uint64_t handle, uint64_t func);
void xj380_gui_load_font(const char *vpath, const uint8_t *data, size_t size);

/* 模拟器内存访问 (从 xj380_emu.c 导出) */
int  xj380_mem_read(xj380_emu_t *emu, uint64_t addr, void *dst, size_t len);
int  xj380_mem_write(xj380_emu_t *emu, uint64_t addr, const void *src, size_t len);
int  xj380_mem_read_str(xj380_emu_t *emu, uint64_t addr, char *buf, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* XJ380_GUI_H */
