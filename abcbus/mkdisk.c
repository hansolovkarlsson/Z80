// abcbus/mkdisk.c - bin/abcdisk, which creates and inspects ABC-bus floppy
// images.
//
// It lives beside disk.c for that file's own reason: the ABC bus is a bus,
// not a machine, and both abc80/ and abc802/ mount the same media through
// the same card. A tool that makes that media belongs with the card, not
// under either target.
//
// What it writes is a *formatted, empty* disk. That is not the same as an
// empty file, which is the trap this tool exists to close: a zero-filled
// image of the right size attaches fine, is recognized as an ABC830, and
// then fails every SAVE with "Error 41" (disk space full), because an
// unformatted image has no free-list for the DOS to allocate out of.
//
// Real hardware does have a formatter, but not in either ROM: it is
// DOSGEN, a program (DOSGEN.ABS) that ships on a Luxor system disk and is
// reached by leaving BASIC with BYE. So on a real ABC802 you need a
// working system disk in order to make a disk. That circularity is what
// this tool breaks - it needs no machine, no system disk and no ROM at
// all, which is exactly the situation someone starting from a bare
// checkout and a few downloaded images is in.
//
// THE FORMAT WAS DERIVED, NOT DOCUMENTED. Every constant below was read
// out of real Luxor media by inspection and then confirmed the only way
// that counts: an image built by this code is attached to bin/abc802, a
// program is SAVEd to it, and a *separate process* LOADs, LISTs and RUNs
// it back. Both drive types were verified that way. If you change a
// constant here, redo that round trip - the failure mode of a wrong
// free-list is a plausible-looking image that silently refuses to write.
//
// That verification is on the ABC802 only. The ABC80 mounts the same
// media through the same card, and its own DOS keeps its directory copies
// at the same sectors (ABC80 Milestone 6), so images from here are
// expected to work there - but bin/abc80 has no scripted-keyboard option,
// so nothing has actually driven a SAVE on that machine. Do not treat it
// as covered.
//
// Layout, per drive type (all sectors are 256 bytes):
//
//                          ABC830 "mo"           ABC832/834 "mf"
//   total sectors          640 (160K)            2560 (640K)
//   sectors per cluster    1                     4
//   system area            sectors 0-23          sectors 0-31
//   free-list, live        sector 6              sector 14
//   free-list, pristine    sector 7              sector 15
//   directory              sectors 16-23         sectors 16-31
//   directory backup       sectors 8-15          (none)
//   unwritten fill         0x00                  0xE5
//
// The free-list is one bit per *cluster*, most significant bit first,
// 1 = allocated. Both drives have 640 clusters, so it is 80 bytes on
// either - which is why the ABC832's four-sectors-per-cluster geometry
// needs no separate bitmap size. Clusters past the usable end are marked
// allocated permanently so the DOS never hands them out; on the ABC832
// that is everything from cluster 320 up, i.e. this DOS uses only the
// first half of a 640K disk, which is what its own real media shows.
//
// Two details found by comparing real disks rather than guessed:
//
//   - Sector 7 (mo) / 15 (mf) is a *pristine* copy of the free-list: on
//     every real disk examined it reports exactly the system area
//     allocated and nothing else, while the live copy tracks real files.
//     That is what made a blank disk reconstructible at all - it is
//     literally a picture of the disk as formatted.
//   - The eight bytes at 0xEF-0xF6 of a free-list sector are counters,
//     1 on a pristine disk and incrementing with use. They are reproduced
//     because real media has them; nothing here depends on their meaning.
//
// A directory sector is a 16-byte header of zeros followed by fifteen
// 16-byte records:
//
//     +0..1   start position; the sector is this >> 5
//     +2..3   length in bytes - confirmed by saving two programs of known
//             size and matching the field against the clusters allocated.
//             Real Luxor media often carries 0 here for files its own
//             tools wrote, so treat 0 as "not recorded", not as empty.
//     +4..11  name, space padded
//     +12..14 extension, space padded
//     +15     0xFF
//
// An unused record is sixteen 0xFF bytes, so an empty directory sector is
// sixteen zeros then 240 0xFF. The ABC830's backup copy at sectors 8-15 is
// written empty too, but note that this DOS does not keep it in sync: of
// three real disks, two had identical copies and one had drifted.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_SIZE 256
#define DIR_RECORD  16
#define DIR_FREE    0xFF

