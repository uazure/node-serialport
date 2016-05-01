#include "./serialport.h"
#include "./serialport_poller.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>

#if defined(MAC_OS_X_VERSION_10_4) && (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_4)
#include <IOKit/serial/ioss.h>
#endif

#if defined(__linux__)
#include <linux/serial.h>
#endif

struct UnixPlatformOptions : OpenBatonPlatformOptions {
  uint8_t vmin;
  uint8_t vtime;
};

OpenBatonPlatformOptions* ParsePlatformOptions(const v8::Local<v8::Object>& options) {
  Nan::HandleScope scope;

  UnixPlatformOptions* result = new UnixPlatformOptions();
  result->vmin = Nan::Get(options, Nan::New<v8::String>("vmin").ToLocalChecked()).ToLocalChecked()->ToInt32()->Int32Value();
  result->vtime = Nan::Get(options, Nan::New<v8::String>("vtime").ToLocalChecked()).ToLocalChecked()->ToInt32()->Int32Value();

  return result;
}

int ToBaudConstant(int baudRate);
int ToDataBitsConstant(int dataBits);
int ToStopBitsConstant(SerialPortStopBits stopBits);

void AfterOpenSuccess(int fd, Nan::Callback* dataCallback, Nan::Callback* disconnectedCallback, Nan::Callback* errorCallback) {
  delete dataCallback;
  delete errorCallback;
  delete disconnectedCallback;
}

int ToBaudConstant(int baudRate) {
  switch (baudRate) {
    case 0: return B0;
    case 50: return B50;
    case 75: return B75;
    case 110: return B110;
    case 134: return B134;
    case 150: return B150;
    case 200: return B200;
    case 300: return B300;
    case 600: return B600;
    case 1200: return B1200;
    case 1800: return B1800;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
#if defined(__linux__)
    case 460800: return B460800;
    case 500000: return B500000;
    case 576000: return B576000;
    case 921600: return B921600;
    case 1000000: return B1000000;
    case 1152000: return B1152000;
    case 1500000: return B1500000;
    case 2000000: return B2000000;
    case 2500000: return B2500000;
    case 3000000: return B3000000;
    case 3500000: return B3500000;
    case 4000000: return B4000000;
#endif
  }
  return -1;
}

int ToDataBitsConstant(int dataBits) {
  switch (dataBits) {
    case 8: default: return CS8;
    case 7: return CS7;
    case 6: return CS6;
    case 5: return CS5;
  }
  return -1;
}

void EIO_Open(uv_work_t* req) {
  OpenBaton* data = static_cast<OpenBaton*>(req->data);

  int flags = (O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_SYNC);
  int fd = open(data->path, flags);

  if (-1 == fd) {
    snprintf(data->errorString, sizeof(data->errorString), "Error: %s, cannot open %s", strerror(errno), data->path);
    return;
  }

  if (-1 == setup(fd, data)) {
    close(fd);
    return;
  }

  data->result = fd;
}

int setBaudRate(ConnectionOptionsBaton *data) {
  // lookup the standard baudrates from the table
  int baudRate = ToBaudConstant(data->baudRate);
  int fd = data->fd;

  // get port options
  struct termios options;
  tcgetattr(fd, &options);

  // If there is a custom baud rate on linux you can do the following trick with B38400
  #if defined(__linux__) && defined(ASYNC_SPD_CUST)
    if (baudRate == -1) {
      struct serial_struct serinfo;
      serinfo.reserved_char[0] = 0;
      if (-1 != ioctl(fd, TIOCGSERIAL, &serinfo)) {
        serinfo.flags &= ~ASYNC_SPD_MASK;
        serinfo.flags |= ASYNC_SPD_CUST;
        serinfo.custom_divisor = (serinfo.baud_base + (data->baudRate / 2)) / data->baudRate;
        if (serinfo.custom_divisor < 1)
          serinfo.custom_divisor = 1;

        ioctl(fd, TIOCSSERIAL, &serinfo);
        ioctl(fd, TIOCGSERIAL, &serinfo);
      } else {
        snprintf(data->errorString, sizeof(data->errorString), "Error: %s setting custom baud rate of %d", strerror(errno), data->baudRate);
        return -1;
      }

      // Now we use "B38400" to trigger the special baud rate.
      baudRate = B38400;
    }
  #endif

  // On OS X, starting with Tiger, we can set a custom baud rate with ioctl
  #if defined(MAC_OS_X_VERSION_10_4) && (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_4)
    if (-1 == baudRate) {
      speed_t speed = data->baudRate;
      if (-1 == ioctl(fd, IOSSIOSPEED, &speed)) {
        snprintf(data->errorString, sizeof(data->errorString), "Error: %s calling ioctl(.., IOSSIOSPEED, %ld )", strerror(errno), speed );
        return -1;
      } else {
        return 1;
      }
    }
  #endif

  // If we have a good baud rate set it and lets go
  if (-1 != baudRate) {
    cfsetospeed(&options, baudRate);
    cfsetispeed(&options, baudRate);
    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &options);
    return 1;
  }

  snprintf(data->errorString, sizeof(data->errorString), "Error baud rate of %d is not supported on your platform", data->baudRate);
  return -1;
}

