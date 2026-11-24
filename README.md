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

The release packages target **32-bit TF2 dedicated servers on Linux and Windows**
with SourceMod 1.12 and Metamod:Source 1.12. CBaseNPC uses SourceMod's bundled
BinTools, SDKTools, and SDKHooks extensions.
[SourceMod](https://www.sourcemod.net/), [Metamod:Source](https://www.metamodsource.net/)

1. Download the archive for your server's OS from
   [Releases](https://github.com/voided/CBaseNPC/releases).
2. Extract it into the server's `tf/` directory so that the archive's
   `addons/` folder merges with the existing one. Keep
   `addons/sourcemod/gamedata/cbasenpc.txt` paired with the extension binary
   when you upgrade.
3. Load a plugin that uses CBaseNPC, then run `sm exts list` in the server
   console to verify that the extension loaded.

## Writing a plugin

Add `#include <cbasenpc>` to your SourcePawn plugin and compile it with the
SourceMod 1.12 compiler. The main include defines `CBaseNPC` and pulls in the
entity, navigation, and NextBot APIs.
[`cbasenpc.inc`](scripting/include/cbasenpc.inc)

- The basic example registers admin commands to spawn a test NPC, teleport it,
  and assign it a goal. [`cbasenpc_example.sp`](scripting/cbasenpc_example.sp)
- The action test defines a Scout-style NPC with a custom entity factory,
  inputs, and NextBot actions. [`cbasenpc_actiontest.sp`](scripting/cbasenpc_actiontest.sp)

The compiled examples ship in `addons/sourcemod/plugins/disabled/`. Move the
example you want to try into `addons/sourcemod/plugins/` to enable it. Use a map
with a navigation mesh to try path following.

## Building from source

The extension uses AMBuild 2.2, the TF2 HL2SDK, and SourceMod and Metamod:Source
headers. Run `git submodule update --init --recursive` to check out SafetyHook.
The CI workflow shows the dependency branches and build commands used for
the Windows and Linux packages. [Build workflow](.github/workflows/ci.yml)

Originally developed by Kenzzer, KitRifty, Arthurdead, and Pelipoika.
[Original AlliedModders release thread](https://forums.alliedmods.net/showthread.php?p=2763133)
