#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdbool.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

#include "common.h"
#include "tftp.h"

#define BLOCK_SIZE (512)
#define MAX_DATAGRAM_SIZE (516) // As defined in Tftp RFC(RFC 1350).
#define BINARY_MODE ("octet")
#define TIMEOUT_IN_SECONDS (3)
#define SEND_DATA_ATTEMPTS (5)

#define RANDOM_PORT (rand() % 10000 + 10000) // 10 thousands ports should be enough.
#define EQUAL_STRINGS (0)
#define MAX_ERROR_MESSAGE_SIZE (MAX_DATAGRAM_SIZE - 4)

typedef enum opcodes_e{
    RRQ = 1, // Read request.
    WRQ, // Write request.
    DATA,
    ACK,
    ERROR
} opcodes_t;

typedef enum tftp_error_codes_e{
    UNDEFINED = 0,
    FILE_NOT_FOUND,
    ACCESS_VIOLATION,
    ALLOCATION_FAILED,
    ILLEGAL_TFTP_OPERATION,
    UKNOWN_TRANSFER_ID,
    FILE_ALREADY_EXISTS,
    NO_SUCH_USER,
} tftp_error_codes_t;

typedef struct request_packet_s{
    uint16_t opcode; // Only RRQ or WRQ.
    char filename_and_mode[MAX_DATAGRAM_SIZE - 2];
} request_packet_t;

typedef struct data_packet_s{
    uint16_t opcode; // Only DATA.
    uint16_t block_number;
    int8_t data[BLOCK_SIZE];
} data_packet_t;

typedef struct ack_packet_s{
    uint16_t opcode; // Only ACK.
    uint16_t block_number;
} ack_packet_t;

typedef struct error_packet_s{
    uint16_t opcode; // Only ERROR.
    uint16_t error_code;
    char error_message[MAX_ERROR_MESSAGE_SIZE];
} error_packet_t;

typedef union tftp_packet_u{
    request_packet_t request;
    data_packet_t data;
    ack_packet_t ack;
    error_packet_t error;
} tftp_packet_t;

typedef struct tftp_connection_s{
    int32_t my_tid; // TID - Transfer indefitier, in TFTP it's the UDP port.
    int32_t other_tid;
    uint32_t last_ack;
    tftp_packet_t *last_packet;
} tftp_connection_t;

/**
 * @brief Send tftp error packet to the client.
 * 
 * @param connection_socket [in] Connection socket to the client (should have the right source port).
 * @param client_address [in] Address of the client.
 * @param error_code [in] Which error has happened.
 * @param message [in] Message that elaborate on the error happened, max size is MAX_ERROR_MESSAGE_SIZE.
 */
static void send_error(
    int32_t connection_socket,
    const struct sockaddr_in *client_address,
    tftp_error_codes_t error_code,
    const char *message
)
{
    error_packet_t packet = {0}; 

    assert(NULL != client_address && NULL != message);

    packet.opcode = htons(ERROR);
    packet.error_code = htons(error_code);
    strncpy(packet.error_message, message, MAX_ERROR_MESSAGE_SIZE);

    sendto(
        connection_socket,
        &packet,
        sizeof(packet),
        0, 
        (struct sockaddr *)client_address,
        sizeof(*client_address)
    );

}

/**
 * @brief Send tftp packet to the client, then waits for response packet. Tries for a few attempts.
 * 
 * @param connection_socket [in] Connection socket to the client (should have the right source port).
 * @param client_address [in] Address of the client.
 * @param send_packet [in] The tftp packet to be sent.
 * @param packet_size [in] Size of packet to be sent.
 * @param is_valid_response_ptr [out] True if recieved a response from the client, false otherwise.
 * @param recv_packet [out] The response packet from the client.
 * @return return_code_t 
 */
static return_code_t send_packet(
    int32_t connection_socket,
    const struct sockaddr_in *client_address,
    const tftp_packet_t *send_packet,
    uint32_t packet_size,
    bool *is_valid_response_ptr,
    tftp_packet_t *recv_packet
)
{
    return_code_t result = RC_UNINITIALIZED;
    ssize_t bytes_read = -1;
    struct sockaddr_in incoming_address = {0};
    socklen_t incoming_address_length = 0;
    bool recived_valid_response = false;

    assert(NULL != client_address && NULL != send_packet);
    assert(NULL != is_valid_response_ptr && NULL != recv_packet);

    for(uint8_t send_attempt = 0; send_attempt < SEND_DATA_ATTEMPTS; ++send_attempt){
        sendto(
            connection_socket,
            send_packet,
            packet_size,
            0, 
            (struct sockaddr *)client_address,
            sizeof(*client_address)
        );
        incoming_address_length = sizeof(incoming_address);
        bytes_read = recvfrom(
            connection_socket,
            recv_packet,
            sizeof(recv_packet),
            0,
            (struct sockaddr *)&incoming_address,
            &incoming_address_length
        );
        if (-1 == bytes_read){
            if (EAGAIN == errno){
                // Timeout on recv, try again.
                continue;
            }
            handle_perror("Recv failed", RC_TFTP__RUN_SERVER__SOCKET_RECV_FAILED);
        }

        if ((client_address->sin_addr.s_addr != incoming_address.sin_addr.s_addr) || 
            (client_address->sin_port != incoming_address.sin_port)){
                send_error(connection_socket, &incoming_address, UKNOWN_TRANSFER_ID, "Wrong dest.");
                continue;
            }

        recived_valid_response = true;
        // Received a valid response, no need to continue.
        break;
    }

    result = RC_SUCCESS;
l_cleanup:
    *is_valid_response_ptr = recived_valid_response;
    return result;
}

