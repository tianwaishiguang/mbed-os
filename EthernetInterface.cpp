/* LWIP implementation of NetworkInterfaceAPI
 * Copyright (c) 2015 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"
#include "EthernetInterface.h"
#include "NetworkStack.h"

#include "lwip/opt.h"
#include "lwip/inet.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/tcpip.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "netif/etharp.h"
#include "eth_arch.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/tcp_impl.h"
#include "lwip/timers.h"
#include "lwip/dns.h"
#include "lwip/def.h"
#include "lwip/ip_addr.h"


/* Predeclared LWIPInterface class */
class LWIPInterface : public NetworkStack
{
    /** Get the local IP address
     *
     *  @return         Null-terminated representation of the local IP address
     *                  or null if not yet connected
     */
    virtual const char *get_ip_address();

    /** Open a socket
     *  @param handle       Handle in which to store new socket
     *  @param proto        Type of socket to open, NSAPI_TCP or NSAPI_UDP
     *  @return             0 on success, negative on failure
     */
    virtual int socket_open(void **handle, nsapi_protocol_t proto);

    /** Close the socket
     *  @param handle       Socket handle
     *  @return             0 on success, negative on failure
     *  @note On failure, any memory associated with the socket must still
     *        be cleaned up
     */
    virtual int socket_close(void *handle);

    /** Bind a server socket to a specific port
     *  @param handle       Socket handle
     *  @param address      Local address to listen for incoming connections on
     *  @return             0 on success, negative on failure.
     */
    virtual int socket_bind(void *handle, const SocketAddress &address);

    /** Start listening for incoming connections
     *  @param handle       Socket handle
     *  @param backlog      Number of pending connections that can be queued up at any
     *                      one time [Default: 1]
     *  @return             0 on success, negative on failure
     */
    virtual int socket_listen(void *handle, int backlog);

    /** Connects this TCP socket to the server
     *  @param handle       Socket handle
     *  @param address      SocketAddress to connect to
     *  @return             0 on success, negative on failure
     */
    virtual int socket_connect(void *handle, const SocketAddress &address);

    /** Accept a new connection.
     *  @param handle       Handle in which to store new socket
     *  @param server       Socket handle to server to accept from
     *  @return             0 on success, negative on failure
     *  @note This call is not-blocking, if this call would block, must
     *        immediately return NSAPI_ERROR_WOULD_WAIT
     */
    virtual int socket_accept(void **handle, void *server);

    /** Send data to the remote host
     *  @param handle       Socket handle
     *  @param data         The buffer to send to the host
     *  @param size         The length of the buffer to send
     *  @return             Number of written bytes on success, negative on failure
     *  @note This call is not-blocking, if this call would block, must
     *        immediately return NSAPI_ERROR_WOULD_WAIT
     */
    virtual int socket_send(void *handle, const void *data, unsigned size);

    /** Receive data from the remote host
     *  @param handle       Socket handle
     *  @param data         The buffer in which to store the data received from the host
     *  @param size         The maximum length of the buffer
     *  @return             Number of received bytes on success, negative on failure
     *  @note This call is not-blocking, if this call would block, must
     *        immediately return NSAPI_ERROR_WOULD_WAIT
     */
    virtual int socket_recv(void *handle, void *data, unsigned size);

    /** Send a packet to a remote endpoint
     *  @param handle       Socket handle
     *  @param address      The remote SocketAddress
     *  @param data         The packet to be sent
     *  @param size         The length of the packet to be sent
     *  @return the         number of written bytes on success, negative on failure
     *  @note This call is not-blocking, if this call would block, must
     *        immediately return NSAPI_ERROR_WOULD_WAIT
     */
    virtual int socket_sendto(void *handle, const SocketAddress &address, const void *data, unsigned size);

    /** Receive a packet from a remote endpoint
     *  @param handle       Socket handle
     *  @param address      Destination for the remote SocketAddress or null
     *  @param buffer       The buffer for storing the incoming packet data
     *                      If a packet is too long to fit in the supplied buffer,
     *                      excess bytes are discarded
     *  @param size         The length of the buffer
     *  @return the         number of received bytes on success, negative on failure
     *  @note This call is not-blocking, if this call would block, must
     *        immediately return NSAPI_ERROR_WOULD_WAIT
     */
    virtual int socket_recvfrom(void *handle, SocketAddress *address, void *buffer, unsigned size);

