// Open_Cart
#include "web_server.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include "app.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "unload_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 1 adds POST /api/unloads/seed, which writes 8 FAKE unloads into the real history. Bench testing only.
#define UNLOAD_TEST_SEED 0

static const char* TAG = "web";

// ---------------------------------------------------------------- page ----
static const char INDEX_HTML[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Open_Cart</title>
<style>
:root{color-scheme:light dark;--bg:#fff;--fg:#111;--mut:#666;--card:#f2f2f2;--acc:#0a7d33;--warn:#b25b00}
@media (prefers-color-scheme:dark){:root{--bg:#111;--fg:#eee;--mut:#999;--card:#1e1e1e;--acc:#3ddc84;--warn:#ffb74d}}
body{margin:0;font:16px system-ui,sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:640px;margin:0 auto;padding:16px}
.w{font-size:min(22vw,120px);font-weight:700;text-align:center;font-variant-numeric:tabular-nums;line-height:1.1}
.u{font-size:24px;color:var(--mut)}
.s{text-align:center;color:var(--mut);min-height:1.4em}
.s b{color:var(--warn)}
.card{background:var(--card);border-radius:12px;padding:14px;margin:14px 0}
button{font:inherit;padding:12px 16px;border-radius:10px;border:0;background:var(--acc);color:#fff;margin:4px 4px 4px 0}
button.g{background:#777}button.r{background:#b3261e}
select{font:inherit;padding:10px;border-radius:8px;border:1px solid #888;width:100%;box-sizing:border-box;background:var(--bg);color:var(--fg);margin:6px 0}
input[type=number],input[type=text],input[type=password]{font:inherit;padding:10px;border-radius:8px;border:1px solid #888;width:100%;box-sizing:border-box;background:var(--bg);color:var(--fg)}
label{display:block;margin:8px 0 4px;color:var(--mut)}
.row{display:flex;gap:8px;align-items:center}
h2{font-size:17px;margin:0 0 6px}
small{color:var(--mut)}
.card.warn{border:2px solid var(--warn)}
.modal{position:fixed;inset:0;background:#000a;z-index:10;display:flex;align-items:center;justify-content:center;padding:16px}
.modal .card{max-width:420px;margin:0;background:var(--bg)}
.ft{margin-top:10px;padding:10px;border-radius:8px;background:var(--bg);text-align:center}
.ft b{font-size:28px;font-variant-numeric:tabular-nums}
.ul{text-align:center;font-size:22px;margin:10px 0;font-variant-numeric:tabular-nums;color:var(--mut)}
.ul.on{font-size:30px;font-weight:700;color:var(--acc);background:var(--card);border-radius:12px;padding:12px}
.tabs{display:flex;gap:8px;margin:14px 0 0}
.tabs button{flex:1;margin:0;background:var(--card);color:var(--fg)}
.tabs button.on{background:var(--acc);color:#fff}
table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}
th,td{text-align:right;padding:6px 4px;border-bottom:1px solid #8884}
th:first-child,td:first-child{text-align:left}
</style></head><body><main>
<div class="w"><span id="wt">--</span> <span class="u">lb</span></div>
<div class="s" id="st">connecting...</div>
<div class="ul" id="ul"></div>
<div style="text-align:center;margin:8px 0"><button id="ubtn" onclick="toggleunl()">Start unload</button></div>
<div id="hm" class="modal" style="display:none"><div class="card">
<h2>Unload history is full</h2>
<p id="hmt"></p>
<button onclick="hdl()">Download CSV, then continue</button>
<button class="g" onclick="hroll()">Roll over (overwrite the oldest)</button>
</div></div>
<div class="tabs"><button id="b_scale" class="on" onclick="tab('scale')">Home</button><button id="b_unl" onclick="tab('unl')">Unloads</button><button id="b_fields" onclick="tab('fields')">Fields</button><button id="b_set" onclick="tab('set')">Settings</button></div>

<div id="tab_scale">
<div class="card warn" id="hwarn" style="display:none"></div>
<div class="card">
<button onclick="post('/api/tare')">Tare</button>
<button class="g" onclick="post('/api/untare')">Clear tare</button>
<button class="r" onclick="zerosc()">ZERO</button>
<div><small id="tareinfo"></small></div>
</div>
<div class="card">
<label>Field being harvested (optional)</label>
<div class="row"><input id="fld" type="text" maxlength="31" placeholder="default field" onchange="savefield()">
<button onclick="savefield()">Set</button></div>
<select id="fsel" onchange="if(this.value)usefield(this.value)"><option value="">Choose a saved field...</option></select>
<small>New unloads are saved under this name.</small>
<div class="ft"><small>Total for this field</small><div><b id="ftot">--</b> lb <small id="fcnt"></small></div></div>
</div>
</div>

<div id="tab_fields" style="display:none">
<div class="card">
<h2>Add a field</h2>
<div class="row"><input id="nf" type="text" maxlength="31" placeholder="e.g. Section 34">
<button onclick="addfield()">Add</button></div>
</div>
<div class="card">
<h2>Saved fields</h2>
<div id="fl"><small>loading...</small></div>
<small>Tap Use to make a field current. Deleting a field does not change unloads already saved under it.</small>
</div>
</div>

<div id="tab_set" style="display:none">
<div class="card">
<h2>Calibration</h2>
<small>Put a known weight on the scale, enter it, press Set point. Use an empty scale (0) first,
then one or more known weights across the range you use. At least two points give a real calibration.</small>
<label>Known weight (lb)</label>
<div class="row"><input id="kw" type="number" inputmode="decimal" step="any" value="0">
<button onclick="addcal()">Set point</button></div>
<h2>Calibrate from a certified scale</h2>
<small>Weigh a load on a certified scale. Enter the weight this cart displayed for that load and the weight the
certified scale read. Do this after the zero point is set, and before changing the calibration in between.</small>
<label>Cart said (lb)</label>
<input id="cw" type="number" inputmode="decimal" step="any">
<label>Certified scale said (lb)</label>
<div class="row"><input id="xw" type="number" inputmode="decimal" step="any">
<button onclick="cmpcal()">Apply</button></div>
<div id="cal"><small>no points</small></div>
<button class="r" onclick="if(confirm('Clear all calibration points?'))post('/api/cal/clear')">Clear calibration</button>
<div><small id="raw"></small></div>
</div>

<div class="card">
<h2>Load cells</h2>
<small>Describe the cells under the cart. This sets the approximate reading before you calibrate with 2 or more known weights;
a full calibration overrides it. If you change the real cells, re-calibrate.</small>
<label>Number of cells</label><input id="c_n" type="number" min="1" max="12" step="1">
<label>Capacity of one cell (lb)</label><input id="c_cap" type="number" step="any">
<label>Rated output of one cell (mV/V)</label><input id="c_mv" type="number" step="any">
<button onclick="savecells()">Save</button>
<div><small id="c_info"></small></div>
</div>

<div class="card">
<h2>ADC</h2>
<div><small id="hl">checking...</small></div>
<small>The ADC reads its own temperature every 30 seconds and re-measures its offset when the temperature
moves 3 °C (5 °F) or more, so the zero stays put as the day warms up.</small>
</div>

<div class="card">
<h2>Settings</h2>
<small>Output changes take effect after a restart.</small>
<label><input type="checkbox" id="e_web"> Wi-Fi web page</label>
<label><input type="checkbox" id="e_sb"> Scoreboard serial output</label>
<label><input type="checkbox" id="e_iso"> ISOBUS virtual terminal</label>
<label>Display rounding (lb)</label><input id="inc" type="number" step="any">
<label>Filter samples (1-64, about 20 per second)</label><input id="win" type="number" min="1" max="64">
<label>Wi-Fi name</label><input id="ssid" type="text" maxlength="32">
<label>Wi-Fi password (8+ chars, leave blank to keep)</label><input id="pass" type="password" maxlength="63">
<button onclick="save()">Save and restart</button>
</div>

<div class="card">
<h2>Firmware update</h2>
<small id="fwver"></small><br>
<small>Choose the app image named <b>open_cart_vXXX.bin</b> (the build's firmware.bin, renamed). Do not use firmware.factory.bin.
Keep this page open until it says the update finished. The scale restarts by itself.</small>
<input id="fwfile" type="file" accept=".bin" style="margin:8px 0;max-width:100%">
<button onclick="fwup()">Upload and install</button>
<div><small id="fwmsg"></small></div>
</div>
</div>

<div id="tab_unl" style="display:none">
<div class="card">
<h2>Previous unloads</h2>
<div id="uh"><small>loading...</small></div>
<button onclick="dlcsv()">Download CSV</button>
<button class="r" onclick="if(confirm('Delete the whole unload history?'))clearhist()">Clear history</button>
</div>
<div class="card">
<h2>Unload detection</h2>
<label><input type="checkbox" id="u_en"> Detect unloads automatically</label>
<small>When on, an unload starts when the weight drops this far below the recent peak, and ends when it stops falling. When off, use the Start unload / End unload button.</small>
<label>Start when weight drops by (lb)</label><input id="u_start" type="number" step="any">
<label>Unload ends after no drop for (seconds, 1-9)</label><input id="u_quiet" type="number" step="any">
<label>Ignore unloads smaller than (lb)</label><input id="u_min" type="number" step="any">
<button onclick="saveunl()">Save</button>
</div>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s==null?'':s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
async function post(u,b){let j=null;try{const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b||''});j=await r.json();}catch(e){}tick();return j;}
async function cmpcal(){const c=$('cw').value,x=$('xw').value;if(c===''||x==='')return alert('Enter both weights');
 const j=await post('/api/cal/compare','cart='+encodeURIComponent(c)+'&cert='+encodeURIComponent(x));
 if(j&&!j.ok)alert(j.error||'could not apply');else{$('cw').value='';$('xw').value='';}}
async function zerosc(){if(!confirm('Zero the scale? Only do this with the cart EMPTY. This shifts the saved calibration zero and clears the tare.'))return;
 const j=await post('/api/zero');if(j&&!j.ok)alert(j.error||'could not zero');}
async function addcal(){const j=await post('/api/cal/add','lb='+encodeURIComponent($('kw').value));if(j&&!j.ok)alert(j.error||'could not add point');}
async function tick(){
 try{
  const j=await (await fetch('/api/state',{cache:'no-store'})).json();
  $('wt').textContent=j.ready&&!j.stale?j.display.toFixed(0):'--';
  let s=[];
  if(j.stale)s.push('<b>no data from the ADC</b>');
  else if(!j.ready)s.push('warming up');
  else if(!j.has_cal)s.push('<b>not calibrated</b>');
  else if(j.approx)s.push('<b>approximate (zero only)</b>');
  if(j.motion)s.push('<b>motion</b>');
  $('st').innerHTML=s.join(' · ')||'stable';
  const f=n=>Math.round(n).toLocaleString();
  $('ubtn').textContent=j.ul_man||j.ul_act?'End unload':'Start unload';
  if(j.ul_act){$('ul').className='ul on';$('ul').textContent='Unloading: '+f(j.ul_run)+' lb';}
  else{$('ul').className='ul';$('ul').textContent=j.ul_has?'Last unload: '+f(j.ul_last)+' lb':'';}
  $('ftot').textContent=f(j.ft);$('fcnt').textContent='('+j.fn+' unload'+(j.fn===1?'':'s')+')';
  const near=j.hc>=Math.floor(j.hcap*0.9)&&!j.hneed;
  $('hwarn').style.display=near?'':'none';
  if(near)$('hwarn').innerHTML='<b>Unload history is nearly full</b> ('+j.hc+' of '+j.hcap+'). When it fills, the oldest unloads get overwritten. Download a copy now.<br><button onclick="dlcsv()">Download CSV</button>';
  $('hm').style.display=j.hneed?'':'none';
  if(j.hneed)$('hmt').textContent='All '+j.hcap+' unload slots are used.'+(j.hp?' '+j.hp+' new unload'+(j.hp===1?' is':'s are')+' waiting to be saved.':'')+' Download the history first, or roll over and overwrite the oldest unloads.';
  if(j.ul_seq!==lastseq){lastseq=j.ul_seq;if(cur==='unl')loadhist();}
  $('tareinfo').textContent='tare: '+j.tare.toFixed(0)+' lb · gross: '+j.gross.toFixed(0)+' lb';
  $('raw').textContent='raw counts: '+j.raw.toFixed(0)+' · calibration points: '+j.n_cal;
  $('hl').textContent=(j.tv?'Temperature '+j.tc.toFixed(1)+' °C ('+(j.tc*1.8+32).toFixed(0)+' °F) · ':'')+'offset '+j.off+' counts, re-measured '+j.orc+' times · resets: '+j.rec;
  const c=await (await fetch('/api/cal',{cache:'no-store'})).json();
  $('cal').innerHTML=c.points.length?c.points.map(p=>'<div>'+p.lb.toFixed(1)+' lb &rarr; '+p.raw.toFixed(0)+' counts</div>').join(''):'<small>no points</small>';
 }catch(e){$('st').textContent='connection lost';}
}
let lastseq=null,cur='scale';
function tab(t){cur=t;['scale','unl','fields','set'].forEach(x=>{$('tab_'+x).style.display=x===t?'':'none';$('b_'+x).className=x===t?'on':'';});
 if(t==='unl'){loadhist();loadunl();}if(t==='fields'||t==='scale')loadfield();}
async function loadhist(){
 try{const j=await (await fetch('/api/unloads',{cache:'no-store'})).json();
  const f=n=>Math.round(n).toLocaleString();let tot=0;
  const rows=j.items.map(r=>{tot+=r.lb;const d=r.t?new Date(r.t*1000).toLocaleString([], {month:'short',day:'numeric',hour:'numeric',minute:'2-digit'}):'';
   return '<tr><td>'+r.n+'</td><td>'+d+'</td><td>'+esc(r.f)+'</td><td>'+f(r.s)+'</td><td>'+f(r.e)+'</td><td><b>'+f(r.lb)+'</b></td></tr>';}).join('');
  $('uh').innerHTML=j.items.length?'<table><tr><th>#</th><th>When</th><th>Field</th><th>Start</th><th>End</th><th>Unloaded</th></tr>'+rows+'</table><small>'+j.items.length+' unloads, '+f(tot)+' lb total. Keeps the last '+j.cap+'; older ones are overwritten.</small>':'<small>no unloads recorded yet</small>';
 }catch(e){}
}
const csvurl=()=>'/api/unloads.csv?tz='+(-new Date().getTimezoneOffset());
async function dlcsv(){   // fetch first so the whole file is saved before anything is overwritten
 const r=await fetch(csvurl());if(!r.ok)throw new Error('download failed');
 const u=URL.createObjectURL(await r.blob());const a=document.createElement('a');
 a.href=u;a.download='unloads.csv';document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(u),10000);
}
async function hdl(){try{await dlcsv();}catch(e){alert('Download failed. Nothing was overwritten.');return;}await post('/api/unloads/continue');}
async function hroll(){if(confirm('Overwrite the oldest unloads as new ones come in?'))await post('/api/unloads/continue');}
async function clearhist(){const j=await post('/api/unloads/clear');if(j&&!j.ok)alert(j.error);lastseq=null;loadhist();}
async function toggleunl(){const j=await post('/api/unload/toggle');if(j&&!j.ok)alert(j.error||'could not toggle unload');}
async function loadunl(){const j=await (await fetch('/api/unload/config',{cache:'no-store'})).json();$('u_en').checked=!!j.en;$('u_start').value=j.start;$('u_quiet').value=j.quiet;$('u_min').value=j.min;}
async function saveunl(){const p=new URLSearchParams();p.set('en',$('u_en').checked?1:0);p.set('start',$('u_start').value);p.set('quiet',$('u_quiet').value);p.set('min',$('u_min').value);const j=await post('/api/unload/config',p.toString());if(!j||!j.ok)alert(j&&j.error||'could not save');loadunl();}
let loaded=false;
async function loadcells(){
 const j=await (await fetch('/api/cells',{cache:'no-store'})).json();
 $('c_n').value=j.n;$('c_cap').value=j.cap;$('c_mv').value=j.mv;
 $('c_info').innerHTML='Full scale '+Math.round(j.total).toLocaleString()+' lb · '+j.cpl.toFixed(2)+' counts/lb · excitation about '+Math.round(j.ma)+' mA (assumes '+Math.round(j.ohm)+' &Omega; cells)'+(j.ma>140?' <b>over the 150 mA supply limit</b>':'');
}
async function savecells(){
 const p=new URLSearchParams();p.set('n',$('c_n').value);p.set('cap',$('c_cap').value);p.set('mv',$('c_mv').value);
 const j=await post('/api/cells',p.toString());if(j&&!j.ok)alert(j.error||'invalid values');loadcells();
}
async function loadfield(){try{
 const j=await (await fetch('/api/fields',{cache:'no-store'})).json();
 const items=j.items.slice().sort((a,b)=>a.localeCompare(b));
 if(document.activeElement!==$('fld'))$('fld').value=j.current;
 $('fsel').innerHTML='<option value="">Choose a saved field...</option>'+items.map(n=>'<option>'+esc(n)+'</option>').join('');
 $('fsel').value=items.includes(j.current)?j.current:'';
 $('fl').innerHTML=items.length?items.map(n=>'<div class="row"><span style="flex:1">'+esc(n)+(n===j.current?' <small>(current)</small>':'')+'</span>'+
  '<button data-n="'+esc(n)+'" onclick="usefield(this.dataset.n)">Use</button>'+
  '<button class="r" data-n="'+esc(n)+'" onclick="delfield(this.dataset.n)">Delete</button></div>').join(''):'<small>No saved fields yet. Add one above, or type a name on the Home tab.</small>';
}catch(e){}}
async function usefield(n){await post('/api/field','field='+encodeURIComponent(n));loadfield();}
function savefield(){return usefield($('fld').value);}
async function addfield(){const j=await post('/api/fields/add','field='+encodeURIComponent($('nf').value));
 if(j&&!j.ok)alert(j.error||'could not add field');else $('nf').value='';loadfield();}
async function delfield(n){if(!confirm('Delete field "'+n+'" from the list?'))return;await post('/api/fields/delete','field='+encodeURIComponent(n));loadfield();}
async function loadSettings(){
 const j=await (await fetch('/api/settings',{cache:'no-store'})).json();
 $('e_web').checked=!!j.web;$('e_sb').checked=!!j.sb;$('e_iso').checked=!!j.iso;
 $('inc').value=j.inc;$('win').value=j.win;$('ssid').value=j.ssid;
}
async function loadfw(){try{const j=await (await fetch('/api/fw',{cache:'no-store'})).json();$('fwver').textContent='Running version: '+j.ver;}catch(e){}}
function fwup(){
 const f=$('fwfile').files[0],m=$('fwmsg');
 if(!f){m.textContent='Choose a file first.';return;}
 if(!/^open_cart_v[0-9A-Za-z._-]+\.bin$/.test(f.name)){m.textContent='File name must look like open_cart_v123.bin';return;}
 if(!confirm('Install '+f.name+'? The scale restarts when it finishes.'))return;
 const x=new XMLHttpRequest();
 x.open('POST','/api/fw?name='+encodeURIComponent(f.name));
 x.upload.onprogress=e=>{if(e.lengthComputable)m.textContent='Uploading '+Math.round(100*e.loaded/e.total)+'%';};
 x.onload=()=>{let j={};try{j=JSON.parse(x.responseText);}catch(e){}
  if(x.status===200&&j.ok){m.textContent='Update installed. Restarting... reconnect to the scale Wi-Fi in about 20 seconds.';}
  else m.textContent='Update failed: '+(j.error||x.responseText||x.status)+'. The old firmware is still running.';};
 x.onerror=()=>{m.textContent='Connection lost during upload. The old firmware is still running; try again.';};
 m.textContent='Uploading 0%';
 x.send(f);
}
async function save(){
 const p=new URLSearchParams();
 p.set('web',$('e_web').checked?1:0);p.set('sb',$('e_sb').checked?1:0);p.set('iso',$('e_iso').checked?1:0);
 p.set('inc',$('inc').value);p.set('win',$('win').value);p.set('ssid',$('ssid').value);p.set('pass',$('pass').value);
 if(!$('e_web').checked&&!confirm('Wi-Fi web page will be off after restart. To get it back, hold the recovery button while powering up. Continue?'))return;
 const j=await post('/api/settings',p.toString());
 if(!j||!j.ok){alert(j&&j.error||'could not save settings');return;}
 $('st').textContent='restarting... reconnect to the scale Wi-Fi';
}
post('/api/time','t='+Math.floor(Date.now()/1000));loadcells();loadSettings();loadfield();loadfw();tick();setInterval(tick,500);
</script></body></html>)HTML";

// -------------------------------------------------------------- helpers ----
static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decodes in place. A '%' not followed by two hex digits is kept as is; "%00" is dropped
// (a NUL would silently cut the value short).
static void url_decode(char* s) {
    char* o = s;
    while (*s) {
        if (*s == '+') {
            *o++ = ' ';
            ++s;
        } else if (*s == '%' && hex_val(s[1]) >= 0 && hex_val(s[2]) >= 0) {
            const char c = static_cast<char>(hex_val(s[1]) * 16 + hex_val(s[2]));
            if (c) *o++ = c;
            s += 3;
        } else {
            *o++ = *s++;
        }
    }
    *o = 0;
}

// Whole-string number parsing: rejects empty input, trailing junk, overflow, NaN and infinity.
static bool parse_double(const char* s, double* out) {
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(s, &end);
    if (end == s || errno == ERANGE || !std::isfinite(v)) return false;
    while (*end == ' ') ++end;
    if (*end) return false;
    *out = v;
    return true;
}

static bool parse_long(const char* s, long* out) {
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(s, &end, 10);
    if (end == s || errno == ERANGE) return false;
    while (*end == ' ') ++end;
    if (*end) return false;
    *out = v;
    return true;
}

// Find key in an application/x-www-form-urlencoded body. Returns true and fills out (decoded).
static bool form_get(const char* body, const char* key, char* out, size_t outlen) {
    size_t klen = std::strlen(key);
    const char* p = body;
    while (p && *p) {
        if (std::strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            size_t n = 0;
            while (p[n] && p[n] != '&') ++n;
            if (n >= outlen) n = outlen - 1;
            std::memcpy(out, p, n);
            out[n] = 0;
            url_decode(out);
            return true;
        }
        p = std::strchr(p, '&');
        if (p) ++p;
    }
    return false;
}

// Copy with truncation and guaranteed NUL termination.
static void copy_str(char* dst, size_t dstlen, const char* src) {
    size_t n = std::strlen(src);
    if (n >= dstlen) n = dstlen - 1;
    std::memcpy(dst, src, n);
    dst[n] = 0;
}

static bool read_body(httpd_req_t* req, char* buf, size_t cap) {
    size_t total = req->content_len;
    if (total >= cap) return false;
    size_t got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, total - got);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    buf[got] = 0;
    return true;
}

// Escape a string for JSON output.
static void json_escape(const char* in, char* out, size_t cap) {
    size_t o = 0;
    for (; *in && o + 7 < cap; ++in) {
        unsigned char c = static_cast<unsigned char>(*in);
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = static_cast<char>(c); }
        else if (c < 0x20) { o += std::snprintf(out + o, cap - o, "\\u%04x", c); }
        else out[o++] = static_cast<char>(c);
    }
    out[o] = 0;
}

static esp_err_t send_json(httpd_req_t* req, const char* json) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_ok(httpd_req_t* req) { return send_json(req, "{\"ok\":true}"); }

// {"ok":false,"error":"..."}: the page shows the message to the user.
static esp_err_t send_error(httpd_req_t* req, const char* msg) {
    char e[192], buf[224];
    json_escape(msg, e, sizeof(e));
    std::snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", e);
    return send_json(req, buf);
}

static const char* kNotSaved = "Applied, but it could not be saved to flash and will be lost at restart. Try again.";

// After a tare/zero/calibration change: report whether it reached flash.
static esp_err_t persist_and_reply(httpd_req_t* req) {
    return app_persist_scale() ? send_ok(req) : send_error(req, kNotSaved);
}

// Appends to buf at *o and keeps *o inside the buffer even if the text is cut short.
static void append(char* buf, size_t cap, size_t* o, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
static void append(char* buf, size_t cap, size_t* o, const char* fmt, ...) {
    if (*o >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf + *o, cap - *o, fmt, ap);
    va_end(ap);
    if (n > 0) *o = std::min(cap - 1, *o + static_cast<size_t>(n));
}

static void restart_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

// ------------------------------------------------------------- handlers ----
static esp_err_t h_index(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_state(httpd_req_t* req) {
    Snapshot s = app_snapshot();
    double ft_lb = 0;
    uint32_t ft_n = 0;
    field_total(&ft_lb, &ft_n);
    uint32_t h_count = 0, h_pend = 0;
    bool h_need = false;
    unload_log_status(&h_count, &h_need, &h_pend);
    char buf[896];
    std::snprintf(buf, sizeof(buf),
        "{\"tv\":%s,\"tc\":%.2f,\"off\":%ld,\"orc\":%u,\"rec\":%u,"
        "\"ul_man\":%s,\"ul_auto\":%s,\"display\":%.1f,\"net\":%.1f,\"gross\":%.1f,\"tare\":%.1f,\"raw\":%.1f,"
        "\"n_cal\":%u,\"motion\":%s,\"approx\":%s,\"has_cal\":%s,\"ready\":%s,\"stale\":%s,"
        "\"ul_act\":%s,\"ul_run\":%.1f,\"ul_has\":%s,\"ul_last\":%.1f,\"ul_seq\":%u,\"ft\":%.1f,\"fn\":%u,\"hc\":%u,\"hcap\":%d,\"hneed\":%s,\"hp\":%u}",
        s.adc.temp_valid ? "true" : "false", s.adc.temp_c,
        static_cast<long>(s.adc.offset), static_cast<unsigned>(s.adc.offset_recals), static_cast<unsigned>(s.adc.recoveries),
        s.unload_manual ? "true" : "false", s.unload_auto ? "true" : "false",
        s.display_lb, s.net_lb, s.gross_lb, s.tare_lb, s.raw, static_cast<unsigned>(s.n_cal),
        s.motion ? "true" : "false", s.approximate ? "true" : "false",
        s.has_cal ? "true" : "false", s.ready ? "true" : "false", s.stale ? "true" : "false",
        s.unloading ? "true" : "false", s.unload_running_lb, s.has_last_unload ? "true" : "false",
        s.last_unload_lb, static_cast<unsigned>(unload_log_newest_seq()), ft_lb, static_cast<unsigned>(ft_n),
        static_cast<unsigned>(h_count), UNLOAD_LOG_CAPACITY, h_need ? "true" : "false", static_cast<unsigned>(h_pend));
    return send_json(req, buf);
}

static esp_err_t h_cal_get(httpd_req_t* req) {
    CalPoint pts[MAX_CAL_POINTS];
    size_t n;
    {
        AppLock l;
        n = g_app.scale.get_calibration(pts, MAX_CAL_POINTS);
    }
    char buf[64 + MAX_CAL_POINTS * 64];
    size_t o = 0;
    append(buf, sizeof(buf), &o, "{\"points\":[");
    for (size_t i = 0; i < n; ++i)
        append(buf, sizeof(buf), &o, "%s{\"raw\":%.1f,\"lb\":%.2f}", i ? "," : "", pts[i].raw, pts[i].lb);
    append(buf, sizeof(buf), &o, "]}");
    return send_json(req, buf);
}

static esp_err_t h_tare(httpd_req_t* req) {
    { AppLock l; g_app.scale.tare(); }
    return persist_and_reply(req);
}

static esp_err_t h_zero(httpd_req_t* req) {
    bool ok;
    { AppLock l; ok = g_app.scale.zero(); }
    if (!ok) return send_error(req, "scale is still warming up");
    return persist_and_reply(req);
}

static esp_err_t h_untare(httpd_req_t* req) {
    { AppLock l; g_app.scale.clear_tare(); }
    return persist_and_reply(req);
}

static esp_err_t h_cal_add(httpd_req_t* req) {
    char body[64];
    if (!read_body(req, body, sizeof(body))) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    char v[24];
    double lb = 0;
    if (!form_get(body, "lb", v, sizeof(v)) || !parse_double(v, &lb)) return send_error(req, "enter the known weight as a number");
    bool ok;
    {
        AppLock l;
        ok = g_app.scale.add_cal_point(lb);
    }
    if (!ok) return send_error(req, "not ready, table full, or weights not increasing with signal");
    return persist_and_reply(req);
}

// Calibrate from "the cart said <cart> lb, a certified scale said <cert> lb".
static esp_err_t h_cal_compare(httpd_req_t* req) {
    char body[96];
    if (!read_body(req, body, sizeof(body))) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    char v[24];
    double cart = 0, cert = 0;
    if (!form_get(body, "cart", v, sizeof(v)) || !parse_double(v, &cart)) return send_error(req, "enter what the cart said as a number");
    if (!form_get(body, "cert", v, sizeof(v)) || !parse_double(v, &cert)) return send_error(req, "enter what the certified scale said as a number");
    bool ok;
    {
        AppLock l;
        ok = g_app.scale.add_cal_from_reading(cart, cert);
    }
    if (!ok) return send_error(req, "needs an existing calibration (set the zero first), table not full, and weights increasing with signal");
    return persist_and_reply(req);
}

static esp_err_t h_cal_clear(httpd_req_t* req) {
    { AppLock l; g_app.scale.clear_calibration(); }
    return persist_and_reply(req);
}

static esp_err_t h_settings_get(httpd_req_t* req) {
    Settings s;
    { AppLock l; s = g_app.settings; }
    char ssid[80];
    json_escape(s.ap_ssid, ssid, sizeof(ssid));
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "{\"web\":%u,\"sb\":%u,\"iso\":%u,\"inc\":%.2f,\"win\":%u,\"ssid\":\"%s\"}",
        s.enable_wifi_web, s.enable_scoreboard, s.enable_isobus,
        s.display_increment_lb, static_cast<unsigned>(s.filter_samples), ssid);
    return send_json(req, buf);
}

static esp_err_t start_restart(httpd_req_t* req, const char* json) {
    send_json(req, json);
    if (xTaskCreate(restart_task, "restart", 4096, nullptr, 5, nullptr) != pdPASS) esp_restart();
    return ESP_OK;
}

// Wi-Fi SSID is limited to 32 bytes and a WPA2 passphrase to 8..63.
static constexpr size_t kMaxSsidLen = 32, kMinPassLen = 8, kMaxPassLen = 63;

static esp_err_t h_settings_post(httpd_req_t* req) {
    char body[384];
    if (!read_body(req, body, sizeof(body))) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    const char* error = nullptr;
    const SaveResult r = app_update_settings([&](Settings& s) {
        char v[80];
        if (form_get(body, "web", v, sizeof(v))) s.enable_wifi_web = v[0] == '1';
        if (form_get(body, "sb", v, sizeof(v)))  s.enable_scoreboard = v[0] == '1';
        if (form_get(body, "iso", v, sizeof(v))) s.enable_isobus = v[0] == '1';
        if (form_get(body, "inc", v, sizeof(v))) {
            double d = 0;
            if (!parse_double(v, &d) || !display_increment_ok(d)) { error = "Display rounding must be more than 0 and at most 1000 lb"; return false; }
            s.display_increment_lb = d;
        }
        if (form_get(body, "win", v, sizeof(v))) {
            long w = 0;
            if (!parse_long(v, &w) || !filter_samples_ok(w)) { error = "Filter samples must be a whole number from 1 to 64"; return false; }
            s.filter_samples = static_cast<uint16_t>(w);
        }
        if (form_get(body, "ssid", v, sizeof(v))) {
            const size_t n = std::strlen(v);
            if (n < 1 || n > kMaxSsidLen) { error = "Wi-Fi name must be 1 to 32 characters"; return false; }
            copy_str(s.ap_ssid, sizeof(s.ap_ssid), v);
        }
        if (form_get(body, "pass", v, sizeof(v)) && v[0]) {   // blank keeps the current password
            const size_t n = std::strlen(v);
            if (n < kMinPassLen || n > kMaxPassLen) { error = "Wi-Fi password must be 8 to 63 characters"; return false; }
            copy_str(s.ap_password, sizeof(s.ap_password), v);
        }
        return true;
    });
    if (r == SaveResult::Invalid) return send_error(req, error ? error : "invalid settings");
    if (r == SaveResult::Failed) return send_error(req, "Settings could not be saved to flash. Nothing was changed; try again.");
    return start_restart(req, "{\"ok\":true,\"restart\":true}");
}

static esp_err_t h_cells_get(httpd_req_t* req) {
    CellConfig c;
    double cpl;
    { AppLock l; c = g_app.cells; cpl = g_app.scale.nominal_counts_per_lb(); }
    char buf[192];
    std::snprintf(buf, sizeof(buf), "{\"n\":%u,\"cap\":%.0f,\"mv\":%.3f,\"total\":%.0f,\"cpl\":%.3f,\"ma\":%.0f,\"ohm\":%.0f}",
        static_cast<unsigned>(c.n_cells), c.capacity_lb, c.mv_per_v, c.total_capacity_lb(), cpl,
        1000.0 * BRIDGE_EXCITATION_V * c.n_cells / CELL_RESISTANCE_OHM, CELL_RESISTANCE_OHM);
    return send_json(req, buf);
}

static esp_err_t h_cells_post(httpd_req_t* req) {
    char body[96];
    if (!read_body(req, body, sizeof(body))) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    CellConfig c;
    { AppLock l; c = g_app.cells; }
    char v[24];
    bool ok = true;
    long n = 0;
    double d = 0;
    if (form_get(body, "n", v, sizeof(v)))   { ok = ok && parse_long(v, &n) && CellConfig::cells_ok(n); if (ok) c.n_cells = static_cast<uint8_t>(n); }
    if (form_get(body, "cap", v, sizeof(v))) { ok = ok && parse_double(v, &d); if (ok) c.capacity_lb = static_cast<float>(d); }
    if (form_get(body, "mv", v, sizeof(v)))  { ok = ok && parse_double(v, &d); if (ok) c.mv_per_v = static_cast<float>(d); }
    if (!ok || !c.valid()) return send_error(req, "cells 1-12, capacity 100-1,000,000 lb, 0.5-5 mV/V");
    if (!app_save_cells(c)) return send_error(req, "could not save to flash; nothing was changed");
    return send_ok(req);
}

// Newest first, streamed because the full history is too big for one buffer.
static esp_err_t h_unloads_get(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char buf[256];
    std::snprintf(buf, sizeof(buf), "{\"cap\":%d,\"items\":[", UNLOAD_LOG_CAPACITY);
    httpd_resp_sendstr_chunk(req, buf);
    const uint32_t newest = unload_log_newest_seq();
    const size_t n = unload_log_count();
    bool first = true;
    for (size_t i = 0; i < n; ++i) {
        UnloadRecord r;
        if (!unload_log_read(newest - static_cast<uint32_t>(i), &r)) continue;
        char f[kFieldLen * 6 + 1];
        json_escape(r.field, f, sizeof(f));
        std::snprintf(buf, sizeof(buf), "%s{\"n\":%u,\"t\":%u,\"s\":%.1f,\"e\":%.1f,\"lb\":%.1f,\"f\":\"%s\"}",
                      first ? "" : ",", static_cast<unsigned>(r.seq), static_cast<unsigned>(r.epoch), r.start_lb, r.end_lb, r.total_lb, f);
        first = false;
        if (httpd_resp_sendstr_chunk(req, buf) != ESP_OK) return ESP_FAIL;
    }
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_sendstr_chunk(req, nullptr);
}

// CSV download, newest first. The board has no timezone, so the page passes its UTC offset
// in minutes as ?tz=. Unloads logged before the page set the clock have a blank time.
static esp_err_t h_unloads_csv(httpd_req_t* req) {
    long tz_min = 0;
    char q[32], v[16];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "tz", v, sizeof(v)) == ESP_OK) {
        tz_min = std::strtol(v, nullptr, 10);
        if (tz_min < -840 || tz_min > 840) tz_min = 0;
    }
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"unloads.csv\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "n,time,field,start_lb,end_lb,unload_lb\r\n");
    char buf[192];
    const uint32_t newest = unload_log_newest_seq();
    const size_t n = unload_log_count();
    for (size_t i = 0; i < n; ++i) {
        UnloadRecord r;
        if (!unload_log_read(newest - static_cast<uint32_t>(i), &r)) continue;
        char when[24] = "";
        if (r.epoch) {
            time_t t = static_cast<time_t>(r.epoch) + tz_min * 60;
            struct tm tm;
            gmtime_r(&t, &tm);
            std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tm);
        }
        // Field names never contain quotes (stripped on entry), so quoting is enough for commas.
        // A leading = + - @ would make a spreadsheet run the name as a formula: prefix a quote mark.
        const bool formula = std::strchr("=+-@", r.field[0]) != nullptr && r.field[0];
        std::snprintf(buf, sizeof(buf), "%u,%s,\"%s%s\",%.1f,%.1f,%.1f\r\n",
                      static_cast<unsigned>(r.seq), when, formula ? "'" : "", r.field, r.start_lb, r.end_lb, r.total_lb);
        if (httpd_resp_sendstr_chunk(req, buf) != ESP_OK) return ESP_FAIL;
    }
    return httpd_resp_sendstr_chunk(req, nullptr);
}

static esp_err_t h_field_get(httpd_req_t* req) {
    char f[kFieldLen], e[kFieldLen * 6 + 1], buf[kFieldLen * 6 + 24];
    field_get(f, sizeof(f));
    json_escape(f, e, sizeof(e));
    std::snprintf(buf, sizeof(buf), "{\"field\":\"%s\"}", e);
    return send_json(req, buf);
}

static esp_err_t h_field_post(httpd_req_t* req) {
    char body[160];
    if (!read_body(req, body, sizeof(body))) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    char v[96] = "";
    form_get(body, "field", v, sizeof(v));   // missing or blank resets to the default
    field_set(v);
    return h_field_get(req);
}

// {"current":"...","items":["...", ...]}: the current field and the saved list.
static esp_err_t h_fields_get(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char f[kFieldLen], e[kFieldLen * 6 + 1], buf[kFieldLen * 6 + 32];
    field_get(f, sizeof(f));
    json_escape(f, e, sizeof(e));
    std::snprintf(buf, sizeof(buf), "{\"current\":\"%s\",\"items\":[", e);
    httpd_resp_sendstr_chunk(req, buf);
    static char list[kMaxFields][kFieldLen];   // handlers run one at a time on the httpd task
    const size_t n = fields_list(list, kMaxFields);
    for (size_t i = 0; i < n; ++i) {
        json_escape(list[i], e, sizeof(e));
        std::snprintf(buf, sizeof(buf), "%s\"%s\"", i ? "," : "", e);
        if (httpd_resp_sendstr_chunk(req, buf) != ESP_OK) return ESP_FAIL;
    }
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_sendstr_chunk(req, nullptr);
}

static esp_err_t h_fields_add(httpd_req_t* req) {
    char body[160];
    if (!read_body(req, body, sizeof(body))) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    char v[96] = "";
    form_get(body, "field", v, sizeof(v));
    if (!field_add(v)) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "list is full (%u max), the name is blank, or it could not be saved",
                      static_cast<unsigned>(kMaxFields));
        return send_error(req, msg);
    }
    return send_ok(req);
}

static esp_err_t h_fields_delete(httpd_req_t* req) {
    char body[160];
    if (!read_body(req, body, sizeof(body))) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    char v[96] = "";
    if (form_get(body, "field", v, sizeof(v))) field_remove(v);
    return send_ok(req);
}

#if UNLOAD_TEST_SEED
// BENCH TESTING ONLY: POST /api/unloads/seed adds 8 fake unloads (all stamped with the current time).
static esp_err_t h_unloads_seed(httpd_req_t* req) {
    static const double s[] = {31200, 29850, 30400, 28100, 31000, 27500, 30100, 29000};
    static const double e[] = {2400, 1900, 3100, 800, 2600, 1200, 2200, 1500};
    for (size_t i = 0; i < sizeof(s) / sizeof(s[0]); ++i) unload_log_append(s[i], e[i], s[i] - e[i]);
    return send_ok(req);
}
#endif

static esp_err_t h_unloads_continue(httpd_req_t* req) {
    unload_log_continue();
    return send_ok(req);
}

static esp_err_t h_unloads_clear(httpd_req_t* req) {
    if (!unload_log_clear()) return send_error(req, "the history could not be erased; nothing was deleted");
    { AppLock l; g_app.unload.invalidate(); }
    return send_ok(req);
}

static esp_err_t h_unl_cfg_get(httpd_req_t* req) {
    UnloadConfig c = unload_cfg_load();
    char buf[112];
    std::snprintf(buf, sizeof(buf), "{\"start\":%.0f,\"quiet\":%.1f,\"min\":%.0f,\"en\":%d}", c.start_drop_lb, c.end_quiet_s, c.min_unload_lb, c.enabled ? 1 : 0);
    return send_json(req, buf);
}

static esp_err_t h_unload_toggle(httpd_req_t* req) {
    if (!app_toggle_unload()) return send_error(req, "scale is not ready or not calibrated");
    return send_ok(req);
}

static esp_err_t h_unl_cfg_post(httpd_req_t* req) {
    char body[128];
    if (!read_body(req, body, sizeof(body))) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    UnloadConfig c = unload_cfg_load();
    char v[24];
    double d = 0;
    if (form_get(body, "start", v, sizeof(v))) {
        if (!parse_double(v, &d) || !UnloadTracker::start_drop_ok(d)) return send_error(req, "start drop must be 10 to 20,000 lb");
        c.start_drop_lb = static_cast<float>(d);
    }
    if (form_get(body, "quiet", v, sizeof(v))) {
        if (!parse_double(v, &d) || !UnloadTracker::quiet_ok(d)) return send_error(req, "end time must be 1 to 9 seconds");
        c.end_quiet_s = static_cast<float>(d);
    }
    if (form_get(body, "min", v, sizeof(v))) {
        if (!parse_double(v, &d) || !UnloadTracker::min_unload_ok(d)) return send_error(req, "minimum unload must be 0 to 100,000 lb");
        c.min_unload_lb = static_cast<float>(d);
    }
    if (form_get(body, "en", v, sizeof(v))) c.enabled = v[0] == '1';
    if (!app_save_unload_cfg(c)) return send_error(req, "could not save to flash; nothing was changed");
    return send_ok(req);
}

// The page sends its clock so unloads can be time-stamped (the board has no RTC).
static esp_err_t h_time(httpd_req_t* req) {
    char body[48];
    if (!read_body(req, body, sizeof(body))) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    char v[24];
    if (form_get(body, "t", v, sizeof(v)) && v[0] >= '0' && v[0] <= '9') {   // digits only: strtoull accepts "-1"
        char* end = nullptr;
        errno = 0;
        const unsigned long long t = std::strtoull(v, &end, 10);
        if (*end == 0 && errno == 0 && t <= UINT32_MAX) clock_set_from_client(static_cast<uint32_t>(t));
    }
    return send_ok(req);
}

// ------------------------------------------------------ firmware update ----
static esp_err_t h_fw_get(httpd_req_t* req) {
    const esp_app_desc_t* d = esp_app_get_description();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "{\"ver\":\"%s\",\"date\":\"%s %s\"}", d->version, d->date, d->time);
    return send_json(req, buf);
}