void EIO_Update(uv_work_t* req) {
  ConnectionOptionsBaton* data = static_cast<ConnectionOptionsBaton*>(req->data);
  setBaudRate(data);
}

int setup(int fd, OpenBaton *data) {
  UnixPlatformOptions* platformOptions = static_cast<UnixPlatformOptions*>(data->platformOptions);

  int dataBits = ToDataBitsConstant(data->dataBits);
  if (-1 == dataBits) {
    snprintf(data->errorString, sizeof(data->errorString), "Invalid data bits setting %d", data->dataBits);
    return -1;
  }

  // Snow Leopard doesn't have O_CLOEXEC
  if (-1 == fcntl(fd, F_SETFD, FD_CLOEXEC)) {
    snprintf(data->errorString, sizeof(data->errorString), "Error %s Cannot open %s", strerror(errno), data->path);
    return -1;
  }

  // Copy the connection options into the ConnectionOptionsBaton to set the baud rate
  ConnectionOptionsBaton* connectionOptions = new ConnectionOptionsBaton();
  memset(connectionOptions, 0, sizeof(ConnectionOptionsBaton));
  connectionOptions->fd = fd;
  connectionOptions->baudRate = data->baudRate;

  if (-1 == setBaudRate(connectionOptions)) {
    strncpy(data->errorString, connectionOptions->errorString, sizeof(data->errorString));
    delete(connectionOptions);
    return -1;
  }
  delete(connectionOptions);

  // Get port configuration for modification
  struct termios options;
  tcgetattr(fd, &options);

  // IGNPAR: ignore bytes with parity errors
  options.c_iflag = IGNPAR;

  // ICRNL: map CR to NL (otherwise a CR input on the other computer will not terminate input)
  // Future potential option
  // options.c_iflag = ICRNL;
  // otherwise make device raw (no other input processing)

  // Specify data bits
  options.c_cflag &= ~CSIZE;
  options.c_cflag |= dataBits;

  options.c_cflag &= ~(CRTSCTS);

  if (data->rtscts) {
    options.c_cflag |= CRTSCTS;
    // evaluate specific flow control options
  }

  options.c_iflag &= ~(IXON | IXOFF | IXANY);

  if (data->xon) {
    options.c_iflag |= IXON;
  }

  if (data->xoff) {
    options.c_iflag |= IXOFF;
  }

  if (data->xany) {
    options.c_iflag |= IXANY;
  }

  switch (data->parity) {
  case SERIALPORT_PARITY_NONE:
    options.c_cflag &= ~PARENB;
    // options.c_cflag &= ~CSTOPB;
    // options.c_cflag &= ~CSIZE;
    // options.c_cflag |= CS8;
    break;
  case SERIALPORT_PARITY_ODD:
    options.c_cflag |= PARENB;
    options.c_cflag |= PARODD;
    // options.c_cflag &= ~CSTOPB;
    // options.c_cflag &= ~CSIZE;
    // options.c_cflag |= CS7;
    break;
  case SERIALPORT_PARITY_EVEN:
    options.c_cflag |= PARENB;
    options.c_cflag &= ~PARODD;
    // options.c_cflag &= ~CSTOPB;
    // options.c_cflag &= ~CSIZE;
    // options.c_cflag |= CS7;
    break;
  default:
    snprintf(data->errorString, sizeof(data->errorString), "Invalid parity setting %d", data->parity);
    return -1;
  }

  switch (data->stopBits) {
  case SERIALPORT_STOPBITS_ONE:
    options.c_cflag &= ~CSTOPB;
    break;
  case SERIALPORT_STOPBITS_TWO:
    options.c_cflag |= CSTOPB;
    break;
  default:
    snprintf(data->errorString, sizeof(data->errorString), "Invalid stop bits setting %d", data->stopBits);
    return -1;
  }

  options.c_cflag |= CLOCAL;  // ignore status lines
  options.c_cflag |= CREAD;   // enable receiver
  if (data->hupcl) {
    options.c_cflag |= HUPCL;  // drop DTR (i.e. hangup) on close
  }

  // Raw output
  options.c_oflag = 0;

  // ICANON makes partial lines not readable. It should be optional.
  // It works with ICRNL.
  options.c_lflag = 0;  // ICANON;

  options.c_cc[VMIN]= platformOptions->vmin;
  options.c_cc[VTIME]= platformOptions->vtime;

  tcflush(fd, TCIFLUSH);
  tcsetattr(fd, TCSANOW, &options);

  return 1;
}

