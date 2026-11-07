<?php
// Set the Hourly Level for the current UTC hour. POST: pin, accountID, userID, levelID
// The players are on official GD, so their IDs can't be verified against this server's accounts:
// the PIN is the real lock and the IDs are matched against the whitelist (hl_ids.txt, accountIDs or userIDs).
// Returns: 1 ok | -1 bad request | -2 not on the whitelist | -4 server error | -6 wrong PIN | 2 too many wrong PINs
header('Content-Type: text/plain');
if($_SERVER['REQUEST_METHOD'] !== 'POST') exit("-1");
require __DIR__ . "/hl_common.php";
if(hlThrottled()) exit("2");
if(!hlPinOk($_POST['pin'] ?? '')) { hlRecordFail(); exit("-6"); }
hlClearFails();

$accountID = (int)($_POST['accountID'] ?? 0);
$userID = (int)($_POST['userID'] ?? 0);
$levelID = (int)($_POST['levelID'] ?? 0);
if($accountID <= 0 || $userID <= 0 || $levelID <= 0) exit("-1");
$allowed = hlAllowedIds();
if(!in_array($accountID, $allowed, true) && !in_array($userID, $allowed, true)) exit("-2");

try {
	$db = hlDb();
	$t = time();
	$q = $db->prepare("INSERT INTO hourlylevels (hourID, levelID, setBy, setAt) VALUES (:h, :l, :a, :t)
		ON DUPLICATE KEY UPDATE levelID = :l2, setBy = :a2, setAt = :t2");
	$q->execute([':h' => intdiv($t, 3600), ':l' => $levelID, ':a' => $accountID, ':t' => $t, ':l2' => $levelID, ':a2' => $accountID, ':t2' => $t]);
} catch (PDOException $e) {
	exit("-4");
}
echo "1";
