#!/bin/sh
set -eu
umask 077
[ "$#" -eq 3 ] || { printf 'Usage: %s SIGNED_APK LOCAL_FIXTURE_KUBECONFIG NEW_EVIDENCE_DIRECTORY\n' "$0" >&2; exit 2; }
sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
system_image=${PODLORD_ANDROID_SYSTEM_IMAGE:-system-images;android-36;google_apis;arm64-v8a}
case "$system_image" in
  'system-images;android-36;google_apis;arm64-v8a'|'system-images;android-36;google_apis_ps16k;arm64-v8a') ;;
  *) printf 'Unsupported Android test system image: %s\n' "$system_image" >&2; exit 2 ;;
esac
for tool in node lsof; do command -v "$tool" >/dev/null 2>&1 || { printf 'Required test tool is missing: %s\n' "$tool" >&2; exit 2; }; done
apk=$1
source=$2
out=$3
adbPort=5039
# Stay outside another ADB server's default emulator scan (ending at 5585).
consolePort=5680
serial="emulator-$consolePort"
case "${PODLORD_ANDROID_ADB_TRACE:-0}" in
  0) adbTrace="" ;;
  1) adbTrace="transport,shell,services,sockets" ;;
  *) printf 'PODLORD_ANDROID_ADB_TRACE must be 0 or 1\n' >&2; exit 2 ;;
esac
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
printf '%s\n' "$system_image" > "$out/system-image.txt"
"$sdk/build-tools/36.0.0/aapt" dump badging "$apk" > "$out/badging.txt"
[ "$(sed -n "s/^package: name='\([^']*\)'.*/\1/p" "$out/badging.txt")" = "berlin.yuna.podlord" ] || { printf 'Unexpected APK identity.\n' >&2; exit 2; }
mkdir -p "$HOME/.cache/podlord"
tmp=$(mktemp -d "$HOME/.cache/podlord/private-android-release.XXXXXX")
export ANDROID_SDK_ROOT="$sdk" ANDROID_HOME="$sdk" ANDROID_USER_HOME="$tmp/user" ANDROID_EMULATOR_HOME="$tmp/emulator" ANDROID_AVD_HOME="$tmp/avd" ANDROID_ADB_SERVER_PORT="$adbPort" ADB_SERVER_SOCKET="tcp:localhost:$adbPort" ADB_LOCAL_TRANSPORT_MAX_PORT=0 ADB_MDNS_AUTO_CONNECT=0
if [ -n "${JAVA_HOME:-}" ]; then PATH="$JAVA_HOME/bin:$PATH"; export PATH; fi
mkdir -p "$ANDROID_USER_HOME" "$ANDROID_EMULATOR_HOME" "$ANDROID_AVD_HOME"
name="podlord-private-$(basename "$tmp" | tr '[:upper:]' '[:lower:]')"
emupid=""
adbpid=""
adb() {
  if [ -z "$adbpid" ] || ! kill -0 "$adbpid" 2>/dev/null; then printf 'Owned private ADB server is not running\n' >&2; return 1; fi
  node - "$sdk/platform-tools/adb" "$adbPort" "$serial" "$@" <<'ADB_COMMAND'
const cp=require('node:child_process');
const [adb,port,serial,...args]=process.argv.slice(2);
const result=cp.spawnSync(adb,['-P',port,'-s',serial,...args],{stdio:'inherit',timeout:30000,killSignal:'SIGKILL',env:{...process.env,ADB_TRACE:''}});
if(result.error)console.error('ADB command failed: '+result.error.message);
process.exitCode=result.status??1;
ADB_COMMAND
}
cleanup() {
  result=$?
  testResult=$result
  cleanupFailed=0
  trap - 0
  if [ -n "$emupid" ]; then
    node - "$out" "$sdk/platform-tools/adb" "$adbPort" "$serial" "$adbpid" <<'CLEANUP_LOGS' || :
const fs=require('node:fs'),cp=require('node:child_process'),path=require('node:path');
const [out,adb,port,serial,serverPid]=process.argv.slice(2);
const commands=[['crash-logcat.txt',['logcat','-b','crash','-d']]];
const pidFile=path.join(out,'product-pid.txt');
const pid=fs.existsSync(pidFile)?fs.readFileSync(pidFile,'utf8').trim():'';
if(/^\d+$/.test(pid))commands.push(['product-final-logcat.txt',['logcat','-d','--pid='+pid]]);
commands.push(['emulator-stop.log',['emu','kill']]);
for(const [name,args] of commands){
  try{process.kill(Number(serverPid),0);}catch{break;}
  const log=fs.openSync(path.join(out,name),'w');
  try{
    const result=cp.spawnSync(adb,['-P',port,'-s',serial,...args],{stdio:['ignore',log,log],timeout:5000,killSignal:'SIGKILL',env:{...process.env,ADB_TRACE:''}});
    if(result.error)fs.writeSync(log,'\n'+result.error.message+'\n');
  }finally{fs.closeSync(log);}
}
CLEANUP_LOGS
  fi
  for ownedpid in "$emupid" "$adbpid"; do
    [ -n "$ownedpid" ] || continue
    printf 'Stopping owned PID %s\n' "$ownedpid" >> "$out/owned-process-stop.log"
    kill "$ownedpid" 2>/dev/null || :
    for attempt in $(seq 1 5); do
      if ! kill -0 "$ownedpid" 2>/dev/null; then break; fi
      sleep 1
    done
    if kill -0 "$ownedpid" 2>/dev/null; then kill -KILL "$ownedpid" 2>/dev/null || cleanupFailed=1; fi
    wait "$ownedpid" 2>/dev/null || :
    if kill -0 "$ownedpid" 2>/dev/null; then cleanupFailed=1; fi
  done
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
printf 'no\n' | "$sdk/cmdline-tools/latest/bin/avdmanager" create avd -n "$name" -k "$system_image" -p "$tmp/device" > "$out/avd-create.log" 2>&1
ADB_TRACE="$adbTrace" "$sdk/platform-tools/adb" -P "$adbPort" --one-device "$serial" server nodaemon > "$out/adb-server.log" 2>&1 &
adbpid=$!
printf '%s\n' "$adbpid" > "$out/adb-server-pid.txt"
adbready=0
for attempt in $(seq 1 10); do
  if ! kill -0 "$adbpid" 2>/dev/null; then tail -n 16 "$out/adb-server.log"; exit 1; fi
  if lsof -nP -a -p "$adbpid" -iTCP:"$adbPort" -sTCP:LISTEN > "$out/adb-server-listener.txt" 2>&1; then adbready=1; break; fi
  sleep 1
done
if [ "$adbready" != 1 ]; then printf 'Owned private ADB server did not listen\n' >&2; exit 1; fi
set --
if [ -n "$adbTrace" ]; then
  set -- -logcat 'adbd:D lmkd:I AndroidRuntime:E ActivityManager:W *:S'
fi
"$sdk/emulator/emulator" -avd "$name" -port "$consolePort" -no-window -no-audio -no-snapshot -no-boot-anim -gpu swiftshader -memory 2048 -cores 4 "$@" > "$out/emulator.log" 2>&1 &
emupid=$!
ready=0
for attempt in $(seq 1 90); do
  if ! kill -0 "$adbpid" 2>/dev/null; then printf 'Owned private ADB server exited during boot\n' >&2; exit 1; fi
  if ! kill -0 "$emupid" 2>/dev/null; then tail -n 16 "$out/emulator.log"; exit 1; fi
  if [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; then ready=1; break; fi
  sleep 2
done
if [ "$ready" != 1 ]; then adb devices -l; tail -n 12 "$out/emulator.log"; exit 1; fi
page_size=$(adb shell getconf PAGE_SIZE | tr -d '\r')
case "$page_size" in 4096|16384) ;; *) printf 'Unexpected Android page size: %s\n' "$page_size" >&2; exit 1 ;; esac
printf '%s\n' "$page_size" > "$out/page-size.txt"
case "$system_image" in
  *google_apis_ps16k*) [ "$page_size" = 16384 ] || { printf 'The 16 KB image did not boot a 16 KB kernel.\n' >&2; exit 1; } ;;
