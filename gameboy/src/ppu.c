#include "ppu.h"
#include "cpu.h"
#include <string.h>

// Real DMG post-boot register values (pandocs' Power_Up_Sequence.md,
// same page Phase 1/2 already cited for CPU registers and the header
// checksum). mode/ly/dots are reset to the deterministic start of a
// fresh frame rather than whatever mid-boot-animation snapshot real
// hardware would show at PC=0x0100 - no test ROM this phase depends on
// that transient state (they all set up LCDC etc. themselves before
// enabling the display). OBP0/OBP1 are genuinely undocumented at
// power-on per pandocs (marked "??" there) - 0xFF is an arbitrary but
// harmless placeholder, not a cited value.
void gb_ppu_reset(GBPpu *ppu) {
    memset(ppu, 0, sizeof(*ppu));
    ppu->lcdc = 0x91;
    ppu->stat = 0x85;
    ppu->bgp = 0xFC;
    ppu->obp0 = 0xFF;
    ppu->obp1 = 0xFF;
    ppu->mode = 2;
}

static void request_stat_interrupt(struct GBCpu *cpu) {
    gb_write_byte(cpu, 0xFF0F, (uint8_t)(gb_read_byte(cpu, 0xFF0F) | 0x02));
}

static void update_lyc_flag(GBPpu *ppu, struct GBCpu *cpu) {
    int was_equal = (ppu->stat & 0x04) != 0;
    int now_equal = (ppu->ly == ppu->lyc);
    if (now_equal) ppu->stat |= 0x04; else ppu->stat &= (uint8_t)~0x04;
    if (now_equal && !was_equal && (ppu->stat & 0x40)) request_stat_interrupt(cpu);
}

// Reads one BG/window tile's pixel row and returns the raw 2-bit color
// index (0-3) at column `px` (0-7, already accounting for X flip - BG/
// window tiles are never flipped, only objects are). Shared by the BG
// and window paths below since both read tiles identically once the
// tile map base and pixel coordinates are worked out - see
// gameboy/docs/GAMEBOY_ROADMAP.md's Tile_Data.md citation for the addressing
// modes and bit-packing this implements.
static uint8_t read_tile_pixel(struct GBCpu *cpu, uint8_t lcdc, uint8_t tile_id, int px, int py) {
    uint16_t tile_addr;
    if (lcdc & 0x10) {
        tile_addr = (uint16_t)(0x8000 + tile_id * 16); // "$8000 method": unsigned
    } else {
        tile_addr = (uint16_t)(0x9000 + (int8_t)tile_id * 16); // "$8800 method": signed, base $9000
    }
    uint8_t lo_byte = gb_read_byte(cpu, (uint16_t)(tile_addr + py * 2));
    uint8_t hi_byte = gb_read_byte(cpu, (uint16_t)(tile_addr + py * 2 + 1));
    int bit = 7 - px;
    uint8_t lo = (lo_byte >> bit) & 1;
    uint8_t hi = (hi_byte >> bit) & 1;
    return (uint8_t)((hi << 1) | lo);
}

static uint8_t apply_palette(uint8_t palette, uint8_t color_idx) {
    return (uint8_t)((palette >> (color_idx * 2)) & 0x03);
}

