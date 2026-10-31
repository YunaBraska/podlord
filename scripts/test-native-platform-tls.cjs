'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const execute = promisify(execFile);
const [driver, openssl, scenario, faultShim] = process.argv.slice(2);
const cases = ['trusted', 'unknown_issuers', 'system_untrusted', 'wrong_ca', 'wrong_host', 'expired', 'invalid_peer',
    'invalid_ca', 'missing_peer', 'empty_host', 'null_host', 'no_errors', 'mixed_errors', 'revoked', 'canceled', 'delayed', 'cancel_pending',
    'fault_data', 'fault_certificates', 'fault_anchors', 'fault_name', 'fault_policy', 'fault_trust', 'fault_null_trust', 'fault_set_anchors', 'fault_exclusive_anchors'];
assert.ok(driver && openssl && cases.includes(scenario));
async function run() {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-platform-tls-'));
    const identity = path.basename(directory);
    const config = path.join(directory, 'openssl.cnf');
    const command = args => execute(openssl, args, { cwd: directory, timeout: 15000, env: { ...process.env, OPENSSL_CONF: config } });
    try {
        await fs.writeFile(config, '[req]\ndistinguished_name=subject\n[subject]\n[ca]\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\nsubjectKeyIdentifier=hash\n', { mode: 0o600 });
        for (const root of ['root', 'other']) {
            await command(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', `${root}.key`]);
            await command(['req', '-new', '-x509', '-key', `${root}.key`, '-out', `${root}.pem`, '-days', '1', '-subj', `/CN=Podlord ${root} ${identity}`, '-extensions', 'ca']);
        }
        await command(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', 'server.key']);
        await command(['req', '-new', '-key', 'server.key', '-out', 'server.csr', '-subj', '/CN=Podlord native TLS']);
        await fs.writeFile(path.join(directory, 'server.ext'), 'subjectAltName=DNS:localhost,IP:127.0.0.1\nextendedKeyUsage=serverAuth\nauthorityKeyIdentifier=keyid:always\n');
        let issuer = 'root';
        if (scenario === 'delayed' || scenario === 'cancel_pending') {
            issuer = 'intermediate';
            await command(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', 'intermediate.key']);
            await command(['req', '-new', '-key', 'intermediate.key', '-out', 'intermediate.csr', '-subj', `/CN=Podlord isolated intermediate ${identity}`]);
            await fs.writeFile(path.join(directory, 'intermediate.ext'), 'basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid:always\n');
            await command(['x509', '-req', '-in', 'intermediate.csr', '-CA', 'root.pem', '-CAkey', 'root.key', '-CAcreateserial', '-out', 'intermediate.pem', '-days', '1', '-extfile', 'intermediate.ext']);
        }
        if (scenario === 'expired') {
            await fs.writeFile(path.join(directory, 'index'), '');
            await fs.writeFile(path.join(directory, 'serial'), '1000\n');
            await fs.appendFile(config, '[signer]\ndatabase=index\nserial=serial\nnew_certs_dir=.\ndefault_md=sha256\npolicy=leaf_subject\n[leaf_subject]\ncommonName=supplied\n');
            await command(['ca', '-batch', '-name', 'signer', '-cert', 'root.pem', '-keyfile', 'root.key', '-in', 'server.csr',
                '-out', 'server.pem', '-startdate', '20200101000000Z', '-enddate', '20200102000000Z', '-extfile', 'server.ext']);
        } else {
            await command(['x509', '-req', '-in', 'server.csr', '-CA', `${issuer}.pem`, '-CAkey', `${issuer}.key`, '-CAcreateserial',
                '-out', 'server.pem', '-days', '1', '-extfile', 'server.ext']);
        }
        if (scenario === 'delayed' || scenario === 'cancel_pending') {
            await fs.writeFile(path.join(directory, 'peer-chain.pem'), Buffer.concat([
                await fs.readFile(path.join(directory, 'server.pem')),
                await fs.readFile(path.join(directory, 'intermediate.pem'))
            ]), { mode: 0o600 });
        }
        await fs.writeFile(path.join(directory, 'invalid.pem'), 'Not a certificate.\n');
        const input = {
            peer: path.join(directory, scenario === 'invalid_peer' ? 'invalid.pem' : ['delayed', 'cancel_pending'].includes(scenario) ? 'peer-chain.pem' : 'server.pem'),
            authorities: path.join(directory, scenario === 'wrong_ca' ? 'other.pem' : scenario === 'invalid_ca' ? 'invalid.pem' : 'root.pem'),
            name: scenario === 'wrong_host' ? 'different.invalid' : scenario === 'empty_host' ? '' : scenario === 'null_host' ? 'localhost\0different.invalid' : 'localhost',
            errors: scenario === 'unknown_issuers' ? [1, 9, 10, 11, 12, 17] : scenario === 'no_errors' ? [] : scenario === 'mixed_errors' ? [17, 22] : scenario === 'revoked' ? [13] : [17],
            expected: ['trusted', 'unknown_issuers', 'delayed'].includes(scenario), canceled: scenario === 'canceled', cancelAfterMs: scenario === 'cancel_pending' ? 500 : 0
        };
        if (scenario === 'system_untrusted') input.authorities = '';
        if (scenario === 'missing_peer') input.peer = '';
        const filename = path.join(directory, 'case.json');
        await fs.writeFile(filename, JSON.stringify(input), { mode: 0o600 });
        const env = { ...process.env, DYLD_LIBRARY_PATH: path.resolve(path.dirname(openssl), '../lib') + (process.env.DYLD_LIBRARY_PATH ? path.delimiter + process.env.DYLD_LIBRARY_PATH : '') };
        const observeNativeTrust = Boolean(faultShim) && ['delayed', 'cancel_pending'].includes(scenario);
        if (['delayed', 'cancel_pending'].includes(scenario)) assert.ok(observeNativeTrust, 'Async TLS cases require the test-only Security callback interposer.');
        if (scenario.startsWith('fault_') || observeNativeTrust) {
            assert.ok(faultShim && path.isAbsolute(faultShim));
            env.DYLD_INSERT_LIBRARIES = faultShim;
            env.PODLORD_TEST_PLATFORM_FAULT = observeNativeTrust ? 'trace' : scenario;
        }
        const result = await execute(driver, [filename], { timeout: 20000, env });
        if (observeNativeTrust) {
            assert.equal((result.stderr.match(/PODLORD_TRUST_COMPLETION scheduled delay_ms=1500/g) || []).length, 1,
                'The real Security completion must be deferred exactly once at the external callback boundary.');
            assert.equal((result.stderr.match(/PODLORD_TRUST_COMPLETION delivered/g) || []).length, 1,
                'The deferred real Security completion must be delivered exactly once.');
            if (scenario === 'cancel_pending') {
                const returned = result.stderr.indexOf('PODLORD_TRUST_DRIVER returned');
                const delivered = result.stderr.indexOf('PODLORD_TRUST_COMPLETION delivered');
                assert.ok(returned >= 0 && delivered > returned,
                    'Cancellation must return before the deferred Security completion is delivered.');
            }
        }
        process.stdout.write(result.stdout);
    } finally {
        await fs.rm(directory, { recursive: true, force: true });
    }
}
run().catch(error => {
    if (error.stdout) process.stderr.write(error.stdout);
    process.stderr.write(`${error.stderr || error.message}\n`);
    process.exitCode = 1;
});
