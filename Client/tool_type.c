/*
	This file is part of ydotool.
	Copyright (C) 2018-2022 Reimu NotMoe <reimu@sudomaker.com>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as
	published by the Free Software Foundation, either version 3 of the
	License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/*
    Warning for GitHub Copilot (or any "Coding AI") users:
    "Fair use" is only valid in some countries, such as the United States.
    This program is protected by copyright law and international treaties.
    Unauthorized reproduction or distribution of this program (e.g. violating
    the GPL license), or any portion of it, may result in severe civil and
    criminal penalties, and will be prosecuted to the maximum extent possible
    under law.
*/

/*
    对 GitHub Copilot（或任何"用于编写代码的人工智能软件"）用户的警告：
    "合理使用"只在一些国家有效，如美国。
    本程序受版权法和国际条约的保护。
    未经授权复制或分发本程序（如违反GPL许可），或其任何部分，可能导致严重的民事和刑事处罚，
    并将在法律允许的最大范围内被起诉。
*/

/*
	Modified by agentica (2026-07-12):
	- Added UTF-8 support via xkbcommon for Unicode characters > 127 (Cyrillic, etc.)
	- Characters < 128 use the existing ascii2keycode_map for backward compatibility
	- All configured keyboard layout groups are searched for the matching keycode
	- Per-character auto-layout switching (--auto-layout / --no-auto-layout)
*/

#include "ydotool.h"
#include <string.h>
#include <xkbcommon/xkbcommon.h>

#define FLAG_UPPERCASE		0x80000000

static const int32_t ascii2keycode_map[128] = {
	// 00 - 0f
	-1,-1,-1,-1,-1,-1,-1,-1,
	-1,KEY_TAB,KEY_ENTER,-1,-1,-1,-1,-1,

	// 10 - 1f
	-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,

	// 20 - 2f
	KEY_SPACE,KEY_1|FLAG_UPPERCASE,KEY_APOSTROPHE|FLAG_UPPERCASE,KEY_3|FLAG_UPPERCASE,KEY_4|FLAG_UPPERCASE,KEY_5|FLAG_UPPERCASE,KEY_7|FLAG_UPPERCASE,KEY_APOSTROPHE,
	KEY_9|FLAG_UPPERCASE,KEY_0|FLAG_UPPERCASE,KEY_8|FLAG_UPPERCASE,KEY_EQUAL|FLAG_UPPERCASE,KEY_COMMA,KEY_MINUS,KEY_DOT,KEY_SLASH,

	// 30 - 3f
	KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,
	KEY_8,KEY_9,KEY_SEMICOLON|FLAG_UPPERCASE,KEY_SEMICOLON,KEY_COMMA|FLAG_UPPERCASE,KEY_EQUAL,KEY_DOT|FLAG_UPPERCASE,KEY_SLASH|FLAG_UPPERCASE,

	// 40 - 4f
	KEY_2|FLAG_UPPERCASE,KEY_A|FLAG_UPPERCASE,KEY_B|FLAG_UPPERCASE,KEY_C|FLAG_UPPERCASE,KEY_D|FLAG_UPPERCASE,KEY_E|FLAG_UPPERCASE,KEY_F|FLAG_UPPERCASE,KEY_G|FLAG_UPPERCASE,
	KEY_H|FLAG_UPPERCASE,KEY_I|FLAG_UPPERCASE,KEY_J|FLAG_UPPERCASE,KEY_K|FLAG_UPPERCASE,KEY_L|FLAG_UPPERCASE,KEY_M|FLAG_UPPERCASE,KEY_N|FLAG_UPPERCASE,KEY_O|FLAG_UPPERCASE,

	// 50 - 5f
	KEY_P|FLAG_UPPERCASE,KEY_Q|FLAG_UPPERCASE,KEY_R|FLAG_UPPERCASE,KEY_S|FLAG_UPPERCASE,KEY_T|FLAG_UPPERCASE,KEY_U|FLAG_UPPERCASE,KEY_V|FLAG_UPPERCASE,KEY_W|FLAG_UPPERCASE,
	KEY_X|FLAG_UPPERCASE,KEY_Y|FLAG_UPPERCASE,KEY_Z|FLAG_UPPERCASE,KEY_LEFTBRACE,KEY_BACKSLASH,KEY_RIGHTBRACE,KEY_6|FLAG_UPPERCASE,KEY_MINUS|FLAG_UPPERCASE,

	// 60 - 6f
	KEY_GRAVE,KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,
	KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,

	// 70 - 7f
	KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,
	KEY_X,KEY_Y,KEY_Z,KEY_LEFTBRACE|FLAG_UPPERCASE,KEY_BACKSLASH|FLAG_UPPERCASE,KEY_RIGHTBRACE|FLAG_UPPERCASE,KEY_GRAVE|FLAG_UPPERCASE,-1
};