static void render_scanline(GBPpu *ppu, struct GBCpu *cpu) {
    int ly = ppu->ly;
    uint8_t bg_color_idx[GB_SCREEN_WIDTH];

    int bg_win_enabled = (ppu->lcdc & 0x01) != 0;
    int window_enabled = bg_win_enabled && (ppu->lcdc & 0x20) != 0;
    // Window visibility per pandocs' Tile_Maps.md: WY <= LY for the
    // line, and (checked per-pixel below) WX-7 <= x. WX <= 166 keeps a
    // window that's scrolled fully off the right edge from advancing
    // the internal line counter for a line nothing was actually drawn
    // on.
    int window_visible_this_line = window_enabled && (ppu->wy <= ly) && (ppu->wx <= 166);
    int window_drawn_this_line = 0;

    for (int x = 0; x < GB_SCREEN_WIDTH; x++) {
        if (!bg_win_enabled) {
            // pandocs' LCDC.md: "both background and window become
            // blank (white)" - literal white, not "whatever BGP maps
            // color 0 to".
            bg_color_idx[x] = 0;
            ppu->framebuffer[ly][x] = 0;
            continue;
        }

        int use_window = window_visible_this_line && (x + 7 >= ppu->wx);
        uint16_t tile_map_base;
        int tile_col, tile_row, px, py;
        if (use_window) {
            window_drawn_this_line = 1;
            tile_map_base = (ppu->lcdc & 0x40) ? 0x9C00 : 0x9800;
            int wx_pixel = x - (ppu->wx - 7);
            tile_col = wx_pixel / 8;
            px = wx_pixel % 8;
            tile_row = ppu->window_line / 8;
            py = ppu->window_line % 8;
        } else {
            tile_map_base = (ppu->lcdc & 0x08) ? 0x9C00 : 0x9800;
            int bg_x = (x + ppu->scx) & 0xFF;
            int bg_y = (ly + ppu->scy) & 0xFF;
            tile_col = bg_x / 8;
            px = bg_x % 8;
            tile_row = bg_y / 8;
            py = bg_y % 8;
        }

        uint16_t map_addr = (uint16_t)(tile_map_base + tile_row * 32 + tile_col);
        uint8_t tile_id = gb_read_byte(cpu, map_addr);
        uint8_t color_idx = read_tile_pixel(cpu, ppu->lcdc, tile_id, px, py);

        bg_color_idx[x] = color_idx;
        ppu->framebuffer[ly][x] = apply_palette(ppu->bgp, color_idx);
    }

    if (window_drawn_this_line) ppu->window_line++;

    if (!(ppu->lcdc & 0x02)) return; // objects disabled

    int obj_height = (ppu->lcdc & 0x04) ? 16 : 8;

    // Selection priority (pandocs' OAM.md): scan OAM in order, keep the
    // first (up to) 10 objects overlapping this scanline.
    int selected[10];
    int selected_count = 0;
    for (int i = 0; i < 40 && selected_count < 10; i++) {
        uint16_t oam_addr = (uint16_t)(0xFE00 + i * 4);
        uint8_t obj_y = gb_read_byte(cpu, oam_addr);
        int obj_top = obj_y - 16;
        if (ly >= obj_top && ly < obj_top + obj_height) {
            selected[selected_count++] = i;
        }
    }

    // Drawing priority (Non-CGB mode, same page): smaller X first, ties
    // broken by OAM order. Stable insertion sort - selected[] is
    // already in ascending OAM order from the scan above, so this only
    // needs to break ties correctly, which insertion sort does for free.
    for (int a = 1; a < selected_count; a++) {
        int key = selected[a];
        uint8_t key_x = gb_read_byte(cpu, (uint16_t)(0xFE00 + key * 4 + 1));
        int b = a - 1;
        while (b >= 0) {
            uint8_t b_x = gb_read_byte(cpu, (uint16_t)(0xFE00 + selected[b] * 4 + 1));
            if (b_x <= key_x) break;
            selected[b + 1] = selected[b];
            b--;
        }
        selected[b + 1] = key;
    }

    // claimed[x]: an opaque pixel from a higher-priority object already
    // resolved this column, win or lose against BG - per pandocs'
    // "Interaction with BG over OBJ flag" note, priority between
    // objects is resolved *before* BG priority is considered, so a
    // higher-priority object's opaque-but-BG-losing pixel still blocks
    // a lower-priority object from being drawn there at all.
    uint8_t claimed[GB_SCREEN_WIDTH] = {0};

    for (int s = 0; s < selected_count; s++) {
        int i = selected[s];
        uint16_t oam_addr = (uint16_t)(0xFE00 + i * 4);
        uint8_t obj_y = gb_read_byte(cpu, oam_addr);
        uint8_t obj_x = gb_read_byte(cpu, (uint16_t)(oam_addr + 1));
        uint8_t tile_id = gb_read_byte(cpu, (uint16_t)(oam_addr + 2));
        uint8_t attr = gb_read_byte(cpu, (uint16_t)(oam_addr + 3));

        int obj_top = obj_y - 16;
        int obj_left = obj_x - 8;
        int y_flip = (attr & 0x40) != 0;
        int x_flip = (attr & 0x20) != 0;
        int bg_priority = (attr & 0x80) != 0;
        uint8_t palette = (attr & 0x10) ? ppu->obp1 : ppu->obp0;

        int row = ly - obj_top;
        if (y_flip) row = obj_height - 1 - row;
        if (obj_height == 16) tile_id &= 0xFE; // "top 8x8 tile is NN & $FE" - pandocs' OAM.md

        // Objects always use $8000 addressing regardless of LCDC.4
        // (pandocs' Tile_Data.md) - read directly rather than going
        // through read_tile_pixel(), which honors LCDC.4 for BG/window.
        uint16_t tile_addr = (uint16_t)(0x8000 + tile_id * 16 + row * 2);
        uint8_t lo_byte = gb_read_byte(cpu, tile_addr);
        uint8_t hi_byte = gb_read_byte(cpu, (uint16_t)(tile_addr + 1));

        for (int col = 0; col < 8; col++) {
            int x = obj_left + col;
            if (x < 0 || x >= GB_SCREEN_WIDTH || claimed[x]) continue;

            int bit = x_flip ? col : (7 - col);
            uint8_t lo = (lo_byte >> bit) & 1;
            uint8_t hi = (hi_byte >> bit) & 1;
            uint8_t color_idx = (uint8_t)((hi << 1) | lo);
            if (color_idx == 0) continue; // transparent for objects

            claimed[x] = 1;
            if (bg_priority && bg_color_idx[x] != 0) continue; // BG colors 1-3 win

            ppu->framebuffer[ly][x] = apply_palette(palette, color_idx);
        }
    }
}

