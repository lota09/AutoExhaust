#ifndef WEBUI_H
#define WEBUI_H

// Single-page dashboard. Static: all data comes from /api/* (docs/API.md).
static const char WEB_UI[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="ko"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AutoExhaust</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2129;--sub:#6b7280;--line:#e5e7eb;--acc:#2563eb;
--ok:#16a34a;--warn:#d97706;--bad:#dc2626;--track:#e5e7eb;color-scheme:light}
@media (prefers-color-scheme:dark){:root{--bg:#0f1115;--card:#181b21;--fg:#e8eaed;--sub:#9aa0a6;
--line:#262a31;--acc:#60a5fa;--ok:#4ade80;--warn:#fbbf24;--bad:#f87171;--track:#2a2f37;color-scheme:dark}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",
"Apple SD Gothic Neo","Malgun Gothic",sans-serif}
main{max-width:560px;margin:0 auto;padding:16px}
header{display:flex;align-items:center;justify-content:space-between;margin:4px 2px 14px}
h1{font-size:18px;margin:0;font-weight:650}
.conn{font-size:12px;color:var(--sub);display:flex;align-items:center;gap:6px}
.conn i{width:8px;height:8px;border-radius:50%;background:var(--ok)}
.conn.off i{background:var(--bad)}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:12px}
.card h2{font-size:13px;font-weight:600;color:var(--sub);margin:0 0 12px;letter-spacing:.02em}
.hero{display:flex;align-items:center;gap:14px}
.dot{width:44px;height:44px;border-radius:50%;flex:none;background:#9ca3af}
.st-RUNNING .dot{background:conic-gradient(red,#ff0,lime,cyan,blue,#f0f,red);animation:spin 4s linear infinite}
.st-COOLDOWN .dot{background:#2563eb;animation:breathe 3s ease-in-out infinite}
.st-MANUAL .dot{background:#8b2cf5}
.st-OFFLINE .dot{background:#f97316;animation:blink 1s steps(1) infinite}
.st-FAULT .dot{background:#dc2626;animation:blink .25s steps(1) infinite}
.st-IDLE .dot{background:var(--track)}
@keyframes spin{to{transform:rotate(1turn)}}
@keyframes breathe{50%{opacity:.15}}
@keyframes blink{50%{opacity:.15}}
.state{font-size:22px;font-weight:700}
.desc{color:var(--sub);font-size:13px}
.fault{display:none;margin-top:12px;padding:10px 12px;border-radius:10px;background:color-mix(in srgb,var(--bad) 14%,transparent);color:var(--bad);font-size:13px}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.stat{background:var(--bg);border-radius:10px;padding:10px}
.stat b{display:block;font-size:19px;font-weight:650}
.stat span{font-size:12px;color:var(--sub)}
.bar{height:4px;border-radius:2px;background:var(--track);margin-top:6px;overflow:hidden}
.bar i{display:block;height:100%;background:var(--acc);width:0;transition:width .4s}
.row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-top:1px solid var(--line)}
.row:first-of-type{border-top:0}
.row span{color:var(--sub)}
button{font:inherit;border:0;border-radius:10px;padding:12px 14px;cursor:pointer;font-weight:600;
background:var(--bg);color:var(--fg);border:1px solid var(--line)}
button:disabled{opacity:.5;cursor:default}
.primary{width:100%;background:var(--acc);color:#fff;border-color:transparent}
.primary.stop{background:var(--bad)}
.slider{margin:14px 0 4px}
.slider label{display:flex;justify-content:space-between;font-size:14px}
.slider small{color:var(--sub)}
input[type=range]{width:100%;margin:10px 0 0;accent-color:var(--acc)}
.switch{position:relative;width:46px;height:26px;border-radius:13px;background:var(--track);border:0;padding:0}
.switch::after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#fff;transition:.2s}
.switch.on{background:var(--ok)}.switch.on::after{left:23px}
details summary{cursor:pointer;color:var(--sub);font-size:13px;font-weight:600}
pre{margin:10px 0 0;max-height:260px;overflow:auto;background:var(--bg);border-radius:10px;padding:10px;
font:12px/1.5 ui-monospace,Consolas,monospace;white-space:pre-wrap;word-break:break-all}
.toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--fg);color:var(--bg);
padding:10px 16px;border-radius:10px;font-size:14px;opacity:0;transition:opacity .2s;pointer-events:none}
.toast.show{opacity:1}
footer{text-align:center;color:var(--sub);font-size:12px;margin:8px 0 24px}
</style></head><body><main>
<header><h1>AutoExhaust</h1><div class="conn" id="conn"><i></i><span>연결 중</span></div></header>

<section class="card" id="hero">
 <div class="hero"><div class="dot"></div>
  <div><div class="state" id="state">-</div><div class="desc" id="desc">불러오는 중</div></div></div>
 <div class="fault" id="fault"></div>
</section>

<section class="card"><h2>출력</h2>
 <div class="grid">
  <div class="stat"><b id="fan">-</b><span>팬</span><div class="bar"><i id="fanBar"></i></div></div>
  <div class="stat"><b id="light">-</b><span>조명</span><div class="bar"><i id="lightBar"></i></div></div>
  <div class="stat"><b id="volt">-</b><span>12V 전원</span></div>
 </div>
</section>

<section class="card"><h2>제어</h2>
 <button class="primary" id="manual">수동 환기 시작</button>
 <div class="slider"><label>팬 세기 <small id="fanSetV"></small></label>
  <input type="range" id="fanSet" min="30" max="100" step="5"></div>
 <div class="slider"><label>조명 밝기 <small id="lightSetV"></small></label>
  <input type="range" id="lightSet" min="0" max="70" step="5"></div>
 <div class="desc" style="margin-top:6px">출력 중·쿨다운·수동 환기 때 이 값으로 켜집니다. 바꾸면 바로 저장됩니다.</div>
</section>

<section class="card"><h2>프린터</h2>
 <div class="row"><span>상태</span><b id="pState">-</b></div>
 <div class="row"><span>노즐 / 베드</span><b id="pTemp">-</b></div>
 <div class="row"><span>챔버 조명</span><button class="switch" id="chamber" aria-label="챔버 조명"></button></div>
</section>

<section class="card"><h2>시스템</h2>
 <div class="row"><span>WiFi</span><b id="rssi">-</b></div>
 <div class="row"><span>칩 온도</span><b id="chip">-</b></div>
 <div class="row"><span>가동 시간</span><b id="uptime">-</b></div>
 <div class="row"><span>마지막 리셋</span><b id="reset">-</b></div>
 <div class="row"><span>펌웨어</span><b id="fw">-</b></div>
 <div class="row"><span>메모리</span><b id="heap">-</b></div>
 <div class="row"><span></span><button id="reboot">재부팅</button></div>
</section>

<section class="card"><details id="logBox"><summary>로그</summary><pre id="logs"></pre></details></section>
<footer>API: <a href="/api/status" style="color:inherit">/api/status</a> · docs/API.md</footer>
</main><div class="toast" id="toast"></div>
<script>
const $=id=>document.getElementById(id);
const STATES={BOOT:["시작 중","프린터 상태를 기다리는 중"],IDLE:["대기","프린터가 쉬는 중"],
RUNNING:["출력 중","팬·조명 가동"],COOLDOWN:["쿨다운","출력 후 남은 연기 배출"],
MANUAL:["수동 환기","프린터와 무관하게 가동"],OFFLINE:["연결 끊김","프린터 상태를 알 수 없음"],
FAULT:["오류","부하 차단됨"]};
let token="",last=null,editUntil=0;
try{token=localStorage.getItem("token")||""}catch(e){}

function toast(m){const t=$("toast");t.textContent=m;t.classList.add("show");
 clearTimeout(t._h);t._h=setTimeout(()=>t.classList.remove("show"),2200)}
function dur(s){const d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;
 return d?`${d}일 ${h}시간`:h?`${h}시간 ${m}분`:m?`${m}분 ${s%60}초`:`${s}초`}

async function api(path,body){
 const opt={headers:{}};
 if(body!==undefined){opt.method="POST";opt.headers["Content-Type"]="application/json";
  if(token)opt.headers.Authorization="Bearer "+token;opt.body=JSON.stringify(body)}
 const r=await fetch("/api/"+path,opt);
 if(r.status===401){token=prompt("API 토큰")||"";try{localStorage.setItem("token",token)}catch(e){}
  throw new Error("토큰이 필요합니다")}
 const j=await r.json();
 if(!r.ok)throw new Error(j.error||r.status);
 return j}

function render(d){
 last=d;const s=d.system,o=d.output,p=d.printer,n=d.network,st=STATES[s.state]||[s.state,""];
 $("hero").className="card st-"+s.state;
 $("state").textContent=st[0];
 $("desc").textContent=s.remaining_s?`${st[1]} · ${dur(s.remaining_s)} 남음`:st[1];
 const f=$("fault");f.style.display=s.fault?"block":"none";
 f.textContent=s.fault?`${s.fault} — 원인을 확인한 뒤 장치 버튼을 2초 눌렀다 떼면 해제됩니다.`:"";
 $("fan").textContent=o.fan_pct+"%";$("fanBar").style.width=o.fan_pct+"%";
 $("light").textContent=o.light_pct+"%";$("lightBar").style.width=o.light_pct+"%";
 $("volt").textContent=o.supply_v<5?"없음":o.supply_v.toFixed(2)+"V";
 const m=$("manual"),manual=s.state==="MANUAL";
 m.textContent=manual?"수동 환기 중지":"수동 환기 시작";m.classList.toggle("stop",manual);
 m.disabled=s.state==="FAULT"||s.state==="RUNNING";
 if(Date.now()>editUntil){
  $("fanSet").min=d.settings.fan_min_pct;$("lightSet").max=d.settings.light_max_pct;
  $("fanSet").value=d.settings.fan_pct;$("lightSet").value=d.settings.light_pct;showSet()}
 $("pState").textContent=p.online?p.state:"오프라인";
 $("pTemp").textContent=`${Math.round(p.nozzle_c)}°C / ${Math.round(p.bed_c)}°C`;
 $("chamber").classList.toggle("on",p.chamber_light);$("chamber").disabled=!n.mqtt;
 $("rssi").textContent=`${n.rssi_dbm} dBm · MQTT ${n.mqtt?"연결":"끊김"}`;
 $("chip").textContent=s.chip_temp_c+"°C";
 $("uptime").textContent=dur(s.uptime_s);
 $("reset").textContent=s.reset_reason;
 $("fw").textContent=`${s.firmware} (${s.firmware_state})`;
 $("heap").textContent=Math.round(s.free_heap/1024)+" KB"}

function setConn(ok){const c=$("conn");c.classList.toggle("off",!ok);
 c.lastChild.textContent=ok?"실시간":"연결 끊김"}

async function poll(){
 try{render(await api("status"));setConn(true)}catch(e){setConn(false)}
 if($("logBox").open)loadLogs()}

async function loadLogs(){try{const pre=$("logs"),end=pre.scrollTop+pre.clientHeight>=pre.scrollHeight-8;
 pre.textContent=(await api("logs")).logs.join("\n");if(end)pre.scrollTop=pre.scrollHeight}catch(e){}}

function showSet(){$("fanSetV").textContent=$("fanSet").value+"%";$("lightSetV").textContent=$("lightSet").value+"%"}
async function act(fn,ok){try{await fn();if(ok)toast(ok);setTimeout(poll,300)}catch(e){toast("실패: "+e.message)}}

for(const id of["fanSet","lightSet"]){
 $(id).oninput=()=>{editUntil=Date.now()+4000;showSet()};
 $(id).onchange=()=>act(()=>api("settings",{fan_pct:+$("fanSet").value,light_pct:+$("lightSet").value}),"저장했습니다")}
$("manual").onclick=()=>act(()=>api("manual",{on:last.system.state!=="MANUAL"}));
$("chamber").onclick=()=>act(()=>api("chamber_light",{on:!last.printer.chamber_light}),"프린터에 전송했습니다");
$("reboot").onclick=()=>{if(confirm("장치를 재부팅할까요? 약 10초 뒤 다시 연결됩니다."))
 act(()=>api("reboot",{}),"재부팅 중")};
$("logBox").ontoggle=()=>{if($("logBox").open)loadLogs()};
poll();setInterval(poll,2000);
</script></body></html>)rawliteral";

#endif