esac
if [ "$page_size" = 16384 ]; then
  adb root > "$out/adb-root-page-size.txt" 2>&1
  adb wait-for-device
  adb shell setprop bionic.linker.16kb.app_compat.enabled false
  adb shell setprop pm.16kb.app_compat.disabled true
  linker_compat=$(adb shell getprop bionic.linker.16kb.app_compat.enabled | tr -d '\r')
  package_compat=$(adb shell getprop pm.16kb.app_compat.disabled | tr -d '\r')
  printf 'linker_compat=%s\npackage_compat_disabled=%s\n' "$linker_compat" "$package_compat" > "$out/page-size-compat.txt"
  [ "$linker_compat" = false ] && [ "$package_compat" = true ] || { printf 'The private emulator still permits page-size compatibility mode.\n' >&2; exit 1; }
fi
adb shell input keyevent 82
adb install --no-streaming "$apk" > "$out/install.log" 2>&1
activity=$(sed -n "s/^launchable-activity: name='\([^']*\)'.*/\1/p" "$out/badging.txt")
if [ -z "$activity" ]; then printf 'No launchable activity in signed APK\n' >&2; exit 1; fi
adb shell am start -W -n "berlin.yuna.podlord/$activity" > "$out/startup.txt"
adb shell pidof berlin.yuna.podlord > "$out/product-pid.txt"
adb root > "$out/adb-root-ingress.txt" 2>&1
adb wait-for-device
if [ -n "$adbTrace" ]; then
  adb logcat -G 16M
  adb logcat -g > "$out/logcat-buffer.txt"
