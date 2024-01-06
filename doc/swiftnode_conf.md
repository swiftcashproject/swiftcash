Multi swiftnode config
=======================

The multi swiftnode config allows you to control multiple swiftnodes from a single wallet. The wallet needs to have a valid collateral output of 50,000 coins for each swiftnode. To use this, place a file named swiftnode.conf in the data directory of your install:
 * Windows: %APPDATA%\SwiftCash\
 * Mac OS: ~/Library/Application Support/SwiftCash/
 * Unix/Linux: ~/.swiftcash/

The new swiftnode.conf format consists of a space seperated text file. Each line consisting of an alias, IP address followed by port, swiftnode private key, collateral output transaction id, collateral output index, donation address and donation percentage (the latter two are optional and should be in format "address:percentage").

Example:
```
mn1 127.0.0.2:28544 73HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0
```

In the example above:
* the collateral for mn1 consists of transaction 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c, output index 0 has amount 50000


The following new RPC commands are supported:
* list-conf: shows the parsed swiftnode.conf
* start-alias \<alias\>
* stop-alias \<alias\>
* start-many
* stop-many
* outputs: list available collateral output transaction ids and corresponding collateral output indexes

When using the multi swiftnode setup, it is advised to run the wallet with 'swiftnode=0' as it is not needed anymore.
