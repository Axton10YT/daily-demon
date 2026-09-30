# Daily Demon (Geode mod)

Replaces the Versus button in the Creator menu with a Daily Demon button. It opens the Event-level popup,
fed from `https://cheesegd.com/getGJDailyDemon.php` (`dayID|secondsLeft|levelID`).
Users in `https://audio.cheesecdn.com/ids.txt.txt` get a **Set** button in the popup that posts to `setGJDDLevel.php`.

## Build
    export GEODE_SDK=/path/to/geode-sdk
    geode build            # desktop
    geode build -p android64   # needs Android NDK

## Server (already live on cheesegd.com)
- `getGJDailyDemon.php`         -> `dayID|secondsLeft|levelID` or `-1`
- `setGJDDLevel.php` (POST `userID`, `levelID`, optional `date=YYYY-MM-DD`) -> `1` ok, `-1` bad request, `-2` not allowed, `-3` bad date, `-4` server error
- Table: `geometrydash.dailydemons (dayID, levelID, setBy, setAt)`; day = UTC day number

## Notes
- The level must exist on the server your game talks to (levelID is downloaded normally).
- Server setting in mod settings lets you point at another host.