fi
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
node - "$out" "$sdk/platform-tools/adb" "$adbPort" "$serial" "$emupid" "$consolePort" "$adbpid" <<'JOURNEY'
const fs=require('node:fs'),cp=require('node:child_process'),path=require('node:path');
const [out,adb,port,device,emulatorPid,consolePort,adbServerPid]=process.argv.slice(2), serial=['-P',port,'-s',device];
const adbTraceEnabled=process.env.PODLORD_ANDROID_ADB_TRACE==='1',adbServerLog=path.join(out,'adb-server.log'),adbClientStderrLog=path.join(out,'adb-client-stderr.log');
const adbClientStderr=adbTraceEnabled?fs.openSync(adbClientStderrLog,'a+'):null;
const adbEnvironment={...process.env,ADB_TRACE:adbTraceEnabled?'transport,shell,services,sockets':''};
process.on('exit',()=>{if(adbClientStderr!==null)fs.closeSync(adbClientStderr);});
const inspectorCycles=Number(process.env.PODLORD_ANDROID_INSPECTOR_CYCLES||0);
if(!Number.isInteger(inspectorCycles)||inspectorCycles<0||inspectorCycles>50)throw Error('Inspector diagnostic cycles must be an integer from 0 to 50');
const coldInspectorCycles=Number(process.env.PODLORD_ANDROID_COLD_INSPECTOR_CYCLES||0);
if(!Number.isInteger(coldInspectorCycles)||coldInspectorCycles<0||coldInspectorCycles>10)throw Error('Cold inspector diagnostic cycles must be an integer from 0 to 10');
if(inspectorCycles&&coldInspectorCycles)throw Error('Choose warm or cold inspector diagnostics, not both');
const inspectorOnly=process.env.PODLORD_ANDROID_INSPECTOR_ONLY||'0';
if(!['0','1'].includes(inspectorOnly)||(inspectorOnly==='1'&&(inspectorCycles||coldInspectorCycles)))throw Error('Choose a single inspector diagnostic mode');
const sourceOnly=process.env.PODLORD_ANDROID_SOURCE_ONLY||'0';
if(!['0','1'].includes(sourceOnly)||(sourceOnly==='1'&&(inspectorOnly==='1'||inspectorCycles||coldInspectorCycles)))throw Error('Choose a single Sources diagnostic mode');
const mapRoundtrip=process.env.PODLORD_ANDROID_MAP_ROUNDTRIP||'0';
if(!['0','1'].includes(mapRoundtrip)||(mapRoundtrip==='1'&&(sourceOnly==='1'||inspectorOnly==='1'||inspectorCycles||coldInspectorCycles)))throw Error('Choose a single map roundtrip diagnostic mode');
const mapOnly=process.env.PODLORD_ANDROID_MAP_ONLY||'0';
if(!['0','1'].includes(mapOnly)||(mapOnly==='1'&&mapRoundtrip!=='1'))throw Error('Map-only diagnostics require the map roundtrip mode');
const fixture=process.env.PODLORD_ANDROID_FIXTURE_CONTAINER||'';
if(mapRoundtrip==='1'&&(!/^podlord-visual-run-[a-z0-9]+$/.test(fixture)||process.env.PODLORD_KUBERNETES_METADATA_AUDIT!=='1'||!/^unix:\/\//.test(process.env.DOCKER_HOST||'')))throw Error('Map roundtrip requires its owned local Metadata-audited fixture');
let firstCommandFailure=false,lastSuccessfulUiAt=null;
const runAdb=(args,encoding='utf8')=>{
 const startedAt=new Date().toISOString();
 const traceStart=adbClientStderr===null?null:fs.fstatSync(adbClientStderr).size;
 try{
  try{process.kill(Number(adbServerPid),0);}catch(error){throw Object.assign(Error('Owned private ADB server is not running'),{code:error.code});}
  return cp.execFileSync(adb,[...serial,...args],{encoding,timeout:30000,maxBuffer:4000000,env:adbEnvironment,...(adbClientStderr===null?{}:{stdio:['ignore','pipe',adbClientStderr]})});
 }
 catch(error){
  const observe=(!firstCommandFailure&&error.status===255)&&((args[0]==='shell'&&args[1]==='cat'&&args[2]==='/sdcard/podlord-journey.xml')||(args[0]==='shell'&&args[1]==='uiautomator'&&args[2]==='dump')||(args[0]==='exec-out'&&args[1]==='screencap'));
  if(!firstCommandFailure){
   firstCommandFailure=true;
   try{
    const traceEnd=adbClientStderr===null?null:fs.fstatSync(adbClientStderr).size;
    const tracedStderr=adbClientStderr===null?null:Buffer.alloc(traceEnd-traceStart);
    if(tracedStderr)fs.readSync(adbClientStderr,tracedStderr,0,tracedStderr.length,traceStart);
    const failure={startedAt,failedAt:new Date().toISOString(),command:[adb,...serial,...args],status:error.status??null,signal:error.signal??null,code:error.code??null,stdout:String(error.stdout||''),stderr:String(tracedStderr||error.stderr||''),ownedAdbServerPid:Number(adbServerPid),adbTrace:{enabled:adbTraceEnabled,serverLog:adbServerLog,clientStderrLog:adbTraceEnabled?adbClientStderrLog:null,traceStart,traceEnd},lastSuccessfulUiAt,uiSnapshot:'Saved UI XML is the last successful read, not proof of current device state',host:{}};
    for(const [name,command,args] of [
     ['emulatorProcess','ps',['-p',emulatorPid,'-o','pid=,ppid=,stat=,etime=,command=']],
     ['adbServerProcess','ps',['-p',adbServerPid,'-o','pid=,ppid=,stat=,etime=,command=']],
     ['privatePortListeners','lsof',['-nP','-iTCP:'+port,'-iTCP:'+consolePort,'-iTCP:'+(Number(consolePort)+1),'-sTCP:LISTEN']],
     ['privatePortConnections','lsof',['-nP','-iTCP:'+port,'-iTCP:'+consolePort,'-iTCP:'+(Number(consolePort)+1)]]
    ]){
     const result=cp.spawnSync(command,args,{encoding:'utf8',timeout:3000,maxBuffer:1000000});
     failure.host[name]={status:result.status,signal:result.signal,error:result.error?.message,stdout:result.stdout||'',stderr:result.stderr||''};
    }
    fs.writeFileSync(path.join(out,'first-adb-failure.json'),JSON.stringify(failure,null,2)+'\n');
   }catch(diagnosticError){console.error('First ADB failure diagnostics unavailable: '+String(diagnosticError));}
  }
  if(observe){
   const recovery={status:'failed',command:args,scope:'One read-only observation retry on the owned private ADB server. Input, import, root and write commands are never replayed.'};
   try{
    process.kill(Number(emulatorPid),0);process.kill(Number(adbServerPid),0);
    const options={encoding:'utf8',timeout:10000,maxBuffer:1000000,env:adbEnvironment};
    const devices=cp.execFileSync(adb,['-P',port,'devices'],options).trim().split('\n').slice(1).filter(Boolean).map(line=>line.trim().split(/\s+/)[0]);
    if(devices.some(serial=>serial!==device))throw Error('Foreign transport on private server; recovery refused');
    cp.execFileSync(adb,['-P',port,'reconnect','offline'],options);
    cp.execFileSync(adb,[...serial,'wait-for-device'],options);
    const value=runAdb(args,encoding);recovery.status='recovered';fs.writeFileSync(path.join(out,'transport-recovery.json'),JSON.stringify(recovery)+'\n');return value;
   }catch(retryError){recovery.error=retryError.code||retryError.status||'observation failed';fs.writeFileSync(path.join(out,'transport-recovery.json'),JSON.stringify(recovery)+'\n');}
  }
  throw error;
 }
};
const run=(...args)=>runAdb(args);
const decode=s=>s.replaceAll('&quot;','"').replaceAll('&apos;',"'").replaceAll('&lt;','<').replaceAll('&gt;','>').replaceAll('&#10;','\n').replaceAll('&#13;','\r').replaceAll('&#9;','\t').replaceAll('&amp;','&');
const parseNodes=xml=>[...xml.matchAll(/<node\b(?:[^"'<>]|"[^"]*"|'[^']*')*>/g)].map(m=>Object.fromEntries([...m[0].matchAll(/([\w-]+)=(["'])([\s\S]*?)\2/g)].map(a=>[a[1],decode(a[3])])));
require('node:assert/strict').deepEqual(parseNodes(`<node text='key: "value > other" &amp; &apos;' resource-id="yaml"/><node text="&amp;lt;"/>`),[{text:`key: "value > other" & '`, 'resource-id':'yaml'},{text:'&lt;'}]);
require('node:assert/strict').equal(decode('&#10;&#13;&#9;&amp;#10;'),'\n\r\t&#10;');
let xml='',nodes=[];const steps=[],journeyFailures=[];
function state(){
 const deadline=Date.now()+30000;
 xml='';nodes=[];
 do{
  run('wait-for-device');
  // UIAutomator may return success without writing a hierarchy when the UI is busy.
  run('shell','rm','-f','/sdcard/podlord-journey.xml');
  const dumped=run('shell','uiautomator','dump','/sdcard/podlord-journey.xml');
  if(/dumped to:\s*\/sdcard\/podlord-journey\.xml/.test(dumped)){
   const fresh=run('shell','cat','/sdcard/podlord-journey.xml');
   if(fresh.trimEnd().endsWith('</hierarchy>')){
    xml=fresh;nodes=parseNodes(xml);lastSuccessfulUiAt=new Date().toISOString();return nodes;
   }
  }
  fs.appendFileSync(path.join(out,'ui-hierarchy-retries.jsonl'),JSON.stringify({at:new Date().toISOString(),reason:'No fresh hierarchy produced; no previous coordinates reused'})+'\n');
 }while(Date.now()<deadline);
 throw Error('Android did not produce a fresh accessibility hierarchy within the command watchdog');
}
function target(id){
 let scrolled=false;
 const sourceRow=id==='sourceRow_0',limit=sourceRow?20:8;
 const matches=n=>sourceRow?/^sourceCell_0_[0-7]$/.test(n['resource-id']?.split('.').at(-1)||''):n['resource-id']?.endsWith('.'+id);
 const bounds=n=>n?.bounds?.match(/\[(\d+),(\d+)\]\[(\d+),(\d+)\]/)?.slice(1).map(Number);
 for(let attempt=0;attempt<=limit;attempt++){
  const n=nodes.find(n=>matches(n)&&n.enabled==='true'&&bounds(n)?.[2]>bounds(n)?.[0]&&bounds(n)?.[3]-bounds(n)?.[1]>=18&&(()=>{
    const b=bounds(n), nav=n['resource-id']?.includes('.landscapeNavigation.'), side=n['resource-id']?.includes('.ResourceSidebar_');
    const inspector=/\.(workspaceInspector|sourceInspector)\./.test(n['resource-id']||'');
    const settings=sourceRow||/^inline/.test(id)||n['resource-id']?.includes('.settingsContent.');
    const host=settings?nodes.find(x=>x['resource-id']?.endsWith('.settingsContent')):inspector?nodes.find(x=>/\.(workspaceInspector|sourceInspector)$/.test(x['resource-id']||'')):nav?nodes.find(x=>x['resource-id']?.includes('.landscapeNavigation.ScrollBar_')&&bounds(x)?.[3]-bounds(x)?.[1]>50):side?nodes.find(x=>x['resource-id']?.endsWith('.sessionHealthStrip')):null;
    const clip=bounds(host);return !clip||(b[1]>=clip[1]&&b[3]<=clip[3]);
  })());
  if(n){if(!scrolled)return n;const before=n.bounds,identity=n['resource-id'];state();const stable=nodes.find(n=>n['resource-id']===identity&&n.bounds===before&&n.enabled==='true');if(stable)return stable;continue;}
  const nav=/WorkspaceButton|portForwardTasksButton|workspaceActionsButton|toggleSidebar/.test(id);
  if(attempt===limit)break;
  const current=nodes.find(matches);
  const inspector=/\.(workspaceInspector|sourceInspector)\./.test(current?.['resource-id']||'');
  const settings=sourceRow||/^inline/.test(id)||current?.['resource-id']?.includes('.settingsContent.');
  const host=settings?nodes.find(n=>n['resource-id']?.endsWith('.settingsContent')):inspector?nodes.find(n=>/\.(workspaceInspector|sourceInspector)$/.test(n['resource-id']||'')):nav?nodes.find(n=>n['resource-id']?.includes('.landscapeNavigation.ScrollBar_')&&bounds(n)?.[3]-bounds(n)?.[1]>50):nodes.find(n=>n['resource-id']?.endsWith('.sessionHealthStrip'));
  const b=bounds(host);if(!b)break;
  const x=settings||inspector?b[0]+8:nav?Math.max(10,b[0]-16):Math.floor((b[2]+Math.max(...nodes.map(n=>bounds(n)?.[2]||0)))/2);
  const firstVisible=settings?nodes.findIndex(n=>n['resource-id']?.startsWith(host['resource-id']+'.')&&!n['resource-id'].includes('.ScrollBar_')&&(()=>{const visible=bounds(n);return visible&&visible[2]>visible[0]&&visible[1]>=b[1]&&visible[3]<=b[3]&&visible[3]>visible[1];})()):-1;
  const currentBounds=bounds(current);
  const partialSidebar=!settings&&!inspector&&!nav&&currentBounds&&currentBounds[2]>currentBounds[0]&&currentBounds[3]>currentBounds[1];
  const reverse=settings&&current&&firstVisible>=0?nodes.indexOf(current)<firstVisible:partialSidebar?currentBounds[1]<b[1]:id==='toggleLandscapeFilters'?attempt<4:attempt>=4;
  const inset=settings?4:12;
  const start=reverse?b[1]+inset:b[3]-inset;
  // A full swipe can jump over a short control that misses the viewport by one pixel.
  const distance=sourceRow?Math.max(24,Math.floor((b[3]-b[1])/4)):partialSidebar?Math.min(b[3]-b[1]-2*inset,Math.max(24,(reverse?b[1]-currentBounds[1]:currentBounds[3]-b[3])+inset)):b[3]-b[1]-2*inset;
  run('shell','input','swipe',String(x),String(start),String(x),String(start+(reverse?distance:-distance)),settings&&!sourceRow?'150':'350');scrolled=true;state();
  if(!settings&&!inspector&&!nav)fs.appendFileSync(path.join(out,'input-events.jsonl'),JSON.stringify({action:'sidebar-scroll',id,attempt,x,viewport:b,reverse,targetBounds:nodes.find(n=>n['resource-id']?.endsWith('.'+id))?.bounds})+'\n');
  if(settings)fs.appendFileSync(path.join(out,'settings-scroll-events.jsonl'),JSON.stringify({id,attempt,viewport:b,reverse,firstVisible,visibleFields:nodes.filter(n=>/\.(inlineRadarWaterEnabled|inlineRadarWaterSpeed|inlineReducedMotion)$/.test(n['resource-id']||'')).map(n=>({id:n['resource-id'].split('.').at(-1),bounds:n.bounds}))})+'\n');
 }
 capture('unreachable-'+id);throw Error('Public UI target unavailable after scrolling: '+id);
}
function tap(n){const b=n.bounds.match(/\[(\d+),(\d+)\]\[(\d+),(\d+)\]/).slice(1).map(Number);if(b[2]<=b[0]||b[3]<=b[1])throw Error('Target outside viewport');const point=[Math.floor((b[0]+b[2])/2),Math.floor((b[1]+b[3])/2)];run('shell','input','tap',...point.map(String));state();const after=nodes.find(x=>x['resource-id']===n['resource-id']);fs.appendFileSync(path.join(out,'input-events.jsonl'),JSON.stringify({id:n['resource-id'],bounds:n.bounds,point,beforeChecked:n.checked,afterBounds:after?.bounds,afterChecked:after?.checked,focused:nodes.filter(x=>x.focused==='true').map(x=>({id:x['resource-id'],text:x.text,description:x['content-desc']}))})+'\n');}
function click(id){tap(target(id));}
function capture(name){fs.writeFileSync(path.join(out,name+'.xml'),xml);fs.writeFileSync(path.join(out,name+'.png'),runAdb(['exec-out','screencap','-p'],null));}
function completeJourney(){const status=journeyFailures.length?'failed':'passed';fs.writeFileSync(path.join(out,'journey.json'),JSON.stringify({status,mode:mapOnly==='1'?'map-only':sourceOnly==='1'?'sources-only':'full',scope:'Real installed product, owned K3s TLS source and public Android accessibility/touch actions; map-only diagnostics do not replace the full journey or live cloud OAuth',steps,failures:journeyFailures,transportIncidentObserved:firstCommandFailure},null,2)+'\n');console.log(JSON.stringify({androidRealJourney:status,resources:count,steps:steps.length}));if(journeyFailures.length)process.exitCode=1;}
process.on('uncaughtException',error=>{
 fs.writeFileSync(path.join(out,'journey.json'),JSON.stringify({status:'failed',steps,failures:journeyFailures,error:String(error)},null,2)+'\n');
 try{capture('failure');state();capture('failure-settled');fs.writeFileSync(path.join(out,'failure-nodes.json'),JSON.stringify(nodes,null,2)+'\n')}catch(captureError){console.error(String(captureError))}
 const pid=fs.readFileSync(path.join(out,'product-pid.txt'),'utf8').trim();
 const diagnostics=[['crash-logcat',['logcat','-b','crash','-d']],['system-logcat',['logcat','-b','all','-d','-t','2000']],['activity',['shell','dumpsys','activity','top']],['window',['shell','dumpsys','window','windows']],['input',['shell','dumpsys','input']],['input-method',['shell','dumpsys','input_method']]];
 if(/^\d+$/.test(pid))diagnostics.push(['process',['shell','cat','/proc/'+pid+'/status']],['native-backtrace',['shell','debuggerd','-b',pid]]);
 for(const [name,args] of diagnostics){try{fs.writeFileSync(path.join(out,'failure-'+name+'.txt'),run(...args))}catch(diagnosticError){fs.writeFileSync(path.join(out,'failure-'+name+'.txt'),String(diagnosticError)+'\n'+(diagnosticError.stdout||'')+(diagnosticError.stderr||''))}}
 console.error(error);process.exitCode=1;
});
waitNode('sourcePath',n=>n.enabled==='true'&&!/\[0,0\]\[0,0\]/.test(n.bounds));click('sourcePath');run('shell','input','text','/data/user/0/berlin.yuna.podlord/files/release-fixture.kubeconfig');state();run('shell','input','keyevent','4');state();waitNode('sourcePath',n=>n.text==='/data/user/0/berlin.yuna.podlord/files/release-fixture.kubeconfig');click('importButton');state();
waitNode('closeSourceControls',n=>n.enabled==='true');click('closeSourceControls');
const cardDeadline=Date.now()+30000;let contextCard='';
do{state();const card=nodes.find(n=>n['resource-id']?.split('.').at(-1).startsWith('contextMapCard_')&&n.enabled==='true'&&n.bounds!=='[0,0][0,0]');if(card){contextCard=card['resource-id'].split('.').at(-1);break;}}while(Date.now()<cardDeadline);
if(!contextCard){capture('missing-context-card');throw Error('Imported context card did not appear');}
capture('contexts-before-opening');click(contextCard);click('resourcesWorkspaceButton');
run('shell','settings','put','system','accelerometer_rotation','0');run('shell','settings','put','system','user_rotation','1');state();
const deadline=Date.now()+120000;let count=0;
do{state();const match=nodes.map(n=>n['content-desc']||n.text||'').join('\n').match(/visible: \d+\/(\d+)/);count=match?Number(match[1]):0;const health=nodes.find(n=>n['resource-id']?.endsWith('.sessionHealthStrip'));if(count>=1000&&health?.['content-desc']?.startsWith('Session health:'))break;count=0;}while(Date.now()<deadline);
if(count<1000){capture('load-failure');throw Error('Real cluster cache did not fill');}steps.push({step:'Real TLS source import and cached resources',resources:count});run('shell','settings','put','system','user_rotation','0');state();capture('loaded-portrait');
if(sourceOnly==='1'){search('visual-multi-container');sourceJourney();completeJourney();process.exit(0);}
run('shell','settings','put','system','accelerometer_rotation','0');run('shell','settings','put','system','user_rotation','1');state();capture('loaded-landscape');
if(inspectorOnly==='1'){
 search('visual-multi-container');waitNode('cell_0_0',n=>(n.text+' '+n['content-desc']).includes('visual-multi-container'));
 click('cell_0_0');waitNode('inspectorResourceName',n=>(n.text+' '+n['content-desc']).includes('Pod/visual-multi-container'));
 target('terminalButton');capture('first-inspector');
 steps.push({step:'First inspector and terminal action on a fresh installed profile'});completeJourney();process.exit(0);
}
if(coldInspectorCycles){
 const activity=fs.readFileSync(path.join(out,'badging.txt'),'utf8').match(/^launchable-activity: name='([^']+)'/m)?.[1];
 if(!activity)throw Error('Cold inspector diagnostics require the installed launch activity');
 for(let cycle=1;cycle<=coldInspectorCycles;cycle++){
  run('shell','am','force-stop','berlin.yuna.podlord');run('shell','am','start','-W','-n','berlin.yuna.podlord/'+activity);
  const pid=run('shell','pidof','berlin.yuna.podlord').trim();
  if(!/^\d+$/.test(pid))throw Error('Cold inspector diagnostic did not start the product');
  fs.writeFileSync(path.join(out,'product-pid.txt'),pid+'\n');
  waitNode(contextCard,n=>n.enabled==='true');click(contextCard);click('resourcesWorkspaceButton');
  waitNode('resourceMatchCount',n=>Number((n.text+' '+n['content-desc']).match(/visible: \d+\/(\d+)/)?.[1])>=1000,120000);
  search('visual-multi-container');waitNode('cell_0_0',n=>(n.text+' '+n['content-desc']).includes('visual-multi-container'));
  click('cell_0_0');waitNode('inspectorResourceName',n=>(n.text+' '+n['content-desc']).includes('Pod/visual-multi-container'));
  target('terminalButton');click('closeInspector');
  if(run('shell','pidof','berlin.yuna.podlord').trim()!==pid)throw Error('Product restarted while opening the cold inspector');
  fs.appendFileSync(path.join(out,'inspector-cold-starts.jsonl'),JSON.stringify({completedCycle:cycle,pid})+'\n');
 }
 capture('inspector-cold-starts-final');
 steps.push({step:'First inspector opening after process restart and explicit context loading',cycles:coldInspectorCycles});
 completeJourney();process.exit(0);
}
if(mapRoundtrip==='1'){
 try{mapTouchRoundtrip();}
 catch(error){
  journeyFailures.push({step:'Map touch roundtrip',error:String(error)});
  console.error('Map touch roundtrip failed: '+String(error));
  capture('map-roundtrip-failure');
  if(nodes.some(n=>n['resource-id']?.endsWith('.closeInspector')&&n.enabled==='true'))click('closeInspector');
  const resources=nodes.find(n=>n['resource-id']?.endsWith('.resourcesWorkspaceButton')&&n.enabled==='true'&&n.bounds!=='[0,0][0,0]');
  if(!resources)throw error;
  click('resourcesWorkspaceButton');search('');
 }
 if(mapOnly==='1'){completeJourney();process.exit(journeyFailures.length?1:0);}
}
terminalJourney();
search('');steps.push({step:'Landscape search remains above the real soft keyboard and restores navigation after dismissal'});
const miniRadar=target('resourceRadar').bounds.match(/\[(\d+),(\d+)\]\[(\d+),(\d+)\]/).slice(1).map(Number);
click('mapWorkspaceButton');
waitNode('resourceRadar',n=>{const b=n.bounds.match(/\[(\d+),(\d+)\]\[(\d+),(\d+)\]/).slice(1).map(Number);return b[2]-b[0]>miniRadar[2]-miniRadar[0]+50;});
for(let i=0;i<4;i++)click('radarZoomOut');capture('map-cluster-overview');
for(let i=0;i<7;i++)click('radarZoom');capture('map-namespace-details');
click('resetRadar');click('mapOverview');waitNode(contextCard,n=>n.enabled==='true');
const cardViewport=target(contextCard).bounds.match(/\[(\d+),(\d+)\]\[(\d+),(\d+)\]/).slice(1).map(Number);
for(const prefix of ['contextMapName_','contextMapCluster_','renameMapContext_']){
 const control=waitNode(prefix+contextCard.slice('contextMapCard_'.length),n=>{
  const b=n.bounds?.match(/\[(\d+),(\d+)\]\[(\d+),(\d+)\]/)?.slice(1).map(Number);
  return b&&b[0]>=cardViewport[0]&&b[1]>=cardViewport[1]&&b[2]<=cardViewport[2]&&b[3]<=cardViewport[3]&&b[3]-b[1]>=16;
 });
 if(!control)throw Error('Context card caption/action clipped in landscape');
}
capture('contexts-after-loading');click(contextCard);
waitNode('resourceRadar',n=>n.enabled==='true');click('radarWorkspaceButton');
waitNode('resourceRadar',n=>{const b=n.bounds.match(/\[(\d+),(\d+)\]\[(\d+),(\d+)\]/).slice(1).map(Number);return b[2]-b[0]<=miniRadar[2]-miniRadar[0]+1;});
steps.push({step:'Same-session Map overview, namespace zoom and return to Table share the live cache'});
click('toggleLandscapeFilters');waitNode('toggleLandscapeFilters',n=>n.checked==='true');capture('radar-and-filters');
click('problemsOnly');if(target('problemsOnly').checked!=='true')throw Error('Problems filter did not enable');capture('problems-filter');const filtered=nodes.map(n=>n['content-desc']||'').join('\n').match(/(\d+) matching resources/);if(!filtered||Number(filtered[1])<=0||Number(filtered[1])>=count)throw Error('Problems filter did not reduce cached matches');click('problemsOnly');if(target('problemsOnly').checked!=='false')throw Error('Problems filter did not disable');steps.push({step:'Problems filter enables, reduces cached matches and disables',matches:Number(filtered[1])});
click('toggleLandscapeFilters');
for(const id of ['eventsWorkspaceButton','portForwardTasksButton','settingsWorkspaceButton','resourcesWorkspaceButton']){state();click(id);if(target(id).checked!=='true')throw Error('Navigation did not select '+id);capture(id);steps.push({step:id});}

