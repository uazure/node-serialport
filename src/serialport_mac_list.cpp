#include "./serialport.h"
#include "./serialport_mac_list.h"

Boolean lockInitialised = FALSE;
uv_mutex_t list_mutex;

static io_service_t GetUsbDevice(io_service_t service) {
  IOReturn status;
  io_iterator_t   iterator = 0;
  io_service_t    device = 0;

  if (!service) {
    return device;
  }

  status = IORegistryEntryCreateIterator(
    service,
    kIOServicePlane,
    (kIORegistryIterateParents | kIORegistryIterateRecursively),
    &iterator
  );

  if (status == kIOReturnSuccess) {
    io_service_t currentService;
    while ((currentService = IOIteratorNext(iterator)) && device == 0) {
      io_name_t serviceName;
      status = IORegistryEntryGetNameInPlane(currentService, kIOServicePlane, serviceName);
      if (status == kIOReturnSuccess && IOObjectConformsTo(currentService, kIOUSBDeviceClassName)) {
        device = currentService;
      } else {
        // Release the service object which is no longer needed
        (void) IOObjectRelease(currentService);
      }
    }

    // Release the iterator
    (void) IOObjectRelease(iterator);
  }

  return device;
}


static void ExtractUsbInformation(stSerialDevice *serialDevice, IOUSBDeviceInterface  **deviceInterface) {
  kern_return_t kernResult;
  UInt32 locationID;
  kernResult = (*deviceInterface)->GetLocationID(deviceInterface, &locationID);
  if (KERN_SUCCESS == kernResult) {
    snprintf(serialDevice->locationId, 11, "0x%08x", locationID);
  }

  UInt16 vendorID;
  kernResult = (*deviceInterface)->GetDeviceVendor(deviceInterface, &vendorID);
  if (KERN_SUCCESS == kernResult) {
    snprintf(serialDevice->vendorId, 7, "0x%04x", vendorID);
  }

  UInt16 productID;
  kernResult = (*deviceInterface)->GetDeviceProduct(deviceInterface, &productID);
  if (KERN_SUCCESS == kernResult) {
    snprintf(serialDevice->productId, 7, "0x%04x", productID);
  }
}


static kern_return_t FindModems(io_iterator_t *matchingServices) {
    kern_return_t     kernResult;
    CFMutableDictionaryRef  classesToMatch;
    classesToMatch = IOServiceMatching(kIOSerialBSDServiceValue);
    if (classesToMatch != NULL) {
        CFDictionarySetValue(
          classesToMatch,
          CFSTR(kIOSerialBSDTypeKey),
          CFSTR(kIOSerialBSDAllTypes)
        );
    }

    kernResult = IOServiceGetMatchingServices(kIOMasterPortDefault, classesToMatch, matchingServices);

    return kernResult;
}

