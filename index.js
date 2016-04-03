function mcs(config) {
  this.config = config;
}

mcs.prototype.register = function() {
  return __mcsTCPClient(config.deviceId, config.deviceKey, function(data) {
    // Example: AAA,XXX,1459307476444,encodeByMD5,test
    global.eventStatus.emit(data.split(',')[3], data.split(data.split(',')[3] + ',')[1]);
  });
}

mcs.prototype.on = function(channel, cb) {
  return global.eventStatus.on(channel, cb);
}

mcs.prototype.emit = function(channel, value) {
  __mcsUploadData(channel, value)
}

module.exports.mcs = mcs;