function waitNode(id,predicate,timeout=30000){
 const until=Date.now()+timeout;
 do{state();const n=nodes.find(n=>n['resource-id']?.endsWith('.'+id));if(n&&predicate(n))return n;}while(Date.now()<until);
 capture('waiting-'+id);throw Error('Public state did not arrive: '+id);
}
function inputAboveKeyboard(ids,name){
 const deadline=Date.now()+10000;
 let windows='';
 do{
  state();windows=run('shell','dumpsys','window');
  const ime=windows.match(/(?:mType|type)=ime\b[^\n]*?(?:mFrame|frame)=\[(\d+),(\d+)\]\[(\d+),(\d+)\][^\n]*?(?:mVisible|visible)=true/);
  const status=windows.match(/(?:mType|type)=statusBars\b[^\n]*?(?:mFrame|frame)=\[(\d+),(\d+)\]\[(\d+),(\d+)\][^\n]*?(?:mVisible|visible)=true/);
  const top=status?Number(status[4]):0;
  if(ime&&ids.every(id=>{
   const n=nodes.find(n=>n['resource-id']?.endsWith('.'+id));
   const b=n?.bounds?.match(/\[(\d+),(\d+)\]\[(\d+),(\d+)\]/)?.slice(1).map(Number);
   return b&&b[0]>=0&&b[2]>b[0]&&b[1]>=top&&b[3]<=Number(ime[2])&&b[3]-b[1]>=(id==='terminalSurface'?28:16);
  })){capture(name);return;}
 }while(Date.now()<deadline);
 fs.writeFileSync(path.join(out,name+'-windows.txt'),windows);capture(name+'-failure');
 throw Error('Input or connection status is obscured by keyboard/system bars: '+ids.join(', '));
}
function search(value){
 state();if(!nodes.some(n=>n['resource-id']?.endsWith('.resourceFilter')&&n.enabled==='true'&&n.bounds!=='[0,0][0,0]'))click('workspaceSearchButton');
 click('resourceFilter');inputAboveKeyboard(['resourceFilter'],'search-open');run('shell','input','keyevent','123',...Array(64).fill('67'));
 if(value)run('shell','input','text',"'"+value.replaceAll("'","'\\''")+"'");
 run('shell','input','keyevent','4');state();click('workspaceSearchButton');
}
function describe(id){const n=nodes.find(n=>n['resource-id']?.endsWith('.'+id));return n?[n.text,n['content-desc']].filter(Boolean).join('\n'):'';}
function mapTouchRoundtrip(){
 const assert=require('node:assert/strict'),crypto=require('node:crypto'),zlib=require('node:zlib');
 const value=n=>n?.text||n?.['content-desc']||'';
 const find=id=>nodes.find(n=>n['resource-id']?.endsWith('.'+id));
 const rect=n=>n.bounds.match(/\[(\d+),(\d+)\]\[(\d+),(\d+)\]/).slice(1).map(Number);
 const zoom=()=>{const left=rect(target('radarZoomOut')),right=rect(target('radarZoom'));const n=nodes.find(n=>/^\d+%$/.test(value(n))&&n.bounds&&rect(n)[0]>left[2]&&rect(n)[2]<right[0]&&rect(n)[1]>=left[1]&&rect(n)[3]<=left[3]);assert.ok(n,'Visible public zoom percentage is required');return Number(value(n).slice(0,-1));};
 const counter=label=>{
  const owner=cp.execFileSync('docker',['inspect','--type','container',fixture,'--format','{{index .Config.Labels "podlord.native.e2e"}}'],{encoding:'utf8',timeout:10000,maxBuffer:1000000}).trim();
  assert.equal(owner,fixture,'Only the owned fixture audit may be read');
  const script=`awk '/"stage":"RequestReceived"/ && /"username":"system:admin"/ { n++; if (/"userAgent":"Mozilla/) { qt++; if (/"verb":"(create|update|patch|delete|deletecollection)"/) writes++ } } END { print n+0,qt+0,writes+0 }' /var/log/podlord-metadata-audit.jsonl`;
  const [count,nativeRequests,nativeWrites]=cp.execFileSync('docker',['exec',fixture,'/bin/sh','-c',script],{encoding:'utf8',timeout:10000,maxBuffer:1000000}).trim().split(/\s+/).map(Number);
  assert.ok(Number.isSafeInteger(count)&&count>0&&Number.isSafeInteger(nativeRequests)&&nativeRequests>0&&Number.isSafeInteger(nativeWrites)&&nativeWrites>=0,'Metadata audit must identify actual native Qt requests');
  const result={label,at:new Date().toISOString(),count,nativeRequests,nativeWrites,scope:'Owned system:admin Metadata events; native Qt Mozilla actor identified. Read totals include passive synchronization and are diagnostic, not a causal UI admission count. No request bodies.'};
  fs.appendFileSync(path.join(out,'map-request-counts.jsonl'),JSON.stringify(result)+'\n');return result;
 };
 const image=file=>{
  const bytes=fs.readFileSync(path.join(out,file+'.png'));let width,height,bpp,interlace,parts=[];
  for(let p=8;p<bytes.length;){const length=bytes.readUInt32BE(p),type=bytes.toString('ascii',p+4,p+8),data=bytes.subarray(p+8,p+8+length);if(type==='IHDR'){width=data.readUInt32BE(0);height=data.readUInt32BE(4);assert.equal(data[8],8);assert.ok([2,6].includes(data[9]));bpp=data[9]===6?4:3;interlace=data[12];}if(type==='IDAT')parts.push(data);p+=length+12;}
  assert.equal(interlace,0);assert.deepEqual([width,height],[640,320]);const raw=zlib.inflateSync(Buffer.concat(parts)),stride=width*bpp,pixels=Buffer.alloc(height*stride);let offset=0;
  const paeth=(a,b,c)=>{const p=a+b-c,pa=Math.abs(p-a),pb=Math.abs(p-b),pc=Math.abs(p-c);return pa<=pb&&pa<=pc?a:pb<=pc?b:c;};
  for(let y=0;y<height;y++){const filter=raw[offset++];assert.ok(filter<=4);for(let x=0;x<stride;x++){const i=y*stride+x,a=x>=bpp?pixels[i-bpp]:0,b=y?pixels[i-stride]:0,c=y&&x>=bpp?pixels[i-stride-bpp]:0;pixels[i]=(raw[offset++]+(filter===0?0:filter===1?a:filter===2?b:filter===3?Math.floor((a+b)/2):paeth(a,b,c)))&255;}}
  return {width,height,bpp,pixels,crop:b=>{const chunks=[];for(let y=b[1];y<=b[3];y++)chunks.push(pixels.subarray((y*width+b[0])*bpp,(y*width+b[2]+1)*bpp));return Buffer.concat(chunks);},rgb:(x,y)=>pixels.subarray((y*width+x)*bpp,(y*width+x)*bpp+3).toString('hex')};
 };
 const density=run('shell','wm','density'),display=run('shell','wm','size');
 fs.writeFileSync(path.join(out,'touch-display.txt'),density+display+run('shell','dumpsys','display'));
 assert.match(density,/Physical density:\s*160/);assert.doesNotMatch(density,/Override density:/);
 click('settingsWorkspaceButton');click('settingsSectionPicker');
 const graphics=nodes.find(n=>value(n)==='Graphics'&&n.enabled==='true'&&n.bounds&&rect(n)[2]>rect(n)[0]);assert.ok(graphics,'Graphics must be selected through the public settings picker');tap(graphics);
 if(target('inlineRadarWaterEnabled').checked==='true')click('inlineRadarWaterEnabled');
 waitNode('inlineRadarWaterEnabled',n=>n.checked==='false');
 if(target('inlineReducedMotion').checked!=='true')click('inlineReducedMotion');
 waitNode('inlineReducedMotion',n=>n.checked==='true');capture('roundtrip-frozen-settings');
 const mapResourceQuery='"visual-config-0001"';
 click('resourcesWorkspaceButton');search(mapResourceQuery);click('workspaceSearchButton');
 waitNode('resourceFilter',n=>value(n)===mapResourceQuery);
 run('shell','input','keyevent','122');run('shell','input','text','x');state();
 waitNode('resourceFilter',n=>value(n)==='x'+mapResourceQuery);
 run('shell','input','keyevent','123');run('shell','input','text','z');state();
 waitNode('resourceFilter',n=>value(n)==='x'+mapResourceQuery+'z');
 run('shell','input','keyevent','123','67','122','112');state();
 waitNode('resourceFilter',n=>value(n)===mapResourceQuery);
 steps.push({step:'Native Android Home and End preserve Qt search editing'});capture('roundtrip-query-before');
 run('shell','input','keyevent','4');state();click('workspaceSearchButton');
 click('mapWorkspaceButton');waitNode('resourceRadar',n=>rect(n)[2]>rect(n)[0]&&rect(n)[3]>rect(n)[1]);
 click('resetRadar');const resetZoom=zoom();
 for(let i=0;i<16&&zoom()<25;i++)click('radarZoom');
 for(let i=0;i<16&&zoom()>100;i++)click('radarZoomOut');
 assert.ok(zoom()>=20&&zoom()<=100,'Prepare an observable, unclamped public zoom range');
 const ids=['mapBack','mapForward','mapOverview','mapSources','radarWorkspaceButton','radarZoomOut','radarZoom','resetRadar','toggleLandscapeFilters'];
 const controls=ids.map(id=>{const matches=nodes.filter(n=>n['resource-id']?.endsWith('.'+id));assert.equal(matches.length,1,id+' must have one public instance');const n=matches[0],b=rect(n);assert.deepEqual([b[2]-b[0]+1,b[3]-b[1]+1],[44,44],id+' must retain the Qt inclusive 44px rectangle');return {id,bounds:b,androidDelta:[b[2]-b[0],b[3]-b[1]],qtInclusiveSize:[44,44],enabled:n.enabled};});
 const edge=(id,position,expect)=>{
  const n=target(id),b=rect(n),before=zoom(),center=[Math.floor((b[0]+b[2])/2),Math.floor((b[1]+b[3])/2)];assert.equal(n.enabled,'true');
  const point=position==='right'?[b[2],center[1]]:position==='bottom'?[center[0],b[3]]:position==='corner'?[b[2],b[3]]:position==='outside-right'?[b[2]+1,center[1]]:[center[0],b[3]+1];
  run('shell','input','tap',...point.map(String));state();const after=zoom(),result={at:new Date().toISOString(),id,position,bounds:b,point,beforeZoomPercent:before,afterZoomPercent:after,expected:expect};fs.appendFileSync(path.join(out,'native-edge-touches.jsonl'),JSON.stringify(result)+'\n');capture('edge-'+id+'-'+position);
  if(expect==='increase')assert.ok(after>before,'Native last-pixel zoom-in touch must change visible zoom');else if(expect==='decrease')assert.ok(after<before,'Native last-pixel zoom-out touch must change visible zoom');else if(expect==='reset'){assert.notEqual(before,resetZoom);assert.equal(after,resetZoom);}else assert.equal(after,before,'The next coordinate beyond the 44px target must not activate zoom');
 };
 edge('radarZoom','right','increase');edge('radarZoomOut','bottom','decrease');edge('radarZoom','corner','increase');edge('radarZoom','outside-right','unchanged');edge('radarZoom','outside-bottom','unchanged');edge('resetRadar','corner','reset');
 capture('roundtrip-map-selection-setup');const initial=image('roundtrip-map-selection-setup'),b=rect(target('resourceRadar'));let waterPoint=null;
 for(let y=b[1]+2;y<b[3]-2&&!waterPoint;y++)for(let x=b[0]+2;x<b[2]-2;x++)if(['061621','071621'].includes(initial.rgb(x,y))){waterPoint=[x,y];break;}
 assert.ok(waterPoint,'Focus the real radar through visible water, not a hidden getter');run('shell','input','tap',...waterPoint.map(String));state();run('shell','input','keyevent','122');state();waitNode('radarInfoText',n=>value(n).includes('visual-config-0001'));
 const selection={title:value(find('radarInfoText')),namespace:value(target('radarInfoNamespace'))};
 assert.equal(selection.title,'ConfigMap visual-config-0001');assert.ok(selection.namespace&&selection.namespace!=='Cluster-scoped');
 click('closeRadarInfo');waitNode('resourceRadar',()=>!find('radarInfoText'));
 steps.push({step:'Native radar Home reveals the real ConfigMap and touch X closes its details'});
 click('radarZoom');click('radarZoomOut');waitNode('syncStatus',n=>/Synced:/i.test(value(n)));
 const visibleResourceCounts=()=>{
  const text=value(find('resourceMatchCount')),counts=/(\d+)\s*\/\s*(\d+)/.exec(text);
  assert.ok(counts,'Visible matching/cache counts must be readable: '+text);
  return {text,matching:Number(counts[1]),cached:Number(counts[2])};
 };
 const snapshot=name=>{const scene=rect(target('resourceRadar')),navigation=rect(target('radarNavigationOverlay')),counts=visibleResourceCounts(),percent=zoom();const canvas=[scene[0]+2,Math.max(scene[1]+2,navigation[3]+8),scene[2]-2,scene[3]-2];assert.ok(canvas[3]-canvas[1]>=16,'A nonempty terrain area must remain below navigation');capture(name);const pixels=image(name).crop(canvas);return {bounds:scene,canvas,matches:counts.text,matching:counts.matching,cached:counts.cached,zoomPercent:percent,sha256:crypto.createHash('sha256').update(pixels).digest('hex'),pixels};};
 const before=snapshot('map-filter-before'),requestBefore=counter('before');
 click('toggleLandscapeFilters');waitNode('toggleLandscapeFilters',n=>n.checked==='true');capture('map-filter-open');const requestOpen=counter('open');assert.equal(zoom(),before.zoomPercent,'Opening filters must retain visible zoom');assert.equal(visibleResourceCounts().matching,before.matching,'Opening filters must retain matching resources');
 click('toggleLandscapeFilters');waitNode('toggleLandscapeFilters',n=>n.checked==='false');const after=snapshot('map-filter-closed'),requestClosed=counter('closed');
 assert.deepEqual(after.bounds,before.bounds);assert.deepEqual(after.canvas,before.canvas);assert.equal(after.zoomPercent,before.zoomPercent);assert.equal(after.matching,before.matching);assert.equal(after.sha256,before.sha256,'Frozen public terrain pixels must roundtrip exactly');assert.equal(requestBefore.nativeWrites,0);assert.equal(requestOpen.nativeWrites,0,'Opening filters must not write to Kubernetes');assert.equal(requestClosed.nativeWrites,0,'Closing filters must not write to Kubernetes');
 click('workspaceSearchButton');waitNode('resourceFilter',n=>value(n)===mapResourceQuery);capture('roundtrip-query-after');run('shell','input','keyevent','4');state();click('workspaceSearchButton');const requestAfterQuery=counter('after-query-verification');assert.equal(requestAfterQuery.nativeWrites,0);
 if(find('radarInfoText')){click('closeRadarInfo');waitNode('resourceRadar',()=>!find('radarInfoText'));}
 const finalFrame=image('map-filter-closed');let finalWater=null;
 for(let y=after.canvas[1];y<after.canvas[3]&&!finalWater;y++)for(let x=after.canvas[0]+6;x<after.canvas[2]-6;x++)if(['061621','071621'].includes(finalFrame.rgb(x,y))){finalWater=[x,y];break;}
 assert.ok(finalWater,'Refocus on water without Home/End reselection or recentering');run('shell','input','tap',...finalWater.map(String));state();
 waitNode('resourceRadar',n=>n.focused==='true');capture('roundtrip-before-enter');
 run('shell','input','keyevent','66');state();
 waitNode('inspectorResourceName',n=>value(n).includes('ConfigMap/visual-config-0001'));click('yamlButton');
 target('inspectorYaml');state();
 waitNode('inspectorYaml',n=>value(n).split(/\r?\n|\u2028|\u2029/).some(line=>[selection.namespace,JSON.stringify(selection.namespace)].some(name=>line.trim()==='namespace: '+name)));
 capture('roundtrip-retained-selection');click('closeInspector');
 const report={status:'passed',displayDensity:density.trim(),controls,qtRectangleBoundary:'Qt 6.11.2 JNI passes inclusive QRect right/bottom directly into Android Rect; raw 43px subtraction is not the tested native hit extent',nativeTouchEvidence:'native-edge-touches.jsonl',before:{...before,pixels:undefined},after:{...after,pixels:undefined},selection:{...selection,verifiedAfter:'Native Enter after water focus opens the same kind/name/namespace, without Home/End reselection'},requests:[requestBefore,requestOpen,requestClosed,requestAfterQuery],filter:mapResourceQuery,motion:'Water disabled and Reduced motion enabled through public disposable-profile settings',scope:'Terrain pixels, public zoom/filter and retained selection; no private camera getters. Passive read totals remain diagnostic. Cache-only admission is separately asserted by the real_search/real_map HTTP-boundary scenarios. Inspector is explicitly opened only after the filter audit. Disabled Back/Forward are not activated.'};
 fs.writeFileSync(path.join(out,'touch-roundtrip.json'),JSON.stringify(report,null,2)+'\n');
 click('resourcesWorkspaceButton');search('');
}
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
if(inspectorCycles){
 click('closeInspector');
 const expectedPid=fs.readFileSync(path.join(out,'product-pid.txt'),'utf8').trim();
 for(let cycle=1;cycle<=inspectorCycles;cycle++){
  waitNode('cell_0_0',n=>(n.text+' '+n['content-desc']).includes('visual-multi-container'));
  click('cell_0_0');waitNode('inspectorResourceName',n=>(n.text+' '+n['content-desc']).includes('Pod/visual-multi-container'));
  click('closeInspector');
  if(run('shell','pidof','berlin.yuna.podlord').trim()!==expectedPid)throw Error('Product restarted during inspector lifecycle diagnostics');
  fs.appendFileSync(path.join(out,'inspector-lifecycle.jsonl'),JSON.stringify({completedCycle:cycle,pid:expectedPid})+'\n');
 }
 capture('inspector-lifecycle-final');
 steps.push({step:'Repeated real accessibility/touch inspector opening and closing without process restart',cycles:inspectorCycles});
}
function terminalJourney(){
search('visual-multi-container');waitNode('cell_0_0',n=>(n.text+' '+n['content-desc']).includes('visual-multi-container'));click('cell_0_0');
click('terminalButton');click('terminalConnect');
waitNode('terminalStatus',n=>/\bconnected\b/i.test(n.text+' '+n['content-desc']));
waitNode('terminalSurface',n=>n.focused==='true');inputAboveKeyboard(['terminalStatus','terminalSurface'],'container-terminal-connected');
run('shell','input','text','echo%sPODLORD_ANDROID_E2E_OK');run('shell','input','keyevent','66');state();inputAboveKeyboard(['terminalStatus','terminalSurface'],'container-terminal-typing');run('shell','input','keyevent','4');state();
waitNode('terminalDisconnect',n=>n.enabled==='true'&&!/\[0,0\]\[0,0\]/.test(n.bounds));
waitNode('terminalStatus',n=>/\bconnected\b/i.test(n.text+' '+n['content-desc']));
const terminalDeadline=Date.now()+20000;let output=false;
do{state();output=/(?:^|, |\r?\n|\u2029)PODLORD_ANDROID_E2E_OK(?:\r?\n|\u2029|$)/.test(describe('terminalSurface'));if(output)break;}while(Date.now()<terminalDeadline);
capture('container-terminal-echo');
if(!output)throw Error('The real container shell output marker was not exposed by the terminal');
click('terminalSurface');run('shell','input','text','exit');run('shell','input','keyevent','66');state();
waitNode('terminalStatus',n=>/\b(closed|exited|disconnected)\b/i.test(n.text+' '+n['content-desc']));
steps.push({step:'Native Kubernetes PTY connects, executes echo, returns output and exits without kubectl'});
click('collapseTerminal');click('closeInspector');
}

