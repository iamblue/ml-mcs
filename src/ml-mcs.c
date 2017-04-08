#include <string.h>
#include "jerry-api.h"
#include "microlattice.h"
#include "MQTTClient.h"
#include "./mcs.h"

#define MIN(a,b) ((a) < (b) ? a : b)

#include "hal_sys.h"
#include "fota.h"
#include "fota_config.h"

static int arrivedcount = 0;
Client c;   //MQTT client
MQTTMessage message;
int rc = 0;

void mcs_splitn(char ** dst, char * src, const char * delimiter, uint32_t max_split)
{
    uint32_t split_cnt = 0;
    char *saveptr = NULL;
    char *s = strtok_r(src, delimiter, &saveptr);
    while (s != NULL && split_cnt < max_split) {
        *dst++ = s;
        s = strtok_r(NULL, delimiter, &saveptr);
        split_cnt++;
    }
}

char *mcs_replace(char *st, char *orig, char *repl) {
  static char buffer[1024];
  char *ch;
  if (!(ch = strstr(st, orig)))
   return st;
  strncpy(buffer, st, ch-st);
  buffer[ch-st] = 0;
  sprintf(buffer+(ch-st), "%s%s", repl, ch+strlen(orig));
  return buffer;
}

DELCARE_HANDLER(__mcs) {
  arrivedcount = 0;

  unsigned char msg_buf[100];     //generate messages such as unsubscrube
  unsigned char msg_readbuf[100]; //receive messages such as unsubscrube ack

  Network n;  //TCP network
  MQTTPacket_connectData data = MQTTPacket_connectData_initializer;

  //init mqtt network structure
  NewNetwork(&n);

  jerry_size_t port_req_sz = jerry_get_string_size (args_p[1]);
  jerry_char_t port_buffer[port_req_sz];
  jerry_string_to_char_buffer (args_p[1], port_buffer, port_req_sz);
  port_buffer[port_req_sz] = '\0';

  jerry_size_t server_req_sz = jerry_get_string_size (args_p[0]);
  jerry_char_t server_buffer[server_req_sz];
  jerry_string_to_char_buffer (args_p[0], server_buffer, server_req_sz);
  server_buffer[server_req_sz] = '\0';

  rc = ConnectNetwork(&n, server_buffer, port_buffer);

  if (rc != 0) {
    printf("TCP connect fail,status -%4X\n", -rc);
    return true;
  }

  //init mqtt client structure
  MQTTClient(&c, &n, 12000, msg_buf, 100, msg_readbuf, 100);

  jerry_size_t clientId_req_sz = jerry_get_string_size (args_p[3]);
  jerry_char_t clientId_buffer[clientId_req_sz];
  jerry_string_to_char_buffer (args_p[3], clientId_buffer, clientId_req_sz);
  clientId_buffer[clientId_req_sz] = '\0';

  //mqtt connect req packet header
  data.willFlag = 0;
  data.MQTTVersion = 3;
  data.clientID.cstring = clientId_buffer;
  data.username.cstring = NULL;
  data.password.cstring = NULL;
  data.keepAliveInterval = 10;
  data.cleansession = 1;

  //send mqtt connect req to remote mqtt server
  rc = MQTTConnect(&c, &data);

  if (rc != 0) {
    printf("MQTT connect fail,status%d\n", rc);
  }

  void messageArrived(MessageData *md) {
    MQTTMessage *message = md->message;

    char rcv_buf[200] = {0};

    const size_t write_len = MIN((size_t)(message->payloadlen), 200 - 1);
    strncpy(rcv_buf, message->payload, write_len);

    jerry_value_t params[0];
    params[0] = jerry_create_string(rcv_buf);

    char split_buf[MCS_MAX_STRING_SIZE] = {0};
    strncpy(split_buf, rcv_buf, MCS_MAX_STRING_SIZE);

    char *arr[5];
    char *del = ",";
    mcs_splitn(arr, split_buf, del, 5);

    if (0 == strncmp (arr[1], "FOTA", 4)) {
      char *s = mcs_replace(arr[4], "https", "http");
      fota_download_by_http(s);
      fota_ret_t err;
      err = fota_trigger_update();
      if (0 == err){
          hal_sys_reboot(HAL_SYS_REBOOT_MAGIC, WHOLE_SYSTEM_REBOOT_COMMAND);
          return 0;
      } else {
          return -1;
      }
    }

    jerry_value_t this_val = jerry_create_undefined();
    jerry_value_t ret_val = jerry_call_function (args_p[5], this_val, &params, 1);

    jerry_release_value(params);
    jerry_release_value(this_val);
    jerry_release_value(ret_val);
  }

  jerry_size_t topic_req_sz = jerry_get_string_size (args_p[2]);
  jerry_char_t topic_buffer[topic_req_sz];
  jerry_string_to_char_buffer (args_p[2], topic_buffer, topic_req_sz);
  topic_buffer[topic_req_sz] = '\0';

  printf("Subscribing to %s\n", topic_buffer);

  switch ((int) jerry_get_number_value(args_p[4])) {
    case 0:
      rc = MQTTSubscribe(&c, topic_buffer, QOS0, messageArrived);
      break;
    case 1:
      rc = MQTTSubscribe(&c, topic_buffer, QOS1, messageArrived);
      break;
    case 2:
      rc = MQTTSubscribe(&c, topic_buffer, QOS2, messageArrived);
      break;
  }

  while (arrivedcount < 1) {
    MQTTYield(&c, 1000);
  }

  jerry_release_value(server_buffer);
  jerry_release_value(topic_buffer);
  jerry_release_value(port_buffer);
  jerry_release_value(clientId_buffer);

  return jerry_create_boolean(true);
}

DELCARE_HANDLER(__mcsClose) {
  arrivedcount = 1;
  return jerry_create_boolean(true);
}

DELCARE_HANDLER(__mcsSend) {

  MQTTMessage message_rsp;

  jerry_size_t topic_req_sz = jerry_get_string_size (args_p[0]);
  jerry_char_t topic_buffer[topic_req_sz];
  jerry_string_to_char_buffer (args_p[0], topic_buffer, topic_req_sz);
  topic_buffer[topic_req_sz] = '\0';

  jerry_size_t msg_req_sz = jerry_get_string_size (args_p[1]);
  jerry_char_t msg_buffer[msg_req_sz];
  jerry_string_to_char_buffer (args_p[1], msg_buffer, msg_req_sz);
  msg_buffer[msg_req_sz] = '\0';

  switch ((int) jerry_get_number_value(args_p[2])) {
    case 0:
      message_rsp.qos = QOS0;
      break;
    case 1:
      message_rsp.qos = QOS1;
      break;
    case 2:
      message_rsp.qos = QOS2;
      break;
  }

  message_rsp.retained = false;
  message_rsp.dup = false;
  message_rsp.payload = (void *)msg_buffer;
  message_rsp.payloadlen = strlen(msg_buffer) + 1;

  MQTTPublish(&c, topic_buffer, &message_rsp);

  return jerry_create_boolean(true);
}

void ml_mcs_init(void) {
  REGISTER_HANDLER(__mcs);
  REGISTER_HANDLER(__mcsClose);
  REGISTER_HANDLER(__mcsSend);
}