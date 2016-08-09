#include <string.h>
#include "jerry.h"
#include "microlattice.h"
#include "MQTTClient.h"
#include "httpclient.h"
#include "./mcs.h"
#include "fota_download_interface.h"

#define MAX_STRING_SIZE 200
#define BUF_SIZE   (1024 * 3)

static int arrivedcount = 0;
Client c;   //MQTT client
char topic_buffer[100];
MQTTMessage message;
int rc = 0;

char rcv_buf[100] = {0};
char split_buf[MAX_STRING_SIZE] = {0};

/* utils */
void mcs_split(char **arr, char *str, const char *del)
{
  char *s = strtok(str, del);
  while(s != NULL) {
    *arr++ = s;
    s = strtok(NULL, del);
  }
}

char *mcs_replace(char *st, char *orig, char *repl)
{
  static char buffer[1024];
  char *ch;
  if (!(ch = strstr(st, orig)))
   return st;
  strncpy(buffer, st, ch-st);
  buffer[ch-st] = 0;
  sprintf(buffer+(ch-st), "%s%s", repl, ch+strlen(orig));
  return buffer;
}

void livereload(char *url)
{
  jerry_cleanup();

  httpclient_t client = {0};
  httpclient_data_t client_data = {0};
  char *buf;

  buf = pvPortMalloc(BUF_SIZE);
  if (buf == NULL) {
      printf("httpclient_test malloc failed.\r\n");
      return;
  }
  client_data.response_buf = buf;
  client_data.response_buf_len = BUF_SIZE;

  httpclient_get(&client, url, HTTP_PORT, &client_data);
  printf("url: %s\n", url);
  strcpy(&script, client_data.response_buf);
  _js_init();
  vPortFree(buf);
}

void _js_init()
{
  jerry_init (JERRY_FLAG_EMPTY);
  jerry_api_value_t eval_ret;
  js_lib_init();
  jerry_api_eval (&script, strlen (&script), false, false, &eval_ret);
  jerry_api_release_value (&eval_ret);

  vTaskDelete(NULL);
}

