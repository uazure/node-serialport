#include "./serialport.h"
#include "./serialport_win_list.h"

/*
 * listComPorts.c -- list COM ports
 *
 * http://github.com/todbot/usbSearch/
 *
 * 2012, Tod E. Kurt, http://todbot.com/blog/
 *
 *
 * Uses DispHealper : http://disphelper.sourceforge.net/
 *
 */
void EIO_List(uv_work_t* req) {
  ListBaton* data = static_cast<ListBaton*>(req->data);

  {
    DISPATCH_OBJ(wmiSvc);
    DISPATCH_OBJ(colDevices);

    dhInitialize(TRUE);
    dhToggleExceptions(FALSE);

    dhGetObject(L"winmgmts:{impersonationLevel=impersonate}!\\\\.\\root\\cimv2", NULL, &wmiSvc);
    dhGetValue(L"%o", &colDevices, wmiSvc, L".ExecQuery(%S)", L"Select * from Win32_PnPEntity");

    int port_count = 0;
    FOR_EACH(objDevice, colDevices, NULL) {
      char* name = NULL;
      char* pnpid = NULL;
      char* manu = NULL;
      char* match;

      dhGetValue(L"%s", &name,  objDevice, L".Name");
      dhGetValue(L"%s", &pnpid, objDevice, L".PnPDeviceID");

      if (name != NULL && ((match = strstr(name, "(COM")) != NULL)) {  // look for "(COM23)"
        // 'Manufacturuer' can be null, so only get it if we need it
        dhGetValue(L"%s", &manu, objDevice,  L".Manufacturer");
        port_count++;
        char* comname = strtok(match, "()");
        ListResultItem* resultItem = new ListResultItem();
        resultItem->comName = comname;
        resultItem->manufacturer = manu;
        resultItem->pnpId = pnpid;
        data->results.push_back(resultItem);
        dhFreeString(manu);
      }

      dhFreeString(name);
      dhFreeString(pnpid);
    } NEXT(objDevice);

    SAFE_RELEASE(colDevices);
    SAFE_RELEASE(wmiSvc);

    dhUninitialize(TRUE);
  }

  std::vector<UINT> ports;
  if (CEnumerateSerial::UsingQueryDosDevice(ports)) {
    for (size_t i = 0; i < ports.size(); i++) {
      char comname[64] = { 0 };
      _snprintf_s(comname, sizeof(comname), _TRUNCATE, "COM%u", ports[i]);
      bool bFound = false;
      for (std::list<ListResultItem*>::iterator ri = data->results.begin(); ri != data->results.end(); ++ri) {
        if (stricmp((*ri)->comName.c_str(), comname) == 0) {
          bFound = true;
          break;
        }
      }
      if (!bFound) {
        ListResultItem* resultItem = new ListResultItem();
        resultItem->comName = comname;
        resultItem->manufacturer = "";
        resultItem->pnpId = "";
        data->results.push_back(resultItem);
      }
    }
  }
}