typedef struct {
    const char *name;
    unsigned sectors;           // total, and so the image's size
    unsigned system_sectors;    // reserved: boot, free-lists, directory
    unsigned sectors_per_cluster;
    unsigned usable_clusters;   // clusters past this are permanently allocated
    unsigned freelist_live;     // sector holding the working free-list
    unsigned freelist_pristine; // sector holding the as-formatted copy
    unsigned dir_first, dir_count;         // the directory the DOS writes
    unsigned backup_first, backup_count;   // its backup copy; count 0 if none
    uint8_t fill;               // what unwritten media reads as
    bool has_descriptor;        // sector 0 carries a volume descriptor
} Format;

static const Format FORMAT_MO = {
    .name = "mo",
    .sectors = 640, .system_sectors = 24, .sectors_per_cluster = 1,
    .usable_clusters = 640,
    .freelist_live = 6, .freelist_pristine = 7,
    .dir_first = 16, .dir_count = 8,
    .backup_first = 8, .backup_count = 8,
    .fill = 0x00, .has_descriptor = true,
};

static const Format FORMAT_MF = {
    .name = "mf",
    .sectors = 2560, .system_sectors = 32, .sectors_per_cluster = 4,
    .usable_clusters = 320,
    .freelist_live = 14, .freelist_pristine = 15,
    .dir_first = 16, .dir_count = 16,
    .backup_first = 0, .backup_count = 0,
    .fill = 0xE5, .has_descriptor = false,
};

static const Format *FORMATS[] = {&FORMAT_MO, &FORMAT_MF};
#define NFORMATS ((int)(sizeof(FORMATS) / sizeof(FORMATS[0])))

