// def.h - Common protocol definitions shared between NM, SS, and Client
// Standard message types and error codes for network communication

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

// Message types for NM-SS-Client protocol
// Used for structured communication (though current impl uses text commands mostly)
typedef enum {
    MSG_ERR = 0,           // Error response
    MSG_PING,              // Heartbeat check
    MSG_ACK,               // Acknowledgment
    MSG_SS_INIT,           // SS registration
    MSG_REQ_LIST,          // List files request
    MSG_RES_LIST,          // List files response
    MSG_REQ_CREATE,        // Create file request
    MSG_REQ_DELETE,        // Delete file request
    MSG_REQ_INFO,          // File info request
    MSG_REQ_READ_LOC,      // Request SS location for reading
    MSG_REQ_WRITE_LOC,     // Request SS location for writing
    MSG_RES_LOC,           // SS location response
    MSG_REQ_READ,          // Read file content
    MSG_REQ_WRITE,         // Write file content
    MSG_DATA_CHUNK,        // Data transfer chunk
    MSG_STREAM_CHUNK,      // Streaming data chunk
    MSG_END_DATA           // End of data transfer
} MessageType;

// HTTP-style error codes for consistent error handling
typedef enum {
    ERR_NONE = 0,
    ERR_FILE_NOT_FOUND = 404,      // File doesn't exist
    ERR_ACCESS_DENIED = 403,       // ACL permission denied
    ERR_SENTENCE_LOCKED = 423,     // Another user editing this sentence
    ERR_SS_OFFLINE = 503,          // Storage server unavailable
    ERR_TIMEOUT = 408,             // Operation timed out
    ERR_UNKNOWN = 999              // Generic error
} ErrorCode;

// Standard packet header (for binary protocol, not used in text-based impl)
typedef struct {
    uint32_t msg_type;      // MessageType enum value
    uint32_t error_code;    // ErrorCode enum value
    uint64_t payload_len;   // Length of following payload
} PacketHeader;

#endif