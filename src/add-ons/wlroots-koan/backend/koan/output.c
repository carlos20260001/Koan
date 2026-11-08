/*
 * Koan output for wlroots.
 *
 * Represents a Koan display as a wlr_output. Wayland surfaces committed to
 * this output are presented through Koan's native windowing system.
 */

#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

bool wlr_koan_output_create(struct wlr_backend *backend, int32_t width,
		int32_t height) {
	wlr_log(WLR_INFO, "Koan output: %dx%d", width, height);
	return true;
}
