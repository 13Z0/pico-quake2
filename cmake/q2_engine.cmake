# The Quake II engine (client, server, game module, software renderer) as a static
# library, libq2.a. Its .bss/.data are placed in PSRAM by cmake/psram_linker.cmake,
# which matches on the archive name, so keep the target named "q2".
#
# Same sources and flags as hosttest/build.sh and tools/arm_check.sh. The platform
# layer (src/pico) provides the QG_*/SWimp_*/SNDDMA_* hooks, the heap (q_mem.c) and
# the flash access for the asset cache.

set(Q2_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/quake2)

set(Q2_ENGINE_SOURCES
    # client
    client/cl_cin.c client/cl_ents.c client/cl_fx.c client/cl_newfx.c client/cl_input.c
    client/cl_inv.c client/cl_main.c client/cl_parse.c client/cl_pred.c client/cl_tent.c
    client/cl_scrn.c client/cl_view.c client/console.c client/keys.c client/menu.c
    client/qmenu.c client/snd_adpcm.c client/snd_dma.c client/snd_mem.c client/snd_mix.c
    # common
    qcommon/cmd.c qcommon/cmodel.c qcommon/common.c qcommon/crc.c qcommon/cvar.c
    qcommon/fcache.c qcommon/files.c qcommon/md4.c qcommon/net_chan.c qcommon/pmove.c
    # server
    server/sv_ccmds.c server/sv_ents.c server/sv_game.c server/sv_init.c server/sv_main.c
    server/sv_send.c server/sv_user.c server/sv_world.c
    # platform-independent glue from quake2generic
    other/q_hunk.c other/q_system.c other/vid_lib.c other/vid_menu.c other/glob.c
    net/net_loopback.c null/cd_null.c
    # game module (baseq2)
    game/g_ai.c game/p_client.c game/g_cmds.c game/g_svcmds.c game/g_combat.c
    game/g_func.c game/g_items.c game/g_main.c game/g_misc.c game/g_monster.c
    game/g_phys.c game/g_save.c game/g_spawn.c game/g_target.c game/g_trigger.c
    game/g_turret.c game/g_utils.c game/g_weapon.c game/m_actor.c game/m_berserk.c
    game/m_boss2.c game/m_boss3.c game/m_boss31.c game/m_boss32.c game/m_brain.c
    game/m_chick.c game/m_flipper.c game/m_float.c game/m_flyer.c game/m_gladiator.c
    game/m_gunner.c game/m_hover.c game/m_infantry.c game/m_insane.c game/m_medic.c
    game/m_move.c game/m_mutant.c game/m_parasite.c game/m_soldier.c game/m_supertank.c
    game/m_tank.c game/p_hud.c game/p_trail.c game/p_view.c game/p_weapon.c
    game/q_shared.c game/g_chase.c game/m_flash.c
    # software renderer
    ref_soft/r_aclip.c ref_soft/r_alias.c ref_soft/r_bsp.c ref_soft/r_draw.c
    ref_soft/r_edge.c ref_soft/r_image.c ref_soft/r_light.c ref_soft/r_main.c
    ref_soft/r_misc.c ref_soft/r_model.c ref_soft/r_part.c ref_soft/r_poly.c
    ref_soft/r_polyse.c ref_soft/r_rast.c ref_soft/r_scan.c ref_soft/r_sprite.c
    ref_soft/r_surf.c
)
list(TRANSFORM Q2_ENGINE_SOURCES PREPEND "${Q2_SRC}/")

# Savegame name tables, generated from the game sources (tools/gen_savetables.py).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB Q2_GAME_SOURCES ${Q2_SRC}/game/*.c)
set(Q2_SAVETABLES ${CMAKE_BINARY_DIR}/generated/g_savetables.c)
add_custom_command(
    OUTPUT ${Q2_SAVETABLES}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_savetables.py
            ${Q2_SRC}/game ${Q2_SAVETABLES}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_savetables.py ${Q2_GAME_SOURCES}
    COMMENT "Generating savegame name tables"
)

add_library(q2 STATIC ${Q2_ENGINE_SOURCES} ${Q2_SAVETABLES})

target_include_directories(q2 PUBLIC ${Q2_SRC} ${Q2_SRC}/other)
target_include_directories(q2 PRIVATE ${Q2_SRC}/game)

# Q2_ZBUFFER_SRAM: z-buffer in SRAM (150 KB) instead of PSRAM.
option(Q2_ZBUFFER_SRAM "Put the z-buffer in SRAM" OFF)
target_compile_definitions(q2 PRIVATE stricmp=strcasecmp Q2_DEVICE=1
    Q2_ZBUFFER_SRAM=$<BOOL:${Q2_ZBUFFER_SRAM}>)

# -fwrapv / -fno-aggressive-loop-optimizations: the renderer relies on wrapping
# signed arithmetic. -fcommon: tentative definitions in shared headers.
# -fsingle-precision-constant + q2_force.h's <tgmath.h>: float math stays in float.
# -ffunction-sections -fdata-sections: per-symbol placement and --gc-sections.
target_compile_options(q2 PRIVATE
    -O2
    -fno-strict-aliasing
    -fwrapv
    -fno-aggressive-loop-optimizations
    -fcommon
    -fsingle-precision-constant
    -fno-math-errno
    -ffunction-sections
    -fdata-sections
    -w
    -include q2_force.h
)

# Like pico-duke3D's libduke: no SDK libraries are linked into this static library
# (that would compile the SDK's INTERFACE sources into it). The platform layer links
# the SDK and resolves the engine's platform hooks.
