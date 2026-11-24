import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

// Execute the production script against a small DOM/transport fixture. This
// tests UI state transitions without pretending to test ESP32 flash hardware.
const html=readFileSync(new URL('../dashboard/index.html',import.meta.url),'utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
class Element {
  textContent='';value='';className='';checked=false;disabled=false;hidden=false;
  files=[];children=[];events={};scrollTop=0;scrollHeight=100;
  addEventListener(event,fn){this.events[event]=fn;}
  append(...nodes){this.children.push(...nodes);}
  replaceChildren(fragment){this.children=fragment.children;}
  click(){this.clicked=true;return this.events.click?.();}
}
const elements=new Map([...html.matchAll(/\bid="([^"]+)"/g)].map(m=>[m[1],new Element()]));
elements.get('follow').checked=true;
const el=id=>elements.get(id);
let bootId='boot-1',calls=[],requests=[],logs=[{id:1,ms:200,text:'Boot ready'},{id:2,ms:250,text:'Audison TX: 01 04 00 01 6C 00 72'}];
let stats={firmware:'test',build:'test',uptimeMs:1000,resetReason:'Power on',heapFree:100000,heapMin:90000,heapLargest:60000,
  chip:'ESP32',cores:2,cpuMHz:240,flashBytes:4194304,sketchBytes:1080000,otaBytes:1966080,partition:'app0',ssid:'test',ip:'192.168.4.1',clients:1,
  maintenance:false,canReady:true,canState:1,listenOnly:true,canFrames:100,canAgeMs:2,busErrors:0,rxErrors:0,rxMissed:0,rxOverruns:0,
  serialPackets:1,serialRxBytes:0,loops:1000,maxLoopUs:1400,volume:20,muted:false,audisonDb:-30,logLatest:2,logRetained:2,logCapacity:256};
let rejectFetch=false,heldLogs=null;
const response=(data,status=200)=>({ok:status<400,status,json:async()=>data});
class XHR {
  upload={};headers={};status=0;responseText='';
  constructor(){requests.push(this);}
  open(method,path){this.method=method;this.path=path;}
  setRequestHeader(k,v){this.headers[k]=v;}
  send(body){this.body=body;}
  complete(status,data){this.status=status;this.responseText=JSON.stringify(data);this.onload();}
}
const sandbox={console,Blob,AbortSignal,Date,
  document:{getElementById:el,createElement:()=>new Element(),createDocumentFragment:()=>new Element()},
  setTimeout:()=>1,clearTimeout:()=>{},confirm:()=>true,XMLHttpRequest:XHR,
  FormData:class{append(){}},URL:{createObjectURL:()=> 'blob:test',revokeObjectURL:()=>{}},
  fetch:async(path,options)=>{
    calls.push({path,options});if(rejectFetch)throw Error('Offline');
    if(path==='/api/session')return response({bootId,token:'token-'+bootId});
    if(path==='/api/stats')return response({...stats,bootId});
    if(path==='/api/reboot')return response({ok:true,rebooting:true});
    if(path.startsWith('/api/logs?')){
      if(heldLogs)return await heldLogs;
      const after=Number(path.split('=')[1]);const lines=logs.filter(l=>l.id>after);
      return response({bootId,lines,oldest:logs[0]?.id??1,latest:logs.at(-1)?.id??0,next:lines.at(-1)?.id??after,missed:0});
    }
    throw Error('Unexpected API '+path);
  }
};
vm.createContext(sandbox);
vm.runInContext(script+'\nglobalThis.ui={state,poll,collectLogs,renderLogs,controls};',sandbox);
const ui=sandbox.ui;
async function settle(){for(let i=0;i<30;i++)await Promise.resolve();}
await settle();
assert.equal(ui.state.online,true);assert.equal(ui.state.lines.length,2);
assert.equal(el('volume').textContent,'20 / 44');assert.equal(el('audison').textContent,'-30.0 dB');
assert.equal(ui.state.token,'token-boot-1');
el('search').value='Audison TX';el('search').events.input();assert.equal(el('logWindow').children.length,1);
el('search').value='';
await el('pause').click();const logCalls=calls.filter(c=>c.path.startsWith('/api/logs')).length;
await ui.poll();assert.equal(calls.filter(c=>c.path.startsWith('/api/logs')).length,logCalls);
assert.equal(ui.state.paused,true);
await el('history').click();await ui.poll();assert.equal(ui.state.lines.length,2);assert.equal(ui.state.paused,true);
await el('pause').click();

// Malicious log content must be text, never inserted as HTML.
logs.push({id:3,ms:300,text:'<img src=x onerror=alert(1)>'});await ui.poll();
assert.equal(el('logWindow').children.at(-1).children[1].textContent,'<img src=x onerror=alert(1)>');
assert.equal('innerHTML' in el('logWindow').children.at(-1),false);

// A late response from before "Load history" must not corrupt the new view.
let releaseLogs;heldLogs=new Promise(resolve=>{releaseLogs=resolve;});
const pending=ui.collectLogs();await settle();await el('history').click();
releaseLogs(response({bootId,lines:[{id:99,ms:999,text:'stale'}],next:99,latest:99,missed:0}));
await pending;heldLogs=null;assert.equal(ui.state.lines.length,0);assert.equal(ui.state.after,0);
await ui.poll();await el('pause').click();

el('firmwareFile').files=[{name:'wrong.txt',size:100}];el('firmwareFile').events.change();assert.equal(el('upload').disabled,true);
el('firmwareFile').files=[{name:'app.bin',size:2000000}];el('firmwareFile').events.change();assert.equal(el('upload').disabled,true);
el('firmwareFile').files=[{name:'app.bin',size:1080000}];el('firmwareFile').events.change();assert.equal(el('upload').disabled,false);
await el('upload').click();assert.equal(ui.state.uploading,true);assert.equal(el('reboot').disabled,true);
assert.equal(requests.at(-1).headers['X-Bridge-Token'],'token-boot-1');
assert.equal(requests.at(-1).headers['X-Firmware-Size'],'1080000');
requests.at(-1).upload.onprogress({lengthComputable:true,loaded:40,total:100});assert.equal(el('uploadProgress').value,40);
requests.at(-1).complete(400,{error:'Bad firmware image'});assert.equal(ui.state.uploading,false);assert.equal(ui.state.restarting,false);
assert.equal(el('uploadStatus').textContent,'Bad firmware image');
await el('upload').click();requests.at(-1).complete(200,{ok:true});assert.equal(ui.state.restarting,true);
bootId='boot-2';logs=[{id:1,ms:200,text:'New boot'}];await ui.poll();
assert.equal(ui.state.restarting,false);assert.equal(ui.state.token,'token-boot-2');assert.equal(ui.state.lines[0].text,'New boot');

await el('upload').click();requests.at(-1).onerror();assert.equal(ui.state.operation,'uncertain');
ui.state.restartSince=Date.now()-31000;await ui.poll();assert.equal(ui.state.restarting,false);
assert.match(el('uploadStatus').textContent,/not confirmed/);

await el('reboot').click();assert.equal(ui.state.restarting,true);
assert.equal(calls.at(-1).options.headers['X-Bridge-Token'],'token-boot-2');
bootId='boot-3';await ui.poll();assert.equal(el('rebootStatus').textContent,'Device restarted successfully.');
rejectFetch=true;await ui.poll();assert.equal(ui.state.online,false);assert.equal(el('reboot').disabled,true);
rejectFetch=false;await ui.poll();assert.equal(ui.state.online,true);assert.equal(el('reboot').disabled,false);
console.log('PASS: production dashboard script: stats, filtering/text safety, pause/history, stale responses, file validation, upload progress/failure, token renewal, reboot recovery, uncertain upload recovery, disconnect/reconnect');
