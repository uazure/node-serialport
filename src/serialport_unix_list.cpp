#include "./serialport.h"

void EIO_List(uv_work_t* req) {
  ListBaton* data = static_cast<ListBaton*>(req->data);
  snprintf(data->errorString, sizeof(data->errorString), "List is not Implemented");
  return;
}
