"""Start Contact with both arrow-key code styles understood (ESC [ A and ESC O A).

The Zero's terminal may send either style; ncurses only knows the one in the
terminfo entry, so we register the other style with define_key() after initscr.
"""
import ctypes
import ctypes.util
import curses
import sys

_lib = ctypes.CDLL(ctypes.util.find_library("ncursesw") or "libncursesw.so.6")
_orig_initscr = curses.initscr
_KEYS = {"A": curses.KEY_UP, "B": curses.KEY_DOWN, "C": curses.KEY_RIGHT, "D": curses.KEY_LEFT,
         "H": curses.KEY_HOME, "F": curses.KEY_END}


def _initscr():
    scr = _orig_initscr()
    for letter, code in _KEYS.items():
        for prefix in ("\x1b[", "\x1bO"):
            _lib.define_key((prefix + letter).encode(), code)
    return scr


curses.initscr = _initscr

from contact.__main__ import start  # noqa: E402

sys.argv[0] = "contact"
sys.exit(start())