DELCARE_HANDLER(__mcs)
{
  arrivedcount = 0;
  /* server */
  int server_req_sz = -jerry_api_string_to_char_buffer (args_p[0].v_string, NULL, 0);
  char * server_buffer = (char*) malloc (server_req_sz);
  server_req_sz = jerry_api_string_to_char_buffer (args_p[0].v_string, server_buffer, server_req_sz);
  server_buffer[server_req_sz] = '\0';

  /* port */
  int port_req_sz = -jerry_api_string_to_char_buffer (args_p[1].v_string, NULL, 0);
  char * port_buffer = (char*) malloc (port_req_sz);
  port_req_sz = jerry_api_string_to_char_buffer (args_p[1].v_string, port_buffer, port_req_sz);
  port_buffer[port_req_sz] = '\0';

  /* topic */
  int topic_req_sz = -jerry_api_string_to_char_buffer (args_p[2].v_string, NULL, 0);
  char * topic_buffer = (char*) malloc (topic_req_sz);
  topic_req_sz = jerry_api_string_to_char_buffer (args_p[2].v_string, topic_buffer, topic_req_sz);
  topic_buffer[topic_req_sz] = '\0';

  /* clientId */
  int clientId_req_sz = -jerry_api_string_to_char_buffer (args_p[3].v_string, NULL, 0);
  char * clientId_buffer = (char*) malloc (clientId_req_sz);
  clientId_req_sz = jerry_api_string_to_char_buffer (args_p[3].v_string, clientId_buffer, clientId_req_sz);
  clientId_buffer[clientId_req_sz] = '\0';

  /* tls */

  printf("topic: %s\n", topic_buffer);
  unsigned char msg_buf[200];     //generate messages such as unsubscrube
  unsigned char msg_readbuf[200]; //receive messages such as unsubscrube ack

  Network n;  //TCP network
  MQTTPacket_connectData data = MQTTPacket_connectData_initializer;

  //init mqtt network structure
  NewNetwork(&n);

  rc = ConnectNetwork(&n, server_buffer, port_buffer);

  if (rc != 0) {
    printf("TCP connect fail,status -%4X\n", -rc);
    return true;
  }

  //init mqtt client structure
  MQTTClient(&c, &n, 12000, msg_buf, 200, msg_readbuf, 200);

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

  printf("Subscribing to %s\n", topic_buffer);

  void messageArrived(MessageData *md) {
    // char rcv_buf_old[100] = {0};
    MQTTMessage *message = md->message;

    jerry_api_value_t params[0];
    params[0].type = JERRY_API_DATA_TYPE_STRING;
    params[0].v_string = jerry_api_create_string (message->payload);

    jerry_api_call_function(args_p[5].v_object, NULL, false, &params, 1);
    jerry_api_release_value(&params);

    strcpy(rcv_buf, message->payload);
    strcpy(split_buf, rcv_buf);

    char *arr[7];
    char *del = ",";
    mcs_split(arr, split_buf, del);

    if (0 == strncmp (arr[1], "FOTA", 4)) {
      char *s = mcs_replace(arr[4], "https", "http");
      fota_download_by_http(s);
    } else if ( 0 == strncmp (arr[1], "Livereload", 10)){
      printf("livereloadUrl: %s\n", arr[2]);
      livereload(arr[2]);
    }

    memset(rcv_buf, 0, 100);
    memset(split_buf, 0, MAX_STRING_SIZE);

  }

  switch ((int)args_p[4].v_float32) {
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

  ret_val_p->type = JERRY_API_DATA_TYPE_BOOLEAN;
  ret_val_p->v_bool = true;
  free(server_buffer);
  free(topic_buffer);
  free(port_buffer);
  free(clientId_buffer);
  return true;
}

DELCARE_HANDLER(__mcsClose)
{
  arrivedcount = 1;
  return true;
}

DELCARE_HANDLER(__mcsSend)
{
  char buf[100];

  int msg_req_sz = -jerry_api_string_to_char_buffer (args_p[0].v_string, NULL, 0);
  char * msg_buffer = (char*) malloc (msg_req_sz);
  msg_req_sz = jerry_api_string_to_char_buffer (args_p[0].v_string, msg_buffer, msg_req_sz);
  msg_buffer[msg_req_sz] = '\0';

  switch ((int)args_p[1].v_float32) {
    case 0:
      message.qos = QOS0;
      break;
    case 1:
      message.qos = QOS1;
      break;
    case 2:
      message.qos = QOS2;
      break;
  }

  message.retained = false;
  message.dup = false;
  message.payload = (void *)msg_buffer;
  message.payloadlen = strlen(msg_buffer) + 1;
  rc = MQTTPublish(&c, topic_buffer, &message);
  return true;
}

DELCARE_HANDLER(__rebootScript)
{
  jerry_cleanup();

  /* url */
  int url_req_sz = -jerry_api_string_to_char_buffer (args_p[0].v_string, NULL, 0);
  char * url_buffer = (char*) malloc (url_req_sz);
  url_req_sz = jerry_api_string_to_char_buffer (args_p[0].v_string, url_buffer, url_req_sz);
  url_buffer[url_req_sz] = '\0';

  httpclient_t client = {0};
  httpclient_data_t client_data = {0};
  char *buf;

  buf = pvPortMalloc(BUF_SIZE);
  if (buf == NULL) {
      printf("httpclient_test malloc failed.\r\n");
      return;
  }
  client_data.response_buf = buf;
  client_data.response_buf_len = BUF_SIZE;

  httpclient_get(&client, url_buffer, HTTP_PORT, &client_data);

  strcpy(&script, client_data.response_buf);
  _js_init();
  vPortFree(buf);

  ret_val_p->type = JERRY_API_DATA_TYPE_BOOLEAN;
  ret_val_p->v_bool = true;
  return true;
}

void ml_mcs_init(void)
{
  REGISTER_HANDLER(__mcs);
  REGISTER_HANDLER(__mcsSend);
  REGISTER_HANDLER(__mcsClose);
  REGISTER_HANDLER(__rebootScript);
}