import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';
import {pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';

// 参数 value 为一条结果；标准输出只传递结构化协议，不混入调试文本。
function reply(value) { process.stdout.write(JSON.stringify(value) + '\n'); }
const reaRoot = process.env.DIVA_REA_ROOT || path.join(process.env.APPDATA, 'npm/node_modules/rea-agents');
const require = createRequire(path.join(reaRoot, 'package.json'));
const frida = await import(pathToFileURL(require.resolve('frida')).href);
let session = null, script = null;
try {
    session = await frida.attach(Number(process.argv[2]));
    const extension = process.argv[3] ? '\n' + fs.readFileSync(process.argv[3], 'utf8') : '';
    script = await session.createScript(fs.readFileSync(new URL('./page_probe.js', import.meta.url), 'utf8') + extension);
    script.message.connect(message => {
        if (message.type === 'error') reply({event: '探针错误', error: message.description});
    });
    await script.load();
    reply({event: '就绪', version: frida.version || '17.23.2', readonly: !extension, extension: process.argv[3] || null});
    for await (const line of readline.createInterface({input: process.stdin})) {
        const request = JSON.parse(line);
        if (request.command === 'close') break;
        try {
            const methods = ['snapshot', 'details', 'outerinfo', 'observe', 'events', 'cancel', 'armparent', 'armlocate', 'code', 'observemodule', 'observeaction', 'armaction', 'observesibling', 'armnavigation', 'observeouter', 'observeactivation', 'armactivatesong', 'armfinish', 'observemenuwriter', 'observeouterroute', 'armhome'];
            const actionMethods = ['actioncontext', 'observeownership', 'ownershipresult', 'watchinput', 'inputevents', 'titleprofile', 'captureframe'];
            if (!methods.includes(request.command) && !actionMethods.includes(request.command)) throw new Error('未登记的探针命令');
            reply({id: request.id, snapshot: await script.exports[request.command](request.parameters || {})});
        }
        catch (error) { reply({id: request.id, error: String(error)}); }
    }
} catch (error) {
    reply({event: '失败', error: String(error)});
    process.exitCode = 1;
} finally {
    if (script) await script.unload().catch(() => {});
    if (session) await session.detach().catch(() => {});
}
