#!/bin/sh
set -eu
umask 077
[ "$#" -eq 3 ] || { printf 'Usage: %s SIGNED_APK LOCAL_FIXTURE_KUBECONFIG NEW_EVIDENCE_DIRECTORY\n' "$0" >&2; exit 2; }
sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
for tool in node lsof; do command -v "$tool" >/dev/null 2>&1 || { printf 'Required test tool is missing: %s\n' "$tool" >&2; exit 2; }; done
apk=$1
source=$2
out=$3
adbPort=5039
consolePort=5680
serial="emulator-$consolePort"
[ -f "$apk" ] && [ -f "$source" ] || { printf 'APK and fixture kubeconfig must exist.\n' >&2; exit 2; }
for tool in platform-tools/adb emulator/emulator cmdline-tools/latest/bin/avdmanager build-tools/36.0.0/aapt; do
  [ -x "$sdk/$tool" ] || { printf 'Required Android SDK tool is missing: %s\n' "$tool" >&2; exit 2; }
done
node - "$source" <<'CHECK'
const fs=require('node:fs'),text=fs.readFileSync(process.argv[2],'utf8');
if((text.match(/^\s*server:/gm)||[]).length!==1||!/^\s*server:\s*https:\/\/127\.0\.0\.1:\d+\s*$/m.test(text)||/^\s*exec:/m.test(text))
    throw Error('Use the owned single-cluster loopback TLS fixture without exec authentication.');
CHECK
for port in "$adbPort" "$consolePort" "$((consolePort + 1))"; do
  if lsof -n -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1; then printf 'Private Android test port occupied: %s\n' "$port" >&2; exit 1; fi
