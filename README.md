# CBaseNPC

CBaseNPC is a SourceMod extension for Team Fortress 2 that exposes Valve's
NextBot system and TF2 entity APIs to SourcePawn. Plugins can use it to create
custom NPCs, direct their movement, and script how they react to players and
game events.

## What plugins can do

- Create `base_npc` entities or register custom entity classes with their own
  fields, inputs, and outputs.
- Define NextBot actions and transitions, override movement callbacks, and
  access the bot's body, vision, and decision-making interfaces.
- Query the map's navigation mesh, compute paths with custom costs, and follow
  paths toward a position or moving target.
- Control animations, inspect and modify damage information, and handle
  entity events such as kills.

## Installation

CBaseNPC 2.x experimental releases target **64-bit TF2 dedicated servers on
Linux and Windows** with SourceMod 1.13 and Metamod:Source 1.12. CBaseNPC uses
SourceMod's bundled BinTools, SDKTools, and SDKHooks extensions. The 1.x
releases target 32-bit servers on SourceMod 1.12.
[SourceMod](https://www.sourcemod.net/), [Metamod:Source](https://www.metamodsource.net/)

1. Download the archive for your server's OS from
   [Releases](https://github.com/voided/CBaseNPC/releases). Look for
   **Experimental x64** in the release title.
2. Extract it into the server's `tf/` directory so that the archive's
   `addons/` folder merges with the existing one. Keep
   `addons/sourcemod/gamedata/cbasenpc.txt` paired with the extension binary
   when you upgrade.
3. Load a plugin that uses CBaseNPC, then run `sm exts list` in the server
   console to verify that the extension loaded.

## Writing a plugin

Add `#include <cbasenpc>` to your SourcePawn plugin and compile it with the
SourceMod 1.13 compiler. Recompile existing CBaseNPC plugins for the 2.x
extension. API parameters and results declared as `Address` use SourceMod 1.13's
`int64` alias, while methodmaps such as `INextBot` and `CNavArea` remain
single-cell values. Plugins that use the typed APIs generally only need a
recompile. On x64, pointer-backed methodmaps carry opaque tokens. Update any
code that casts those values to addresses, stores addresses in `int` or `any`,
or assumes that a pointer field in engine memory is four bytes wide. Use the
API's `Address` getters and constructors when crossing into raw memory.
On x64, keep NextBot and component tokens only while their entity and interface
are live. Navigation tokens expire when the mesh reloads or the map ends.
Extension-owned paths and actions are invalidated when destroyed. Borrowed
vision records, animation layers, path segments, and other engine pointers
with no stable identity expire on the next game frame. Fetch a fresh token
when accessing one in a later frame. A same-frame borrow still depends on the
underlying object staying live. In particular, live mesh editing can remove
ladders or hiding spots before a reload, and the caller owns the lifetime of
any raw `Address` passed to a methodmap constructor.

The main include defines `CBaseNPC` and pulls in the entity, navigation, and
NextBot APIs.
[`cbasenpc.inc`](scripting/include/cbasenpc.inc)

- The basic example registers admin commands to spawn a test NPC, teleport it,
  and assign it a goal. [`cbasenpc_example.sp`](scripting/cbasenpc_example.sp)
- The action test defines a Scout-style NPC with a custom entity factory,
  inputs, and NextBot actions. [`cbasenpc_actiontest.sp`](scripting/cbasenpc_actiontest.sp)

The compiled examples ship in `addons/sourcemod/plugins/disabled/`. Move the
example you want to try into `addons/sourcemod/plugins/` to enable it. Use a map
with a navigation mesh to try path following.

## Building from source

The extension uses AMBuild 2.2, the TF2 HL2SDK, SourceMod 1.13,
Metamod:Source 1.12, and static libffi. Run
`git submodule update --init --recursive` to check out SafetyHook. Pass
`--libffi-root` with an installation under `x86/include` and `x86/lib` for
`--targets=x86`, or under `x86_64/include` and `x86_64/lib` for
`--targets=x86_64`. Build libffi with position-independent code on Linux.
On Windows, use a static CRT build such as the vcpkg `windows-static` triplet.
The branch workflow builds both architectures and publishes x64 prereleases. [Build workflow](.github/workflows/ci.yml)

Originally developed by Kenzzer, KitRifty, Arthurdead, and Pelipoika.
[Original AlliedModders release thread](https://forums.alliedmods.net/showthread.php?p=2763133)
