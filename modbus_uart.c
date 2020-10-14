#include "modbus_uart.h"
#include <termios.h>
#include <dfs_posix.h>
#include <sys/select.h>

static int s = -1;
static rt_mutex_t mtx = RT_NULL;

static int modbus_uart_flush(void)
{
    if(s < 0)
        return -RT_ERROR;
    
    tcflush(s, TCIOFLUSH);

    return RT_EOK;
}

static int modbus_uart_send(uint8_t *buf, int len)
{
    if(s < 0)
        return -RT_ERROR;
    
    if(len <= 0)
        return len;
    
    return write(s, buf, len);
}

static int modbus_uart_receive(uint8_t *buf, int bufsz, int timeout)
{
    if((s < 0) || (bufsz <= 0))
        return -RT_ERROR;
    
    if(timeout <= 0)
    {
        int rc = read(s, buf, bufsz);
        return rc;
    }

    int len = 0;
    int rc = 0;
    fd_set rset;
    struct timeval tv;

    FD_ZERO(&rset);
    FD_SET(s, &rset);
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    while(bufsz > 0)
    {
        rc = select(s + 1, &rset, RT_NULL, RT_NULL, &tv);
        if(rc <= 0)
        {
            break;
        }

        rc = read(s, buf + len, bufsz);
        if(rc <= 0)
        {
            rc = -1;
            break;
        }
        len += rc;
        bufsz -= rc;

        tv.tv_sec = 0;
        tv.tv_usec = 20000;
        FD_ZERO(&rset);
        FD_SET(s, &rset);
    }

    if(rc >= 0)
    {
        rc = len;
    }

    return rc;
}

int modbus_uart_pass(uint8_t *send_buf, int send_len, uint8_t *read_buf, int read_bufsz, int timeout)
{
    if(s < 0)
        return -RT_ERROR;
    
    int rc = rt_mutex_take(mtx, timeout + 1000);
    if(rc != RT_EOK)
        return -RT_ERROR;
    
    rt_thread_mdelay(10);
    modbus_uart_flush();
    modbus_uart_send(send_buf, send_len);
    rc = modbus_uart_receive(read_buf, read_bufsz, timeout);

    rt_mutex_release(mtx);

    return rc;
}

int modbus_uart_init(void)
{
    struct rt_serial_device *serial = (struct rt_serial_device *)rt_device_find("uart6");
    if(serial == RT_NULL)
        return -RT_ERROR;
    
    s = open("/dev/uart6", O_RDWR | O_NONBLOCK, 0);
    if(s < 0)
        return -RT_ERROR;

    struct serial_configure serial_cfg = serial->config;
    serial_cfg.baud_rate = 9600;
    serial_cfg.data_bits = DATA_BITS_8;
    serial_cfg.stop_bits = STOP_BITS_1;
    serial_cfg.parity = PARITY_NONE;

    rt_base_t level = rt_hw_interrupt_disable();
    RT_ASSERT(rt_device_control(&(serial->parent), RT_DEVICE_CTRL_CONFIG, &serial_cfg) == RT_EOK);
    rt_hw_interrupt_enable(level);

    mtx = rt_mutex_create("mb_uart", RT_IPC_FLAG_FIFO);
    RT_ASSERT(mtx != RT_NULL);

    return RT_EOK;
}