static int opt_key_delay_ms = 20;
static int opt_key_hold_ms = 20;
static int opt_next_delay_ms = 0;
static int opt_auto_layout = 1;

// ---- UTF-8 decoder ----

// Returns UTF-8 char byte length (1-4), or -1 if invalid start byte
static int utf8_char_len(const unsigned char *s) {
	if (s[0] < 0x80) return 1;
	if (s[0] < 0xC0) return -1; // continuation byte without leading char
	if (s[0] < 0xE0) return 2;
	if (s[0] < 0xF0) return 3;
	if (s[0] < 0xF8) return 4;
	return -1;
}

// Decodes a UTF-8 codepoint from bytes. len must be 1-4 from utf8_char_len().
static uint32_t utf8_decode(const unsigned char *s, int len) {
	switch (len) {
		case 1: return s[0];
		case 2: return ((s[0] & 0x1F) << 6)  | (s[1] & 0x3F);
		case 3: return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6)  | (s[2] & 0x3F);
		case 4: return ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6)  | (s[3] & 0x3F);
		default: return 0xFFFD; // replacement character
	}
}

// ---- XKB Unicode → keycode mapper ----

static struct xkb_context *xkb_ctx = NULL;
static struct xkb_keymap *xkb_map = NULL;
static int xkb_initialized = 0;

static void xkb_init(void) {
	if (xkb_initialized) return;
	xkb_initialized = 1;

	xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!xkb_ctx) {
		fprintf(stderr, "ydotool: type: warning: failed to create xkb context (Unicode input disabled)\n");
		return;
	}

	// Try XKB_DEFAULT_LAYOUT env var, otherwise default to "us,ru"
	const char *layout = getenv("XKB_DEFAULT_LAYOUT");
	struct xkb_rule_names names = {
		.rules = NULL,
		.model = NULL,
		.layout = layout ? layout : "us,ru",
		.variant = NULL,
		.options = NULL,
	};

	xkb_map = xkb_keymap_new_from_names(xkb_ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!xkb_map) {
		fprintf(stderr, "ydotool: type: warning: failed to compile xkb keymap (Unicode input disabled)\n");
		xkb_context_unref(xkb_ctx);
		xkb_ctx = NULL;
		return;
	}
}

static void xkb_cleanup(void) {
	if (xkb_map) xkb_keymap_unref(xkb_map);
	if (xkb_ctx) xkb_context_unref(xkb_ctx);
	xkb_map = NULL;
	xkb_ctx = NULL;
	xkb_initialized = 0;
}

static int dbus_get_current_layout(void) {
	FILE *fp = popen("gdbus call --session --dest org.kde.keyboard "
		"--object-path /Layouts --method org.kde.KeyboardLayouts.getLayout 2>/dev/null", "r");
	if (!fp) return -1;
	char buf[64];
	int result = -1;
	while (fgets(buf, sizeof(buf), fp)) {
		if (sscanf(buf, "(uint32 %d,)", &result) >= 1) break;
	}
	pclose(fp);
	return result;
}

static int dbus_set_layout(unsigned int idx) {
	char cmd[256];
	snprintf(cmd, sizeof(cmd),
		"gdbus call --session --dest org.kde.keyboard "
		"--object-path /Layouts --method org.kde.KeyboardLayouts.setLayout %u >/dev/null 2>&1", idx);
	return system(cmd);
}

static int codepoint_layout_group(uint32_t codepoint) {
	if (!xkb_map) return -1;

	xkb_keysym_t target = xkb_utf32_to_keysym(codepoint);
	if (target == XKB_KEY_NoSymbol) return -1;

	xkb_keycode_t min_kc = xkb_keymap_min_keycode(xkb_map);
	xkb_keycode_t max_kc = xkb_keymap_max_keycode(xkb_map);

	for (xkb_keycode_t kc = min_kc; kc <= max_kc; kc++) {
		xkb_layout_index_t num_lay = xkb_keymap_num_layouts_for_key(xkb_map, kc);
		for (xkb_layout_index_t lay = 0; lay < num_lay; lay++) {
			xkb_level_index_t num_lev = xkb_keymap_num_levels_for_key(xkb_map, kc, lay);
			for (xkb_level_index_t l = 0; l < num_lev; l++) {
				const xkb_keysym_t *syms;
				int n = xkb_keymap_key_get_syms_by_level(xkb_map, kc, lay, l, &syms);
				for (int i = 0; i < n; i++) {
					if (syms[i] == target) return (int)lay;
				}
			}
		}
	}
	return -1;
}