/**
 * @brief Initializes UDP socket that binds to a given port.
 * 
 * @param server_port [in] Port for new socket.
 * @param server_socket_ptr [out] The new UDP socket.
 * @return return_code_t 
 */
static return_code_t init_udp_server_socket(int32_t server_port, int32_t *server_socket_ptr)
{
    return_code_t result = RC_UNINITIALIZED;
    int32_t server_socket = -1;
    struct sockaddr_in server_address = {0};
    struct in_addr server_inteface = {0};
    int bind_result = -1;

    assert(NULL != server_socket_ptr);

    server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == server_socket){
        handle_perror("Socket failed", RC_TFTP__INIT_UDP_SERVER__SOCKET_INIT_FAILED);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(server_port);
    server_inteface.s_addr = INADDR_ANY;
    server_address.sin_addr = server_inteface;
    bind_result = bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address));
    if (-1 == bind_result){
        handle_perror("Bind failed", RC_TFTP__INIT_UDP_SERVER__SOCKET_BIND_FAILED);
    }

    *server_socket_ptr = server_socket;

    result = RC_SUCCESS;
l_cleanup:
    return result;

}

/**
 * @brief Initializes socket for a tftp connection. Socket will have a random port, 
 *        and will have a timeout for receiving data.
 * 
 * @param connection_socket_ptr [out] Will point to the new socket.
 * @return return_code_t 
 */
