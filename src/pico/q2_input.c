// q2_input.c -- USB keyboard, mouse and gamepads to Quake II key events.
//
// q2_input_sample runs in the USB timer interrupt (q2_usb.cpp) and posts key
// events into the engine's key queue (Quake2_SendKey), which the main loop
// drains in Sys_SendKeyEvents. Every source is folded into one set of Quake II
// keys that are down, and only changes of that set are posted, so a key reached
// from two devices (the arrows from the keyboard and the D-pad) is released
// only when neither holds it.
//
// Gamepad buttons are named by position, as the HID driver reports them: io B
// is the bottom face button (Xbox A, PlayStation cross), io A the right one,
// io Y the left one and io X the top one. In game they send JOY1-4 and AUX1-7,
// bound in QG_PlatformConfig; in menus and the console the bottom button is
// Enter and the right one Escape. Each button keeps the key it pressed until
// it is released, so a menu opening while it is held does not turn the
// release into a different key.
//
// QG_GetMouseDiff (thread context) adds the right stick to the mouse motion.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "client/client.h"
#include "quakegeneric.h"
#include "q2_input.h"

void Quake2_SendKey (int key, int down);

// io::GamePadState::Button bits (3rdparty/pico_shared_drivers/usb_hid/gamepad.h)
#define IO_A		(1u << 0)
#define IO_B		(1u << 1)
#define IO_X		(1u << 2)
#define IO_Y		(1u << 3)
#define IO_C		(1u << 4)
#define IO_Z		(1u << 5)
#define IO_SELECT	(1u << 6)
#define IO_START	(1u << 7)
#define IO_L		(1u << 8)
#define IO_R		(1u << 9)
#define IO_LEFT		(1u << 31)
#define IO_RIGHT	(1u << 30)
#define IO_UP		(1u << 29)
#define IO_DOWN		(1u << 28)

#define TRIGGER_ON	100		// analog trigger threshold, 0..255

// Q2_INPUT_DIAG=1: print pad buttons and posted key events on the UART
#ifndef Q2_INPUT_DIAG
#define Q2_INPUT_DIAG	0
#endif

#define REPEAT_DELAY_MS	400	// console auto-repeat (backspace, page up/down)
#define REPEAT_RATE_MS	50

