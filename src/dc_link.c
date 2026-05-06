#include <dreamlink/dreamlink.h>
#include <kos.h>
#include <dc/scif.h>
#include <string.h>
#include <stdbool.h>

#define LINK_RX_BUFF_SIZE 4096

// Internal state
static dc_link_state_t current_state = LINK_STATE_DISCONNECTED;
static uint32_t current_baud = 0;
static dc_link_frame_cb_t frame_cb = NULL;

// Ring buffer state
static uint8_t rx_buffer[LINK_RX_BUFF_SIZE];
static uint16_t rx_head = 0; // Where we write new bytes
static uint16_t rx_tail = 0; // Where we read completed packets
static uint16_t rx_count = 0;

// Internal sync state
static volatile bool sync_frame_received = false;
static uint32_t sync_expected_frame_id = 0;
static uint8_t sync_remote_buffer[2048];
static uint16_t sync_remote_len = 0;

static volatile bool handshake_received = false;

#define MAX_PIGGYBACK_QUEUE 1024

// Piggyback Queue state
static uint8_t piggyback_queue[MAX_PIGGYBACK_QUEUE];
static uint16_t piggyback_len = 0;
static dc_link_msg_cb_t msg_cb = NULL;

#pragma pack(push, 1)
typedef struct {
    uint8_t msg_type;
    uint16_t len;
} dc_link_piggy_hdr_t;
#pragma pack(pop)

// Standard CRC-16-CCITT calculation
static uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint16_t length) {
    uint8_t x;
    while (length--) {
        x = crc >> 8 ^ *data++;
        x ^= x >> 4;
        crc = (crc << 8) ^ ((uint16_t)(x << 12)) ^ ((uint16_t)(x << 5)) ^ ((uint16_t)x);
    }
    return crc;
}

// Write to ring buffer
static void rb_push(uint8_t data) {
    if (rx_count < LINK_RX_BUFF_SIZE) {
        rx_buffer[rx_head] = data;
        rx_head = (rx_head + 1) % LINK_RX_BUFF_SIZE;
        rx_count++;
    }
}

// Read from ring buffer, or advance past bytes
static void rb_pop(uint16_t amount) {
    if (amount > rx_count) amount = rx_count;
    rx_tail = (rx_tail + amount) % LINK_RX_BUFF_SIZE;
    rx_count -= amount;
}

// Peek bytes linearly from the circular buffer (returns length copied)
static uint16_t rb_peek(uint8_t *out, uint16_t offset, uint16_t length) {
    if (offset + length > rx_count) {
        length = rx_count - offset;
    }
    for (uint16_t i = 0; i < length; i++) {
        out[i] = rx_buffer[(rx_tail + offset + i) % LINK_RX_BUFF_SIZE];
    }
    return length;
}

int dc_link_init(uint32_t baud_rate) {
    // SCIF initialization through KOS.
    // KOS handles basic SCIF init during kernel startup, but 
    // we may need to re-init or set the baud rate.
    
    // For now we set the state
    current_baud = baud_rate;
    
    // Set up hardware through KOS scif primitives
    scif_init(); 
    scif_set_parameters(baud_rate, 1); // 1 = non-blocking FIFO mode usually, depending on KOS specifics

    // Flush any garbage currently in the FIFO
    scif_flush();

    current_state = LINK_STATE_HANDSHAKING;
    return 0;
}

dc_link_state_t dc_link_get_state(void) {
    return current_state;
}

static void send_raw_packet(dc_link_cmd_t cmd, uint32_t frame_id, const void *payload1, uint16_t len1, const void *payload2, uint16_t len2) {
    dc_link_header_t hdr;
    hdr.magic = DREAMLINK_MAGIC;
    hdr.frame_id = frame_id;
    hdr.cmd = cmd;
    hdr.payload_len = len1 + len2;

    // Calculate CRC over header and payload
    uint16_t crc = 0xFFFF; // Standard CCITT init
    crc = crc16_update(crc, (uint8_t*)&hdr, sizeof(hdr));
    if (len1 > 0 && payload1 != NULL) {
        crc = crc16_update(crc, (uint8_t*)payload1, len1);
    }
    if (len2 > 0 && payload2 != NULL) {
        crc = crc16_update(crc, (uint8_t*)payload2, len2);
    }

    // Transmit via KOS scif buffer
    // xlat=0 means pure binary, no newline translation
    scif_write_buffer((const uint8_t*)&hdr, sizeof(hdr), 0);
    if (len1 > 0 && payload1 != NULL) {
        scif_write_buffer((const uint8_t*)payload1, len1, 0);
    }
    if (len2 > 0 && payload2 != NULL) {
        scif_write_buffer((const uint8_t*)payload2, len2, 0);
    }
    scif_write_buffer((const uint8_t*)&crc, sizeof(crc), 0);
}