// Sector 0 of an ABC830 data disk. Copied from real media rather than
// invented: every 160K image examined that is not itself bootable carries
// this same sixteen-byte descriptor followed by 0xFF to the end of the
// sector. A disk written with it is not bootable, which is correct for one
// this tool made - there is no system on it to boot.
static const uint8_t MO_DESCRIPTOR[16] = {
    0x80, 0x00, 0x00, 0xFF, 0x00, 0x01, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void set_cluster(uint8_t *bitmap, unsigned cluster) {
    bitmap[cluster / 8] |= (uint8_t)(0x80u >> (cluster % 8));
}

// Build one free-list sector: the system area allocated, everything up to
// the usable end free, everything past it allocated forever.
static void build_freelist(const Format *f, uint8_t *sector) {
    memset(sector, 0, SECTOR_SIZE);
    unsigned system_clusters = f->system_sectors / f->sectors_per_cluster;
    for (unsigned c = 0; c < system_clusters; c++) set_cluster(sector, c);
    for (unsigned c = f->usable_clusters; c < 640; c++) set_cluster(sector, c);
    // Past the 80-byte bitmap and up to the counters, real media is 0xFF.
    memset(sector + 0x50, 0xFF, 0xEF - 0x50);
    memset(sector + 0xEF, 0x01, 8);
}

static uint8_t *build_image(const Format *f) {
    size_t size = (size_t)f->sectors * SECTOR_SIZE;
    uint8_t *img = malloc(size);
    if (!img) return NULL;
    memset(img, f->fill, size);

    if (f->has_descriptor) {
        uint8_t *s0 = img;
        memset(s0, 0xFF, SECTOR_SIZE);
        memcpy(s0, MO_DESCRIPTOR, sizeof MO_DESCRIPTOR);
        // Sectors 1-5 of real ABC830 media are filled with '@'. Their
        // purpose was not established; they are reproduced because every
        // real image has them and a formatter that omits them would be
        // guessing that they do not matter.
        memset(img + 1 * SECTOR_SIZE, 0x40, 5 * SECTOR_SIZE);
    }

    uint8_t freelist[SECTOR_SIZE];
    build_freelist(f, freelist);
    memcpy(img + (size_t)f->freelist_live * SECTOR_SIZE, freelist, SECTOR_SIZE);
    memcpy(img + (size_t)f->freelist_pristine * SECTOR_SIZE, freelist, SECTOR_SIZE);

    uint8_t dir[SECTOR_SIZE];
    memset(dir, 0, DIR_RECORD);
    memset(dir + DIR_RECORD, DIR_FREE, SECTOR_SIZE - DIR_RECORD);
    for (unsigned i = 0; i < f->dir_count; i++)
        memcpy(img + (size_t)(f->dir_first + i) * SECTOR_SIZE, dir, SECTOR_SIZE);
    for (unsigned i = 0; i < f->backup_count; i++)
        memcpy(img + (size_t)(f->backup_first + i) * SECTOR_SIZE, dir, SECTOR_SIZE);

    return img;
}

static const Format *format_by_name(const char *name) {
    for (int i = 0; i < NFORMATS; i++)
        if (!strcmp(FORMATS[i]->name, name)) return FORMATS[i];
    return NULL;
}

static const Format *format_by_size(long size) {
    for (int i = 0; i < NFORMATS; i++)
        if (size == (long)FORMATS[i]->sectors * SECTOR_SIZE) return FORMATS[i];
    return NULL;
}

static int cmd_create(const char *path, const Format *f, bool force) {
    if (!force) {
        FILE *probe = fopen(path, "rb");
        if (probe) {
            fclose(probe);
            fprintf(stderr, "'%s' already exists; pass --force to overwrite "
                            "it (this destroys any files on it)\n", path);
            return 1;
        }
    }
    uint8_t *img = build_image(f);
    if (!img) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    FILE *out = fopen(path, "wb");
    if (!out) {
        perror(path);
        free(img);
        return 1;
    }
    size_t size = (size_t)f->sectors * SECTOR_SIZE;
    bool ok = fwrite(img, 1, size, out) == size;
    if (fclose(out) != 0) ok = false;
    free(img);
    if (!ok) {
        fprintf(stderr, "Failed to write '%s'\n", path);
        return 1;
    }
    printf("Created '%s': %s, %u sectors, %zu bytes, %u free clusters\n",
           path, f->name, f->sectors, size,
           f->usable_clusters - f->system_sectors / f->sectors_per_cluster);
    printf("Attach it with: bin/abc802 --disk %s%s\n", path,
           strcmp(f->name, "mo") == 0 ? " --interleave 0" : "");
    return 0;
}

// Read the directory back. This is here so a created image can be checked
// without booting a machine, and because it decodes the very records
// cmd_create() writes - a wrong constant in one shows up in the other.
static int cmd_list(const char *path) {
    FILE *in = fopen(path, "rb");
    if (!in) {
        perror(path);
        return 1;
    }
    if (fseek(in, 0, SEEK_END) != 0) {
        fprintf(stderr, "Cannot determine size of '%s'\n", path);
        fclose(in);
        return 1;
    }
    long size = ftell(in);
    const Format *f = format_by_size(size);
    if (!f) {
        fprintf(stderr, "'%s' is %ld bytes, which is neither a 160K (%ld) "
                        "nor a 640K (%ld) image\n",
                path, size, (long)FORMAT_MO.sectors * SECTOR_SIZE,
                (long)FORMAT_MF.sectors * SECTOR_SIZE);
        fclose(in);
        return 1;
    }

    printf("%s: %s, %u sectors\n", path, f->name, f->sectors);
    int files = 0;
    for (unsigned s = 0; s < f->dir_count; s++) {
        uint8_t sector[SECTOR_SIZE];
        if (fseek(in, (long)(f->dir_first + s) * SECTOR_SIZE, SEEK_SET) != 0) break;
        if (fread(sector, 1, SECTOR_SIZE, in) != SECTOR_SIZE) break;
        for (unsigned off = DIR_RECORD; off + DIR_RECORD <= SECTOR_SIZE;
             off += DIR_RECORD) {
            const uint8_t *r = sector + off;
            bool free_slot = true;
            for (int i = 0; i < DIR_RECORD; i++)
                if (r[i] != DIR_FREE) { free_slot = false; break; }
            if (free_slot) continue;
            if (r[15] != DIR_FREE) continue;   // not a record this tool knows
            char name[9], ext[4];
            memcpy(name, r + 4, 8);  name[8] = '\0';
            memcpy(ext, r + 12, 3);  ext[3] = '\0';
            for (int i = 7; i >= 0 && name[i] == ' '; i--) name[i] = '\0';
            for (int i = 2; i >= 0 && ext[i] == ' '; i--) ext[i] = '\0';
            if (!name[0]) continue;
            unsigned start = (unsigned)((r[0] << 8) | r[1]) >> 5;
            unsigned length = (unsigned)((r[2] << 8) | r[3]);
            if (length)
                printf("  %-8s %-3s  sector %-5u %u bytes\n",
                       name, ext, start, length);
            else
                printf("  %-8s %-3s  sector %-5u (size not recorded)\n",
                       name, ext, start);
            files++;
        }
    }
    if (!files) printf("  (empty)\n");

    // Free space, from the live free-list.
    uint8_t bitmap[SECTOR_SIZE];
    if (fseek(in, (long)f->freelist_live * SECTOR_SIZE, SEEK_SET) == 0 &&
        fread(bitmap, 1, SECTOR_SIZE, in) == SECTOR_SIZE) {
        unsigned used = 0;
        for (unsigned c = 0; c < f->usable_clusters; c++)
            if (bitmap[c / 8] & (0x80u >> (c % 8))) used++;
        printf("  %u of %u clusters used, %u free\n",
               used, f->usable_clusters, f->usable_clusters - used);
    }
    fclose(in);
    return 0;
}

static void usage(const char *prog) {
    printf("Usage: %s create FILE [--type mo|mf] [--force]\n", prog);
    printf("       %s list FILE\n", prog);
    printf("\n");
    printf("Creates a formatted, empty ABC-bus floppy image that the DOS can\n");
    printf("write to. A zero-filled file of the right size is NOT one: it\n");
    printf("attaches and is recognized, then fails every SAVE with Error 41,\n");
    printf("because it has no free-list. The machine's own formatter is\n");
    printf("DOSGEN, which lives on a system disk rather than in ROM - so\n");
    printf("without this you need a working disk to make a disk.\n");
    printf("\n");
    printf("  --type mo   ABC830, 640 sectors, 163840 bytes (160K) - default\n");
    printf("  --type mf   ABC832/834, 2560 sectors, 655360 bytes (640K)\n");
    printf("  --force     overwrite FILE if it exists, destroying its contents\n");
    printf("\n");
    printf("Images this tool writes are in logical sector order, so attach a\n");
    printf("160K one with --interleave 0. See abc802/resources/disks/README.md\n");
    printf("for why that flag exists.\n");
}

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        usage(argv[0]);
        return argc < 2 ? 1 : 0;
    }
    if (!strcmp(argv[1], "create")) {
        const char *path = NULL;
        const Format *f = &FORMAT_MO;
        bool force = false;
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "--type") && i + 1 < argc) {
                f = format_by_name(argv[++i]);
                if (!f) {
                    fprintf(stderr, "--type must be mo or mf\n");
                    return 1;
                }
            } else if (!strcmp(argv[i], "--force")) {
                force = true;
            } else if (argv[i][0] == '-') {
                fprintf(stderr, "Unknown option '%s'\n", argv[i]);
                return 1;
            } else if (!path) {
                path = argv[i];
            } else {
                fprintf(stderr, "Unexpected argument '%s'\n", argv[i]);
                return 1;
            }
        }
        if (!path) {
            fprintf(stderr, "create needs a filename\n");
            return 1;
        }
        return cmd_create(path, f, force);
    }
    if (!strcmp(argv[1], "list")) {
        if (argc != 3) {
            fprintf(stderr, "list needs exactly one filename\n");
            return 1;
        }
        return cmd_list(argv[2]);
    }
    fprintf(stderr, "Unknown command '%s'\n", argv[1]);
    usage(argv[0]);
    return 1;
}