done
mkdir "$out"
"$sdk/build-tools/36.0.0/aapt" dump badging "$apk" > "$out/badging.txt"
[ "$(sed -n "s/^package: name='\([^']*\)'.*/\1/p" "$out/badging.txt")" = "berlin.yuna.podlord" ] || { printf 'Unexpected APK identity.\n' >&2; exit 2; }
mkdir -p "$HOME/.cache/podlord"
tmp=$(mktemp -d "$HOME/.cache/podlord/private-android-release.XXXXXX")
export ANDROID_SDK_ROOT="$sdk" ANDROID_HOME="$sdk" ANDROID_USER_HOME="$tmp/user" ANDROID_EMULATOR_HOME="$tmp/emulator" ANDROID_AVD_HOME="$tmp/avd" ANDROID_ADB_SERVER_PORT="$adbPort" ADB_LOCAL_TRANSPORT_MAX_PORT=0 ADB_MDNS_AUTO_CONNECT=0
if [ -n "${JAVA_HOME:-}" ]; then PATH="$JAVA_HOME/bin:$PATH"; export PATH; fi
mkdir -p "$ANDROID_USER_HOME" "$ANDROID_EMULATOR_HOME" "$ANDROID_AVD_HOME"
name="podlord-private-$(basename "$tmp" | tr '[:upper:]' '[:lower:]')"
emupid=""
ownedadb=0
cleanup() {
  result=$?
  testResult=$result
  cleanupFailed=0
  trap - 0
  if [ -n "$emupid" ]; then
    "$sdk/platform-tools/adb" -P "$adbPort" -s "$serial" logcat -d --pid="$(cat "$out/product-pid.txt" 2>/dev/null)" > "$out/product-final-logcat.txt" 2>&1 || :
    "$sdk/platform-tools/adb" -P "$adbPort" -s "$serial" emu kill > "$out/emulator-stop.log" 2>&1 || :
    kill "$emupid" 2>/dev/null || :
    wait "$emupid" 2>/dev/null || :
  fi
  if [ "$ownedadb" = 1 ]; then "$sdk/platform-tools/adb" -P "$adbPort" kill-server > "$out/adb-stop.log" 2>&1 || :; fi
  node -e 'const fs=require("node:fs"),p=process.argv[1];if(!p.startsWith(process.env.HOME+"/.cache/podlord/private-android-release."))throw Error("Unexpected cleanup path");fs.rmSync(p,{recursive:true});' "$tmp" || cleanupFailed=1
  for port in "$adbPort" "$consolePort" "$((consolePort + 1))"; do
    if lsof -n -iTCP:"$port" -sTCP:LISTEN > "$out/cleanup-port-$port.txt" 2>&1; then cleanupFailed=1; fi
  done
  node -e 'const fs=require("node:fs");fs.writeFileSync(process.argv[1]+"/cleanup.json",JSON.stringify({status:Number(process.argv[2])===0?"completed":"failed",testExit:Number(process.argv[3]),at:new Date().toISOString(),scope:"Owned emulator/ADB stopped, private AVD directory removed and private ports checked; unrelated devices and profiles untouched"})+"\n")' "$out" "$cleanupFailed" "$testResult"
  [ "$cleanupFailed" -eq 0 ] || result=1
  exit "$result"
}
trap cleanup 0
trap 'exit 130' INT TERM
printf 'no\n' | "$sdk/cmdline-tools/latest/bin/avdmanager" create avd -n "$name" -k 'system-images;android-36;google_apis;arm64-v8a' -p "$tmp/device" > "$out/avd-create.log" 2>&1
"$sdk/platform-tools/adb" -P "$adbPort" --one-device "$serial" start-server > "$out/adb-start.log" 2>&1
ownedadb=1
"$sdk/emulator/emulator" -avd "$name" -port "$consolePort" -no-window -no-audio -no-snapshot -no-boot-anim -gpu swiftshader -memory 2048 -cores 4 > "$out/emulator.log" 2>&1 &
emupid=$!
ready=0
for attempt in $(seq 1 90); do
  if ! kill -0 "$emupid" 2>/dev/null; then tail -n 16 "$out/emulator.log"; exit 1; fi
  if [ "$("$sdk/platform-tools/adb" -P "$adbPort" -s "$serial" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; then ready=1; break; fi
  sleep 2
done
if [ "$ready" != 1 ]; then "$sdk/platform-tools/adb" -P "$adbPort" devices -l; tail -n 12 "$out/emulator.log"; exit 1; fi
adb() { "$sdk/platform-tools/adb" -P "$adbPort" -s "$serial" "$@"; }
adb shell input keyevent 82
adb install --no-streaming "$apk" > "$out/install.log" 2>&1
activity=$(sed -n "s/^launchable-activity: name='\([^']*\)'.*/\1/p" "$out/badging.txt")
if [ -z "$activity" ]; then printf 'No launchable activity in signed APK\n' >&2; exit 1; fi
adb shell am start -W -n "berlin.yuna.podlord/$activity" > "$out/startup.txt"
adb shell pidof berlin.yuna.podlord > "$out/product-pid.txt"
adb root > "$out/adb-root-ingress.txt" 2>&1
adb wait-for-device
[ -f "$source" ]
node - "$source" "$tmp/emulator-kubeconfig" <<'CONFIG'
const fs=require('node:fs'),text=fs.readFileSync(process.argv[2],'utf8');
const server=/^(\s*server:\s*https:\/\/)127\.0\.0\.1:(\d+)\s*$/m;
if((text.match(/^\s*server:/gm)||[]).length!==1||!server.test(text)||/^\s*exec:/m.test(text))throw Error('Fixture changed after validation');
// The emulator host alias avoids ADB reverse mappings disappearing during the journey.
fs.writeFileSync(process.argv[3],text.replace(server,'$110.0.2.2:$2'),{mode:0o600});
CONFIG
private="/data/user/0/berlin.yuna.podlord/files/release-fixture.kubeconfig"
uid=$(adb shell stat -c %u /data/user/0/berlin.yuna.podlord | tr -d '\r')
adb push "$tmp/emulator-kubeconfig" "$private" > "$out/fixture-push.log" 2>&1
adb shell chown "$uid:$uid" "$private"
adb shell chmod 600 "$private"
node - "$out" "$sdk/platform-tools/adb" "$adbPort" "$serial" <<'JOURNEY'
const fs=require('node:fs'),cp=require('node:child_process'),path=require('node:path');
const [out,adb,port,device]=process.argv.slice(2), serial=['-P',port,'-s',device];
const run=(...args)=>cp.execFileSync(adb,[...serial,...args],{encoding:'utf8',timeout:30000,maxBuffer:4000000});
const decode=s=>s.replaceAll('&quot;','"').replaceAll('&apos;',"'").replaceAll('&lt;','<').replaceAll('&gt;','>').replaceAll('&#10;','\n').replaceAll('&#13;','\r').replaceAll('&#9;','\t').replaceAll('&amp;','&');
const parseNodes=xml=>[...xml.matchAll(/<node\b(?:[^"'<>]|"[^"]*"|'[^']*')*>/g)].map(m=>Object.fromEntries([...m[0].matchAll(/([\w-]+)=(["'])([\s\S]*?)\2/g)].map(a=>[a[1],decode(a[3])])));
require('node:assert/strict').deepEqual(parseNodes(`<node text='key: "value > other" &amp; &apos;' resource-id="yaml"/><node text="&amp;lt;"/>`),[{text:`key: "value > other" & '`, 'resource-id':'yaml'},{text:'&lt;'}]);
require('node:assert/strict').equal(decode('&#10;&#13;&#9;&amp;#10;'),'\n\r\t&#10;');
let xml='',nodes=[];const steps=[];
function state(){
 const read=()=>{run('wait-for-device');run('shell','uiautomator','dump','/sdcard/podlord-journey.xml');return run('shell','cat','/sdcard/podlord-journey.xml');};
 try{xml=read();}catch(error){
  fs.appendFileSync(path.join(out,'transport-read-recovery.log'),String(error)+'\n');
  run('wait-for-device');
  const expected=fs.readFileSync(path.join(out,'product-pid.txt'),'utf8').trim();
  if(run('shell','pidof','berlin.yuna.podlord').trim()!==expected)throw Error('Product exited or restarted during transport recovery');
  xml=read();
 }
 nodes=parseNodes(xml);return nodes;
}
function target(id){
 let scrolled=false;
 const bounds=n=>n?.bounds?.match(/\[(\d+),(\d+)\]\[(\d+),(\d+)\]/)?.slice(1).map(Number);
 for(let attempt=0;attempt<8;attempt++){
  const n=nodes.find(n=>n['resource-id']?.endsWith('.'+id)&&n.enabled==='true'&&bounds(n)?.[2]>bounds(n)?.[0]&&bounds(n)?.[3]-bounds(n)?.[1]>=18&&(()=>{
    const b=bounds(n), nav=n['resource-id']?.includes('.landscapeNavigation.'), side=n['resource-id']?.includes('.ResourceSidebar_');
    const inspector=n['resource-id']?.includes('.workspaceInspector.');
    const host=inspector?nodes.find(x=>x['resource-id']?.endsWith('.workspaceInspector')):nav?nodes.find(x=>x['resource-id']?.includes('.landscapeNavigation.ScrollBar_')&&bounds(x)?.[3]-bounds(x)?.[1]>50):side?nodes.find(x=>x['resource-id']?.endsWith('.sessionHealthStrip')):null;
    const clip=bounds(host);return !clip||(b[1]>=clip[1]&&b[3]<=clip[3]);
  })());
  if(n){if(!scrolled)return n;const before=n.bounds;state();const stable=nodes.find(n=>n['resource-id']?.endsWith('.'+id)&&n.bounds===before&&n.enabled==='true');if(stable)return stable;continue;}
  const nav=/WorkspaceButton|portForwardTasksButton|workspaceActionsButton|toggleSidebar/.test(id);
  const inspector=nodes.find(n=>n['resource-id']?.endsWith('.'+id))?.['resource-id']?.includes('.workspaceInspector.');
  const host=inspector?nodes.find(n=>n['resource-id']?.endsWith('.workspaceInspector')):nav?nodes.find(n=>n['resource-id']?.includes('.landscapeNavigation.ScrollBar_')&&bounds(n)?.[3]-bounds(n)?.[1]>50):nodes.find(n=>n['resource-id']?.endsWith('.sessionHealthStrip'));
  const b=bounds(host);if(!b)break;
  const x=inspector?b[0]+8:nav?Math.max(10,b[0]-16):Math.floor((b[2]+Math.max(...nodes.map(n=>bounds(n)?.[2]||0)))/2);
  const reverse=id==='toggleLandscapeFilters'?attempt<4:attempt>=4;
  run('shell','input','swipe',String(x),String(reverse?b[1]+12:b[3]-12),String(x),String(reverse?b[3]-12:b[1]+12),'350');scrolled=true;state();
 }
 capture('unreachable-'+id);throw Error('Public UI target unavailable after scrolling: '+id);
}
function tap(n){const b=n.bounds.match(/\[(\d+),(\d+)\]\[(\d+),(\d+)\]/).slice(1).map(Number);if(b[2]<=b[0]||b[3]<=b[1])throw Error('Target outside viewport');const point=[Math.floor((b[0]+b[2])/2),Math.floor((b[1]+b[3])/2)];run('shell','input','tap',...point.map(String));state();const after=nodes.find(x=>x['resource-id']===n['resource-id']);fs.appendFileSync(path.join(out,'input-events.jsonl'),JSON.stringify({id:n['resource-id'],bounds:n.bounds,point,beforeChecked:n.checked,afterBounds:after?.bounds,afterChecked:after?.checked,focused:nodes.filter(x=>x.focused==='true').map(x=>({id:x['resource-id'],text:x.text,description:x['content-desc']}))})+'\n');}
function click(id){tap(target(id));}
function capture(name){fs.writeFileSync(path.join(out,name+'.xml'),xml);fs.writeFileSync(path.join(out,name+'.png'),cp.execFileSync(adb,[...serial,'exec-out','screencap','-p'],{timeout:30000,maxBuffer:4000000}));}
process.on('uncaughtException',error=>{
 fs.writeFileSync(path.join(out,'journey.json'),JSON.stringify({status:'failed',steps,error:String(error)},null,2)+'\n');
 try{capture('failure');state();capture('failure-settled');fs.writeFileSync(path.join(out,'failure-nodes.json'),JSON.stringify(nodes,null,2)+'\n')}catch(captureError){console.error(String(captureError))}
 const pid=fs.readFileSync(path.join(out,'product-pid.txt'),'utf8').trim();
 const diagnostics=[['system-logcat',['logcat','-b','all','-d']],['activity',['shell','dumpsys','activity','top']],['window',['shell','dumpsys','window','windows']],['input',['shell','dumpsys','input']],['input-method',['shell','dumpsys','input_method']]];
 if(/^\d+$/.test(pid))diagnostics.push(['process',['shell','cat','/proc/'+pid+'/status']],['native-backtrace',['shell','debuggerd','-b',pid]]);
 for(const [name,args] of diagnostics){try{fs.writeFileSync(path.join(out,'failure-'+name+'.txt'),run(...args))}catch(diagnosticError){fs.writeFileSync(path.join(out,'failure-'+name+'.txt'),String(diagnosticError)+'\n'+(diagnosticError.stdout||'')+(diagnosticError.stderr||''))}}
 console.error(error);process.exitCode=1;
});
waitNode('sourcePath',n=>n.enabled==='true'&&!/\[0,0\]\[0,0\]/.test(n.bounds));click('sourcePath');run('shell','input','text','/data/user/0/berlin.yuna.podlord/files/release-fixture.kubeconfig');state();run('shell','input','keyevent','4');state();waitNode('sourcePath',n=>n.text==='/data/user/0/berlin.yuna.podlord/files/release-fixture.kubeconfig');click('importButton');state();
waitNode('openContext',n=>n.enabled==='true');click('openContext');
run('shell','settings','put','system','accelerometer_rotation','0');run('shell','settings','put','system','user_rotation','1');state();
const deadline=Date.now()+120000;let count=0;
do{state();const match=nodes.map(n=>n['content-desc']||n.text||'').join('\n').match(/visible: \d+\/(\d+)/);count=match?Number(match[1]):0;const health=nodes.find(n=>n['resource-id']?.endsWith('.sessionHealthStrip'));if(count>=1000&&health?.['content-desc']?.startsWith('Session health:'))break;count=0;}while(Date.now()<deadline);
if(count<1000){capture('load-failure');throw Error('Real cluster cache did not fill');}steps.push({step:'Real TLS source import and cached resources',resources:count});run('shell','settings','put','system','user_rotation','0');state();capture('loaded-portrait');
run('shell','settings','put','system','accelerometer_rotation','0');run('shell','settings','put','system','user_rotation','1');state();capture('loaded-landscape');
click('toggleLandscapeFilters');state();capture('radar-and-filters');
click('problemsOnly');if(target('problemsOnly').checked!=='true')throw Error('Problems filter did not enable');capture('problems-filter');const filtered=nodes.map(n=>n['content-desc']||'').join('\n').match(/(\d+) matching resources/);if(!filtered||Number(filtered[1])<=0||Number(filtered[1])>=count)throw Error('Problems filter did not reduce cached matches');click('problemsOnly');if(target('problemsOnly').checked!=='false')throw Error('Problems filter did not disable');steps.push({step:'Problems filter enables, reduces cached matches and disables',matches:Number(filtered[1])});
click('toggleLandscapeFilters');
for(const id of ['eventsWorkspaceButton','portForwardTasksButton','settingsWorkspaceButton','resourcesWorkspaceButton']){state();click(id);if(target(id).checked!=='true')throw Error('Navigation did not select '+id);capture(id);steps.push({step:id});}

