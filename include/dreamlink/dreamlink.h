#ifndef __DREAMLINK_H
#define __DREAMLINK_H

#include <stdint.h>
#include <stdbool.h>

#define DREAMLINK_MAGIC 0xDC1B

// Protocol Commands
typedef enum {
    LINK_CMD_PING = 0x01,
    LINK_CMD_SYNC_FRAME = 0x02,
    LINK_CMD_RETRANSMIT = 0x03,
    LINK_CMD_DISCONNECT = 0x04
} dc_link_cmd_t;

// State of the link
typedef enum {
    LINK_STATE_DISCONNECTED,
    LINK_STATE_HANDSHAKING,
    LINK_STATE_CONNECTED,
    LINK_STATE_ERROR
} dc_link_state_t;

// Frame header structure (packed to ensure exact byte layout)
#pragma pack(push, 1)
typedef struct {
    uint16_t magic;         // DREAMLINK_MAGIC
    uint32_t frame_id;      // Current lock-step frame ID
    uint8_t  cmd;           // dc_link_cmd_t
    uint16_t payload_len;   // Length of the attached payload
} dc_link_header_t;
#pragma pack(pop)

// API Functions

/**
 * @brief Initialize the link communication
 * @param baud_rate Usually 500000 or 1560000 for high speed on Dreamcast SCIF
 * @return 0 on success, < 0 on error
 */
int dc_link_init(uint32_t baud_rate);

/**
 * @brief Performs the initial handshake to synchronize both consoles
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return true if successfully connected and synced to FRAME_ID 0
 */
bool dc_link_handshake(uint32_t timeout_ms);

/**
 * @brief Get the current link state
 */
dc_link_state_t dc_link_get_state(void);

/**
 * @brief Sends a sync packet with attached payload for the given frame
 * @param frame_id The current local frame tick
 * @param payload Pointer to the data to send
 * @param len Length of the payload (max 2048 bytes usually recommended)
 * @return 0 on success
 */
int dc_link_send_sync_frame(uint32_t frame_id, const void *payload, uint16_t len);

/**
 * @brief Transmits local frame data and blocks until the remote console's frame data is received
 * @param frame_id The current lock-step frame ID
 * @param local_payload Pointer to the local data to send
 * @param local_len Length of the local data
 * @param remote_payload_out Buffer to store the received remote data
 * @param remote_len_out Pointer to store the length of the received remote data
 * @param timeout_ms Maximum time to wait in milliseconds (0 = infinite)
 * @return 0 on success, < 0 on timeout or error
 */
int dc_link_sync_frame(uint32_t frame_id, const void *local_payload, uint16_t local_len, void *remote_payload_out, uint16_t *remote_len_out, uint32_t timeout_ms);


/**
 * @brief Type definition for a callback invoked when a piggybacked message is received
 * @param msg_type An application-defined message type identifier
 * @param data The message data
 * @param len Length of the message data
 */
typedef void (*dc_link_msg_cb_t)(uint8_t msg_type, const void *data, uint16_t len);

/**
 * @brief Set the callback for processing received piggybacked messages
 */
void dc_link_set_message_callback(dc_link_msg_cb_t cb);

/**
 * @brief Enqueues an arbitrary message to be piggybacked onto the NEXT sync frame
 * @param msg_type Application-defined message type identifier
 * @param data Message payload
 * @param len Length of the message payload
 * @return 0 on success, -1 if queue is full
 */
int dc_link_enqueue_message(uint8_t msg_type, const void *data, uint16_t len);

/**
 * @brief Type definition for a callback invoked when a valid frame is received
 */
typedef void (*dc_link_frame_cb_t)(uint32_t frame_id, const uint8_t *payload, uint16_t len);

/**
 * @brief Set the callback for when a valid sync frame arrives
 */
void dc_link_set_frame_callback(dc_link_frame_cb_t cb);

/**
 * @brief Polls the serial port, filling the internal ring buffer and processing any complete packets
 */
void dc_link_poll(void);

#endif // __DREAMLINK_H