int dc_link_send_sync_frame(uint32_t frame_id, const void *payload, uint16_t len) {
    // Structure: [local_len (2b)] [local_payload] [piggybacked_messages...]
    uint16_t sync_len = len;
    
    // We build a temporary buffer for the base payload which includes its length prepended
    uint8_t temp_buf[2048];
    if (sizeof(sync_len) + len > sizeof(temp_buf)) return -1;
    
    memcpy(temp_buf, &sync_len, sizeof(sync_len));
    if (len > 0) {
        memcpy(temp_buf + sizeof(sync_len), payload, len);
    }
    
    send_raw_packet(LINK_CMD_SYNC_FRAME, frame_id, temp_buf, sizeof(sync_len) + len, piggyback_queue, piggyback_len);
    
    // Clear piggyback queue after sending
    piggyback_len = 0;
    return 0;
}

bool dc_link_handshake(uint32_t timeout_ms) {
    current_state = LINK_STATE_HANDSHAKING;
    handshake_received = false;
    
    // Broadcast a PING
    uint64_t start_time = timer_ms_gettime64();
    send_raw_packet(LINK_CMD_PING, 0, NULL, 0, NULL, 0);
    
    // Poll until we get a response or timeout
    while (!handshake_received) {
        dc_link_poll();
        
        if (timeout_ms && (timer_ms_gettime64() - start_time) >= timeout_ms) {
            current_state = LINK_STATE_ERROR;
            return false; // Timeout
        }
        
        thd_sleep(1); // Yield thread slightly to not hard lock the CPU if used this way
    }
    
    current_state = LINK_STATE_CONNECTED;
    return true;
}

int dc_link_sync_frame(uint32_t frame_id, const void *local_payload, uint16_t local_len, void *remote_payload_out, uint16_t *remote_len_out, uint32_t timeout_ms) {
    if (current_state != LINK_STATE_CONNECTED) {
        return -1;
    }

    sync_expected_frame_id = frame_id;
    sync_frame_received = false;

    // Send our local frame
    dc_link_send_sync_frame(frame_id, local_payload, local_len);

    uint64_t start_time = timer_ms_gettime64();

    // Lock-step block until remote arrives
    while (!sync_frame_received) {
        dc_link_poll();

        if (timeout_ms && (timer_ms_gettime64() - start_time) >= timeout_ms) {
            return -2; // Timeout occurred, missing a frame
        }
    }

    // Remote frame successfully arrived
    if (remote_payload_out && remote_len_out) {
        memcpy(remote_payload_out, sync_remote_buffer, sync_remote_len);
        *remote_len_out = sync_remote_len;
    }

    return 0;
}

void dc_link_set_frame_callback(dc_link_frame_cb_t cb) {
    frame_cb = cb;
}

void dc_link_set_message_callback(dc_link_msg_cb_t cb) {
    msg_cb = cb;
}

int dc_link_enqueue_message(uint8_t msg_type, const void *data, uint16_t len) {
    if (piggyback_len + sizeof(dc_link_piggy_hdr_t) + len > MAX_PIGGYBACK_QUEUE) {
        return -1; // Queue full
    }
    
    dc_link_piggy_hdr_t *hdr = (dc_link_piggy_hdr_t*)&piggyback_queue[piggyback_len];
    hdr->msg_type = msg_type;
    hdr->len = len;
    piggyback_len += sizeof(dc_link_piggy_hdr_t);
    
    if (len > 0 && data != NULL) {
        memcpy(&piggyback_queue[piggyback_len], data, len);
        piggyback_len += len;
    }
    return 0;
}

