# Map generation modes

Dead Man's Boots can make a random map more than one way: VCMI's own random map generator, and
each map generator mod the player has enabled ([DMB_Addons.md](DMB_Addons.md)). Each way is a
mode. This page is the contract for how the lobby offers the modes, so every generator mod
gets the same treatment and a player sets a mode once and keeps it.

## What the player sees

- The lobby's Random Map button opens the random map window in the mode used last, and closes
  it, as in VCMI.
- At the top of that window, two golden arrows step through the modes, with the mode's name
  between them. The first mode is VCMI's own random map; the generators follow in the order their
  mods load; after the last, the arrows come round to the first again.
- The mode stays as it was left, from one game to the next and between sessions, until the player
  changes it. A player who always uses one generator sets it once.
- Only the host sees the arrows. Players who join see the host's choice in the map's card, and need
  neither the generator nor its mod.

With no generator mod enabled there are no arrows, and the window is VCMI's alone.

## What a generator mod provides

- Its tab: the window's content in its mode (`mapGenerator.tab`, [DMB_Addons.md](DMB_Addons.md)).
  The tab fills the window below the arrows. The arrows and the mode's name sit in the band from
  the top of the window to y = 50; a tab keeps that band free of its own widgets.
- Its own pages, when it has several: buttons of its own, below that band. The arrows switch modes
  only, never a generator's pages.
- Its name, which the arrows show (`mapGenerator.name`).

A generator that cannot run (its files missing, or not vouched for by DMB's catalog) is not offered
as a mode, and the launcher's log says why.

## Where the mode is kept

The mode is the mod ID of the generator, or empty for VCMI's own random map, in the user folder's
`persistentStorage.json` under `dmb.randomMapMode`. A mode whose mod is later disabled or removed
falls back to VCMI's own random map.

## For DMB's own code

`CLobbyScreen` (client/lobby) owns the modes: one generator tab per offered generator, the arrows
and the name, and the remembered mode. `MapGenerators::active()` (lib/modding) lists the
generators in load order.
