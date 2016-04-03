#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "net_init.h"
#include "network_init.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/sockets.h"
#include "ethernetif.h"
#include "lwip/sockets.h"
#include "netif/etharp.h"
#include "timers.h"
#include "os.h"
#include "httpclient.h"

/* tcp config */
#define MCS_TCP_SERVER "52.77.236.179"
#define SOCK_TCP_SRV_PORT 443

#define MAX_STRING_SIZE 200
TimerHandle_t heartbeat_timer;

/* RESTful config */
#define BUF_SIZE   (1024 * 1)
#define HTTPS_MTK_CLOUD_POST_URL "https://api.mediatek.com/mcs/v2/devices/"

/* utils */
void split(char **arr, char *str, const char *del) {
  char *s = strtok(str, del);
  while(s != NULL) {
    *arr++ = s;
    s = strtok(NULL, del);
  }
}

char *replace(char *st, char *orig, char *repl) {
  static char buffer[1024];
  char *ch;
  if (!(ch = strstr(st, orig)))
   return st;
  strncpy(buffer, st, ch-st);
  buffer[ch-st] = 0;
  sprintf(buffer+(ch-st), "%s%s", repl, ch+strlen(orig));
  return buffer;
}

/* upload mcs datapoint */
httpclient_t client = {0};
char *buf = NULL;

void mcs_upload_datapoint(char *value)
{
    int ret = HTTPCLIENT_ERROR_CONN;
    httpclient_data_t client_data = {0};
    char *content_type = "text/csv";
    // char post_data[32];

    /* deviceKey */
    char deviceKey[20];
    int nvdm_deviceKey_len = sizeof(deviceKey);
    nvdm_read_data_item("common", "deviceKey", (uint8_t *)deviceKey, (uint32_t *)&nvdm_deviceKey_len);

    /* deviceId */
    char deviceId[20];
    int nvdm_deviceId_len = sizeof(deviceId);
    nvdm_read_data_item("common", "deviceId", (uint8_t *)deviceId, (uint32_t *)&nvdm_deviceId_len);

    /* Set post_url */
    char post_url[70] ={0};
    strcat(post_url, HTTPS_MTK_CLOUD_POST_URL);
    strcat(post_url, deviceId);
    strcat(post_url, "/datapoints.csv");

    /* Set header */
    char header[40] = {0};
    strcat(header, "deviceKey:");
    strcat(header, deviceKey);
    strcat(header, "\r\n");

    printf("header: %s\n", header);
    printf("url: %s\n", post_url);
    printf("data: %s\n", value);

    buf = pvPortMalloc(BUF_SIZE);
    if (buf == NULL) {
        printf("buf malloc failed.\r\n");
        return ret;
    }
    buf[0] = '\0';
    ret = httpclient_connect(&client, post_url, HTTPS_PORT);

    client_data.response_buf = buf;
    client_data.response_buf_len = BUF_SIZE;
    client_data.post_content_type = content_type;
    // sprintf(post_data, data);
    client_data.post_buf = value;
    client_data.post_buf_len = strlen(value);
    httpclient_set_custom_header(&client, header);
    ret = httpclient_send_request(&client, post_url, HTTPCLIENT_POST, &client_data);
    if (ret < 0) {
        return ret;
    }
    ret = httpclient_recv_response(&client, &client_data);
    if (ret < 0) {
        return ret;
    }
    printf("\n************************\n");
    printf("httpclient_test_keepalive post data every 5 sec, http status:%d, response data: %s\r\n", httpclient_get_response_code(&client), client_data.response_buf);
    printf("************************\n");
    vPortFree(buf);
    httpclient_close(&client, HTTPS_PORT);
    return ret;
}


/* tcp connection */
void mcs_tcp_init(void (*mcs_tcp_callback)(char *))
{
    int s;
    int ret;
    struct sockaddr_in addr;
    int count = 0;
    int rcv_len, rlen;

    os_memset(&addr, 0, sizeof(addr));

    /* deviceId */
    char deviceId[20];
    int nvdm_deviceId_len = sizeof(deviceId);
    nvdm_read_data_item("common", "deviceId", (uint8_t *)deviceId, (uint32_t *)&nvdm_deviceId_len);

    /* deviceKey */
    char deviceKey[20];
    int nvdm_deviceKey_len = sizeof(deviceKey);
    nvdm_read_data_item("common", "deviceKey", (uint8_t *)deviceKey, (uint32_t *)&nvdm_deviceKey_len);

    /* command buffer */
    char cmd_buf [50]= {0};
    strcat(cmd_buf, deviceId);
    strcat(cmd_buf, ",");
    strcat(cmd_buf, deviceKey);
    strcat(cmd_buf, ",0");

    printf("cmd_buf: %s\n", cmd_buf);

    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SOCK_TCP_SRV_PORT);
    addr.sin_addr.s_addr =inet_addr(MCS_TCP_SERVER);

    printf("============MCS TCP connection ============\n");

    /* create the socket */
    s = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        printf("tcp client create fail 0\n");
        goto idle;
    }

    ret = lwip_connect(s, (struct sockaddr *)&addr, sizeof(addr));

    if (ret < 0) {
        lwip_close(s);
        mcs_tcp_init(mcs_tcp_callback);
        printf("tcp client connect fail 1\n");
        goto idle;
    }

    /* timer */
    void tcpTimerCallback( TimerHandle_t pxTimer ) {
        ret = lwip_write(s, cmd_buf, sizeof(cmd_buf));
    }

    heartbeat_timer = xTimerCreate("TimerMain", (30*1000 / portTICK_RATE_MS), pdTRUE, (void *)0, tcpTimerCallback);
    xTimerStart( heartbeat_timer, 0 );

    for (;;) {
        char rcv_buf[MAX_STRING_SIZE] = {0};
        if (0 == count) {
            ret = lwip_write(s, cmd_buf, sizeof(cmd_buf));
        }
        LOG_I(common, "MCS tcp-client waiting for data...");
        rcv_len = 0;
        rlen = lwip_recv(s, &rcv_buf[rcv_len], sizeof(rcv_buf) - 1 - rcv_len, 0);
        rcv_len += rlen;
        LOG_I(common, "MCS tcp-client received data:%s", rcv_buf);

        /* split the string of rcv_buffer */
        char split_buf[MAX_STRING_SIZE] = {0};
        strcpy(split_buf, rcv_buf);

        char *arr[7];
        char *del = ",";
        split(arr, split_buf, del);
        if (0 == strncmp (arr[3], "FOTA", 4)) {
            char *s = replace(arr[6], "https", "http");
            printf("fota url: %s\n", s);
            _fota_cli_dl_by_http(s);
        } else {
          mcs_tcp_callback(rcv_buf);
        }

        count ++;
    }

idle:
    LOG_I(common, "MCS tcp-client end");

}
