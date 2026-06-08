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

under wireshark: 11111111 == 0xff
this means the peer has 8 pieces that 


```C
    #include <stdio.h>
    #include <stdint.h>

    int main() {
        uint8_t pieces = 0b11111111; // 255 in decimal, 0xFF in hex
        
        // %X outputs uppercase hexadecimal 
        printf("Hex: 0x%X\n", pieces); // Outputs: 0xFF
        return 0;
    }

```


## Bittorent Protocol


Typical bittorent protocol message structure: [4 bytes: length][1 byte: type][N bytes: payload]


Every message starts with a 4 byte big-endian integer saying how many bytes follow.


A length of 0 is a special case — it means keep-alive, which has no type byte and no payload


A Keep-alive messages are is just a heartbeat so the connection doesn't time out.  


## TCP

TCP packet with the flags RST, ACK set means:

RST (Reset): The sender is immediately aborting or rejecting a TCP connection.
ACK (Acknowledgment): The sender is also acknowledging receipt of data up to a certain sequence number.

In practice, a TCP [RST, ACK] usually indicates that the receiving host (or sometimes a firewall/load balancer) is saying:

    "I received your packet, but this connection is not valid, so I'm resetting it."


Common situations include:

1. Connecting to a closed port

        Your machine sends a SYN to a port where no service is listening.

        The target responds with RST, ACK instead of SYN, ACK.

        This is how TCP indicates "port closed."

2. Application closed the connection abruptly

        A server process crashes or intentionally terminates a connection.

        The operating system sends RST, ACK to tear down the session immediately.

3. Packet arrives for a nonexistent connection

        The host receives a packet referencing a TCP session it doesn't know about.

        It responds with RST, ACK to indicate the connection state doesn't exist.

4. Firewall or security device intervention

        Some firewalls actively reject connections by sending forged RST, ACK packets.

        This is often seen during filtering or intrusion prevention.

Difference from FIN, ACK:

    FIN, ACK = graceful shutdown:

    "I'm done sending data; let's close the connection properly."

RST, ACK = immediate termination:

    "This connection is invalid or must be aborted right now."


## Reason you might get [RST, ACK] or [FIN, ACK] when connecting to peers

1. The peer isn't accepting new TCP peers right now

        Many BitTorrent clients limit the number of concurrent connections.

        If a peer is at its connection limit, it may:

        Accept the TCP handshake and then immediately send FIN, ACK (graceful close).

        Accept and then send RST, ACK (abortive close).


        You're connecting to a stale peer

2. Trackers often return peers that:

        Have gone offline.

        Changed IP addresses.

        Restarted their client.

        Are behind NATs that have expired mappings.


3. The peer is a uTP-only peer

It's less common than people assume.

In tracker responses, peers are generally advertised as:

    IP + TCP port

If you connect successfully to that TCP port, the peer is at least advertising TCP reachability.

A peer that prefers uTP often still supports TCP as a fallback.

However, some clients:

    Prioritize uTP heavily.

    Disable incoming TCP.

    Advertise stale TCP endpoints.

In those cases, you may see immediate closes.


5. Firewall or NAT behavior

Sometimes you're not actually talking to the BitTorrent client.

Examples:

    Home router sends RST.

    ISP middlebox sends RST.

    Host firewall rejects connection.

This often appears as: "SYN RST, ACK"