function sourceJourney(){
if(nodes.some(n=>n['resource-id']?.endsWith('.workspaceInspector')&&n.bounds!=='[0,0][0,0]'))click('closeInspector');
run('shell','settings','put','system','user_rotation','0');state();
click('settingsWorkspaceButton');click('settingsSectionPicker');
const sourceSection=nodes.find(n=>(n.text||n['content-desc'])==='Sources'&&n.enabled==='true'&&n.bounds!=='[0,0][0,0]');
if(!sourceSection)throw Error('Sources is unavailable in the public Settings picker');
tap(sourceSection);click('sourceRow_0');
waitNode('sourceOverviewTab',n=>n.checked==='true');capture('source-inspector-overview');
click('sourceYamlTab');waitNode('editSourceYaml',n=>n.enabled==='true');
waitNode('sourceInspectorYaml',n=>/read-only/.test(n['content-desc']||''));capture('source-yaml-read-only');
const importedFile=run('shell','cat','/data/user/0/berlin.yuna.podlord/files/release-fixture.kubeconfig');
click('editSourceYaml');click('sourceInspectorYaml');
inputAboveKeyboard(['sourceInspectorYaml'],'source-editor-keyboard');
if(nodes.some(n=>['sourceOverviewTab','sourceYamlTab','applySourceYaml'].some(id=>n['resource-id']?.endsWith('.'+id))&&n.bounds!=='[0,0][0,0]'))throw Error('Portrait Source editing did not prioritize YAML while the keyboard is visible');
run('shell','input','keyevent','123','66');run('shell','input','text',"'#%sandroid-owned-source-draft'");
run('shell','input','keyevent','4');state();
waitNode('sourceInspectorYaml',n=>(n.text+' '+n['content-desc']).includes('android-owned-source-draft'));
waitNode('applySourceYaml',n=>n.enabled==='true'&&n.bounds!=='[0,0][0,0]');
capture('source-yaml-local-draft');click('applySourceYaml');
waitNode('sourceOverviewTab',n=>n.checked==='true');click('sourceYamlTab');
waitNode('sourceInspectorYaml',n=>/read-only/.test(n['content-desc']||'')&&(n.text+' '+n['content-desc']).includes('android-owned-source-draft'));
if(run('shell','cat','/data/user/0/berlin.yuna.podlord/files/release-fixture.kubeconfig')!==importedFile)throw Error('Source Apply modified the original imported file');
capture('source-yaml-applied');click('closeSourceInspector');click('resourcesWorkspaceButton');
waitNode('cell_0_0',n=>(n.text+' '+n['content-desc']).includes('visual-multi-container'));
steps.push({step:'Portrait Sources touch editing keeps YAML above the keyboard, restores actions on dismissal and preserves the original file and existing session on immutable Apply'});
}