// HID keyboard usage -> Quake II key (0: ignored). Letters and other printable
// keys are passed unshifted; the console applies shift itself.
static const unsigned char hid_to_key[0x68] = {
	[0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd', [0x08] = 'e',
	[0x09] = 'f', [0x0a] = 'g', [0x0b] = 'h', [0x0c] = 'i', [0x0d] = 'j',
	[0x0e] = 'k', [0x0f] = 'l', [0x10] = 'm', [0x11] = 'n', [0x12] = 'o',
	[0x13] = 'p', [0x14] = 'q', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
	[0x18] = 'u', [0x19] = 'v', [0x1a] = 'w', [0x1b] = 'x', [0x1c] = 'y',
	[0x1d] = 'z',
	[0x1e] = '1', [0x1f] = '2', [0x20] = '3', [0x21] = '4', [0x22] = '5',
	[0x23] = '6', [0x24] = '7', [0x25] = '8', [0x26] = '9', [0x27] = '0',
	[0x28] = K_ENTER, [0x29] = K_ESCAPE, [0x2a] = K_BACKSPACE, [0x2b] = K_TAB,
	[0x2c] = K_SPACE, [0x2d] = '-', [0x2e] = '=', [0x2f] = '[', [0x30] = ']',
	[0x31] = '\\', [0x32] = '\\', [0x33] = ';', [0x34] = '\'', [0x35] = '`',
	[0x36] = ',', [0x37] = '.', [0x38] = '/',
	[0x3a] = K_F1, [0x3b] = K_F2, [0x3c] = K_F3, [0x3d] = K_F4,
	[0x3e] = K_F5, [0x3f] = K_F6, [0x40] = K_F7, [0x41] = K_F8,
	[0x42] = K_F9, [0x43] = K_F10, [0x44] = K_F11, [0x45] = K_F12,
	[0x48] = K_PAUSE, [0x49] = K_INS, [0x4a] = K_HOME, [0x4b] = K_PGUP,
	[0x4c] = K_DEL, [0x4d] = K_END, [0x4e] = K_PGDN,
	[0x4f] = K_RIGHTARROW, [0x50] = K_LEFTARROW, [0x51] = K_DOWNARROW, [0x52] = K_UPARROW,
	[0x54] = K_KP_SLASH, [0x55] = '*', [0x56] = K_KP_MINUS, [0x57] = K_KP_PLUS,
	[0x58] = K_KP_ENTER, [0x59] = K_KP_END, [0x5a] = K_KP_DOWNARROW, [0x5b] = K_KP_PGDN,
	[0x5c] = K_KP_LEFTARROW, [0x5d] = K_KP_5, [0x5e] = K_KP_RIGHTARROW, [0x5f] = K_KP_HOME,
	[0x60] = K_KP_UPARROW, [0x61] = K_KP_PGUP, [0x62] = K_KP_INS, [0x63] = K_KP_DEL,
	[0x64] = '\\',
};

// Gamepad inputs: what they send in game and in menus (0: nothing).
typedef struct
{
	uint32_t		bit;		// io button bit; 0 for a trigger
	int				trigger;	// 0 left, 1 right (when bit is 0)
	unsigned char	game_key;
	unsigned char	menu_key;
} pad_input_t;

static const pad_input_t pad_inputs[] = {
	{ IO_UP,     0, K_UPARROW,    K_UPARROW },
	{ IO_DOWN,   0, K_DOWNARROW,  K_DOWNARROW },
	{ IO_LEFT,   0, K_LEFTARROW,  K_LEFTARROW },
	{ IO_RIGHT,  0, K_RIGHTARROW, K_RIGHTARROW },
	{ IO_B,      0, K_JOY1,       K_ENTER },	// bottom
	{ IO_A,      0, K_JOY2,       K_ESCAPE },	// right
	{ IO_Y,      0, K_JOY3,       0 },			// left
	{ IO_X,      0, K_JOY4,       0 },			// top
	{ IO_L,      0, K_AUX1,       0 },
	{ IO_R,      0, K_AUX2,       0 },
	{ IO_SELECT, 0, K_AUX3,       0 },
	{ IO_START,  0, K_ESCAPE,     K_ESCAPE },
	{ IO_C,      0, K_AUX4,       0 },			// Genesis pads
	{ IO_Z,      0, K_AUX5,       0 },
	{ 0,         0, K_AUX6,       0 },			// left trigger
	{ 0,         1, K_AUX7,       0 },			// right trigger
};
#define NUM_PAD_INPUTS	(sizeof(pad_inputs) / sizeof(pad_inputs[0]))

static unsigned char	pad_latched[NUM_PAD_INPUTS];	// key sent on press, 0 = none
static qboolean			pad_held[NUM_PAD_INPUTS];
static uint32_t			keys_down[8];					// 256-bit set of Quake II keys
static int				repeat_key;
static uint32_t			repeat_at;

static void set_key (uint32_t *set, int key)
{
	if (key > 0 && key < 256)
		set[key >> 5] |= 1u << (key & 31);
}

static qboolean repeatable (int key)
{
	return key == K_BACKSPACE || key == K_PGUP || key == K_PGDN
		|| key == K_KP_PGUP || key == K_KP_PGDN;
}

void q2_input_sample (const q2_usb_state_t *s)
{
	uint32_t	cur[8];
	qboolean	in_game = cls.key_dest == key_game;
	uint32_t	now;
	unsigned	i;
	static q2_usb_state_t	last;

	// Nothing changed and nothing to repeat: the key set is the same (samples are
	// zero-filled, so padding compares equal). Most 4 ms ticks end here.
	if (!repeat_key && !s->mouse_wheel && !memcmp (s, &last, sizeof(last)))
		return;
	last = *s;
	now = to_ms_since_boot (get_absolute_time ());

	memset (cur, 0, sizeof(cur));

#if Q2_INPUT_DIAG
	{
		static uint32_t	last_buttons;
		static uint8_t	last_trigger[2];

		if (s->pad_buttons != last_buttons || s->pad_trigger[0] != last_trigger[0]
			|| s->pad_trigger[1] != last_trigger[1])
		{
			printf ("input: pad %08lx trig %u/%u\n", (unsigned long)s->pad_buttons,
				s->pad_trigger[0], s->pad_trigger[1]);
			last_buttons = s->pad_buttons;
			last_trigger[0] = s->pad_trigger[0];
			last_trigger[1] = s->pad_trigger[1];
		}
	}
#endif

	// keyboard
	for (i = 0; i < 6; i++)
		if (s->kb_keys[i] < sizeof(hid_to_key))
			set_key (cur, hid_to_key[s->kb_keys[i]]);
	if (s->kb_modifier & 0x11)
		set_key (cur, K_CTRL);
	if (s->kb_modifier & 0x22)
		set_key (cur, K_SHIFT);
	if (s->kb_modifier & 0x44)
		set_key (cur, K_ALT);

	// mouse
	if (s->mouse_buttons & 1)
		set_key (cur, K_MOUSE1);
	if (s->mouse_buttons & 2)
		set_key (cur, K_MOUSE2);
	if (s->mouse_buttons & 4)
		set_key (cur, K_MOUSE3);

	// gamepads: the key is chosen when the input goes down and kept until it is up
	for (i = 0; i < NUM_PAD_INPUTS; i++)
	{
		const pad_input_t	*p = &pad_inputs[i];
		qboolean			down = p->bit ? (s->pad_buttons & p->bit) != 0
										  : s->pad_trigger[p->trigger] > TRIGGER_ON;

		if (down && !pad_held[i])
			pad_latched[i] = in_game ? p->game_key : p->menu_key;
		else if (!down)
			pad_latched[i] = 0;
		pad_held[i] = down;
		set_key (cur, pad_latched[i]);
	}

	// post the changes
	for (i = 0; i < 8; i++)
	{
		uint32_t	changed = cur[i] ^ keys_down[i];

		while (changed)
		{
			int		b = __builtin_ctz (changed);
			int		key = (int)(i * 32 + b);
			int		down = (cur[i] >> b) & 1;

			changed &= changed - 1;
#if Q2_INPUT_DIAG
			printf ("input: key %d %s (dest %d)\n", key, down ? "down" : "up", cls.key_dest);
#endif
			Quake2_SendKey (key, down);
			if (down && repeatable (key))
			{
				repeat_key = key;
				repeat_at = now + REPEAT_DELAY_MS;
			}
		}
		keys_down[i] = cur[i];
	}

	// auto-repeat outside the game (the engine ignores repeats of other keys)
	if (repeat_key)
	{
		if (!(keys_down[repeat_key >> 5] & (1u << (repeat_key & 31))))
			repeat_key = 0;
		else if (!in_game && (int32_t)(now - repeat_at) >= 0)
		{
			Quake2_SendKey (repeat_key, 1);
			repeat_at = now + REPEAT_RATE_MS;
		}
	}

	// the wheel has no state: one press and release per notch
	for (i = 0; i < 4 && i < (unsigned)(s->mouse_wheel < 0 ? -s->mouse_wheel : s->mouse_wheel); i++)
	{
		int		key = s->mouse_wheel > 0 ? K_MWHEELUP : K_MWHEELDOWN;

		Quake2_SendKey (key, 1);
		Quake2_SendKey (key, 0);
	}
}

// Right stick look speed at full deflection, in degrees per second.
#define LOOK_YAW_DPS	180.0f
#define LOOK_PITCH_DPS	100.0f
#define LOOK_DEADZONE	8000

extern cvar_t	*sensitivity;
extern cvar_t	*m_yaw;
extern cvar_t	*m_pitch;

// -1..1 past the dead zone, squared for finer aim near the centre
static float stick_curve (int v)
{
	float	n;

	if (v > -LOOK_DEADZONE && v < LOOK_DEADZONE)
		return 0;
	n = ((v < 0 ? -v : v) - LOOK_DEADZONE) / (32767.0f - LOOK_DEADZONE);
	if (n > 1)
		n = 1;
	return v < 0 ? -n * n : n * n;
}

void QG_GetMouseDiff (int *dx, int *dy)
{
	static int		last_ms;
	static float	frac_x, frac_y;
	int				now = QG_Milliseconds ();
	float			dt = (now - last_ms) * 0.001f;
	int				lx, ly;

	last_ms = now;
	q2_usb_mouse_motion (dx, dy);

	// The right stick becomes mouse motion, scaled so its speed in degrees per
	// second does not depend on the mouse sensitivity.
	q2_usb_look (&lx, &ly);
	if (dt > 0.1f)
		dt = 0.1f;
	if ((lx || ly) && sensitivity && m_yaw && m_pitch
		&& sensitivity->value > 0 && m_yaw->value > 0 && m_pitch->value > 0)
	{
		frac_x += stick_curve (lx) * LOOK_YAW_DPS * dt / (m_yaw->value * sensitivity->value);
		frac_y -= stick_curve (ly) * LOOK_PITCH_DPS * dt / (m_pitch->value * sensitivity->value);
	}
	*dx += (int)frac_x;
	*dy += (int)frac_y;
	frac_x -= (int)frac_x;
	frac_y -= (int)frac_y;
}

const char *QG_PlatformConfig (void)
{
	return
		// gamepad; buttons named by position (bottom = Xbox A, PlayStation cross)
		"bind JOY1 +attack\n"			// bottom
		"bind JOY2 +moveup\n"			// right: jump
		"bind JOY3 +movedown\n"			// left: crouch
		"bind JOY4 weapnext\n"			// top
		"bind AUX1 +moveleft\n"			// L / LB
		"bind AUX2 +moveright\n"		// R / RB
		"bind AUX3 +klook\n"			// Select / Back: held, up and down look
		"bind AUX4 \"cmd help\"\n"		// C (Genesis pads)
		"bind AUX5 inven\n"				// Z (Genesis pads)
		"bind AUX6 \"cmd help\"\n"		// left trigger
		"bind AUX7 +attack\n"			// right trigger
		// mouse
		"bind MWHEELUP weapprev\n"
		"bind MWHEELDOWN weapnext\n"
		"set freelook 1\n"				// mouse and right stick look up and down
		"set cl_run 1\n"				// always run (Shift walks)
		// display
		"set viewsize 70\n"			// screen size 70%: fewer pixels to draw
		"set vid_gamma 0.7\n";			// brightness (the options menu slider)
}