void gb_ppu_step(GBPpu *ppu, struct GBCpu *cpu, int cycles) {
    if (!(ppu->lcdc & 0x80)) return; // LCD off: PPU fully idle, per pandocs' LCDC.md

    ppu->dots += cycles;

    switch (ppu->mode) {
        case 2: // OAM scan
            if (ppu->dots >= 80) {
                ppu->dots -= 80;
                ppu->mode = 3;
            }
            break;

        case 3: // Drawing (fixed-length simplification - see this file's header comment)
            if (ppu->dots >= 172) {
                ppu->dots -= 172;
                render_scanline(ppu, cpu);
                ppu->mode = 0;
                if (ppu->stat & 0x08) request_stat_interrupt(cpu); // Mode 0 int select
            }
            break;

        case 0: // HBlank: remainder of 456 dots (456-80-172=204)
            if (ppu->dots >= 204) {
                ppu->dots -= 204;
                ppu->ly++;
                update_lyc_flag(ppu, cpu);
                if (ppu->ly == 144) {
                    ppu->mode = 1;
                    gb_write_byte(cpu, 0xFF0F, (uint8_t)(gb_read_byte(cpu, 0xFF0F) | 0x01)); // VBlank
                    if (ppu->stat & 0x10) request_stat_interrupt(cpu); // Mode 1 int select
                    ppu->frame_ready = 1;
                } else {
                    ppu->mode = 2;
                    if (ppu->stat & 0x20) request_stat_interrupt(cpu); // Mode 2 int select
                }
            }
            break;

        case 1: // VBlank: 10 scanlines of 456 dots each
            if (ppu->dots >= 456) {
                ppu->dots -= 456;
                ppu->ly++;
                if (ppu->ly > 153) {
                    ppu->ly = 0;
                    ppu->window_line = 0; // new frame
                    ppu->mode = 2;
                    if (ppu->stat & 0x20) request_stat_interrupt(cpu);
                }
                update_lyc_flag(ppu, cpu);
            }
            break;
    }
}

uint8_t gb_ppu_read_reg(GBPpu *ppu, uint16_t addr) {
    switch (addr) {
        case 0xFF40: return ppu->lcdc;
        case 0xFF41: {
            // Mode bits are live-computed, not stored redundantly;
            // pandocs' STAT.md: "Reports 0 instead when the PPU is
            // disabled."
            uint8_t mode = (ppu->lcdc & 0x80) ? (uint8_t)ppu->mode : 0;
            return (uint8_t)((ppu->stat & 0xFC) | mode);
        }
        case 0xFF42: return ppu->scy;
        case 0xFF43: return ppu->scx;
        case 0xFF44: return ppu->ly;
        case 0xFF45: return ppu->lyc;
        case 0xFF46: return ppu->dma;
        case 0xFF47: return ppu->bgp;
        case 0xFF48: return ppu->obp0;
        case 0xFF49: return ppu->obp1;
        case 0xFF4A: return ppu->wy;
        case 0xFF4B: return ppu->wx;
        default: return 0xFF;
    }
}

void gb_ppu_write_reg(GBPpu *ppu, struct GBCpu *cpu, uint16_t addr, uint8_t val) {
    switch (addr) {
        case 0xFF40:
            if ((ppu->lcdc & 0x80) && !(val & 0x80)) {
                // LCD turning off - pandocs' LCDC.md warns this should
                // only happen during VBlank (real hardware can be
                // damaged otherwise); not enforced/hard-failed here,
                // just documented. Reset to a fresh frame's start so
                // re-enabling later behaves predictably.
                ppu->mode = 0;
                ppu->dots = 0;
                ppu->ly = 0;
            }
            ppu->lcdc = val;
            break;
        case 0xFF41:
            // Bits 0-2 (mode, LYC==LY) stay PPU-owned regardless of
            // what the CPU writes - only the interrupt-select bits
            // (3-6) and the unused bit 7 are genuinely writable.
            ppu->stat = (uint8_t)((ppu->stat & 0x07) | (val & 0xF8));
            break;
        case 0xFF42: ppu->scy = val; break;
        case 0xFF43: ppu->scx = val; break;
        case 0xFF44: break; // read-only
        case 0xFF45: ppu->lyc = val; break;
        case 0xFF46: {
            // Instant transfer rather than the real 160 M-cycle timed
            // one (pandocs' OAM_DMA_Transfer.md) - a documented
            // simplification: real programs busy-wait in HRAM for it
            // to finish before touching OAM again, so an instant
            // transfer produces the same end result for any program
            // that follows that (universal) convention.
            ppu->dma = val;
            uint16_t src = (uint16_t)(val << 8);
            for (int i = 0; i < 160; i++) {
                gb_write_byte(cpu, (uint16_t)(0xFE00 + i), gb_read_byte(cpu, (uint16_t)(src + i)));
            }
            break;
        }
        case 0xFF47: ppu->bgp = val; break;
        case 0xFF48: ppu->obp0 = val; break;
        case 0xFF49: ppu->obp1 = val; break;
        case 0xFF4A: ppu->wy = val; break;
        case 0xFF4B: ppu->wx = val; break;
        default: break;
    }
}