sourceJourney();
completeJourney();
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
const bytes=files.reduce((sum,line)=>{if(!/^[0-9]+ .+$/.test(line))throw Error('Invalid installed file measurement');const n=Number(line.slice(0,line.indexOf(' ')));if(!Number.isSafeInteger(n)||n<0||!Number.isSafeInteger(sum+n))throw Error('Invalid installed file measurement');return sum+n},0);
if(bytes<=0)throw Error('Invalid installed file measurement');
const warnings=bytes>100000000?[`WARNING: package-size advisory: platform=android architecture=arm64-v8a metric=installed measured_bytes=${bytes} target_bytes=100000000`]:[];
const data={status:'passed',sizePolicy:'advisory',sizeStatus:warnings.length?'advisory-exceeded':'within-target',warnings,scope:'Actual signed APK install, initial portrait/OS-rotated landscape capture, installed code and app-private regular-file logical sum on disposable Android 36 ARM64 emulator',installedRegularFileBytes:bytes,installedLimitBytes:100000000,files:files.length,productLaunched:true,device:'headless emulator; not physical hardware',systemImage:fs.readFileSync(path.join(dir,'system-image.txt'),'utf8').trim(),pageSizeBytes:Number(fs.readFileSync(path.join(dir,'page-size.txt'),'utf8').trim()),cleanup:'pending trap'};
fs.writeFileSync(path.join(dir,'result.json'),JSON.stringify(data,null,2)+'\n');console.log(JSON.stringify(data));
for(const warning of warnings)console.warn(warning);
NODE
