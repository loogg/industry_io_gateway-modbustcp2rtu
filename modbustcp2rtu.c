#include "init_module.h"
#include "plugins.h"
#include <dfs_posix.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <modbus.h>
#include "client_session.h"
#include "modbus_uart.h"

#define DBG_TAG "modbustcp2rtu"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define DEFAULT_PORT    502


static struct plugins_module modbustcp2rtu_plugin = {
	.name = "modbustcp2rtu",
	.version = "v1.0.0",
	.author = "malongwei"
};

static struct init_module modbustcp2rtu_init_module;

static void mbtcp2rtu_entry(void *parameter)
{
    modbus_t *ctx = modbus_new_tcp(RT_NULL, DEFAULT_PORT, AF_INET);
    if (ctx == RT_NULL)
        return;

    modbustcp2rtu_plugin.state = PLUGINS_STATE_RUNNING;

    // 服务器fd
    int server_fd = -1;
    uint32_t loption = 1;
    // select使用
    fd_set readset, exceptset;
    // select超时时间
    struct timeval select_timeout;
    select_timeout.tv_sec = 1;
    select_timeout.tv_usec = 0;

    rt_thread_mdelay(5000);

_mbtcp_start:
    server_fd = modbus_tcp_listen(ctx, 1);
    if (server_fd < 0)
        goto _mbtcp_restart;

    ioctlsocket(server_fd, FIONBIO, &loption);

    while (1)
    {
        FD_ZERO(&readset);
        FD_ZERO(&exceptset);

        FD_SET(server_fd, &readset);
        FD_SET(server_fd, &exceptset);

        int rc = select(server_fd + 1, &readset, RT_NULL, &exceptset, &select_timeout);
        if (rc < 0)
            break;
        if (rc > 0)
        {
            if (FD_ISSET(server_fd, &exceptset))
                break;
            if (FD_ISSET(server_fd, &readset))
            {
                int client_fd = modbus_tcp_accept(ctx, &server_fd);
                if (client_fd < 0)
                    break;
                if (client_session_create(client_fd) != RT_EOK)
                    close(client_fd);
            }
        }
    }

_mbtcp_restart:
    if (server_fd >= 0)
    {
        close(server_fd);
        server_fd = -1;
    }

    rt_thread_mdelay(10000);
    goto _mbtcp_start;
}

static int modbustcp2rtu_init(void)
{
    rt_thread_t tid = rt_thread_create("mbt2r", mbtcp2rtu_entry, RT_NULL, 2048, 19, 100);
    if(tid != RT_NULL)
        rt_thread_startup(tid);

    modbus_uart_init();
    
    return RT_EOK;
}


int fregister(const char *path, void *dlmodule, uint8_t is_sys)
{
    plugins_register(&modbustcp2rtu_plugin, path, dlmodule, is_sys);

    modbustcp2rtu_init_module.init = modbustcp2rtu_init;
    init_module_app_register(&modbustcp2rtu_init_module);

    return RT_EOK;
} 
