# Debugging Telescreen

## Logs

Telescreen logs to the `LogTelescreen` category in the game log:
`%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log` (dedicated servers: the server's `FactoryGame/Saved/Logs`).

What to look for at game start:
- `Resolving vanilla assets for Telescreen content` followed by one line per screen size listing the billboard
  mesh, background, plane, screen material and hologram as `ok` or `MISSING`, then the descriptor, recipe and
  schematic lines. Any `MISSING` or `Could not load vanilla ...` means a vanilla asset path changed.
- `Created test pattern texture N` the first time a screen is seen.
- `<screen> began play at <location>, screen W x H cm` for every screen in the world.

Verbose detail (end of play) needs `-LogCmds="LogTelescreen Verbose"` on the game's command line, or
`Log LogTelescreen Verbose` in the console.

## Common problems

- Screen face blank or black: check for `no screen material` in the log.
- Test pattern mirrored or upside down: the corner colours show which axis is flipped (see `Architecture.md`).
- Not in the AWESOME Shop: look for the schematic line (`cost entries 1, shop subcategories 1, unlocks 1`).
