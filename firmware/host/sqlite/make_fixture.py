"""Writes the fixture database for the SQLite VFS test.

The tables are TREEWALK's (wiki/components/treewalk.md): no keys, no indexes.
`filler` is a wide table that makes the file span many pages.
"""
import sqlite3
import sys

db = sqlite3.connect(sys.argv[1])
db.executescript("""
CREATE TABLE book   (bookID INTEGER, name TEXT);
CREATE TABLE node   (bookID INTEGER, nodeID INTEGER, parentID INTEGER, body TEXT);
CREATE TABLE choice (bookID INTEGER, nodeID INTEGER, seq INTEGER, label TEXT, childID INTEGER);
CREATE TABLE filler (n INTEGER, pad TEXT);
INSERT INTO book VALUES (1, 'Test Book');
INSERT INTO node VALUES (1, 1, NULL, 'You stand at a door.');
INSERT INTO node VALUES (1, 2, 1, 'The room is dark.');
INSERT INTO node VALUES (1, 3, 1, 'The hall is bright.');
INSERT INTO node VALUES (1, 4, 2, 'The end.');
INSERT INTO choice VALUES (1, 1, 2, 'Go left', 3);
INSERT INTO choice VALUES (1, 1, 1, 'Go right', 2);
INSERT INTO choice VALUES (1, 2, 1, 'Sleep', 4);
INSERT INTO choice VALUES (1, 3, 1, 'Sleep', 4);
""")
db.executemany("INSERT INTO filler VALUES (?, ?)",
               ((i, "x" * 200) for i in range(5000)))
db.commit()
db.close()