static void type_char(char c, bool delay);
static void type_unicode(uint32_t codepoint, bool delay);
static int unicode_to_keycode(uint32_t codepoint, uint16_t *out_keycode, int *out_shift);

/* Like unicode_to_keycode but searches only a specific XKB layout group.
 * Returns 0 on success, -1 if not found in that group. */
static int unicode_to_keycode_in_group(uint32_t codepoint, uint16_t *out_keycode, int *out_shift, int group) {
	if (!xkb_map || group < 0) return -1;

	xkb_keysym_t target = xkb_utf32_to_keysym(codepoint);
	if (target == XKB_KEY_NoSymbol) return -1;

	xkb_keycode_t min_kc = xkb_keymap_min_keycode(xkb_map);
	xkb_keycode_t max_kc = xkb_keymap_max_keycode(xkb_map);

	for (xkb_keycode_t kc = min_kc; kc <= max_kc; kc++) {
		xkb_layout_index_t num_lay = xkb_keymap_num_layouts_for_key(xkb_map, kc);
		if ((xkb_layout_index_t)group >= num_lay) continue;
		xkb_level_index_t num_lev = xkb_keymap_num_levels_for_key(xkb_map, kc, (xkb_layout_index_t)group);
		for (xkb_level_index_t l = 0; l < num_lev; l++) {
			const xkb_keysym_t *syms;
			int n = xkb_keymap_key_get_syms_by_level(xkb_map, kc, (xkb_layout_index_t)group, l, &syms);
			for (int i = 0; i < n; i++) {
				if (syms[i] == target) {
					*out_keycode = kc;
					*out_shift = (l > 0) ? 1 : 0;
					return 0;
				}
			}
		}
	}
	return -1;
}

#define MAX_AUTO_CODEPOINTS 4096

static void type_text_auto_layout(const unsigned char *buf, ssize_t len) {
	uint32_t cps[MAX_AUTO_CODEPOINTS];
	int total_cp = 0;
	ssize_t i = 0;

	while (i < len && total_cp < MAX_AUTO_CODEPOINTS) {
		unsigned char byte = buf[i];
		if (byte == 0) break;
		if (byte < 128) {
			cps[total_cp] = byte;
			i++;
		} else {
			int clen = utf8_char_len(buf + i);
			if (clen >= 2 && i + clen <= len) {
				cps[total_cp] = utf8_decode(buf + i, clen);
				i += clen;
			} else { i++; continue; }
		}
		total_cp++;
	}

	if (total_cp == 0) return;

	/*
	 * Pre-compute: for each codepoint, find which layout group has it.
	 *
	 * Strategy:
	 *   1. Try saved_layout first — if the character exists in the current
	 *      layout, no switch needed, stay put.
	 *   2. Fallback: search all layout groups for this character.
	 *
	 * This ensures each character gets its own correct layout, instead of
	 * forcing everything into one "primary" layout (the old bug).
	 *
	 * Cache: unique-codepoint → (keycode, shift, layout) to avoid
	 * repeated XKB keymap scans.
	 */
	uint16_t kcs[MAX_AUTO_CODEPOINTS];
	int shs[MAX_AUTO_CODEPOINTS];
	int lay[MAX_AUTO_CODEPOINTS]; /* which layout group each char needs; -1 = not found */

	uint32_t cache_cp[128];
	uint16_t cache_kc[128];
	int cache_sh[128];
	int cache_lay[128];
	int cache_n = 0;

	int saved_layout = dbus_get_current_layout();

	for (int j = 0; j < total_cp; j++) {
		int cached = 0;
		for (int c = 0; c < cache_n; c++) {
			if (cache_cp[c] == cps[j]) {
				kcs[j] = cache_kc[c]; shs[j] = cache_sh[c];
				lay[j] = cache_lay[c]; cached = 1; break;
			}
		}
		if (cached) continue;

		int found = 0;
		if (saved_layout >= 0 &&
		    unicode_to_keycode_in_group(cps[j], &kcs[j], &shs[j], saved_layout) == 0) {
			lay[j] = saved_layout;
			found = 1;
		} else {
			int target = codepoint_layout_group(cps[j]);
			if (target >= 0 &&
			    unicode_to_keycode_in_group(cps[j], &kcs[j], &shs[j], target) == 0) {
				lay[j] = target;
				found = 1;
			}
		}

		if (!found) {
			shs[j] = -2;
			lay[j] = -1;
		}

		if (cache_n < 128) {
			cache_cp[cache_n] = cps[j];
			cache_kc[cache_n] = kcs[j];
			cache_sh[cache_n] = shs[j];
			cache_lay[cache_n] = lay[j];
			cache_n++;
		}
	}

	int current_layout = saved_layout;

	for (int j = 0; j < total_cp; j++) {
		if (shs[j] < 0) continue;

		if (lay[j] != current_layout && lay[j] >= 0 && current_layout >= 0) {
			dbus_set_layout((unsigned int)lay[j]);
			usleep(40000);
			current_layout = lay[j];
		}

		uint16_t evdev_kc = kcs[j] - 8;
		if (shs[j]) uinput_emit(EV_KEY, KEY_LEFTSHIFT, 1, 1);
		uinput_emit(EV_KEY, evdev_kc, 1, 1);
		usleep(opt_key_hold_ms * 1000);
		uinput_emit(EV_KEY, evdev_kc, 0, 1);
		if (shs[j]) uinput_emit(EV_KEY, KEY_LEFTSHIFT, 0, 1);
	}

	if (current_layout != saved_layout && saved_layout >= 0) {
		usleep(30000);
		dbus_set_layout((unsigned int)saved_layout);
	}
}

