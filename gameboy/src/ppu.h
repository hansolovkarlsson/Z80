#ifndef _GB_PPU_H
#define _GB_PPU_H

#include <stdint.h>

#define GB_SCREEN_WIDTH 160
#define GB_SCREEN_HEIGHT 144

struct GBCpu; // forward-declared - see cpu.h's own comment on why

// Phase 3: the LCD controller. Registers (0xFF40-0xFF4B), the mode/
// timing state machine, and a scanline-at-a-time renderer (background,
// window, objects). Every register layout, addressing mode, and
// priority rule below is grounded against pandocs' LCDC.md/STAT.md/
// Tile_Data.md/Tile_Maps.md/OAM.md/Rendering.md/Palettes.md/
// OAM_DMA_Transfer.md (fetched during this phase - see
// gameboy/docs/GAMEBOY_ROADMAP.md), not guessed.
//
// Phase 8 (see the roadmap's own status entry) replaced Mode 3's fixed
// 172-dot length with pandocs' Rendering.md "Mode 3 length" algorithm -
// SCX%8 + a flat 6-dot window penalty + a per-object penalty (see
// compute_mode3_length() in ppu.c) - closing the STAT-interrupt-timing
// gap that caused dmg-acid2's real, previously-documented flicker.
// Deliberately still *not* a full per-dot pixel-FIFO simulation
// (pixel_fifo.md's fetcher/FIFO push-pop state machine): pandocs'
// Rendering.md algorithm gives Mode 3's exact *duration* without
// needing one, and duration (not literal per-pixel FIFO mixing) is
// what STAT/OAM-scan interrupt timing actually depends on - render_scanline()
// still computes all 160 pixels' final color at once, using the same
// per-scanline register/OAM snapshot compute_mode3_length() used at
// the Mode 2->3 transition. This means genuinely obscure *mid-Mode-3*
// raster tricks (a register write timed to land between two pixels
// within the same scanline, or pixel_fifo.md's own documented "WX
// changed mid-scanline" bug) still aren't modeled - those need the
// full FIFO simulation this phase deliberately didn't build.
typedef struct GBPpu {
    uint8_t lcdc, stat, scy, scx, ly, lyc, dma, bgp, obp0, obp1, wy, wx;

    int mode;         // 0=HBlank 1=VBlank 2=OAM scan 3=Drawing
    int dots;         // dot (T-cycle) counter within the current scanline
    int mode3_dots;   // this scanline's real Mode 3 length, computed once at
                       // the Mode 2->3 transition - see compute_mode3_length()
    int window_line;  // internal window line counter - see Tile_Maps.md's
                       // "Window Internal Line Counter" tip: only advances
                       // on scanlines where the window was actually drawn

    // One byte per pixel: the final DMG shade (0=white .. 3=black),
    // already palette-translated - not the raw tile color index. Good
    // enough for a pixel-for-pixel comparison against a reference image
    // (this phase's actual correctness gate); real RGB/display output
    // is a front-end's job, not modeled until Phase 7.
    uint8_t framebuffer[GB_SCREEN_HEIGHT][GB_SCREEN_WIDTH];

    int frame_ready; // set once per VBlank entry; a driver clears it after grabbing a frame
} GBPpu;

void gb_ppu_reset(GBPpu *ppu);

// Advances the PPU by `cycles` T-states (the same unit gb_cpu_step()
// returns) - called once per gb_cpu_step(), the same way real hardware
// ticks the PPU off the identical clock the CPU runs from. `cpu` is
// needed read-only for VRAM/OAM/palette memory access while rendering,
// and to set IF (0xFF0F) interrupt-request bits on VBlank/STAT events -
// full interrupt *dispatch* is still Phase 4, but there's no reason for
// the PPU side of "an interrupt became pending" to wait for that.
void gb_ppu_step(GBPpu *ppu, struct GBCpu *cpu, int cycles);

uint8_t gb_ppu_read_reg(GBPpu *ppu, uint16_t addr);
// `cpu` is needed only for the DMA register (0xFF46), which triggers an
// immediate OAM transfer (see gb_ppu_write_reg's own comment on why
// this is instant rather than timed).
void gb_ppu_write_reg(GBPpu *ppu, struct GBCpu *cpu, uint16_t addr, uint8_t val);

#endif
