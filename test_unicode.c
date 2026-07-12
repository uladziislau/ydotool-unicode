/*
 * Unit test for ydotool-unicode XKB + UTF-8 mapping.
 * AGPL v3 — same as ydotool.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <xkbcommon/xkbcommon.h>

/* -------- Copy of the functions from tool_type.c -------- */

static int utf8_char_len(const unsigned char *s) {
	if (s[0] < 0x80) return 1;
	if (s[0] < 0xC0) return -1;
	if (s[0] < 0xE0) return 2;
	if (s[0] < 0xF0) return 3;
	if (s[0] < 0xF8) return 4;
	return -1;
}

static uint32_t utf8_decode(const unsigned char *s, int len) {
	switch (len) {
		case 1: return s[0];
		case 2: return ((s[0] & 0x1F) << 6)  | (s[1] & 0x3F);
		case 3: return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6)  | (s[2] & 0x3F);
		case 4: return ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6)  | (s[3] & 0x3F);
		default: return 0xFFFD;
	}
}

/* -------- Test harness -------- */

static int passed = 0;
static int failed = 0;

#define TEST(name, expr) do { \
	if (expr) { passed++; printf("  ✅ %s\n", name); } \
	else { failed++; printf("  ❌ %s\n", name); } \
} while(0)

int main() {
	printf("=== ydotool-unicode: UTF-8 decoder tests ===\n\n");

	/* -------- UTF-8 decoding -------- */
	const unsigned char hello[] = "Привет";
	int expected_len[] = {2, 2, 2, 2, 2, 2}; // each Cyrillic letter = 2 bytes

	int ok = 1;
	for (int i = 0; i < 6; i++) {
		int clen = utf8_char_len(hello + i);
		if (clen != expected_len[i]) {
			printf("  ❌ utf8_char_len at byte %d: got %d expected %d\n", i, clen, expected_len[i]);
			ok = 0;
		}
	}
	if (ok) { passed++; printf("  ✅ utf8_char_len for \"Привет\" — 6×2 bytes\n"); }
	else failed++;

	// Verify actual codepoints
	uint32_t expected_cp[] = {0x41F, 0x440, 0x438, 0x432, 0x435, 0x442}; // П р и в е т
	for (int i = 0; i < 6; i++) {
		int clen = utf8_char_len(hello + i);
		uint32_t cp = utf8_decode(hello + i, clen);
		char buf[32];
		snprintf(buf, sizeof(buf), "  utf8_decode \"%c\" → U+%04X", "Привет"[i], cp);
		TEST(buf, cp == expected_cp[i]);
	}

	printf("\n=== ydotool-unicode: XKB keycode mapping ===\n\n");

	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_rule_names names = {
		.rules = NULL,
		.model = NULL,
		.layout = "us,ru",  // our standard layout
		.variant = NULL,
		.options = NULL,
	};
	struct xkb_keymap *map = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!map) {
		printf("❌ Failed to compile keymap \"us,ru\"\n");
		return 1;
	}
	printf("Keymap \"us,ru\" compiled: min_kc=%d max_kc=%d\n\n",
		xkb_keymap_min_keycode(map), xkb_keymap_max_keycode(map));

	/* -------- Russian lowercase letters -------- */
	uint32_t ru_lower[] = {
		0x430, 0x431, 0x432, 0x433, 0x434, 0x435, 0x451, 0x436, 0x437,
		0x438, 0x439, 0x43A, 0x43B, 0x43C, 0x43D, 0x43E, 0x43F,
		0x440, 0x441, 0x442, 0x443, 0x444, 0x445, 0x446, 0x447,
		0x448, 0x449, 0x44A, 0x44B, 0x44C, 0x44D, 0x44E, 0x44F
	};
	char *ru_letters[] = {
		"а","б","в","г","д","е","ё","ж","з",
		"и","й","к","л","м","н","о","п",
		"р","с","т","у","ф","х","ц","ч",
		"ш","щ","ъ","ы","ь","э","ю","я"
	};
	int ru_expected_keycodes[] = {
		KEY_F, KEY_COMMA, KEY_D, KEY_U, KEY_L, KEY_T, KEY_GRAVE, KEY_J, KEY_P,
		KEY_B, KEY_Q, KEY_R, KEY_K, KEY_V, KEY_Y, KEY_J, KEY_G,
		KEY_H, KEY_C, KEY_N, KEY_E, KEY_A, KEY_O, KEY_W, KEY_X,
		KEY_I, KEY_S, KEY_Z, KEY_S, KEY_M, KEY_SEMICOLON, KEY_DOT
	};

	printf("--- Russian lowercase (левел 0, без Shift) ---\n");
	int ru_lower_fail = 0;
	for (int i = 0; i < 33; i++) {
		xkb_keysym_t ks = xkb_utf32_to_keysym(ru_lower[i]);
		if (ks == XKB_KEY_NoSymbol) {
			printf("  ❌ %s: xkb_utf32_to_keysym failed\n", ru_letters[i]);
			ru_lower_fail++;
			continue;
		}
		uint16_t kc = 0;
		int shift = -1;
		int found = 0;
		xkb_keycode_t min_kc = xkb_keymap_min_keycode(map);
		xkb_keycode_t max_kc = xkb_keymap_max_keycode(map);
		for (xkb_keycode_t k = min_kc; k <= max_kc && !found; k++) {
			xkb_layout_index_t num_lay = xkb_keymap_num_layouts_for_key(map, k);
			for (xkb_layout_index_t lay = 0; lay < num_lay && !found; lay++) {
				xkb_level_index_t num_lev = xkb_keymap_num_levels_for_key(map, k, lay);
				for (xkb_level_index_t l = 0; l < num_lev && !found; l++) {
					const xkb_keysym_t *syms;
					int n = xkb_keymap_key_get_syms_by_level(map, k, lay, l, &syms);
					for (int si = 0; si < n && !found; si++) {
						if (syms[si] == ks) {
							kc = k;
							shift = (l > 0) ? 1 : 0;
							found = 1;
						}
					}
				}
			}
		}
		if (!found) {
			printf("  ❌ %s: not found in keymap (keysym 0x%04X)\n", ru_letters[i], (unsigned)ks);
			ru_lower_fail++;
		} else {
			uint16_t evdev_kc = kc - 8;
			if (shift != 0 || evdev_kc != ru_expected_keycodes[i]) {
				printf("  ❌ %s: got kc=%d(evdev=%d) shift=%d, expected kc=%d shift=0\n",
					ru_letters[i], kc, evdev_kc, shift, ru_expected_keycodes[i] + 8);
				ru_lower_fail++;
			} else {
				passed++;
			}
		}
	}
	if (ru_lower_fail == 0) printf("  ✅ All 33 lowercase letters correct\n");

	/* -------- Russian uppercase letters -------- */
	printf("\n--- Russian uppercase (левел 1, с Shift) ---\n");
	uint32_t ru_upper[] = {
		0x410, 0x411, 0x412, 0x413, 0x414, 0x415, 0x401, 0x416, 0x417,
		0x418, 0x419, 0x41A, 0x41B, 0x41C, 0x41D, 0x41E, 0x41F,
		0x420, 0x421, 0x422, 0x423, 0x424, 0x425, 0x426, 0x427,
		0x428, 0x429, 0x42A, 0x42B, 0x42C, 0x42D, 0x42E, 0x42F
	};
	char *ru_upper_letters[] = {
		"А","Б","В","Г","Д","Е","Ё","Ж","З",
		"И","Й","К","Л","М","Н","О","П",
		"Р","С","Т","У","Ф","Х","Ц","Ч",
		"Ш","Щ","Ъ","Ы","Ь","Э","Ю","Я"
	};
	// Uppercase letters should be on the same keys but with shift=1
	int ru_upper_fail = 0;
	for (int i = 0; i < 33; i++) {
		xkb_keysym_t ks = xkb_utf32_to_keysym(ru_upper[i]);
		if (ks == XKB_KEY_NoSymbol) {
			printf("  ❌ %s: xkb_utf32_to_keysym failed\n", ru_upper_letters[i]);
			ru_upper_fail++;
			continue;
		}
		uint16_t kc = 0;
		int shift = -1;
		int found = 0;
		xkb_keycode_t min_kc = xkb_keymap_min_keycode(map);
		xkb_keycode_t max_kc = xkb_keymap_max_keycode(map);
		for (xkb_keycode_t k = min_kc; k <= max_kc && !found; k++) {
			xkb_layout_index_t num_lay = xkb_keymap_num_layouts_for_key(map, k);
			for (xkb_layout_index_t lay = 0; lay < num_lay && !found; lay++) {
				xkb_level_index_t num_lev = xkb_keymap_num_levels_for_key(map, k, lay);
				for (xkb_level_index_t l = 0; l < num_lev && !found; l++) {
					const xkb_keysym_t *syms;
					int n = xkb_keymap_key_get_syms_by_level(map, k, lay, l, &syms);
					for (int si = 0; si < n && !found; si++) {
						if (syms[si] == ks) {
							kc = k;
							shift = (l > 0) ? 1 : 0;
							found = 1;
						}
					}
				}
			}
		}
		if (!found) {
			printf("  ❌ %s: not found in keymap (keysym 0x%04X)\n", ru_upper_letters[i], (unsigned)ks);
			ru_upper_fail++;
		} else {
			uint16_t evdev_kc = kc - 8;
			if (shift != 1) {
				printf("  ❌ %s: kc=%d(evdev=%d) shift=%d, expected shift=1\n",
					ru_upper_letters[i], kc, evdev_kc, shift);
				ru_upper_fail++;
			} else {
				// Verify it's the same keycode as lowercase
				int lower_idx = i;
				uint16_t lower_kc = 0;
				{
					xkb_keysym_t lks = xkb_utf32_to_keysym(ru_lower[lower_idx]);
					int lfound = 0;
					for (xkb_keycode_t k = min_kc; k <= max_kc && !lfound; k++) {
						xkb_layout_index_t num_lay = xkb_keymap_num_layouts_for_key(map, k);
						for (xkb_layout_index_t lay = 0; lay < num_lay && !lfound; lay++) {
							xkb_level_index_t num_lev = xkb_keymap_num_levels_for_key(map, k, lay);
							for (xkb_level_index_t l = 0; l < num_lev && !lfound; l++) {
								const xkb_keysym_t *syms;
								int n = xkb_keymap_key_get_syms_by_level(map, k, lay, l, &syms);
								for (int si = 0; si < n && !lfound; si++) {
									if (syms[si] == lks) { lower_kc = k; lfound = 1; }
								}
							}
						}
					}
				}
				if (kc != lower_kc) {
					printf("  ❌ %s: uppercase kc=%d different from lowercase kc=%d\n",
						ru_upper_letters[i], kc, lower_kc);
					ru_upper_fail++;
				} else {
					passed++;
				}
			}
		}
	}
	if (ru_upper_fail == 0) printf("  ✅ All 33 uppercase letters correct (same keycodes + shift)\n");

	printf("\n=== Test summary ===\n");
	printf("  Passed: %d\n", passed);
	printf("  Failed: %d\n", failed);

	xkb_keymap_unref(map);
	xkb_context_unref(ctx);

	return failed > 0 ? 1 : 0;
}
