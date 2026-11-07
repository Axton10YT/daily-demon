<?php
// Comma-separated level IDs of every Hourly Level (newest first), or -1 if none. Feeds "The Safe".
header('Content-Type: text/plain');
require __DIR__ . "/hl_common.php";
try {
	$db = hlDb();
	$ids = $db->query("SELECT levelID FROM hourlylevels GROUP BY levelID ORDER BY MAX(hourID) DESC LIMIT 500")->fetchAll(PDO::FETCH_COLUMN);
} catch (PDOException $e) { exit("-1"); }
echo $ids ? implode(",", array_map('intval', $ids)) : "-1";
