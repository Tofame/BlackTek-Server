function onUpdateDatabase()
    print("> Updating database to version 2 : Add quest pouch support")

    -- Create player_questpouchitems table
    db.query([[
        CREATE TABLE IF NOT EXISTS `player_questpouchitems` (
            `player_id` INT NOT NULL,
            `pid` INT NOT NULL DEFAULT '0',
            `sid` INT NOT NULL AUTO_INCREMENT,
            `itemtype` SMALLINT NOT NULL DEFAULT '0',
            `count` SMALLINT NOT NULL DEFAULT '0',
            `attributes` BLOB,
            `augments` BLOB,
            `skills` BLOB,
            `stats` BLOB,
            PRIMARY KEY (`player_id`, `sid`),
            KEY `sid` (`sid`),
            CONSTRAINT `player_questpouchitems_ibfk_1`
                FOREIGN KEY (`player_id`)
                REFERENCES `players` (`id`)
                ON DELETE CASCADE
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
    ]])

    print("  > Created player_questpouchitems table")
    print("> Database update completed")
    return true
end