    /*  Set stack-specific socket options
     *
     *  The setsockopt allow an application to pass stack-specific hints
     *  to the underlying stack. For unsupported options,
     *  NSAPI_ERROR_UNSUPPORTED is returned and the socket is unmodified.
     *
     *  @param handle   Socket handle
     *  @param level    Stack-specific protocol level
     *  @param optname  Stack-specific option identifier
     *  @param optval   Option value
     *  @param optlen   Length of the option value
     *  @return         0 on success, negative error code on failure
     */
    virtual int setsockopt(void *handle, int level, int optname, const void *optval, unsigned optlen);

    /** Register a callback on state change of the socket
     *  @param handle       Socket handle
     *  @param callback     Function to call on state change
     *  @param data         Argument to pass to callback
     *  @note Callback may be called in an interrupt context.
     */
    virtual void socket_attach(void *handle, void (*callback)(void *), void *data);
};


/* Static arena of sockets */
static struct lwip_socket {
    bool in_use;

    struct netconn *conn;
    struct netbuf *buf;
    u16_t offset;

    void (*cb)(void *);
    void *data;
} lwip_arena[MEMP_NUM_NETCONN];

static void lwip_arena_init()
{
    memset(lwip_arena, 0, sizeof lwip_arena);
}

static struct lwip_socket *lwip_arena_alloc()
{
    sys_prot_t prot = sys_arch_protect();

    for (int i = 0; i < MEMP_NUM_NETCONN; i++) {
        if (!lwip_arena[i].in_use) {
            struct lwip_socket *s = &lwip_arena[i];
            memset(s, 0, sizeof *s);
            s->in_use = true;
            sys_arch_unprotect(prot);
            return s;
        }
    }

    sys_arch_unprotect(prot);
    return 0;
}

static void lwip_arena_dealloc(struct lwip_socket *s)
{
    s->in_use = false;
}

static void lwip_socket_callback(
        struct netconn *nc, enum netconn_evt, u16_t len) {
    sys_prot_t prot = sys_arch_protect();

    for (int i = 0; i < MEMP_NUM_NETCONN; i++) {
        if (lwip_arena[i].in_use 
            && lwip_arena[i].conn == nc
            && lwip_arena[i].cb) {
            lwip_arena[i].cb(lwip_arena[i].data);
        }
    }

    sys_arch_unprotect(prot);
}


/* TCP/IP and Network Interface Initialisation */
static struct netif lwip_netif;

static char lwip_ip_addr[NSAPI_IP_SIZE] = "\0";
static char lwip_mac_addr[NSAPI_MAC_SIZE] = "\0";

static Semaphore lwip_tcpip_inited(0);
static void lwip_tcpip_init_irq(void *)
{
    lwip_tcpip_inited.release();
}

static Semaphore lwip_netif_linked(0);
static void lwip_netif_link_irq(struct netif *lwip_netif)
{
    if (netif_is_link_up(lwip_netif)) {
        lwip_netif_linked.release();
    }
}

static Semaphore lwip_netif_up(0);
static void lwip_netif_status_irq(struct netif *lwip_netif)
{
    if (netif_is_up(lwip_netif)) {
        strcpy(lwip_ip_addr, inet_ntoa(lwip_netif->ip_addr));
        lwip_netif_up.release();
    }
}

