## Plan: Dreamcast Serial Link Library

**Goal:** Create a C library over KOS (`/opt/toolchains/dc/kos/`) to enable two Dreamcast consoles to communicate via the serial cable. The library will support lock-step frame synchronization for controller inputs, with the flexibility to send larger payloads for game state synchronization later.

**Steps**
1. **Define Protocol & Frame Structure:**
   - Create a transport packet structure: `[MAGIC (2b)] [FRAME_ID (4b)] [CMD (1b)] [PAYLOAD_LEN (2b)] [PAYLOAD...] [CRC16 (2b)]`.
   - The `FRAME_ID` is critical for lock-step synchronization.
2. **Hardware Initialization (SCIF):**
   - Initialize the Serial Communication Interface (`<dc/scif.h>`).
   - Implement `link_init(baud_rate)`. We will target a high baud rate (e.g. 500kbps or 1.56Mbps) to minimize frame transfer times.
3. **Transmission Layer (Sending & Receiving):**
   - Implement `link_send_packet(cmd, frame_id, data, len)` using `scif_write_buffer()`.
   - Implement `link_read_poll()` using non-blocking `scif_read()` to pull bytes into an internal ring buffer.
   - Add parsing logic to extract complete, CRC-validated packets from the ring buffer.
4. **Lock-Step Synchronization Engine:**
   - Implement `link_sync_frame(local_frame_id, local_inputs, out_remote_inputs)`.
   - This function transmits the local console's data for the current frame, then loops (or yields) while calling `link_read_poll()` until it receives the remote console's matching `FRAME_ID`.
5. **Message Queue for Game State (Advanced Play):**
   - Add a secondary queue for arbitrary game messages (e.g., handing over objects, split-screen events).
   - Implement `link_enqueue_message(type, data, len)`. These messages are appended to the next frame's payload to avoid breaking lock-step timing.
6. **Handshake & Disconnect Handling:**
   - Implement a connection handshake to establish the initial `FRAME_ID` zero and confirm link cable presence.
   - Handle timeouts (e.g., if the cable is yanked, gracefully pause the game instead of locking up forever).

**Relevant KOS Concepts**
- Native functions to use: `scif_read()`, `scif_write_buffer()`, `scif_flush()` from `<dc/scif.h>`.
- KOS timer primitives (`timer_ms_get()`) for implementing connection/sync timeouts.

**Verification**
1. **Multitap Demo:** Console A sends 4 controller states (approx 128 bytes) to Console B every frame (`VBlank`). Console B echoes its controllers back.
2. **Desync Test:** Artificially delay one console (e.g. `thd_sleep(50)`) and verify the other console waits perfectly in lock-step.
3. **Throughput Test:** Push a larger payload (e.g. 1KB game state sync) alongside the controller data and verify the 60FPS lock-step doesn't drop.

**Decisions**
- Data loss shouldn't be a major issue over a short serial cable, but if corruption occurs (bad CRC), the library will request a retransmission for that specific `FRAME_ID` before advancing, maintaining the lock-step.
- Messages are piggybacked onto the mandatory frame-sync packets to minimize serial overhead and ensure they arrive exactly on the frame they are needed.

**Further Considerations**
1. Do you want the library to use polling (checking the serial port manually in your game loop) or an interrupt-driven approach using KOS threads or DMA (`<dc/sci.h>`)? Polling is safer for raw performance, but DMA offloads work from the SH4 CPU.