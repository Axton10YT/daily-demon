# Hourly Level

Geode v6 mod for Geometry Dash 2.2091. Replaces the Map Pack button in the Creator menu with an Hourly Level button that opens an Event-style popup showing a new level every hour.

Ported from [Daily Demon](https://github.com/Axton10YT/daily-demon) (`geode-5-port` branch).

## Server

Drop the files from `server/` into the GDPS webroot next to `connection.php`'s site, run `server/setup.sql` once, and create the two config files in `/var/www/ddata`:

```
php -r 'echo password_hash("YOUR_PIN", PASSWORD_DEFAULT);' > /var/www/ddata/hl_pin.txt
echo "ACCOUNT_ID ACCOUNT_ID" > /var/www/ddata/hl_ids.txt     # accountIDs allowed to set levels
```

The **Server** setting defaults to `https://cheesegd.com`. Endpoints (all under `{server}/`):

| Endpoint | Method | Response |
|---|---|---|
| `getGJHourlyLevel.php[?number=N]` | GET | `hourID\|secondsLeft\|levelID\|number`, or `-1`. Current level = the one set for this UTC hour, else the latest earlier one. |
| `getGJHLList.php` | GET | Comma-separated level IDs of every Hourly Level (`-1` if none) |
| `validateGJHLPIN.php` | POST `pin` | `1` correct, `0` wrong, `2` too many wrong tries from this IP |
| `setGJHLLevel.php` | POST `pin`, `accountID`, `userID`, `levelID` | `1` ok, `-6` wrong PIN, `-2` account not whitelisted, `-7` userID doesn't match the account, `2` throttled |

## Setting a level

Open any level page and tap the lock button (bottom right). Enter the PIN; once it is accepted you get a menu to set that level as the Hourly Level. The PIN is remembered until the game closes. The server re-checks the PIN on every set and checks your accountID against `hl_ids.txt`.

## Building

```
geode build
```

Needs Geode SDK v6.0.0-alpha.1+, Geode CLI 4.0.0+ and C++23.
