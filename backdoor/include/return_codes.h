/**
 * @file return_codes.h
 * @author Yuval Krinsky (ykrinksy@gmail.com)
 * @brief Return codes of the program.
 * @date 2020-09-17
 */
#pragma once

typedef enum return_code_e{
    RC_UNINITIALIZED = -1,
    RC_SUCCESS = 0,
    
    RC_BACKDOOR__MAIN__FORK_FAILED,
    RC_BACKDOOR__MAIN__WAITPID_FAILED,

    RC_SHELL__INIT_SERVER__BAD_PARAMS,
    RC_SHELL__INIT_SERVER__SOCKET_INIT_FAILED,
    RC_SHELL__INIT_SERVER__SOCKET_BIND_FAILED,
    RC_SHELL__INIT_SOCKET__SOCKET_LISTEN_FAILED,
    RC_SHELL__INIT_SOCKET__FCNTL_FAILED,
    RC_SHELL__HANDLE_CONNECTION__SOCKET_ACCEPT_FAILED,
    RC_SHELL__HANDLE_CONNECTION__FORK_FAILED,
    RC_SHELL__DESTROY_SERVER__BAD_PARAMS,
    RC_SHELL__DESTROY_SERVER__CLOSE_SOCKET_FAILED,
    RC_SHELL__START_SHELL__EXEC_FAILED,
    RC_SHELL__START_SHELL__DUP2_FAILED,

    RC_LOGGER__INIT_LOGGER__BAD_PARAMS,
    RC_LOGGER__INIT_LOGGER__LOGGER_ALREADY_INITIALIZED,
    RC_LOGGER__INIT_LOGGER__FOPEN_FAILED,
    RC_LOGGER__INIT_LOGGER__MALLOC_FAILED,
    RC_LOGGER__DESTROY_LOGGER__LOGGER_NOT_INITIALIZED,
    RC_LOGGER__DESTROY_LOGGER__FCLOSE_FAILED,
    RC_LOGGER__LOG__LOGGER_NOT_INITIALIZED,
    RC_LOGGER__LOG__BAD_PARAMS,
    RC_LOGGER__LOG__FFLUSH_FAILED,

    RC_TFTP__INIT_SERVER__BAD_PARAMS,
    RC_TFTP__INIT_UDP_SERVER__SOCKET_INIT_FAILED,
    RC_TFTP__INIT_UDP_SERVER__SOCKET_BIND_FAILED,
    RC_TFTP__RUN_SERVER__SOCKET_RECV_FAILED,
    RC_TFTP__HANDLE_REQUEST__SETSOCKOPT_FAILED,
    RC_TFTP__DESTROY_SERVER__BAD_PARAMS,
    RC_TFTP__DESTROY_SERVER__CLOSE_SOCKET_FAILED,

} return_code_t;