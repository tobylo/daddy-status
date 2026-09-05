import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const html=readFileSync(new URL('../main/auth.html',import.meta.url),'utf8');
const source=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements=Object.fromEntries(['status','login','code','expiry'].map(id=>[id,{textContent:'',hidden:true}]));
let now=0, body={state:'code',user_code:'ABCD-EFGH',expires_in:2}, fail=false;
const context=vm.createContext({
 document:{getElementById:id=>elements[id]},
 Date:{now:()=>now}, AbortController,
 setTimeout:()=>1,clearTimeout:()=>{},setInterval:()=>{},
 fetch:async()=>{if(fail)throw Error('Offline');return {ok:true,json:async()=>body};}
});
vm.runInContext(source,context);
await new Promise(resolve=>setImmediate(resolve));
assert.equal(elements.code.textContent,'ABCD-EFGH');
assert.equal(elements.login.hidden,false);
now=2000;vm.runInContext('tick()',context);
assert.equal(elements.login.hidden,true);
assert.equal(elements.code.textContent,'');
body={state:'signed_in',user_code:'',expires_in:0};
await vm.runInContext('update()',context);
assert.match(elements.status.textContent,/Signed in/);
body={state:'code',user_code:'<script>alert(1)</script>',expires_in:900};
await vm.runInContext('update()',context);
assert.equal(elements.code.textContent,body.user_code);
fail=true;await vm.runInContext('update()',context);
assert.equal(elements.login.hidden,true);
assert.equal(elements.code.textContent,'');
assert.match(elements.status.textContent,/Cannot reach/);
fail=false;await vm.runInContext('update()',context);
assert.equal(elements.login.hidden,false);
console.log('Browser code expiry, sign-in, text rendering, disconnect and recovery tests passed');