void EIO_Write(uv_work_t* req) {
  QueuedWrite* queuedWrite = static_cast<QueuedWrite*>(req->data);
  WriteBaton* data = static_cast<WriteBaton*>(queuedWrite->baton);
  int bytesWritten = 0;

  do {
    errno = 0; // probably don't need this
    bytesWritten = write(data->fd, data->bufferData + data->offset, data->bufferLength - data->offset);
    if (-1 != bytesWritten) {
      // there wasn't an error, do the math on what we actually wrote and keep writing until finished
      data->offset += bytesWritten;
      continue;
    }

    // The write call was interrupted before anything was written, try again immediately.
    if (errno == EINTR) {
      // why try again right away instead of in another event loop?
      continue;
    }

    // Try again in another event loop
    if (errno == EAGAIN || errno == EWOULDBLOCK){
      return;
    }

    // EBAD would mean we're "disconnected"

    // a real error so lets bail
    snprintf(data->errorString, sizeof(data->errorString), "Error: %s, calling write", strerror(errno));
    return;
  } while (data->bufferLength > data->offset);
}

void EIO_Close(uv_work_t* req) {
  CloseBaton* data = static_cast<CloseBaton*>(req->data);
  if (-1 == close(data->fd)) {
    snprintf(data->errorString, sizeof(data->errorString), "Error: %s, unable to close fd %d", strerror(errno), data->fd);
  }
}

void EIO_Flush(uv_work_t* req) {
  FlushBaton* data = static_cast<FlushBaton*>(req->data);

  data->result = tcflush(data->fd, TCIFLUSH);
}

void EIO_Set(uv_work_t* req) {
  SetBaton* data = static_cast<SetBaton*>(req->data);

  int bits;
  ioctl(data->fd, TIOCMGET, &bits);

  bits &= ~(TIOCM_RTS | TIOCM_CTS | TIOCM_DTR | TIOCM_DSR);

  if (data->rts) {
    bits |= TIOCM_RTS;
  }

  if (data->cts) {
    bits |= TIOCM_CTS;
  }

  if (data->dtr) {
    bits |= TIOCM_DTR;
  }

  if (data->dsr) {
    bits |= TIOCM_DSR;
  }

  // TODO check these returns
  if (data->brk) {
    ioctl(data->fd, TIOCSBRK, NULL);
  } else {
    ioctl(data->fd, TIOCCBRK, NULL);
  }

  data->result = ioctl(data->fd, TIOCMSET, &bits);
}

void EIO_Drain(uv_work_t* req) {
  DrainBaton* data = static_cast<DrainBaton*>(req->data);

  data->result = tcdrain(data->fd);
}
