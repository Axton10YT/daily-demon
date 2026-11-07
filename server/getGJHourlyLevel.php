<?php
// Returns "hourID|secondsLeft|levelID|number", or -1 if nothing has ever been set.
// Current level = the one set for this UTC hour, or else the most recent earlier one (it stays until replaced).
// ?number=N returns the Nth Hourly Level ever set (1 = first).
header('Content-Type: text/plain');
require __DIR__ . "/hl_common.php";
try { $db = hlDb(); } catch (PDOException $e) { exit("-1"); }
$now = time();
$hour = intdiv($now, 3600);
$left = (($hour + 1) * 3600) - $now;
if(isset($_GET['number']) && (int)$_GET['number'] >= 1) {
	$want = (int)$_GET['number'];
	$q = $db->prepare("SELECT hourID, levelID FROM hourlylevels ORDER BY hourID ASC LIMIT 1 OFFSET :o");
	$q->bindValue(':o', $want - 1, PDO::PARAM_INT);
	$q->execute();
	$row = $q->fetch(PDO::FETCH_ASSOC);
	if(!$row) exit("-1");
	echo (int)$row['hourID'] . "|" . $left . "|" . (int)$row['levelID'] . "|" . $want;
	exit;
}
$q = $db->prepare("SELECT hourID, levelID FROM hourlylevels WHERE hourID <= :h ORDER BY hourID DESC LIMIT 1");
$q->execute([':h' => $hour]);
$row = $q->fetch(PDO::FETCH_ASSOC);
if(!$row) exit("-1");
$n = $db->prepare("SELECT COUNT(*) FROM hourlylevels WHERE hourID <= :h");
$n->execute([':h' => (int)$row['hourID']]);
echo (int)$row['hourID'] . "|" . $left . "|" . (int)$row['levelID'] . "|" . (int)$n->fetchColumn();
