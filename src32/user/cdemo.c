#include "leonos_user.h"

static leonos_u32 clamp_min(leonos_u32 value, leonos_u32 min)
{
    return value < min ? min : value;
}

int leonos_user_main(void)
{
    struct leonos_fb_info fb;
    struct leonos_event event;

    leonos_write("UCDEMO C userland app\r\n");
    if (!leonos_fb_info(&fb)) {
        return 1;
    }

    leonos_u32 margin = clamp_min(fb.width >> 5, 32u);
    leonos_u32 top = clamp_min(fb.height >> 5, 28u);
    leonos_u32 panel_w = fb.width - margin * 2u;
    leonos_u32 panel_h = fb.height >> 2;
    if (panel_h < 180u) {
        panel_h = 180u;
    }
    if (panel_h > fb.height - top * 2u) {
        panel_h = fb.height - top * 2u;
    }

    leonos_fb_fill(margin, top, panel_w, panel_h, 0x00F8FAFCu);
    leonos_fb_fill(margin, top, panel_w, 54u, 0x001A73E8u);
    leonos_fb_fill(margin + 28u, top + 84u, panel_w - 56u, 46u, 0x00DDEBFFu);
    leonos_fb_fill(margin + 28u, top + 150u, panel_w >> 1, 38u, 0x00DFF7EAu);
    leonos_fb_fill(margin + 28u, top + 208u, panel_w - 56u, 12u, 0x0034A853u);
    leonos_fb_fill(margin + 28u, top + 232u, panel_w - 120u, 12u, 0x00FBBC04u);

    (void) leonos_event_poll(&event);
    leonos_fb_present();
    return 0;
}