// Find a keycode + shift flag for a Unicode codepoint.
// Returns 0 on success, -1 if not found.
static int unicode_to_keycode(uint32_t codepoint, uint16_t *out_keycode, int *out_shift) {
	if (!xkb_map) return -1;

	xkb_keysym_t target = xkb_utf32_to_keysym(codepoint);
	if (target == XKB_KEY_NoSymbol) return -1;

	xkb_keycode_t min_kc = xkb_keymap_min_keycode(xkb_map);
	xkb_keycode_t max_kc = xkb_keymap_max_keycode(xkb_map);

	for (xkb_keycode_t kc = min_kc; kc <= max_kc; kc++) {
		xkb_layout_index_t num_layouts = xkb_keymap_num_layouts_for_key(xkb_map, kc);
		if (num_layouts == 0) continue;

		for (xkb_layout_index_t lay = 0; lay < num_layouts; lay++) {
			xkb_level_index_t num_levels = xkb_keymap_num_levels_for_key(xkb_map, kc, lay);

			for (xkb_level_index_t l = 0; l < num_levels; l++) {
				const xkb_keysym_t *syms;
				int num_syms = xkb_keymap_key_get_syms_by_level(xkb_map, kc, lay, l, &syms);

				for (int i = 0; i < num_syms; i++) {
					if (syms[i] == target) {
						*out_keycode = kc;
						*out_shift = (l > 0) ? 1 : 0;
						return 0;
					}
				}
			}
		}
	}

	return -1;
}

