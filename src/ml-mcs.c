#include <string.h>
#include <stdint.h>

#include "./mcs.h"

#include "jerry.h"
#include "microlattice.h"

DELCARE_HANDLER(__mcsRegister) {
  /* deviceId */
  int deviceId_req_sz = -jerry_api_string_to_char_buffer(args_p[0].v_string, NULL, 0);
  char * deviceId_buffer = (char*) malloc (deviceId_req_sz);
  deviceId_req_sz = jerry_api_string_to_char_buffer (args_p[0].v_string, (jerry_api_char_t *) deviceId_buffer, deviceId_req_sz);
  deviceId_buffer[deviceId_req_sz] = '\0';

  /* deviceKey */
  int deviceKey_req_sz = -jerry_api_string_to_char_buffer(args_p[1].v_string, NULL, 0);
  char * deviceKey_buffer = (char*) malloc (deviceKey_req_sz);
  deviceKey_req_sz = jerry_api_string_to_char_buffer (args_p[1].v_string, (jerry_api_char_t *) deviceKey_buffer, deviceKey_req_sz);
  deviceKey_buffer[deviceKey_req_sz] = '\0';

  ret_val_p->type = JERRY_API_DATA_TYPE_BOOLEAN;
  ret_val_p->v_bool = true;

  free(deviceId_buffer);
  free(deviceKey_buffer);
  return true;
}

DELCARE_HANDLER(__mcsTCPClient) {
  void _mcs_tcp_callback(char *rcv_buf) {
    jerry_api_value_t params[0];
    params[0].type = JERRY_API_DATA_TYPE_STRING;
    params[0].v_string = jerry_api_create_string(rcv_buf);
    jerry_api_call_function(args_p[0].v_object, NULL, false, &params, 1);
    jerry_api_release_value(&params);
  }

  mcs_tcp_init(_mcs_tcp_callback)
}

DELCARE_HANDLER(__mcsUploadData) {

  /* data */
  int data_req_sz = -jerry_api_string_to_char_buffer(args_p[0].v_string, NULL, 0);
  char * data_buffer = (char*) malloc (data_req_sz);
  data_req_sz = jerry_api_string_to_char_buffer (args_p[0].v_string, (jerry_api_char_t *) data_buffer, data_req_sz);
  data_buffer[data_req_sz] = '\0';

  mcs_upload_datapoint(data_buffer);

  ret_val_p->type = JERRY_API_DATA_TYPE_BOOLEAN;
  ret_val_p->v_bool = true;

  free(data_buffer);
  return true;
}

void ml_mcs_init(void) {
  REGISTER_HANDLER(__mcsRegister);
  REGISTER_HANDLER(__mcsTCPClient);
  REGISTER_HANDLER(__mcsUploadData);
}