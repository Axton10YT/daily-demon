-- Run once against the GDPS database (the same one connection.php points at).
CREATE TABLE IF NOT EXISTS hourlylevels (
	hourID INT NOT NULL PRIMARY KEY,   -- unix time / 3600 (UTC hour index)
	levelID INT NOT NULL,
	setBy INT NOT NULL,                -- accountID that set it
	setAt INT NOT NULL
);