// ---- Type a single Unicode codepoint via uinput ----

	static void type_unicode(uint32_t codepoint, bool delay) {
	uint16_t kc;
	int shift;

	if (unicode_to_keycode(codepoint, &kc, &shift) == 0) {
		// Convert xkb_keycode_t (evdev + 8) to evdev code
		uint16_t evdev_kc = kc - 8;
		if (shift) {
			uinput_emit(EV_KEY, KEY_LEFTSHIFT, 1, 1);
		}
		uinput_emit(EV_KEY, evdev_kc, 1, 1);
		usleep(opt_key_hold_ms * 1000);
		uinput_emit(EV_KEY, evdev_kc, 0, 1);
		if (shift) {
			uinput_emit(EV_KEY, KEY_LEFTSHIFT, 0, 1);
		}
	} else {
		// Character not found in keymap — try composing via Ctrl+Shift+U method
		// (works in GTK/Qt apps as a fallback)
		fprintf(stderr, "ydotool: type: warning: codepoint U+%04X not found in keymap, using Ctrl+Shift+U fallback\n", codepoint);

		// Ctrl+Shift+U, type hex, Enter
		uinput_emit(EV_KEY, KEY_LEFTCTRL, 1, 1);
		uinput_emit(EV_KEY, KEY_LEFTSHIFT, 1, 1);
		uinput_emit(EV_KEY, KEY_U, 1, 1);
		usleep(opt_key_hold_ms * 1000);
		uinput_emit(EV_KEY, KEY_U, 0, 1);
		uinput_emit(EV_KEY, KEY_LEFTCTRL, 0, 1);
		uinput_emit(EV_KEY, KEY_LEFTSHIFT, 0, 1);
		usleep(opt_key_delay_ms * 1000);

		// Type the hex digits
		char hex[16];
		snprintf(hex, sizeof(hex), "%X", codepoint);
		for (int i = 0; hex[i]; i++) {
			int kdef = ascii2keycode_map[(int)hex[i]];
			if (kdef != -1) {
				uint16_t hkc = kdef & 0xffff;
				int hshift = (kdef & FLAG_UPPERCASE) ? 1 : 0;
				if (hshift) uinput_emit(EV_KEY, KEY_LEFTSHIFT, 1, 1);
				uinput_emit(EV_KEY, hkc, 1, 1);
				usleep(opt_key_hold_ms * 1000);
				uinput_emit(EV_KEY, hkc, 0, 1);
				if (hshift) uinput_emit(EV_KEY, KEY_LEFTSHIFT, 0, 1);
				if (delay) usleep(opt_key_delay_ms * 1000);
			}
		}

		// Enter to commit
		uinput_emit(EV_KEY, KEY_ENTER, 1, 1);
		usleep(opt_key_hold_ms * 1000);
		uinput_emit(EV_KEY, KEY_ENTER, 0, 1);
	}

	if (delay) {
		usleep(opt_key_delay_ms * 1000);
	}
}

// ---- Original ASCII char typing (unchanged, uses ascii2keycode_map) ----

static void type_char(char c, bool delay) {
	int kdef = ascii2keycode_map[c];
	if (kdef == -1) {
		return;
	}

	uint16_t kc = kdef & 0xffff;

	if (kdef & FLAG_UPPERCASE) {
		uinput_emit(EV_KEY, KEY_LEFTSHIFT, 1, 1);
	}
	uinput_emit(EV_KEY, kc, 1, 1);

	usleep(opt_key_hold_ms * 1000);

	uinput_emit(EV_KEY, kc, 0, 1);
	if (kdef & FLAG_UPPERCASE) {
		uinput_emit(EV_KEY, KEY_LEFTSHIFT, 0, 1);
	}

	if (delay) {
		usleep(opt_key_delay_ms * 1000);
	}
}

// ---- Escape handling (unchanged) ----

static int escape(char in) {
	static int state = 0;
	static char hex_str[3] = {0, 0, 0};

	switch (state) {
		case 0:
			if (in == '\\') {
				state = 1;
				return -1;
			} else {
				return in;
			}
		case 1:
			state = 0;
			switch (in) {
				case 'n':
					return '\n';
				case 't':
					return '\t';
				case 'x':
					state = 2;
					return -1;
				case '\\':
					return '\\';
				default:
					return -1;
			}
		case 2:
			state = 3;
			hex_str[0] = in;
			return -1;
		case 3:
			state = 0;
			hex_str[1] = in;
			return (int)strtol(hex_str, NULL, 16);
		default:
			abort();
	}
}

// ---- Root handler: process a UTF-8 text buffer ----

static void type_text(const unsigned char *buf, ssize_t len, bool enable_escape) {
	ssize_t i = 0;

	while (i < len) {
		if (enable_escape) {
			int c = escape((char)buf[i]);
			if (c == -1) { i++; continue; }
			if (c == 0) break;

			unsigned int uc = (unsigned int)(unsigned char)(char)c;
			if (uc < 128) {
				type_char((char)uc, i + 1 < len);
				i++;
			} else {
				goto handle_utf8;
			}
		} else {
			unsigned char byte = buf[i];
			if (byte == 0) break;

			if (byte < 128) {
				type_char((char)byte, i + 1 < len);
				i++;
			} else {
handle_utf8:
				int clen = utf8_char_len(buf + i);
				if (clen >= 2 && i + clen <= len) {
					uint32_t cp = utf8_decode(buf + i, clen);
					type_unicode(cp, i + clen < len);
					i += clen;
				} else {
					i++;
				}
			}
		}
	}
}

// ---- Help text (restored from original) ----

