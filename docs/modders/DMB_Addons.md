# Addons: mods that bring code (Dead Man's Boots)

In VCMI a mod is data. Dead Man's Boots (DMB) also lets a mod bring code of two kinds: an AI
player, and a random map generator with its own lobby tab. Such a mod is packaged, listed,
installed, updated and switched on or off like any other mod. DMB itself ships no addon: without
one, the AI menus hold VCMI's own AIs and the lobby has no generator tab.

## An AI plugin

The mod carries the AI's library in a folder named `ai`, and its `mod.json` declares it:

```json
"aiPlugin" : {
    "library" : "MyAI",           // ai/MyAI.dll (Windows), ai/libMyAI.so, ai/libMyAI.dylib
    "name" : "My AI",             // what the launcher's AI menus show and the settings store
    "kinds" : [ "adventure" ],    // "adventure", "battle", or both
    "builtFor" : "0.1.1"          // the exact DMB version the library was built against
}
```

The library exports VCMI's usual `GetAiName`, plus `GetNewAI` (adventure) and/or
`GetNewBattleAI` (battle), so an existing VCMI AI needs no code change. A C++ AI shares classes with
the engine and has no stable binary interface, so DMB loads a plugin only in the version it names in
`builtFor`. Once the mod is enabled, the launcher lists the AI by name in its AI menus. A plugin
that cannot load is shown greyed out, with the reason. A game that asks for an AI that is missing or
refused gets VCMI's own AI instead, and the log says why.

## A map generator

The mod carries everything it runs in a folder named `generator`, and brings the layout of its lobby
tab and its texts in `Content`, like any mod's resources:

```text
mymapgen/
    mod.json
    generator/                            everything that runs (the command included)
    Content/config/widgets/mygen/tab.json and its pages
    Content/config/mygen/english.json     its texts (a path of its own: not config/translations)
```

```json
"mapGenerator" : {
    "name" : "My Generator",                     // what the lobby's button says
    "command" : "generator/generate.cmd",        // inside the generator folder
    "tab" : "config/widgets/mygen/tab.json"      // the tab's layout, in the mod's Content
}
```

The tab layout is an ordinary VCMI widget layout. Its pages are a `pages` widget named `pages`
(addon API level 2, [DMB_UI_Modding.md](DMB_UI_Modding.md)), or, in layouts written for level 1, a
`pages` list naming one layout per page, opened by buttons that call `activateMapGenPage`. Its
`defaults` hold the map settings (`map`), the generator's own settings (`params`) and an optional
`preset`. Widgets bind to settings with `"setting": "persistent:mapGen/params/<name>"`, which keeps
their values in `persistentStorage.json`. The callbacks `activateMapGenPage`, `resetMapGenDefaults`,
`generateMapGenMap` and `chooseMapGenTemplate` are the tab's own. The texts the tab itself shows can
be reworded by the mod's translation: `vcmi.mapGen.generate.hover`, `.running`, `.failed`,
`.refused` (two `%s`: the generator's name, then the reason), `.humans` (one `%d`: the players in
the lobby), and `vcmi.mapGen.template.hover`,
`.choose`, `.none`. Where the mod brings none, DMB's generic wording shows
(`vcmi.dmb.mapGenerator.*`).

Generate runs the command with these arguments, and the command writes a `.vmap` at `--out`:

```text
--w N --h N --players N --humans N --seed N --out PATH
--underground 1          (a two-level map)
--declaremods 1|0
--vcmiroot DIR --vcmiuserdir DIR     (the game's data folder and the player's user folder)
--template NAME          (a template the player chose)
--preset NAME
--bio.<name> VALUE       (each stored setting; rivers as --rivers VALUE)
```

Its output goes to `extmapgen_log.txt` in the logs folder. When it fails, a line there that starts
with `Error:` is what the player is shown. The generator must write nothing inside its own folder
(caches belong under `--vcmiuserdir`), because DMB checks that folder before every run, as below.
With several generator mods enabled, the last one in load order gets the tab. The new map lands in
the player's `Maps/RandomMaps`, and the lobby selects it.

When more players are in the lobby than the tab's human players setting (friends who joined, or
hotseat names), `--humans` is raised to one per player, and `--players` with it if needed: a map
with fewer human slots cannot be played there, and a multiplayer lobby does not list it.

The tab's button sits beside Random Map in every lobby: single player, hotseat, and network games.
In a network lobby it is 64 pixels wide, so a name of about eight letters fits; only the host can
use it, as with Random Map. The players who join need neither the generator nor its mod, as long as
the mod changes no game content (a generator, templates and texts do not): when the game starts,
the host's server sends every player the whole game, the generated map included. Content the map
uses from other mods, such as their towns, needs those mods on every side, as in any VCMI game.

## Trust: DMB's mod catalog pins the code

A mod's code runs with the player's rights, so DMB runs it only when
[DMB's mod catalog](https://github.com/DeadMansBoots/DMB-Mods-Repository) vouches for that exact
code. The catalog entry of an addon carries `codeSha256`, the pin of its `ai` or `generator` folder:

- every file in the folder becomes a line `<its SHA-256>  <its path>`, the path relative to the
  folder with `/` separators;
- the lines are sorted by path, each ends in a newline, and the SHA-256 of all of them is the pin.

`python tools/addon_hash.py --zip mymod.zip` in the catalog's repository prints it. The launcher saves
the catalog's pins whenever it downloads the catalog. Before an AI plugin loads and before each
generator run, the game hashes the folder and compares. A mod the catalog does not list, or whose
files differ from the pinned ones, is refused, and the player is told why.

While you build and test your own addon, switch the check off in your `settings.json`:

```json
"mods" : { "allowUnlistedCode" : true }
```

Players never need this: a mod installed from the launcher's list is pinned.

## Getting listed

Publish the mod's zip and its `mod.json` wherever you host your project, then open a pull request on
the catalog that adds `entries/<mod-id>.json` with the download, the size, and `codeSha256`. The
pull request's check downloads the zip and verifies the pin.
