/* =====================================================================
 *  점잡이 — 시각장애인을 위한 지하철 안내  (디버깅/시연용 웹 UI)
 *  - 흰 바탕 + 밝은 푸른 회색 테마 / 반응형(모바일·PC 모두)
 *  - http://<ESP32_IP>/ 접속 → 같은 IP의 ws://.../ws 자동 연결
 * ===================================================================== */
#pragma once

const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ko">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>점잡이 — 시각장애인을 위한 지하철 안내</title>
  <style>
    :root{
      --bg:#f4f7fb;          /* 하얀 바탕 */
      --card:#ffffff;
      --line:#dbe4ef;
      --ink:#2b3648;         /* 본문 글자 */
      --muted:#7c8aa0;
      --accent:#5b86c4;      /* 밝은 푸른 회색 */
      --accent-soft:#e8eff7;
      --ok:#3f9e6b;
      --off:#c2ccda;
      --shadow:0 2px 10px rgba(70,100,140,.08);
    }
    *{box-sizing:border-box;font-family:'Segoe UI',system-ui,-apple-system,sans-serif;}
    body{margin:0;background:var(--bg);color:var(--ink);padding:16px;line-height:1.5;}
    .wrap{max-width:960px;margin:0 auto;}
    header{margin:6px 4px 18px;}
    h1{font-size:clamp(18px,4.5vw,26px);margin:0;color:#324b73;font-weight:700;}
    .desc{color:var(--muted);font-size:13px;margin-top:4px;}
    .conn{float:right;font-size:12px;padding:4px 12px;border-radius:20px;font-weight:600;}
    .conn.ok{background:#e4f3ea;color:var(--ok);}
    .conn.no{background:#fdeaea;color:#c65454;}

    /* 반응형 그리드: 화면 넓으면 2열, 좁으면 1열 */
    .grid{display:grid;grid-template-columns:1fr;gap:16px;}
    @media(min-width:720px){ .grid{grid-template-columns:1fr 1fr;} .full{grid-column:1/-1;} }

    .card{background:var(--card);border:1px solid var(--line);border-radius:16px;
          padding:18px;box-shadow:var(--shadow);}
    .card h2{font-size:13px;margin:0 0 14px;color:var(--muted);
             letter-spacing:.6px;text-transform:uppercase;font-weight:700;}

    /* 이번 역 대형 표시 */
    .now{ text-align:center;padding:8px 0; }
    .now .label{font-size:13px;color:var(--muted);}
    .now .name{font-size:clamp(28px,8vw,44px);font-weight:800;color:#2c4a7c;margin:6px 0;}
    .badge{display:inline-block;font-size:12px;font-weight:700;padding:4px 12px;border-radius:20px;
           background:var(--accent-soft);color:var(--accent);}
    .badge.remote{background:#fff0e0;color:#c98329;}

    /* 5개 역 노선 및 브라우저 전용 열차 애니메이션 */
    .route-map{position:relative;height:104px;margin:2px 0 8px;overflow:hidden;}
    .route-line{position:absolute;top:32px;left:10%;right:10%;height:4px;
                border-radius:4px;background:var(--accent-soft);}
    .route-line::before{content:"";position:absolute;inset:0;border-radius:inherit;
                        background:linear-gradient(90deg,var(--accent),#88a8d3);opacity:.55;}
    .route-stops{position:absolute;inset:0;display:grid;grid-template-columns:repeat(5,1fr);}
    .route-stop{position:relative;text-align:center;color:var(--muted);min-width:0;}
    .route-node{position:absolute;top:25px;left:50%;width:18px;height:18px;
                transform:translateX(-50%);border:4px solid var(--accent-soft);
                border-radius:50%;background:#fff;transition:.25s;z-index:2;}
    .route-label{display:block;margin:53px auto 0;width:92%;font-size:11px;
                 line-height:1.3;font-weight:600;word-break:keep-all;transition:.25s;}
    .route-stop.current .route-node{border-color:var(--accent);background:var(--accent);
                                    box-shadow:0 0 0 5px rgba(91,134,196,.15);}
    .route-stop.current .route-label{color:#2c4a7c;font-weight:800;}
    .route-stop.next .route-node{border-color:var(--accent);}
    .route-stop.next .route-label{color:var(--accent);}
    .route-train{--route-from:10%;--route-to:30%;position:absolute;top:1px;left:var(--route-from);
                 width:34px;height:25px;transform:translateX(-50%);border:2px solid #fff;
                 border-radius:8px 8px 6px 6px;background:var(--accent);color:var(--accent);
                 box-shadow:0 3px 8px rgba(50,75,115,.28);z-index:4;opacity:0;}
    .route-train::before{content:"";position:absolute;left:5px;right:5px;top:4px;height:8px;
                         border-radius:3px;background:#eaf2fb;
                         box-shadow:inset 8px 0 0 #bad0e9,inset -8px 0 0 #bad0e9;}
    .route-train::after{content:"";position:absolute;left:5px;bottom:-5px;width:6px;height:6px;
                        border-radius:50%;background:#324b73;box-shadow:17px 0 #324b73;}
    .route-train.moving{animation:routeTravel 7s ease-in-out infinite;}
    .route-train.parked{left:var(--route-from);opacity:1;}
    @keyframes routeTravel{
      0%{left:var(--route-from);opacity:0;}
      6%,18%{left:var(--route-from);opacity:1;}
      78%,88%{left:var(--route-to);opacity:1;}
      96%,100%{left:var(--route-to);opacity:0;}
    }
    .route-summary{display:flex;align-items:center;justify-content:center;gap:10px;
                   min-height:22px;color:var(--muted);font-size:12px;text-align:center;}
    .route-summary strong{color:#2c4a7c;font-size:13px;}
    .route-arrow{color:var(--accent);font-weight:800;}

    /* 역 버튼들 */
    .stations{display:grid;
              grid-template-columns:repeat(2,minmax(0,1fr));
              gap:10px;}
    .st-btn{padding:14px 4px;
            border:1.5px solid var(--line);
            border-radius:12px;
            background:#fff;
            color:var(--ink);
            font-size:13px;
            font-weight:600;
            cursor:pointer;
            transition:.15s;
            white-space:nowrap;}
    .st-btn:hover{border-color:var(--accent);background:var(--accent-soft);}
    .st-btn.active{background:var(--accent);border-color:var(--accent);color:#fff;
                   box-shadow:0 4px 12px rgba(91,134,196,.35);}
    .row-btns{display:flex;gap:10px;margin-top:12px;}
    .ctrl{flex:1;padding:11px;border:none;border-radius:10px;font-size:13px;font-weight:600;cursor:pointer;}
    .ctrl.auto{background:var(--accent);color:#fff;}
    .ctrl.refresh{background:var(--accent-soft);color:var(--accent);}
    .ctrl.reboot{background:#fdeaea;color:#b94747;border:1px solid #f3caca;}
    .ctrl.reboot:hover{background:#f9dada;}

    /* 상태 모니터링 */
    .status-row{display:flex;align-items:center;justify-content:space-between;
                padding:9px 0;border-bottom:1px solid #f0f4f9;}
    .status-row:last-child{border-bottom:none;}
    .status-row span:first-child{color:var(--muted);font-size:14px;}
    .dot{width:13px;height:13px;border-radius:50%;background:var(--off);transition:.2s;}
    .dot.on{background:var(--ok);box-shadow:0 0 8px rgba(63,158,107,.6);}
    .mon-val{font-size:13px;font-weight:600;color:#2c4a7c;text-align:right;max-width:60%;}

    /* 로그 */
    #log{background:#f7f9fc;border:1px solid var(--line);border-radius:10px;padding:10px;
         height:150px;overflow-y:auto;font-family:'Consolas',monospace;font-size:12px;color:#41506a;}
    #log div{margin:2px 0;}
    @media(max-width:480px){
      body{padding:10px;}
      .card{padding:14px;}
      .route-map{height:110px;}
      .route-label{width:86%;font-size:10px;word-break:break-all;}
      .route-train{width:31px;height:23px;}
      .route-summary{gap:6px;font-size:11px;}
      .route-summary strong{font-size:12px;}
    }
    @media(prefers-reduced-motion:reduce){
      .route-train.moving{animation:none;left:var(--route-from);opacity:1;}
    }
  </style>
</head>
<body>
<div class="wrap">
  <header>
    <span id="conn" class="conn no">연결 안 됨</span>
    <h1>점잡이</h1>
    <div class="desc">시각장애인을 위한 지하철 안내 · 디버깅/시연 콘솔</div>
  </header>

  <!-- 이번 역 -->
  <div class="card full" style="margin-bottom:16px;">
    <div class="now">
      <div class="label">현재 안내 역</div>
      <div class="name" id="nowName">정보 수신 대기…</div>
      <span class="badge" id="modeBadge">AUTO</span>
    </div>
  </div>

  <!-- 5개 역 노선: 애니메이션은 activeIdx만 사용해 브라우저에서 처리 -->
  <div class="card full" style="margin-bottom:16px;">
    <h2>5개 역 노선</h2>
    <div class="route-map" aria-label="동대문역사공원부터 한양대까지 노선">
      <div class="route-line"></div>
      <div class="route-stops" id="routeStops"></div>
      <div class="route-train" id="routeTrain" role="img" aria-label="지하철"></div>
    </div>
    <div class="route-summary" aria-live="polite">
      <span>현재 <strong id="routeCurrent">정보 수신 대기</strong></span>
      <span class="route-arrow" id="routeArrow">→</span>
      <span>다음 <strong id="routeNext">-</strong></span>
    </div>
  </div>

  <div class="grid">
    <!-- 역별 원격 제어 -->
    <div class="card">
      <h2>역별 원격 제어</h2>
      <div class="stations" id="stations"></div>
      <div class="row-btns">
        <button class="ctrl auto"    onclick="send('auto')">자동 모드</button>
        <button class="ctrl refresh" onclick="send('refresh')">시간표 새로고침</button>
      </div>
      <div class="row-btns">
        <button class="ctrl reboot" onclick="requestReboot()">ESP32 원격 재부팅</button>
      </div>
    </div>

    <!-- 상태 모니터링 -->
    <div class="card">
      <h2>상태 모니터링</h2>
      <div class="status-row"><span>모드</span>     <span class="mon-val" id="s-mode">-</span></div>
      <div class="status-row"><span>모터</span>     <span class="dot" id="s-motor"></span></div>
      <div class="status-row"><span>DFPlayer 준비</span><span class="dot" id="s-speaker"></span></div>
      <div class="status-row"><span>매트릭스 표시</span><span class="dot" id="s-matrix"></span></div>
      <div class="status-row"><span>진동</span>     <span class="dot" id="s-vibration"></span></div>
      <div class="status-row"><span>버튼 확인 LED</span><span class="dot" id="s-button"></span></div>
      <div class="status-row"><span>시간표/API</span><span class="mon-val" id="s-monitor">-</span></div>
      <div class="status-row"><span>다음 realtime 호출</span><span class="mon-val" id="s-next-poll">-</span></div>
      <div class="status-row"><span>오늘 남은 API 횟수</span><span class="mon-val" id="s-api-remaining">-</span></div>
    </div>

    <!-- 통신 로그 -->
    <div class="card full">
      <h2>ESP32 로그 / 통신 로그</h2>
      <div id="log"></div>
    </div>
  </div>
</div>

<script>
  // 5개 역 (인덱스 0~4 = 외부코드 205~209)
  const STATIONS = ["동대문역사공원","신당","상왕십리","왕십리","한양대"];

  const ws = new WebSocket(`ws://${location.host}/ws`);
  const $  = id => document.getElementById(id);
  let lastRouteIndex = -2;

  function log(msg){
    const d=document.createElement('div');
    d.textContent=`[${new Date().toLocaleTimeString()}] ${msg}`;
    $('log').prepend(d);
  }

  // 역 버튼 동적 생성 (데이터 주도)
  STATIONS.forEach((nm,i)=>{
    const b=document.createElement('button');
    b.className='st-btn'; b.id='st-'+i; b.textContent=nm+'역';
    b.onclick=()=>{ ws.send(JSON.stringify({cmd:'station',value:i}));
                    log(`→ 역 선택: ${nm}역`); };
    $('stations').appendChild(b);
  });

  // 노선 역 생성 및 activeIdx 기반 브라우저 애니메이션
  STATIONS.forEach((nm,i)=>{
    const stop=document.createElement('div');
    stop.className='route-stop'; stop.id='route-stop-'+i;
    stop.innerHTML=`<span class="route-node"></span><span class="route-label">${nm}</span>`;
    $('routeStops').appendChild(stop);
  });

  function updateRoute(activeIdx){
    const parsed=Number(activeIdx);
    const valid=Number.isInteger(parsed) && parsed>=0 && parsed<STATIONS.length;
    const current=valid ? parsed : -1;
    if(current===lastRouteIndex) return;
    const next=valid && current+1<STATIONS.length ? current+1 : -1;

    STATIONS.forEach((_,i)=>{
      const stop=$('route-stop-'+i);
      stop.classList.toggle('current', valid && i===current);
      stop.classList.toggle('next', i===next);
      if(valid && i===current) stop.setAttribute('aria-current','step');
      else stop.removeAttribute('aria-current');
    });

    $('routeCurrent').textContent=valid ? STATIONS[current] : '정보 수신 대기';
    $('routeNext').textContent=next>=0 ? STATIONS[next] : (valid ? '종착역' : '-');
    $('routeArrow').style.visibility=valid ? 'visible' : 'hidden';

    const train=$('routeTrain');
    if(!valid){
      train.className='route-train';
      lastRouteIndex=current;
      return;
    }

    // Keep the train parked before the initial departure and at the terminal.
    // Intermediate stations retain the original repeating travel animation.
    const moving=current>0 && next>=0;
    const from=10+current*20;
    const to=moving ? 10+next*20 : from;
    train.style.setProperty('--route-from',from+'%');
    train.style.setProperty('--route-to',to+'%');
    train.setAttribute('aria-label',moving
      ? `${STATIONS[current]}에서 ${STATIONS[next]} 방향으로 이동하는 지하철`
      : `${STATIONS[current]}에 정차한 지하철`);

    train.className='route-train';
    void train.offsetWidth;
    train.classList.add(moving ? 'moving' : 'parked');
    lastRouteIndex=current;
  }

  updateRoute(-1);

  // 제어 명령 전송 (auto / refresh)
  function send(cmd){
    ws.send(JSON.stringify({cmd}));
    log(`→ 명령 전송: ${cmd}`);
  }

  function requestReboot(){
    if(confirm('ESP32를 지금 재부팅할까요? WebSocket 연결이 잠시 끊어집니다.')){
      send('reboot');
    }
  }

  ws.onopen = ()=>{ $('conn').textContent='연결됨'; $('conn').className='conn ok'; log('WebSocket 연결 성공'); };
  ws.onclose= ()=>{ $('conn').textContent='연결 끊김'; $('conn').className='conn no'; log('WebSocket 연결 종료'); };

  ws.onmessage = (e)=>{
    const m=JSON.parse(e.data);

    if(m.type==='ack'){
      log(`← ACK: ${m.cmd}${m.value!==undefined?'='+m.value:''} (${m.ok?'성공':'실패'})`);
    }
    else if(m.type==='status'){
      // 이번 역 / 모드
      $('nowName').textContent = m.station || '-';
      const badge=$('modeBadge');
      badge.textContent = m.mode;
      badge.className = 'badge' + (m.mode==='REMOTE'?' remote':'');

      // 역 버튼 활성 표시
      STATIONS.forEach((_,i)=>{
        $('st-'+i).classList.toggle('active', i===m.activeIdx);
      });
      updateRoute(m.activeIdx);

      // 상태 등
      $('s-mode').textContent = (m.mode==='REMOTE'?'원격 우선':'자동 시간표');
      setDot('s-motor',     m.motor);
      setDot('s-speaker',   m.speaker);
      setDot('s-matrix',    m.matrix);
      setDot('s-vibration', m.vibration);
      setDot('s-button',    m.button);
      $('s-monitor').textContent = m.monitor;
      $('s-next-poll').textContent = !m.realtime
        ? '-'
        : m.realtimePolling
          ? '호출 중'
          : Number(m.realtimeNextSec)>=0
            ? `${m.realtimeNextSec}초`
            : '대기';
      $('s-api-remaining').textContent = m.realtime
        ? `${m.apiRemaining}회`
        : '-';
    }
    else if(m.type==='log'){
      log(m.line || '');
    }
  };

  function setDot(id,on){ $(id).className='dot'+(on?' on':''); }
</script>
</body>
</html>
)HTML";