static esp_err_t fw_fail(httpd_req_t* req, const char* msg) {
    ESP_LOGW(TAG, "firmware update rejected: %s", msg);
    httpd_resp_set_status(req, "400 Bad Request");
    char buf[128];
    std::snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg);
    return send_json(req, buf);
}

// Raw binary POST to /api/fw?name=open_cart_vXXX.bin. The name is checked here as well as in the page.
// The image is written to the inactive OTA slot; esp_ota_end() verifies the image checksum/hash, and the
// app description must carry this project's name so a bootloader or factory image is refused.
static esp_err_t h_fw_post(httpd_req_t* req) {
    char q[96], name[80] = "";
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) httpd_query_key_value(q, "name", name, sizeof(name));
    url_decode(name);
    const size_t nl = std::strlen(name);
    bool name_ok = nl > 12 && nl < 64 && std::strncmp(name, "open_cart_v", 11) == 0 && std::strcmp(name + nl - 4, ".bin") == 0;
    for (size_t i = 11; name_ok && i < nl - 4; ++i) {
        char c = name[i];
        name_ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '-';
    }
    if (!name_ok) return fw_fail(req, "file name must look like open_cart_v123.bin");

    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part) return fw_fail(req, "no update partition");
    const size_t total = req->content_len;
    if (total < 256 * 1024 || total > part->size) return fw_fail(req, "file size is not a valid firmware image");

    esp_ota_handle_t h = 0;
    bool began = false, checked = false;
    size_t got = 0;
    static char buf[2048];   // handlers run one at a time on the httpd task
    constexpr int kMaxIdleTimeouts = 6;   // about 30 s with no data (5 s receive timeout each)
    int idle = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf, std::min(sizeof(buf), total - got));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++idle > kMaxIdleTimeouts) break;
            continue;
        }
        if (r <= 0) break;
        idle = 0;
        if (!checked) {
            // First chunk: image magic and this project's app description.
            const size_t off = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
            while (static_cast<size_t>(r) < off + sizeof(esp_app_desc_t)) {   // the first read can be short
                int m = httpd_req_recv(req, buf + r, sizeof(buf) - r);
                if (m == HTTPD_SOCK_ERR_TIMEOUT) {
                    if (++idle > kMaxIdleTimeouts) return fw_fail(req, "upload stalled");
                    continue;
                }
                if (m <= 0) return fw_fail(req, "upload interrupted");
                idle = 0;
                r += m;
            }
            if (static_cast<size_t>(r) < off + sizeof(esp_app_desc_t) || static_cast<uint8_t>(buf[0]) != ESP_IMAGE_HEADER_MAGIC) {
                return fw_fail(req, "not an app image (use firmware.bin, not firmware.factory.bin)");
            }
            esp_app_desc_t nd;
            std::memcpy(&nd, buf + off, sizeof(nd));
            if (nd.magic_word != ESP_APP_DESC_MAGIC_WORD || std::strncmp(nd.project_name, esp_app_get_description()->project_name, sizeof(nd.project_name)) != 0) {
                return fw_fail(req, "image is not Open_Cart firmware");
            }
            if (esp_ota_begin(part, total, &h) != ESP_OK) return fw_fail(req, "could not start flash write");
            began = checked = true;
            ESP_LOGI(TAG, "firmware update: %s (%s), %u bytes", name, nd.version, static_cast<unsigned>(total));
        }
        if (esp_ota_write(h, buf, r) != ESP_OK) { esp_ota_abort(h); return fw_fail(req, "flash write failed"); }
        got += static_cast<size_t>(r);
    }
    if (got != total) {
        if (began) esp_ota_abort(h);
        return fw_fail(req, "upload interrupted");
    }
    if (esp_ota_end(h) != ESP_OK) return fw_fail(req, "image is corrupt (checksum failed)");
    if (esp_ota_set_boot_partition(part) != ESP_OK) return fw_fail(req, "could not select the new firmware");
    ESP_LOGI(TAG, "firmware update installed, restarting");
    return start_restart(req, "{\"ok\":true}");
}

