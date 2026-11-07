<?php
// Shared helpers for the Hourly Level endpoints. Config lives outside the webroot, in /var/www/ddata:
//   hl_pin.txt - password_hash() of the PIN. Create it with:
//                php -r 'echo password_hash("YOUR_PIN", PASSWORD_DEFAULT);' > /var/www/ddata/hl_pin.txt
//   hl_ids.txt - allowed accountIDs (whitespace / comma separated)
const HL_DATA = "/var/www/ddata";
const HL_MAX_FAILS = 5;       // wrong PINs allowed ...
const HL_FAIL_WINDOW = 900;   // ... per IP in this many seconds

function hlDb() {
	require "/var/www/gdps/config/connection.php";
	$db = new PDO("mysql:host=$servername;port=$port;dbname=$dbname", $username, $password);
	$db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
	return $db;
}

function hlRateFile() {
	$dir = HL_DATA . "/hl_rate";
	if(!is_dir($dir)) @mkdir($dir, 0770, true);
	return $dir . "/" . md5($_SERVER['REMOTE_ADDR'] ?? 'x') . ".json";
}

function hlFails() {
	$f = hlRateFile();
	$d = @json_decode(@file_get_contents($f), true);
	if(!is_array($d) || (time() - ($d['first'] ?? 0)) > HL_FAIL_WINDOW) return ['count' => 0, 'first' => time()];
	return $d;
}

function hlThrottled() { return hlFails()['count'] >= HL_MAX_FAILS; }

function hlRecordFail() {
	$d = hlFails();
	$d['count']++;
	@file_put_contents(hlRateFile(), json_encode($d), LOCK_EX);
}

function hlClearFails() { @unlink(hlRateFile()); }

function hlPinOk($pin) {
	$hash = trim((string)@file_get_contents(HL_DATA . "/hl_pin.txt"));
	$pin = (string)$pin;
	if($hash === '' || $pin === '' || strlen($pin) > 64) return false;
	return password_verify($pin, $hash);
}

function hlAllowedIds() {
	$raw = @file_get_contents(HL_DATA . "/hl_ids.txt");
	if($raw === false) return [];
	return array_map('intval', preg_split('/[\s,;]+/', trim($raw), -1, PREG_SPLIT_NO_EMPTY));
}
