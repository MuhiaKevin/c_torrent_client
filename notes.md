# Torrent Client

A torrent client written in C just for practice


The bitfield message contains a compact bitmap indicating which pieces a peer has.
Example:

Piece 0 -> 1 = have it
Piece 1 -> 0 = don't have it
Piece 2 -> 1 = have it

Suppose a torrent has 10 pieces: 

Pieces: 0 1 2 3 4 5 6 7 8 9
and a peer has: 0, 2, 3, 6, 9


The bitfield would be: 1 0 1 1 0 0 1 0 0 1
Bits are packed from most significant bit (MSB) to least significant bit (LSB). 

For the first 8 pieces:
Piece: 0 1 2 3 4 5 6 7
Bits : 1 0 1 1 0 0 1 0