static void try_parse_packet() {
    uint8_t magic_buf[2];
    
    while (rx_count >= sizeof(dc_link_header_t) + 2) { // Minimum packet size (header + crc16)
        // Find magic boundary
        rb_peek(magic_buf, 0, 2);
        uint16_t magic = magic_buf[0] | (magic_buf[1] << 8); 
        
        if (magic != DREAMLINK_MAGIC) {
            rb_pop(1); // Advance one byte and search again
            continue;
        }

        // We have magic. Extract header
        dc_link_header_t hdr;
        rb_peek((uint8_t*)&hdr, 0, sizeof(hdr));
        
        // Prevent huge payload attacks
        if (hdr.payload_len > LINK_RX_BUFF_SIZE - sizeof(hdr) - 2) {
            rb_pop(1); // Garbage payload size, pop magic and continue
            continue;
        }

        uint16_t full_packet_size = sizeof(hdr) + hdr.payload_len + 2; // header + payload + CRC
        
        // Wait for full packet to arrive in buffer
        if (rx_count < full_packet_size) {
            break; 
        }

        // Pull the entire packet payload out to validate CRC
        uint8_t *packet_raw = (uint8_t*)malloc(full_packet_size);
        rb_peek(packet_raw, 0, full_packet_size);

        // Extract received CRC (last 2 bytes)
        uint16_t received_crc = packet_raw[full_packet_size - 2] | (packet_raw[full_packet_size - 1] << 8);
        
        // Calculate our own CRC
        uint16_t calc_crc = 0xFFFF;
        calc_crc = crc16_update(calc_crc, packet_raw, full_packet_size - 2);

        if (calc_crc == received_crc) {
            // Valid Packet
            rb_pop(full_packet_size); // Consume it completely
            
            if (hdr.cmd == LINK_CMD_SYNC_FRAME) {
                if (hdr.payload_len >= 2) {
                    const uint8_t *payload_ptr = packet_raw + sizeof(hdr);
                    uint16_t sync_len;
                    memcpy(&sync_len, payload_ptr, sizeof(sync_len));
                    payload_ptr += sizeof(sync_len);
                    
                    if (hdr.frame_id == sync_expected_frame_id) {
                        if (sync_len <= sizeof(sync_remote_buffer)) {
                            memcpy(sync_remote_buffer, payload_ptr, sync_len);
                            sync_remote_len = sync_len;
                        }
                        sync_frame_received = true;
                    }
                    
                    if (frame_cb) {
                        frame_cb(hdr.frame_id, payload_ptr, sync_len);
                    }
                    
                    // Parse any piggybacked messages
                    uint16_t parsed_len = sizeof(sync_len) + sync_len;
                    while (parsed_len + sizeof(dc_link_piggy_hdr_t) <= hdr.payload_len) {
                        dc_link_piggy_hdr_t *p_hdr = (dc_link_piggy_hdr_t*)(packet_raw + sizeof(hdr) + parsed_len);
                        parsed_len += sizeof(dc_link_piggy_hdr_t);
                        
                        if (parsed_len + p_hdr->len <= hdr.payload_len) {
                            if (msg_cb) {
                                msg_cb(p_hdr->msg_type, packet_raw + sizeof(hdr) + parsed_len, p_hdr->len);
                            }
                            parsed_len += p_hdr->len;
                        } else {
                            break; // Malformed piggyback data
                        }
                    }
                }
            } else if (hdr.cmd == LINK_CMD_PING) {
                handshake_received = true;
            }
            
            // Note: For other cmds, handle them here (RETRANSMIT, DISCONNECT, etc.)
        } else {
            // Bad checksum
            rb_pop(1); // corrupted packet, pop magic and resync
        }
        
        free(packet_raw);
    }
}

void dc_link_poll(void) {
    // Read actively pending bytes from serial port without blocking forever
    int c;
    while ((c = scif_read()) != -1) {
        rb_push((uint8_t)c);
    }

    // Try to extract packets from what we've amassed
    try_parse_packet();
}
