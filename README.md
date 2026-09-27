# Dead Man's Boots

Dead Man's Boots (DMB) is a Heroes of Might and Magic III engine built on [VCMI](https://github.com/vcmi/vcmi) 1.7.5. It plays the game as VCMI does, keeps its settings, saves and mods apart from any VCMI you already have, and adds a few things of its own.

## Download

Windows installers are on the [Releases](https://github.com/DeadMansBoots/Dead-Mans-Boots/releases) page. DMB does not include the game itself: you need your own copy of Heroes of Might and Magic III, and the launcher asks for its files the first time it starts. If you already play VCMI, it offers to copy your setup across, and leaves the original untouched.

The installer is not signed yet, so Windows may say it protected your PC. Choose More info, then Run anyway.

## What DMB adds to VCMI

- Its own user folder, `Documents\My Games\DMB`, so DMB and VCMI never share settings, saves or mods.
- A dark interface option in the game's settings.
- Its own mod list, [DMB-Mods-Repository](https://github.com/DeadMansBoots/DMB-Mods-Repository): VCMI's mods, plus mods made for DMB, all installed from the launcher.
- A notice in the launcher when a new release is out.

## Bugs and ideas

Use the forms on the [Issues](https://github.com/DeadMansBoots/Dead-Mans-Boots/issues/new/choose) page. The launcher's help page links there, and it can export your logs to attach.

## Mods

Every mod lives in its own repository, wherever its author keeps it, and DMB's own team works the same way. To list a mod in DMB's launcher, open a pull request that adds its entry to [DMB-Mods-Repository](https://github.com/DeadMansBoots/DMB-Mods-Repository/tree/main/entries).

## Building from source

The `dmb` branch holds DMB's changes on top of VCMI 1.7.5, and it builds the way VCMI does: see VCMI's [Windows build guide](docs/developers/Building_Windows.md) and the other guides beside it. The map generator comes in as the `mapgen` submodule. VCMI's own readme is still in [docs](docs/Readme.md).

## Credits and license

DMB is VCMI with changes, and VCMI's authors wrote nearly all of it: see [VCMI on GitHub](https://github.com/vcmi/vcmi) and [vcmi.eu](https://vcmi.eu/). Like VCMI, DMB is free software under the GNU General Public License, version 2 or later: see [license.txt](license.txt). DMB is not affiliated with the owners of Heroes of Might and Magic III.
