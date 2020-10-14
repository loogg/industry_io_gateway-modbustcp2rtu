#include <dfs_posix.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <modbus.h>
#include <modbus-private.h>
#include "client_session.h"
#include "modbus_uart.h"

#define DEFAULT_TIMEOUT 60
// 每个mbtcp总线最大支持客户端数
#define MAX_CLIENT_NUM 3

// 客户端session
struct client_session
{
    int fd;
    rt_tick_t tick_timeout;
    rt_slist_t slist;
};

static rt_slist_t session_header = RT_SLIST_OBJECT_INIT(session_header);

static int client_session_get_num(void)
{
    int num = 0;

    rt_base_t level = rt_hw_interrupt_disable();
    num = rt_slist_len(&session_header);
    rt_hw_interrupt_enable(level);

    return num;
}

static int client_session_delete(struct client_session *session)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_slist_remove(&session_header, &(session->slist));
    rt_hw_interrupt_enable(level);

    close(session->fd);
    rt_free(session);

    return RT_EOK;
}

static void mbtcp2rtu_client_entry(void *parameter)
{
    struct client_session *session = parameter;
    modbus_t *ctx_tcp = RT_NULL;
    modbus_t *ctx_rtu = RT_NULL;
    uint8_t rtu_send_buf[MODBUS_MAX_ADU_LENGTH];
    uint8_t rtu_read_buf[MODBUS_MAX_ADU_LENGTH];

    ctx_tcp = modbus_new_tcp(RT_NULL, 0, AF_INET);
    if(ctx_tcp == RT_NULL)
        goto _exit;
    
    ctx_rtu = modbus_new_rtu("x", 9600, 'N', 8, 1);
    if(ctx_rtu == RT_NULL)
        goto _exit;

    int option = 1;
    int rc = setsockopt(session->fd, IPPROTO_TCP, TCP_NODELAY, (const void *)&option, sizeof(int));
    if(rc < 0)
        goto _exit;
    
    uint32_t loption = 1;
    ioctlsocket(session->fd, FIONBIO, &loption);

    session->tick_timeout = rt_tick_get() + rt_tick_from_millisecond(DEFAULT_TIMEOUT * 1000);

    // select使用
    fd_set readset, exceptset;   
    // select超时时间
    struct timeval select_timeout;
    select_timeout.tv_sec = 1;
    select_timeout.tv_usec = 0;

    while(1)
    {
        FD_ZERO(&readset);
        FD_ZERO(&exceptset);

        FD_SET(session->fd, &readset);
        FD_SET(session->fd, &exceptset);

        rc = select(session->fd + 1, &readset, RT_NULL, &exceptset, &select_timeout);
        if(rc < 0)
            break;
        if(rc > 0)
        {
            if(FD_ISSET(session->fd, &exceptset))
                break;
            if(FD_ISSET(session->fd, &readset))
            {
                uint8_t query[MODBUS_MAX_ADU_LENGTH];
                modbus_set_socket(ctx_tcp, session->fd);
                int tcp_recv_len = modbus_receive(ctx_tcp, query);
                if (tcp_recv_len > 0)
                {
                    session->tick_timeout = rt_tick_get() + rt_tick_from_millisecond(DEFAULT_TIMEOUT * 1000);
                    int offset = ctx_tcp->backend->header_length - 1;
                    rt_memcpy(rtu_send_buf, query + offset, tcp_recv_len - offset);
                    int rtu_send_len = tcp_recv_len - offset;
                    rtu_send_len = ctx_rtu->backend->send_msg_pre(rtu_send_buf, rtu_send_len);
                    int rtu_recv_len = modbus_uart_pass(rtu_send_buf, rtu_send_len, rtu_read_buf, sizeof(rtu_read_buf), 3000);
                    int tcp_reply_len = 0;
                    if(rtu_recv_len <= 0)
                        tcp_reply_len = modbus_reply_exception(ctx_tcp, query, MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE);
                    else
                    {
                        if(ctx_rtu->backend->check_integrity(ctx_rtu, rtu_read_buf, rtu_recv_len) < 0)
                            tcp_reply_len = modbus_reply_exception(ctx_tcp, query, MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE);
                        else
                        {
                            if(rtu_read_buf[0] != rtu_send_buf[0])
                                tcp_reply_len = modbus_reply_exception(ctx_tcp, query, MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE);
                            else
                            {
                                rt_memcpy(query + offset, rtu_read_buf, rtu_recv_len - 2);
                                tcp_reply_len = offset + rtu_recv_len - 2;
                                ctx_tcp->backend->send_msg_pre(query, tcp_reply_len);
                                tcp_reply_len = ctx_tcp->backend->send(ctx_tcp, query, tcp_reply_len);
                            }
                        }
                    }

                    if(tcp_reply_len <= 0)
                        break;
                }
                else
                    break;
            }
        }

        if((rt_tick_get() - session->tick_timeout) < (RT_TICK_MAX / 2))
            break;
    }

_exit:
    if(ctx_tcp)
        modbus_free(ctx_tcp);
    if(ctx_rtu)
        modbus_free(ctx_rtu);
    client_session_delete(session);
}

int client_session_create(int fd)
{
    if(fd < 0)
        return -RT_ERROR;
    
    if(client_session_get_num() >= MAX_CLIENT_NUM)
        return -RT_ERROR;
    
    struct client_session *session = rt_malloc(sizeof(struct client_session));
    session->fd = fd;
    session->tick_timeout = rt_tick_get();
    rt_slist_init(&(session->slist));

    rt_base_t level = rt_hw_interrupt_disable();
    rt_slist_append(&session_header, &(session->slist));
    rt_hw_interrupt_enable(level);

    rt_thread_t tid = rt_thread_create("mbt2rc", mbtcp2rtu_client_entry, session, 4096, 19, 100);
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);

    return RT_EOK;
}


