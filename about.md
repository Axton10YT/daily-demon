# Hourly Level

A new level, every hour. **Hourly Level** replaces the **Map Pack** button in the Creator menu with an **Hourly Level** button that opens the same popup style as the Event level.

## Features
- **New Creator menu button**: the Map Pack tile is swapped for an Hourly Level tile with a clock.
- **Event-style popup**: same look, timer and level card as the Event level popup, retitled *Hourly Level*.
- **Resets every hour (UTC)**: the countdown shows how long is left on the current level.
- **Load Next**: step through numbered Hourly Levels from the popup.
- **The Safe**: lists every level that has ever been an Hourly Level.
- **Level setters**: trusted users get a **Set HL** button on level pages to choose the Hourly Level by level ID. Everyone else never sees it.

## How it works
The popup fetches the current level from the Hourly Level server and downloads it through the normal level download, so it works like any other daily-style level. If nothing has been set for this hour, the popup tells you so.

## Settings
- **Server**: base URL of the Hourly Level server. This has to be set before the button does anything.

## Notes
- Needs an internet connection.
- The chosen level has to exist on the server your game is connected to.
- Only the Map Pack button is touched. Nothing else in the Creator menu moves.
- Built for Geode v6 (Geometry Dash 2.2091).
