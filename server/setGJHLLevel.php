<?php
// Set the Hourly Level for the current UTC hour. POST: pin, accountID, userID, levelID
// Returns: 1 ok | -1 bad request | -2 account not on the whitelist | -4 server error
//          -6 wrong PIN | -7 userID doesn't belong to accountID | 2 too many wrong PINs
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
if(!in_array($accountID, hlAllowedIds(), true)) exit("-2");

try {
	$db = hlDb();
	// the userID the client claims has to be the one the server has for that account
	$u = $db->prepare("SELECT userID FROM users WHERE extID = :a LIMIT 1");
	$u->execute([':a' => $accountID]);
	$row = $u->fetch(PDO::FETCH_ASSOC);
	if(!$row || (int)$row['userID'] !== $userID) exit("-7");

	$t = time();
	$q = $db->prepare("INSERT INTO hourlylevels (hourID, levelID, setBy, setAt) VALUES (:h, :l, :a, :t)
		ON DUPLICATE KEY UPDATE levelID = :l2, setBy = :a2, setAt = :t2");
	$q->execute([':h' => intdiv($t, 3600), ':l' => $levelID, ':a' => $accountID, ':t' => $t, ':l2' => $levelID, ':a2' => $accountID, ':t2' => $t]);
} catch (PDOException $e) {
	exit("-4");
}
echo "1";
