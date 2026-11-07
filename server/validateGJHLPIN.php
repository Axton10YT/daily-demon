<?php
// POST: pin. Returns 1 correct | 0 wrong | 2 too many wrong attempts from this IP.
header('Content-Type: text/plain');
if($_SERVER['REQUEST_METHOD'] !== 'POST') exit("0");
require __DIR__ . "/hl_common.php";
if(hlThrottled()) exit("2");
if(hlPinOk($_POST['pin'] ?? '')) { hlClearFails(); exit("1"); }
hlRecordFail();
echo "0";
