Eternity Core 0.12.0
=====================

Setup
---------------------
[Eternity Core](http://eternity-group.org/download) is the original Eternity client and it builds the backbone of the network. However, it downloads and stores the entire history of Eternity transactions (which is currently several GBs); depending on the speed of your computer and network connection, the synchronization process can take anywhere from a few hours to a day or more. Thankfully you only have to do this once. If you would like the process to go faster you can [download the blockchain directly](bootstrap.md).

Running
---------------------
The following are some helpful notes on how to run Eternity on your native platform.

### Unix

You need the Qt4 run-time libraries to run Eternity-Qt. On Debian or Ubuntu:

	sudo apt-get install libqtgui4

Unpack the files into a directory and run:

- bin/32/eternity-qt (GUI, 32-bit) or bin/32/eternityd (headless, 32-bit)
- bin/64/eternity-qt (GUI, 64-bit) or bin/64/eternityd (headless, 64-bit)



### Windows

Unpack the files into a directory, and then run eternity-qt.exe.

### OSX

Drag Eternity-Qt to your applications folder, and then run Eternity-Qt.

### Need Help?

* See the documentation at the [Bitcoin Wiki](https://en.bitcoin.it/wiki/Main_Page) ***TODO***
for help and more information.
* Ask for help on the [BitcoinTalk](https://bitcointalk.org/index.php?topic=1616533.0) forums.

Building
---------------------
The following are developer notes on how to build Eternity on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [OSX Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)

License
---------------------
Distributed under the [MIT/X11 software license](http://www.opensource.org/licenses/mit-license.php).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
