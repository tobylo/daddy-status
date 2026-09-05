import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const html=readFileSync(new URL('../main/auth.html',import.meta.url),'utf8');
const source=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements=Object.fromEntries([...html.matchAll(/id="([^"]+)"/g)].map(([,id])=>[id,{textContent:'',hidden:true}]));
const buttons=[...html.matchAll(/data-mode="([^"]+)"/g)].map(([,mode])=>({dataset:{mode},disabled:true,addEventListener(event,fn){this.click=fn;}}));
let now=0, fail=false, postStatus=200, lastRequest;
const base={connected:true,error:'none',service:'ready',activity:'Available',fresh:true,age_seconds:1,display:'green',poll_seconds:10,uptime_seconds:600,led_gpio:13,brightness_percent:100,test_seconds:0,control_token:'test-token'};
let body={...base,state:'code',user_code:'ABCD-EFGH',expires_in:2};
const context=vm.createContext({
 document:{getElementById:id=>elements[id],querySelectorAll:()=>buttons},
 Date:{now:()=>now}, AbortController,
 setTimeout:()=>1,clearTimeout:()=>{},setInterval:()=>{},
 fetch:async(url,options)=>{
  if(fail)throw Error('Offline');
  lastRequest={url,...options};
  if(options.method==='POST')return {ok:postStatus===200};
  return {ok:true,json:async()=>body};
 }
});
vm.runInContext(source,context);
await new Promise(resolve=>setImmediate(resolve));
assert.equal(elements.code.textContent,'ABCD-EFGH');
assert.equal(elements.login.hidden,false);
assert.equal(elements.gpio.textContent,'GPIO 13');
now=2000;vm.runInContext('tick()',context);
assert.equal(elements.login.hidden,true);
assert.equal(elements.code.textContent,'');
body={...base,state:'signed_in',user_code:'',expires_in:0};
await vm.runInContext('update()',context);
assert.equal(elements.account.textContent,'Signed in');
assert.match(elements.status.textContent,/receiving Teams presence/);
assert.equal(elements.activity.textContent,'Available');
body.error='permission';body.service='error';body.fresh=false;
await vm.runInContext('update()',context);
assert.match(elements.status.textContent,/denied access to presence/);
assert.equal(elements.freshness.textContent,'Unavailable or stale');
body={...base,state:'code',user_code:'<script>alert(1)</script>',expires_in:900,activity:'<img onerror=alert(1)>'};
await vm.runInContext('update()',context);
assert.equal(elements.code.textContent,body.user_code);
assert.equal(elements.activity.textContent,body.activity);
await buttons.find(b=>b.dataset.mode==='red').click();
assert.equal(lastRequest.url,'/api/led-test');
assert.equal(lastRequest.headers['X-Frame-Token'],'test-token');
assert.deepEqual(JSON.parse(lastRequest.body),{mode:'red'});
assert.match(elements['test-status'].textContent,/Test started/);
postStatus=403;await vm.runInContext('testLights("green")',context);
assert.match(elements['test-status'].textContent,/Could not confirm/);
fail=true;await vm.runInContext('update()',context);
assert.equal(elements.login.hidden,true);
assert.equal(elements.code.textContent,'');
assert.equal(elements.activity.textContent,'Unavailable');
assert.match(elements.status.textContent,/Cannot reach/);
assert.ok(buttons.every(b=>b.disabled));
fail=false;await vm.runInContext('update()',context);
assert.equal(elements.login.hidden,false);
assert.ok(buttons.every(b=>!b.disabled));
body={...base,state:'waiting',service:'clock',fresh:false};
await vm.runInContext('update()',context);
assert.match(elements.status.textContent,/clock synchronization/);
console.log('Browser auth, dashboard health, escaped text, LED actions, errors, and recovery passed');
