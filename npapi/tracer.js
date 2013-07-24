var dgram = require('dgram')
  , server = dgram.createSocket('udp4');

server.on('message', function (msg) {
  console.log(msg.toString());
});

server.bind(7979, '127.0.0.1');
