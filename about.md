# Daily Demon

A new demon, every day. **Daily Demon** replaces the unused **Versus** button in the Creator menu with a **Daily Demon** button that opens the same popup style as the Event level.

## Features
- **New Creator menu button**: the Versus tile is swapped for a Daily Demon tile with a demon face.
- **Event-style popup**: same look, timer and level card as the Event level popup, retitled *Daily Demon*.
- **Resets at 00:00 UTC**: the countdown shows how long is left on today's demon.
- **Level setters**: trusted users get a **Set** button inside the popup to choose the next Daily Demon by level ID. Everyone else never sees it.

## How it works
The popup fetches today's demon from the Daily Demon server and downloads that level through the normal level download, so it works like any other daily-style level. If nothing has been set for today, the popup tells you so.

## Settings
- **Server**: base URL of the Daily Demon server (defaults to the official one).

## Notes
- Needs an internet connection.
- The chosen level has to exist on the server your game is connected to.
- Only the Versus button is touched. Nothing else in the Creator menu moves.