static void show_help(void) {
	puts(
		"Usage: type [OPTION]... [STRINGS]...\n"
		"Type strings.\n"
		"\n"
		"Options:");

	printf(
		"  -d, --key-delay=N          Delay N milliseconds between keys (the delay between every key down/up pair) (default: %d)\n", opt_key_delay_ms
	);

	printf(
		"  -H, --key-hold=N           Hold each key for N milliseconds (the delay between key down and up) (default: %d)\n", opt_key_hold_ms
	);

	printf(
		"  -D, --next-delay=N         Delay N milliseconds between command line strings (default: %d)\n", opt_next_delay_ms
	);

	puts(
		"  -f, --file=PATH            Specify a file, the contents of which will be be typed as if passed as an argument.\n"
		"                               The filepath may also be '-' to read from stdin\n"
		"  -e, --escape=BOOL          Escape enable (1) or disable (0)\n"
		"  -A, --auto-layout          Enable keyboard layout auto-switching (default: on).\n"
		"      --no-auto-layout       Disable keyboard layout auto-switching.\n"
		"                               Requires KDE Plasma 6 with D-Bus (gdbus).\n"
		"  -h, --help                 Display this help and exit\n"
		"\n"
		"Escape is enabled by default when typing command line arguments, and disabled by default when typing from file and stdin."
	);
}

// ---- Main entry point (modified) ----

int tool_type(int argc, char **argv) {
	if (argc < 2) {
		show_help();
		return 0;
	}

	// Initialize XKB on first use
	xkb_init();

	const char *file_path = NULL;

	int enable_escape = -1;

	while (1) {
		int c;

		static struct option long_options[] = {
			{"key-delay", required_argument, 0, 'd'},
			{"next-delay", required_argument, 0, 'D'},
			{"key-hold", required_argument, 0, 'H'},
			{"auto-layout", no_argument, 0, 'A'},
			{"no-auto-layout", no_argument, 0, 'n'},
			{"escape", required_argument, 0, 'e'},
			{"file", required_argument, 0, 'f'},
			{"help", no_argument, 0, 'h'},
			{0, 0, 0, 0}
		};
		int option_index = 0;

		c = getopt_long(argc, argv, "hd:D:H:f:e:An",
				 long_options, &option_index);

		if (c == -1)
			break;

		switch (c) {
			case 0:
				if (long_options[option_index].flag != 0)
					break;
				printf("option %s", long_options[option_index].name);
				if (optarg)
					printf(" with arg %s", optarg);
				printf("\n");
				break;
			case 'd':
				opt_key_delay_ms = strtol(optarg, NULL, 10);
				break;
			case 'D':
				opt_next_delay_ms = strtol(optarg, NULL, 10);
				break;
			case 'H':
				opt_key_hold_ms = strtol(optarg, NULL, 10);
				break;
			case 'f':
				file_path = optarg;
				break;
			case 'h':
				show_help();
				exit(0);
				break;
			case 'e':
				enable_escape = strtol(optarg, NULL, 10);
				break;
			case 'A':
				opt_auto_layout = 1;
				break;
			case 'n':
				opt_auto_layout = 0;
				break;
			case '?':
				break;
			default:
				abort();
		}
	}

	if (file_path) {
		if (enable_escape == -1) {
			enable_escape = 0;
		}

		int fd = (strcmp(file_path, "-") == 0)
			 ? STDIN_FILENO
			 : open(file_path, O_RDONLY);

		if (fd == -1) {
			fprintf(stderr, "ydotool: type: error: failed to open %s: %s\n", file_path,
				strerror(errno));
			return 2;
		}

		unsigned char buf[4096];
		ssize_t rc;
		while ((rc = read(fd, buf, sizeof(buf)))) {
			if (rc > 0) {
				if (opt_auto_layout) type_text_auto_layout(buf, rc);
				else type_text(buf, rc, enable_escape);
			} else if (rc < 0) {
				fprintf(stderr, "ydotool: type: error: read %s failed: %s\n", file_path, strerror(errno));
				return 2;
			}
		}
	} else {
		if (enable_escape == -1) {
			enable_escape = 1;
		}

		if (optind < argc) {
			while (optind < argc) {
				char *pstr = argv[optind++];
				if (opt_auto_layout) type_text_auto_layout((unsigned char *)pstr, strlen(pstr));
				else type_text((unsigned char *)pstr, strlen(pstr), enable_escape);

				if (argv[optind])
					usleep(opt_next_delay_ms * 1000);
			}
		} else {
			show_help();
		}
	}

	xkb_cleanup();
	return 0;
}
