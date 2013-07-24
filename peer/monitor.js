var dgram = require('dgram')
  , http = require('http')
  , https = require('https')
  , crypto = require('crypto');

function say (message) {
  var sock = dgram.createSocket('udp4'), buffer = new Buffer(message);
  sock.send(buffer, 0, buffer.length, 7979, "127.0.0.1", function () { sock.close() });
}

http.createServer(function (req, res) {
  res.writeHead(200, {'Content-Type': 'text/plain'});
  res.end('Hello World\n');
}).listen(7393, '127.0.0.1');

say('started');

crypto.randomBytes(256, function(ex, buf) {
  if (ex) throw ex;
  say("SHUTDOWN IS: " + buf.toString('base64'));
  process.stdout.write(buf.toString('base64') + '\n' + 7393 + '\n');
  process.stdout.on('drain', function () {
    say('written');
  });
});

function ping () {
  https.get({ host: 'www.prettyrobots.com', path: '/synapse/?0.0.2' }, function () {
  });
}

ping();

setInterval(ping, 1000 * 60 * 15);