static stDeviceListItem* GetSerialDevices() {
  kern_return_t kernResult;
  io_iterator_t serialPortIterator;
  char bsdPath[MAXPATHLEN];

  FindModems(&serialPortIterator);

  io_service_t modemService;
  kernResult = KERN_FAILURE;
  Boolean modemFound = false;

  // Initialize the returned path
  *bsdPath = '\0';

  stDeviceListItem* devices = NULL;
  stDeviceListItem* lastDevice = NULL;
  int length = 0;

  while ((modemService = IOIteratorNext(serialPortIterator))) {
    CFTypeRef bsdPathAsCFString;
    bsdPathAsCFString = IORegistryEntrySearchCFProperty(
      modemService,
      kIOServicePlane,
      CFSTR(kIOCalloutDeviceKey),
      kCFAllocatorDefault,
      kIORegistryIterateRecursively);

    if (bsdPathAsCFString) {
      Boolean result;

      // Convert the path from a CFString to a C (NUL-terminated)
      result = CFStringGetCString((CFStringRef) bsdPathAsCFString,
                    bsdPath,
                    sizeof(bsdPath),
                    kCFStringEncodingUTF8);
      CFRelease(bsdPathAsCFString);

      if (result) {
        stDeviceListItem *deviceListItem = (stDeviceListItem*) malloc(sizeof(stDeviceListItem));
        stSerialDevice *serialDevice = &(deviceListItem->value);
        strcpy(serialDevice->port, bsdPath);
        memset(serialDevice->locationId, 0, sizeof(serialDevice->locationId));
        memset(serialDevice->vendorId, 0, sizeof(serialDevice->vendorId));
        memset(serialDevice->productId, 0, sizeof(serialDevice->productId));
        serialDevice->manufacturer[0] = '\0';
        serialDevice->serialNumber[0] = '\0';
        deviceListItem->next = NULL;
        deviceListItem->length = &length;

        if (devices == NULL) {
          devices = deviceListItem;
        } else {
          lastDevice->next = deviceListItem;
        }

        lastDevice = deviceListItem;
        length++;

        modemFound = true;
        kernResult = KERN_SUCCESS;

        uv_mutex_lock(&list_mutex);

        io_service_t device = GetUsbDevice(modemService);

        if (device) {
          CFStringRef manufacturerAsCFString = (CFStringRef) IORegistryEntryCreateCFProperty(device,
                      CFSTR(kUSBVendorString),
                      kCFAllocatorDefault,
                      0);

          if (manufacturerAsCFString) {
            Boolean result;
            char    manufacturer[MAXPATHLEN];

            // Convert from a CFString to a C (NUL-terminated)
            result = CFStringGetCString(manufacturerAsCFString,
                          manufacturer,
                          sizeof(manufacturer),
                          kCFStringEncodingUTF8);

            if (result) {
              strcpy(serialDevice->manufacturer, manufacturer);
            }

            CFRelease(manufacturerAsCFString);
          }

          CFStringRef serialNumberAsCFString = (CFStringRef) IORegistryEntrySearchCFProperty(device,
                      kIOServicePlane,
                      CFSTR(kUSBSerialNumberString),
                      kCFAllocatorDefault,
                      kIORegistryIterateRecursively);

          if (serialNumberAsCFString) {
            Boolean result;
            char    serialNumber[MAXPATHLEN];

            // Convert from a CFString to a C (NUL-terminated)
            result = CFStringGetCString(serialNumberAsCFString,
                          serialNumber,
                          sizeof(serialNumber),
                          kCFStringEncodingUTF8);

            if (result) {
              strcpy(serialDevice->serialNumber, serialNumber);
            }

            CFRelease(serialNumberAsCFString);
          }

          IOCFPlugInInterface **plugInInterface = NULL;
          SInt32        score;
          HRESULT       res;

          IOUSBDeviceInterface  **deviceInterface = NULL;

          kernResult = IOCreatePlugInInterfaceForService(device, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
                               &plugInInterface, &score);

          if ((kIOReturnSuccess != kernResult) || !plugInInterface) {
            continue;
          }

          // Use the plugin interface to retrieve the device interface.
          res = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                               (LPVOID*) &deviceInterface);

          // Now done with the plugin interface.
          (*plugInInterface)->Release(plugInInterface);

          if (res || deviceInterface == NULL) {
            continue;
          }

          // Extract the desired Information
          ExtractUsbInformation(serialDevice, deviceInterface);

          // Release the Interface
          (*deviceInterface)->Release(deviceInterface);

          // Release the device
          (void) IOObjectRelease(device);
        }

        uv_mutex_unlock(&list_mutex);
      }
    }

    // Release the io_service_t now that we are done with it.
    (void) IOObjectRelease(modemService);
  }

  IOObjectRelease(serialPortIterator);  // Release the iterator.

  return devices;
}

void EIO_List(uv_work_t* req) {
  ListBaton* data = static_cast<ListBaton*>(req->data);
  if (!lockInitialised) {
    uv_mutex_init(&list_mutex);
    lockInitialised = TRUE;
  }

  stDeviceListItem* devices = GetSerialDevices();
  if (*(devices->length) > 0) {
    stDeviceListItem* next = devices;

    for (int i = 0, len = *(devices->length); i < len; i++) {
      stSerialDevice device = (* next).value;

      ListResultItem* resultItem = new ListResultItem();
      resultItem->comName = device.port;

      if (*device.locationId) {
        resultItem->locationId = device.locationId;
      }
      if (*device.vendorId) {
        resultItem->vendorId = device.vendorId;
      }
      if (*device.productId) {
        resultItem->productId = device.productId;
      }
      if (*device.manufacturer) {
        resultItem->manufacturer = device.manufacturer;
      }
      if (*device.serialNumber) {
        resultItem->serialNumber = device.serialNumber;
      }
      data->results.push_back(resultItem);

      stDeviceListItem* current = next;

      if (next->next != NULL) {
        next = next->next;
      }

      free(current);
    }
  }
}