function waitNode(id,predicate,timeout=30000){
 const until=Date.now()+timeout;
 do{state();const n=nodes.find(n=>n['resource-id']?.endsWith('.'+id));if(n&&predicate(n))return n;}while(Date.now()<until);
 capture('waiting-'+id);throw Error('Public state did not arrive: '+id);
}
function search(value){
 state();if(!nodes.some(n=>n['resource-id']?.endsWith('.resourceFilter')&&n.enabled==='true'&&n.bounds!=='[0,0][0,0]'))click('workspaceSearchButton');
 capture('search-open');
 click('resourceFilter');run('shell','input','keyevent','123',...Array(64).fill('67'));
 if(value)run('shell','input','text',value);
 run('shell','input','keyevent','4');state();click('workspaceSearchButton');
}
function describe(id){const n=nodes.find(n=>n['resource-id']?.endsWith('.'+id));return n?[n.text,n['content-desc']].filter(Boolean).join('\n'):'';}
search('visual-config-0001');
waitNode('cell_0_0',n=>(n.text+' '+n['content-desc']).includes('visual-config-0001'));
click('cell_0_0');
waitNode('inspectorResourceName',n=>(n.text+' '+n['content-desc']).includes('ConfigMap/visual-config-0001'));
capture('configmap-inspector-overview');click('yamlButton');waitNode('editYaml',n=>n.enabled==='true');
capture('configmap-yaml-fresh');steps.push({step:'Touch row opens ConfigMap inspector and fresh YAML enables explicit editing'});
click('editYaml');click('inspectorYaml');run('shell','input','keyevent','123','66');run('shell','input','text',"'#%sandroid-local-draft'");run('shell','input','keyevent','4');state();
waitNode('inspectorYaml',n=>(n.text+' '+n['content-desc']).includes('android-local-draft'));
waitNode('leaveYamlEdit',n=>(n.text+' '+n['content-desc']).includes('Discard draft'));
click('closeInspector');waitNode('discardStay',n=>n.enabled==='true');capture('yaml-stay-discard');click('discardStay');
waitNode('leaveYamlEdit',n=>(n.text+' '+n['content-desc']).includes('Discard draft'));click('closeInspector');click('discardAccept');
click('workspaceSearchButton');run('shell','input','keyevent','122','4');waitNode('resourceFilter',n=>(n.text+' '+n['content-desc']).includes('visual-config-0001'));capture('filter-restored-after-discard');
steps.push({step:'Local YAML edit, Stay and explicit Discard preserve the cached query without applying'});
search('visual-multi-container');waitNode('cell_0_0',n=>(n.text+' '+n['content-desc']).includes('visual-multi-container'));click('cell_0_0');click('logsButton');
waitNode('logContainers',n=>nodes.some(child=>child['resource-id']?.startsWith(n['resource-id']+'.')&&(child.text||child['content-desc']||'').toLowerCase()==='all'));
click('pauseLogs');
const logDeadline=Date.now()+30000;let bothStreams=false;
do{
 state();
 const entries=nodes.filter(n=>/\.logEntry_\d+$/.test(n['resource-id']||'')).sort((a,b)=>Number(b['resource-id'].split('_').at(-1))-Number(a['resource-id'].split('_').at(-1)));
 if(entries.length)target(entries[0]['resource-id'].split('.').at(-1));
 const visible=nodes.filter(n=>/\.logEntry_\d+$/.test(n['resource-id']||'')&&(()=>{const b=n.bounds?.match(/\[(\d+),(\d+)\]\[(\d+),(\d+)\]/)?.slice(1).map(Number);return b&&b[2]>b[0]&&b[3]-b[1]>=24})()).map(n=>n.text+' '+n['content-desc']).join('\n');
 bothStreams=visible.includes('[alpha]')&&visible.includes('[beta]');
}while(!bothStreams&&Date.now()<logDeadline);
if(!bothStreams){capture('missing-container-output');throw Error('Default All logs did not visibly include both real container streams');}
capture('multi-container-all-logs');
if(/a container name must be specified|400 BadRequest/.test(nodes.map(n=>n.text+' '+n['content-desc']).join('\n')))throw Error('Multi-container logs were requested without container selection');
steps.push({step:'Actual multi-container Pod logs load with the default All selector'});
click('terminalButton');click('terminalConnect');
waitNode('terminalStatus',n=>/\bconnected\b/i.test(n.text+' '+n['content-desc']));capture('container-terminal-connected');
waitNode('terminalSurface',n=>n.focused==='true');run('shell','input','text','echo%sPODLORD_ANDROID_E2E_OK');run('shell','input','keyevent','66');state();run('shell','input','keyevent','4');state();
const terminalDeadline=Date.now()+20000;let output=false;
do{state();output=describe('terminalSurface').split(/\r?\n|\u2029/).some(line=>line.trim()==='PODLORD_ANDROID_E2E_OK');if(output)break;}while(Date.now()<terminalDeadline);
capture('container-terminal-echo');
if(!output)throw Error('The real container shell output marker was not exposed by the terminal');
run('shell','input','text','exit');run('shell','input','keyevent','66');state();
waitNode('terminalStatus',n=>/\b(closed|exited|disconnected)\b/i.test(n.text+' '+n['content-desc']));
steps.push({step:'Native Kubernetes PTY connects, executes echo, returns output and exits without kubectl'});

