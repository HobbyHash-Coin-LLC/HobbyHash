var crypto = require('crypto');

function reverseHex(hex) {
    return Buffer.from(String(hex || '').toLowerCase(), 'hex').reverse().toString('hex');
}

exports.getRoot = function (rpcData, generateTxHash) {
    var hashes = [String(generateTxHash || '').toLowerCase()];
    rpcData.transactions.forEach(function (value) {
         if (value.txid !== undefined) {
             hashes.push(reverseHex(value.txid));
         } else {
             hashes.push(reverseHex(value.hash));
         }
     });
    if (hashes.length === 1) {
        return hashes[0];
    }
    while (hashes.length > 1) {
        var next = [];
        for (var i = 0; i < hashes.length; i += 2) {
            var left = hashes[i];
            var right = hashes[i + 1] || left;
            var data = Buffer.concat([Buffer.from(left, 'hex'), Buffer.from(right, 'hex')]);
            next.push(crypto.createHash('sha256').update(crypto.createHash('sha256').update(data).digest()).digest().toString('hex'));
        }
        hashes = next;
    }
    return hashes[0];
};
