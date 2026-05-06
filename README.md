# DreamLink 🌀

**DreamLink** is a lightweight, lock-step serial communication library for the Sega Dreamcast, built on top of KallistiOS (KOS). It enables two consoles to communicate seamlessly via the standard Dreamcast Serial/Link Cable, providing robust frame synchronization for multiplayer games and data exchange.

## Features
- ⏱ **Lock-Step Frame Synchronization**: Guarantees that both Dreamcasts execute game logic on the exact same frame by blocking until a synchronized payload is exchanged.
- 🎒 **Piggybacked Message Queue**: Need to send game state events (like item pickups or spawns) without breaking frame sync? Queue arbitrary messages that automatically hitch a ride on the next frame's sync packet.
- 🛡 **Data Integrity**: Every packet is validated with a CRC-16-CCITT checksum to ensure data isn't corrupted across the serial line.
- 🤝 **Graceful Handshaking & Timeouts**: Built-in ping/pong connection detection and timeout fallbacks prevent the SH4 CPU from permanently hanging if the cable is suddenly unplugged.

## Usage Example

Include the library in your KOS project and use the lock-step engine within your main game loop:

```c
#include <kos.h>
#include <dreamlink/dreamlink.h>

int main(int argc, char **argv) {
    // 1. Initialize SCIF at a high baud rate (e.g., 500 kbps)
    dc_link_init(500000);

    // 2. Wait up to 5 seconds for another console to connect
    printf("Waiting for player 2...\n");
    if (!dc_link_handshake(5000)) {
        printf("Connection failed or timed out.\n");
        return -1;
    }

    printf("DreamLink Connected!\n");

    uint32_t frame_count = 0;
    while(1) {
        // Collect local controller inputs
        uint8_t local_input[32]; // Replace with real input fetching
        
        // Buffers for receiving the other console's input
        uint8_t remote_input[32];
        uint16_t remote_len = 0;
        
        // 3. Synchronize this frame with the other console (Wait max 33ms)
        int result = dc_link_sync_frame(frame_count, local_input, sizeof(local_input), 
                                        remote_input, &remote_len, 33);
                                        
        if(result == 0) {
            // Success! Both consoles now have identical inputs for this frame
            // Step the game logic synchronously here
        } else {
            // Handle timeout / connection loss
            printf("Desync or connection lost!\n");
            break;
        }
        
        frame_count++;
        vid_waitvbl(); // Wait for VBlank
    }

    return 0;
}
```

## Sending Game Events

DreamLink makes it easy to send async-style messages perfectly synchronized with the hardware frame tick:

```c
#define EVENT_SPAWN_ENEMY 0x01

// Just enqueue it during your normal game logic
int enemy_id = 42;
dc_link_enqueue_message(EVENT_SPAWN_ENEMY, &enemy_id, sizeof(enemy_id));

// During dc_link_sync_frame(), DreamLink will automatically pack this data
// and trigger callbacks on the receiving console.
```

## Structure
- `include/dreamlink/dreamlink.h`: The public API and protocol structures.
- `src/dc_link.c`: The core implementation (Ring buffer, CRC, Packet parsing, Sync loop).

## Building
This library uses the standard KallistiOS `Makefile.prefab`. 

Before building, ensure you initialize the required submodules:

```bash
git submodule update --init --recursive
```

Then you can build the project:

```bash
make
```