fs.writeFileSync(path.join(out,'journey.json'),JSON.stringify({status:'passed',scope:'Real installed product, owned K3s TLS source and public Android accessibility/touch actions; not all mobile features or live cloud OAuth',steps},null,2)+'\n');console.log(JSON.stringify({androidRealJourney:'passed',resources:count,steps:steps.length}));
JOURNEY

adb shell settings put system user_rotation 0
sleep 2
adb shell wm size > "$out/display.txt"
adb shell uiautomator dump /sdcard/podlord-release-ui.xml > "$out/ui-dump.log" 2>&1
adb pull /sdcard/podlord-release-ui.xml "$out/portrait-ui.xml" > "$out/ui-pull.log" 2>&1
adb exec-out screencap -p > "$out/portrait.png"
adb shell settings put system accelerometer_rotation 0
adb shell settings put system user_rotation 1
sleep 3
adb shell uiautomator dump /sdcard/podlord-release-ui.xml >> "$out/ui-dump.log" 2>&1
adb pull /sdcard/podlord-release-ui.xml "$out/landscape-ui.xml" >> "$out/ui-pull.log" 2>&1
adb exec-out screencap -p > "$out/landscape.png"
adb shell pm path berlin.yuna.podlord > "$out/package-paths.txt"
adb logcat -d --pid="$(tr -d '\r\n' < "$out/product-pid.txt")" > "$out/runtime-logcat.txt" 2>&1 || printf 'Product log capture failed\n' > "$out/log-capture-failure.txt"
adb root > "$out/adb-root.txt" 2>&1
adb wait-for-device
base=$(sed -n 's/^package://p' "$out/package-paths.txt" | head -n 1 | tr -d '\r')
adb shell du -sk "$(dirname "$base")" /data/user/0/berlin.yuna.podlord /data/user_de/0/berlin.yuna.podlord > "$out/installed-blocks-kib.txt"
adb shell "find '$(dirname "$base")' /data/user/0/berlin.yuna.podlord /data/user_de/0/berlin.yuna.podlord -type f -exec stat -c '%s %n' {} \\;" > "$out/installed-regular-file-bytes.txt"
adb shell am force-stop berlin.yuna.podlord
node - "$out" <<'NODE'
const fs=require('node:fs'),path=require('node:path');const dir=process.argv[2];
const files=fs.readFileSync(path.join(dir,'installed-regular-file-bytes.txt'),'utf8').trim().split(/\r?\n/);
const bytes=files.reduce((sum,line)=>{const n=Number(line.split(' ')[0]);if(!Number.isSafeInteger(n))throw Error('Invalid installed file measurement');return sum+n},0);
const data={status:bytes<=100000000?'passed':'failed',scope:'Actual signed APK install, initial portrait/OS-rotated landscape capture, installed code and app-private regular-file logical sum on disposable Android 36 ARM64 emulator',installedRegularFileBytes:bytes,installedLimitBytes:100000000,files:files.length,productLaunched:true,device:'headless emulator; not physical hardware',cleanup:'pending trap'};
fs.writeFileSync(path.join(dir,'result.json'),JSON.stringify(data,null,2)+'\n');console.log(JSON.stringify(data));
if(data.status!=='passed')process.exitCode=1;
NODE
