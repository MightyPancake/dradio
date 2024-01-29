# dradio - A prototype for a simple network TCP-based radio

## About

dradio is a quick project made as an end-of-semester project on classes at Poznań University.
The goal of the project was to make a simple-to-use radio that uses the network to broadcast music files.

## Getting started

This project depends on [raylib](https://www.raylib.com/) and the makefile uses gcc, so be sure you have both installed and working on your machine.

To run a server/client simply follow these few steps:
1. Clone or download this repository
2. run either 'make server' or 'make client'. If you're on Nix you can also use 'make nixboth' to compile both client and server. The makefile also includes additional commands.
3. Run the generated program.

## Under the hood

The communication between the server and all clients is made using TCP. Each client gets 1 socket which is later used to communicate in both ways.
Data is enclosed in packets. Each packet is sent by first sending a message kind bundled with the size of incoming data.
The message kind is used to communicate what kind of data is sent in the payload and the size is just the size of the incoming payload.
To avoid clogging the buffer each packet is then chopped down into smaller sub-packets which are then sent separately. The server requires confirmation from clients that the data got delivered before sending more data. This way buffer never gets clogged up.

On the client side, the data is just put into a temporary buffer that consecutively gets filled with data until the amount of bytes received is equal to the expected packet size.
