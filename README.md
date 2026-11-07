# Hourly Level

Geode v6 mod for Geometry Dash 2.2091. Replaces the Map Pack button in the Creator menu with an Hourly Level button that opens an Event-style popup showing a new level every hour.

Ported from [Daily Demon](https://github.com/Axton10YT/daily-demon) (`geode-5-port` branch).

## Server protocol

The **Server** setting defaults to the existing Daily Demon server, which uses the same endpoints. The mod calls these endpoints (all under `{server}/`):

| Endpoint | Method | Response |
|---|---|---|
| `getGJDailyDemon.php[?number=N]` | GET | `periodID\|secondsLeft\|levelID\|number` — `secondsLeft` drives the countdown, so the server decides how long each level lasts. `-1` when nothing is set. |
| `getGJDDIDs.php` | GET | Any text containing the account/user IDs allowed to set levels (digits separated by anything). |
| `setGJDDLevel.php` | POST `accountID`, `authtoken`, `levelID` | `1` ok, `-2` not authorised, `-5` auth failed |
| `requestGJDDAccess.php` | POST `accountID`, `authtoken` | `1` sent, `2` already has access, `3` pending, `-2` declined, `-5` auth failed |
| `getGJDDList.php` | GET | Comma-separated level IDs of every past Hourly Level (`-1` if none) |

`authtoken` comes from [Argon](https://github.com/GlobedGD/argon); the password/GJP is never sent.

## Building

```
geode build
```

Needs Geode SDK v6.0.0-alpha.1+, Geode CLI 4.0.0+ and C++23.