// ---------------------------------------------------------------- start ----
// Logs and returns false on the first failing step. Wi-Fi failing must not stop the scale itself
// (sampling and unload logging keep running), so nothing here restarts the board.
#define WIFI_TRY(expr)                                                                   \
    do {                                                                                 \
        const esp_err_t e_ = (expr);                                                     \
        if (e_ != ESP_OK) {                                                              \
            ESP_LOGE(TAG, "Wi-Fi start failed at %s: %s", #expr, esp_err_to_name(e_));   \
            return false;                                                                \
        }                                                                                \
    } while (0)

static bool wifi_ap_start() {
    WIFI_TRY(esp_netif_init());
    WIFI_TRY(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_ap()) {
        ESP_LOGE(TAG, "Wi-Fi start failed: could not create the AP interface");
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    WIFI_TRY(esp_wifi_init(&cfg));

    Settings s;
    { AppLock l; s = g_app.settings; }

    wifi_config_t wc{};
    size_t ssid_len = std::strlen(s.ap_ssid);
    if (ssid_len > sizeof(wc.ap.ssid)) ssid_len = sizeof(wc.ap.ssid);
    std::memcpy(wc.ap.ssid, s.ap_ssid, ssid_len);
    wc.ap.ssid_len = static_cast<uint8_t>(ssid_len);
    wc.ap.channel = 6;
    wc.ap.max_connection = 4;
    size_t pw_len = std::strlen(s.ap_password);
    if (pw_len >= 8) {
        if (pw_len > 63) pw_len = 63;
        std::memcpy(wc.ap.password, s.ap_password, pw_len);
        wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wc.ap.authmode = WIFI_AUTH_OPEN;   // only if somebody saved a short password; the UI blocks this
        ESP_LOGW(TAG, "AP password too short, running OPEN");
    }
    WIFI_TRY(esp_wifi_set_mode(WIFI_MODE_AP));
    WIFI_TRY(esp_wifi_set_config(WIFI_IF_AP, &wc));
    WIFI_TRY(esp_wifi_start());
    // Full transmit power (units of 0.25 dBm, 80 = 20 dBm, the hardware maximum) and no modem sleep.
    // Must be set after esp_wifi_start().
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (esp_wifi_set_max_tx_power(80) != ESP_OK) ESP_LOGW(TAG, "could not set max TX power");
    int8_t txp = 0;
    if (esp_wifi_get_max_tx_power(&txp) == ESP_OK) ESP_LOGI(TAG, "Wi-Fi max TX power %.2f dBm", txp * 0.25);
    ESP_LOGI(TAG, "AP \"%s\" up; browse to http://192.168.4.1/", s.ap_ssid);

    // http://opencart.local/ (mDNS). Works on iPad/iPhone/Mac out of the box; Windows 10+ and
    // recent Android usually work too. The IP address always works as a fallback.
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set("opencart");
        mdns_instance_name_set("Open_Cart");
        mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
        ESP_LOGI(TAG, "mDNS: http://opencart.local/");
    } else {
        ESP_LOGW(TAG, "mDNS init failed; use the IP address");
    }
    return true;
}
#undef WIFI_TRY

static constexpr size_t kMaxUriHandlers = 32;

bool web_server_start() {
    if (!wifi_ap_start()) return false;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = kMaxUriHandlers;
    cfg.stack_size = 6144;
    httpd_handle_t srv = nullptr;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return false;
    }
    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, h_index, nullptr},
        {"/api/state", HTTP_GET, h_state, nullptr},
        {"/api/cal", HTTP_GET, h_cal_get, nullptr},
        {"/api/tare", HTTP_POST, h_tare, nullptr},
        {"/api/untare", HTTP_POST, h_untare, nullptr},
        {"/api/zero", HTTP_POST, h_zero, nullptr},
        {"/api/cal/add", HTTP_POST, h_cal_add, nullptr},
        {"/api/cal/compare", HTTP_POST, h_cal_compare, nullptr},
        {"/api/cal/clear", HTTP_POST, h_cal_clear, nullptr},
        {"/api/settings", HTTP_GET, h_settings_get, nullptr},
        {"/api/settings", HTTP_POST, h_settings_post, nullptr},
        {"/api/unloads", HTTP_GET, h_unloads_get, nullptr},
        {"/api/unloads.csv", HTTP_GET, h_unloads_csv, nullptr},
        {"/api/field", HTTP_GET, h_field_get, nullptr},
        {"/api/field", HTTP_POST, h_field_post, nullptr},
        {"/api/fields", HTTP_GET, h_fields_get, nullptr},
        {"/api/fields/add", HTTP_POST, h_fields_add, nullptr},
        {"/api/fields/delete", HTTP_POST, h_fields_delete, nullptr},
        {"/api/unloads/clear", HTTP_POST, h_unloads_clear, nullptr},
        {"/api/unloads/continue", HTTP_POST, h_unloads_continue, nullptr},
#if UNLOAD_TEST_SEED
        {"/api/unloads/seed", HTTP_POST, h_unloads_seed, nullptr},
#endif
        {"/api/unload/toggle", HTTP_POST, h_unload_toggle, nullptr},
        {"/api/unload/config", HTTP_GET, h_unl_cfg_get, nullptr},
        {"/api/unload/config", HTTP_POST, h_unl_cfg_post, nullptr},
        {"/api/time", HTTP_POST, h_time, nullptr},
        {"/api/cells", HTTP_GET, h_cells_get, nullptr},
        {"/api/cells", HTTP_POST, h_cells_post, nullptr},
        {"/api/fw", HTTP_GET, h_fw_get, nullptr},
        {"/api/fw", HTTP_POST, h_fw_post, nullptr},
    };
    static_assert(sizeof(routes) / sizeof(routes[0]) <= kMaxUriHandlers, "raise kMaxUriHandlers");
    bool ok = true;
    for (const auto& r : routes) {
        const esp_err_t err = httpd_register_uri_handler(srv, &r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "could not register %s: %s", r.uri, esp_err_to_name(err));
            ok = false;
        }
    }
    return ok;
}
