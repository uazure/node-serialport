'use strict';

var SerialPort = require('serialport');
var port = new SerialPort('/dev/cu.USBXXXX', {
  parser: SerialPort.parsers.readline('\r\n')
});

port.on('open',function(){
  port.write('AT\r\n');
  port.once('data', function(buffer){
    if (buffer.toString() !== 'OK') { throw 'Not OK' }
    this.write('AT+CMGF=1\r\n');
    port.once('data', function(buffer){
      if (buffer.toString() !== 'OK') { throw 'Not OK' }
      port.write('AT+CMGS="555555"\r\n');
      port.once('data', function(buffer){
        if (buffer.toString() !== 'OK') { throw 'Not OK' }
        console.log('done!');
      });
    });
  });
});
