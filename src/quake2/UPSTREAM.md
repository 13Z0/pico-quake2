# Upstream

Vendored from https://github.com/ozkl/quake2generic (GPL-2.0), commit
`84f34ed5382323d96b81a44a44f73496791084a3` (2026-07-18), which is id Software's
Quake II 3.2x source with the game module and the software renderer linked statically.

Imported subset: `client/`, `server/`, `qcommon/`, `game/`, `ref_soft/`,
`net/net_loopback.c`, `null/cd_null.c`, `other/{q_hunk,q_system,vid_lib,vid_menu,glob}.c`,
`quake2.h`, `quakegeneric.h`, licence files. Not imported: `ref_gl/`, `ctf/`, the
SDL/GDI ports, Visual Studio projects and the Unix network driver.

The sources are edited in place for the port; `git log -- src/quake2` lists the changes.