static void lwip_set_mac_address()
{
#if (MBED_MAC_ADDRESS_SUM != MBED_MAC_ADDR_INTERFACE)
    snprintf(lwip_mac_addr, 19, "%02x:%02x:%02x:%02x:%02x:%02x",
            MBED_MAC_ADDR_0, MBED_MAC_ADDR_1, MBED_MAC_ADDR_2,
            MBED_MAC_ADDR_3, MBED_MAC_ADDR_4, MBED_MAC_ADDR_5);
#else
    char mac[6];
    mbed_mac_address(mac);
    snprintf(lwip_mac_addr, 19, "%02x:%02x:%02x:%02x:%02x:%02x", 
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
}

static const char *lwip_get_mac_address()
{
    return lwip_mac_addr[0] ? lwip_mac_addr : 0;
}

static const char *lwip_get_ip_address()
{
    return lwip_ip_addr[0] ? lwip_ip_addr : 0;
}

static int lwip_init()
{
    // Check if we've already connected
    if (lwip_get_mac_address()) {
        return 0;
    }

    // Set up network
    lwip_set_mac_address();

    tcpip_init(lwip_tcpip_init_irq, NULL);
    lwip_tcpip_inited.wait();

    memset(&lwip_netif, 0, sizeof lwip_netif);
    netif_add(&lwip_netif, 0, 0, 0, NULL, eth_arch_enetif_init, tcpip_input);
    netif_set_default(&lwip_netif);

    netif_set_link_callback  (&lwip_netif, lwip_netif_link_irq);
    netif_set_status_callback(&lwip_netif, lwip_netif_status_irq);

    // Connect to network
    eth_arch_enable_interrupts();
    dhcp_start(&lwip_netif);

    // Zero out socket set
    lwip_arena_init();

    // Wait for an IP Address
    // -1: error, 0: timeout
    if (lwip_netif_up.wait(15000) <= 0) {
        return NSAPI_ERROR_DHCP_FAILURE;
    }

    return 0;
}

static void lwip_deinit() 
{
    dhcp_release(&lwip_netif);
    dhcp_stop(&lwip_netif);

    eth_arch_disable_interrupts();
    lwip_ip_addr[0] = '\0';
    lwip_mac_addr[0] = '\0';
}

static int lwip_err_remap(err_t err) {
    switch (err) {
        case ERR_OK:
            return 0;
        case ERR_MEM:   
            return NSAPI_ERROR_NO_MEMORY;
        case ERR_CONN:
        case ERR_CLSD:
            return NSAPI_ERROR_NO_CONNECTION;
        case ERR_TIMEOUT:
        case ERR_RTE:
        case ERR_INPROGRESS:
        case ERR_WOULDBLOCK:
            return NSAPI_ERROR_WOULD_BLOCK;
        case ERR_VAL:
        case ERR_USE:
        case ERR_ISCONN:
        case ERR_ARG:
            return NSAPI_ERROR_PARAMETER;
        default:
            return NSAPI_ERROR_DEVICE_ERROR;
    }
}

/* LWIP stack implementation */
const char *LWIPInterface::get_ip_address() {
    return lwip_get_ip_address();
}

int LWIPInterface::socket_open(void **handle, nsapi_protocol_t proto)
{
    struct lwip_socket *s = lwip_arena_alloc();
    if (!s) {
        return NSAPI_ERROR_NO_SOCKET;
    }

    s->conn = netconn_new_with_callback(
            proto == NSAPI_TCP ? NETCONN_TCP : NETCONN_UDP,
            lwip_socket_callback);

    if (!s->conn) {
        lwip_arena_dealloc(s);
        return NSAPI_ERROR_NO_SOCKET;
    }

    netconn_set_recvtimeout(s->conn, 1);
    *reinterpret_cast<struct lwip_socket**>(handle) = s;
    return 0;
}

int LWIPInterface::socket_close(void *handle)
{
    struct lwip_socket *s = static_cast<struct lwip_socket*>(handle);

    err_t err = netconn_delete(s->conn);
    lwip_arena_dealloc(s);
    return lwip_err_remap(err);
}


int LWIPInterface::socket_bind(void *handle, const SocketAddress &addr)
{
    struct lwip_socket *s = static_cast<struct lwip_socket*>(handle);

    ip_addr_t ip_addr;
    inet_aton(addr.get_ip_address(), &ip_addr);

    err_t err = netconn_bind(s->conn, &ip_addr, addr.get_port());
    return lwip_err_remap(err);
}

int LWIPInterface::socket_listen(void *handle, int backlog)
{
    struct lwip_socket *s = static_cast<struct lwip_socket*>(handle);

    err_t err = netconn_listen_with_backlog(s->conn, backlog);
    return lwip_err_remap(err);
}

int LWIPInterface::socket_connect(void *handle, const SocketAddress &addr)
{
    struct lwip_socket *s = static_cast<struct lwip_socket*>(handle);

    ip_addr_t ip_addr;
    inet_aton(addr.get_ip_address(), &ip_addr);

    netconn_set_nonblocking(s->conn, false);
    err_t err = netconn_connect(s->conn, &ip_addr, addr.get_port());
    netconn_set_nonblocking(s->conn, true);

    return lwip_err_remap(err);
}

int LWIPInterface::socket_accept(void **handle, void *server)
{
    struct lwip_socket *s = static_cast<struct lwip_socket*>(server);
    struct lwip_socket *ns = lwip_arena_alloc();

    err_t err = netconn_accept(s->conn, &ns->conn);
    if (err != ERR_OK) {
        lwip_arena_dealloc(ns);
        return lwip_err_remap(err);
    }

    *reinterpret_cast<struct lwip_socket**>(handle) = ns;
    return 0; 
}

int LWIPInterface::socket_send(void *handle, const void *data, unsigned size)
{
    struct lwip_socket *s = static_cast<struct lwip_socket*>(handle);

    err_t err = netconn_write(s->conn, data, size, NETCONN_COPY);
    if (err != ERR_OK) {
        return lwip_err_remap(err);
    }

    return size;
}

int LWIPInterface::socket_recv(void *handle, void *data, unsigned size)
{
    struct lwip_socket *s = static_cast<struct lwip_socket*>(handle);

    if (!s->buf) {
        err_t err = netconn_recv(s->conn, &s->buf);
        s->offset = 0;

        if (err != ERR_OK) {
            return (err == ERR_CLSD) ? 0 : lwip_err_remap(err);
        }
    }

    u16_t recv = netbuf_copy_partial(s->buf, data,
            static_cast<u16_t>(size), s->offset);
    s->offset += recv;

    if (s->offset >= netbuf_len(s->buf)) {
        netbuf_delete(s->buf);
        s->buf = 0;
    }

    return recv;
}

int LWIPInterface::socket_sendto(void *handle, const SocketAddress &addr, const void *data, unsigned size)
{
    struct lwip_socket *s = static_cast<struct lwip_socket*>(handle);

    struct netbuf *buf = netbuf_new();
    err_t err = netbuf_ref(buf, data, static_cast<u16_t>(size));
    if (err != ERR_OK) {
        netbuf_free(buf);
        return lwip_err_remap(err);;
    }

    ip_addr_t ip_addr;
    inet_aton(addr.get_ip_address(), &ip_addr);

    err = netconn_sendto(s->conn, buf, &ip_addr, addr.get_port());
    netbuf_delete(buf);
    if (err != ERR_OK) {
        return lwip_err_remap(err);
    }

    return size;
}

int LWIPInterface::socket_recvfrom(void *handle, SocketAddress *addr, void *data, unsigned size)
{
    struct lwip_socket *s = static_cast<struct lwip_socket*>(handle);

    struct netbuf *buf;
    err_t err = netconn_recv(s->conn, &buf);
    if (err != ERR_OK) {
        return lwip_err_remap(err);
    }

    if (addr) {
        ip_addr_t *ip_addr = netbuf_fromaddr(buf);
        addr->set_ip_address(inet_ntoa(*ip_addr));
        addr->set_port(netbuf_fromport(buf));
    }

    u16_t recv = netbuf_copy(buf, data, static_cast<u16_t>(size));
    netbuf_delete(buf);

    return recv;
}

int LWIPInterface::setsockopt(void *handle, int level, int optname, const void *optval, unsigned optlen) {
    struct lwip_socket *s = static_cast<struct lwip_socket*>(handle);

    switch (optname) {
        case NSAPI_KEEPALIVE:
            if (optlen != sizeof(int) || s->conn->type != NETCONN_TCP) {
                return NSAPI_ERROR_UNSUPPORTED;
            }
            
            s->conn->pcb.tcp->so_options |= SOF_KEEPALIVE;
            return 0;

        case NSAPI_KEEPIDLE:
            if (optlen != sizeof(int) || s->conn->type != NETCONN_TCP) {
                return NSAPI_ERROR_UNSUPPORTED;
            }

            s->conn->pcb.tcp->keep_idle = *(int*)optval;
            return 0;

        case NSAPI_KEEPINTVL:
            if (optlen != sizeof(int) || s->conn->type != NETCONN_TCP) {
                return NSAPI_ERROR_UNSUPPORTED;
            }

            s->conn->pcb.tcp->keep_intvl = *(int*)optval;
            return 0;
            
        default:
            return NSAPI_ERROR_UNSUPPORTED;
    }
}

void LWIPInterface::socket_attach(void *handle, void (*callback)(void *), void *data)
{
    struct lwip_socket *s = static_cast<struct lwip_socket*>(handle);

    s->cb = callback;
    s->data = data;
}


/* Interface implementation */
EthernetInterface::EthernetInterface()
{
    _stack = new LWIPInterface();
}

int EthernetInterface::connect()
{
    return lwip_init();
}

int EthernetInterface::disconnect()
{
    lwip_deinit();
    return 0;
}

const char *EthernetInterface::get_ip_address()
{
    return lwip_get_ip_address();
}

const char *EthernetInterface::get_mac_address()
{
    return lwip_get_mac_address();
}

NetworkStack *EthernetInterface::get_stack()
{
    return _stack;
}