static return_code_t init_connection_socket(int32_t *connection_socket_ptr)
{
    return_code_t result = RC_UNINITIALIZED;
    int32_t connection_socket = -1;
    int32_t temp_result = -1;
    struct timeval timeout = {0};

    assert(NULL != connection_socket_ptr);

    result = init_udp_server_socket(RANDOM_PORT, &connection_socket);
    if (RC_SUCCESS != result){
        // Try with another random port.
        result = init_udp_server_socket(RANDOM_PORT, &connection_socket);
        if (RC_SUCCESS != result){
            goto l_cleanup;
        }
    }

    timeout.tv_sec = TIMEOUT_IN_SECONDS;
    timeout.tv_usec = 0;
    temp_result = setsockopt(connection_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (-1 == temp_result){
        handle_perror("setsockopt failed", RC_TFTP__HANDLE_REQUEST__SETSOCKOPT_FAILED);
    }

    *connection_socket_ptr = connection_socket;

    result = RC_SUCCESS;
l_cleanup:
    if (RC_SUCCESS != result && -1 != connection_socket){
        close(connection_socket);
    }
    return result;
}

/**
 * @brief Handles the operation of reading file after read request. 
 *        Opens requested file, and sends it's data to the client.
 * 
 * @param request_packet [in] The read file request packet.
 * @param client_address [in] Address of the client.
 * @param logger [in] Logger of the program.
 * @return return_code_t 
 */
static return_code_t handle_read_request(
    const request_packet_t *request_packet,
    const struct sockaddr_in *client_address,
    const logger__log_t *logger
)
{
    return_code_t result = RC_UNINITIALIZED;
    const char *filename = NULL;
    const char *mode = NULL;
    FILE *file = NULL;
    char log_message[MAX_LOG_MESSAGE_SIZE] = {0};
    size_t bytes_read = 0;
    int32_t connection_socket = -1;
    data_packet_t data_packet = {0};
    uint32_t packet_size = 0;
    uint16_t block_number = 0;
    ack_packet_t ack_packet = {0};
    bool received_reponse = false;

    assert(NULL != request_packet && NULL != client_address);

    result = init_connection_socket(&connection_socket);
    if (RC_SUCCESS != result){
        goto l_cleanup;
    }

    filename = request_packet->filename_and_mode;
    mode = filename + strlen(filename) + 1;
    snprintf(log_message, MAX_LOG_MESSAGE_SIZE, "Request to read %s in mode %s", filename, mode);
    logger__log(logger, LOG_DEBUG, log_message);

    if (EQUAL_STRINGS != strncasecmp(mode, BINARY_MODE, MAX_DATAGRAM_SIZE)){
        // We only support binary mode.
        send_error(connection_socket, client_address, ILLEGAL_TFTP_OPERATION, "Unsupported mode.");
        result = RC_SUCCESS;
        goto l_cleanup;
    }

    file = fopen(filename, "r");
    if (NULL == file){
        if (EACCES == errno){
            send_error(connection_socket, client_address, ACCESS_VIOLATION, strerror(errno));
        }
        else{
            send_error(connection_socket, client_address, UNDEFINED, strerror(errno));
        }
        result = RC_SUCCESS;
        goto l_cleanup;
    }

    snprintf(log_message, MAX_LOG_MESSAGE_SIZE, "Starting to read file: %s", filename);
    logger__log(logger, LOG_INFO, log_message);

    data_packet.opcode = htons(DATA);
    block_number = 1;
    while(!feof(file)){
        data_packet.block_number = htons(block_number);
        bytes_read = fread(data_packet.data, 1, BLOCK_SIZE, file);
        if (ferror(file)){
            send_error(connection_socket, client_address, UNDEFINED, strerror(errno));
            break;
        }

        snprintf(log_message, MAX_LOG_MESSAGE_SIZE, "Read %ld bytes, sending DATA with block %d.", bytes_read, block_number);
        logger__log(logger, LOG_DEBUG, log_message);

        packet_size = sizeof(data_packet) - (BLOCK_SIZE - bytes_read); 
        send_packet(
            connection_socket,
            client_address,
            (tftp_packet_t *)&data_packet,
            packet_size,
            &received_reponse,
            (tftp_packet_t *) &ack_packet
        );

        if(!received_reponse || ntohs(ack_packet.opcode) == ERROR){
            break;
        }
        if(ntohs(ack_packet.opcode) != ACK){
            send_error(connection_socket, client_address, ILLEGAL_TFTP_OPERATION, "Received invalid response.");
            break;
        } 
        if(ntohs(ack_packet.block_number) != block_number){
            // TODO: Handle better wrong ack number.
            send_error(connection_socket, client_address, UNDEFINED, "Received wrong ack.");
            break;
        }

        snprintf(log_message, MAX_LOG_MESSAGE_SIZE, "Received ack on block %d.", ntohs(ack_packet.block_number));
        logger__log(logger, LOG_DEBUG, log_message);

        block_number++;
    }

    result = RC_SUCCESS;
l_cleanup:
    if (-1 != connection_socket){
        close(connection_socket);
    }

    return result;
}

return_code_t tftp__init_server(int32_t server_port, logger__log_t *logger, int32_t *server_socket_ptr)
{
    return_code_t result = RC_UNINITIALIZED;
    char log_message[MAX_LOG_MESSAGE_SIZE] = {0};

    if (NULL == server_socket_ptr || NULL == logger || 0 == server_port){
        handle_error(RC_TFTP__INIT_SERVER__BAD_PARAMS);
    }

    result = init_udp_server_socket(server_port, server_socket_ptr);
    if (result != RC_SUCCESS){
        goto l_cleanup;
    }

    snprintf(log_message, MAX_LOG_MESSAGE_SIZE, "TFTP server started on port: %d", server_port);
    logger__log(logger, LOG_INFO, log_message);
    result = RC_SUCCESS;
l_cleanup:
    return result;
}

return_code_t tftp__destroy_server(int32_t *server_socket_ptr, logger__log_t *logger)
{
    return_code_t result = RC_UNINITIALIZED;
    if (NULL == server_socket_ptr || NULL == logger){
        handle_error(RC_TFTP__DESTROY_SERVER__BAD_PARAMS);
    }

    if (-1 == close(*server_socket_ptr)){
        handle_perror("Close failed", RC_TFTP__DESTROY_SERVER__CLOSE_SOCKET_FAILED);
    }

    *server_socket_ptr = -1;

    logger__log(logger, LOG_INFO, "Closed tftp server successfully.");

    result = RC_SUCCESS;
l_cleanup:
    return result;
}

return_code_t tftp__run_server(int32_t server_socket, logger__log_t *logger)
{
    return_code_t result = RC_UNINITIALIZED;
    request_packet_t request_packet = {0};
    ssize_t bytes_read = -1;
    struct sockaddr_in client_address = {0};
    socklen_t client_address_length = sizeof(client_address);
    char log_message[MAX_LOG_MESSAGE_SIZE] = {0};

    if (NULL == logger){
        handle_error(RC_TFTP__RUN_SERVER__BAD_PARAMS);
    }

    while (true){
        bytes_read = recvfrom(
            server_socket,
            &request_packet,
            sizeof(request_packet),
            0,
            (struct sockaddr *)&client_address,
            &client_address_length
        );
        if (-1 == bytes_read){
            handle_perror("Recv failed", RC_TFTP__RUN_SERVER__SOCKET_RECV_FAILED);
        }

        snprintf(
            log_message,
            MAX_LOG_MESSAGE_SIZE,
            "Received tftp request from: IP: %s, port: %d.",
            inet_ntoa(client_address.sin_addr),
            ntohs(client_address.sin_port)
        );
        logger__log(logger, LOG_DEBUG, log_message);
        
        switch(ntohs(request_packet.opcode)){
            case RRQ:
                handle_read_request(&request_packet, &client_address, logger);
                break;
            
            case WRQ:
                // TODO: Add support for writing files.
                // handle_write_connection();
                // break;
                continue;

            default:
                // Invalid request, ignore.
                continue;
        }
    }

    result = RC_SUCCESS;
l_cleanup:
    return result;
}