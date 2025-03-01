# Chia Harvest Graph
Monitor for Chia Harvesting

![screenshot](images/screenshot0.png "screenshot")


## Introduction

The chiaharvestgraph tool will graph Chia Harvesting activity in a linux terminal. Use a 24-bit colour terminal, like xterm or gnome-terminal.

To use it:

Set the loglevel for Chia to **INFO** by editting `~/.chia/mainnet/config/config.yaml` and make sure you have `log_level: INFO` set.

Then do:

**$ ./chiaharvestgraph ~/.chia/mainnet/log**

Leave the tool running, as it keeps checking the log. More pixels will scroll in from the right, plotting top to bottom.

## Rationale

Much can go wrong when harvesting Chia.
The full node may lose connection to peers, the farmer could not be talking to the full node, the harvester could not be talking to the farmer, or maybe just spotty Internet connection?

That's why it is important to keep an eye on the INFO log.
When challenged, the harvester will (on behalf of a farmer) look for proof.
It will look for that in the plots that pass the plot-filter.
(Every plot has a 1:512 chance of passing, by the way.)

The debug log will contain lines that look like:
``` 
0 plots were eligible for farming 3c91c49224... Found 0 proofs. Time: 0.00383 s. Total 39 plots
```

A properly working harvester should be outputting that line every 10 seconds or so to the log file (provided the log level is INFO.)

This tool will look for those lines in the logs.

## Function

A Chia Harvester will get challenged every 10 seconds or so, to look for proof in its plots.
This tool will identify those lines, and register the time-stamps for those.
If there are not enough of those time-stamps within any given period, the harvester is under-harvesting, or even not harvesting. This is colour coded on the graph.

The graph spans from the right of the terminal (NOW) to the left of the terminal (PAST) and every shaded band represents one hour, and every vertical line, one quarter of an hour.

Depending on the vertical resolution of the terminal, every plot pixel represents a number of seconds, 15 minutes from top to bottom.

NOTE: You can see more days of the week by simply resizing your terminal to be wider.

## Colours

A yellow colour means that the harvest frequency is nominal for that time span.

An orange colour means that it was under harvested.

A red colour means that there was no harvesting at those time slots.

And for the incredibly lucky... a blue pixel represents a found proof! Yeehaw!
Better check your wallet!

## Keys

Press ESCAPE or Q to exit diskgraph.

## Donations

Chia Harvest Graph is a tool by Bram Stolk, who is on Twitter as: @BramStolk where he mostly tweets about game development.

If you find this tool useful, donations can go to XCH wallet:
xch1zfgqfqfdse3e2x2z9lscm6dx9cvd5j2jjc7pdemxjqp0xp05xzps602592

## Known issues

* Shows garbage on terminals that do not support 24 bit colour.
* Missing manual page.

## Copyright

chiaharvestgraph is (c)2021 by Bram Stolk and licensed under the MIT license.
