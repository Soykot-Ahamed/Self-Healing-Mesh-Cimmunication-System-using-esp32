
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ── NODE IDENTITY ────────────────────────────────────────────
#define MY_NODE_NAME "A"
#define MY_NODE_ID    0
#define AP_SSID      "MeshChat-A"
#define AP_PASS      "mesh1234"

#define DHTPIN  4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);


uint8_t MAC_B[6] = {0xA4, 0xF0, 0x0F, 0x78, 0x07, 0x54};
uint8_t MAC_C[6] = {0xB0, 0xCB, 0xD8, 0xCF, 0x87, 0x10};


#define BACKEND_URL "http://192.168.1.100:3000"



#define FIREBASE_URL "https://YOUR-PROJECT-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "" 

struct KnownWiFi { const char* ssid; const char* pass; };
KnownWiFi knownNets[] = {
  {"YourHomeWiFi",   "homepassword"},
  {"OfficeWiFi",     "officepass"},
  {"MyHotspot",      "hotspotpass"},
  {"AnotherNet",     "pass123"},
};
const int NUM_KNOWN = sizeof(knownNets)/sizeof(knownNets[0]);


#define HB_MS         3000
#define NODE_TIMEOUT  9000
#define SENSOR_MS     5000
#define FIREBASE_MS  15000
#define WIFI_RETRY   30000

// ── PACKET STRUCTURE ─────────────────────────────────────────
#define MAX_MSG  180
#define MAX_HIST  40

typedef struct __attribute__((packed)) {
  char     type[12];  
  char     from[4];
  char     to[4];
  char     msg[MAX_MSG];
  char     via[4];
  float    temp;
  float    hum;
  uint32_t msgId;
} MeshPacket;

typedef struct {
  char  from[4]; char to[4];
  char  msg[MAX_MSG]; char via[4];
  bool  isBroadcast, isSensor, isGemini;
  float temp, hum;
  unsigned long ts;
} ChatMsg;


WebServer server(80);
ChatMsg   history[MAX_HIST];
int       histCount  = 0;
uint32_t  msgCounter = 0;
bool      nodeOnline[3] = {true,false,false};
unsigned long lastSeen[3]= {0,0,0};
float     nodeTemp[3]={0,0,0}, nodeHum[3]={0,0,0};
float     myTemp=0, myHum=0;
bool      wifiOK=false;
String    connSSID="";
unsigned long tHB=0,tSensor=0,tFirebase=0,tWifiRetry=0;
esp_now_peer_info_t peerB,peerC;


void addHist(const char* fr,const char* to,const char* msg,
             const char* via,bool sen,bool gem,float t=0,float h=0){
  if(histCount>=MAX_HIST){
    for(int i=0;i<MAX_HIST-1;i++) history[i]=history[i+1];
    histCount=MAX_HIST-1;
  }
  ChatMsg& m=history[histCount++];
  strncpy(m.from,fr,3);m.from[3]=0;
  strncpy(m.to,to,3);m.to[3]=0;
  strncpy(m.msg,msg,MAX_MSG-1);m.msg[MAX_MSG-1]=0;
  strncpy(m.via,via?via:"",3);m.via[3]=0;
  m.isBroadcast=strcmp(to,"ALL")==0;
  m.isSensor=sen; m.isGemini=gem;
  m.temp=t; m.hum=h; m.ts=millis();
}

void mkPkt(MeshPacket& p,const char* type,const char* to,
           const char* msg,float t=0,float h=0){
  memset(&p,0,sizeof(p));
  strncpy(p.type,type,11); strncpy(p.from,MY_NODE_NAME,3);
  strncpy(p.to,to,3); strncpy(p.msg,msg,MAX_MSG-1);
  p.temp=t; p.hum=h; p.msgId=++msgCounter;
}
void espSend(uint8_t* mac,MeshPacket& p){
  esp_now_send(mac,(uint8_t*)&p,sizeof(p));
}
void sendAll(MeshPacket& p){ espSend(MAC_B,p); espSend(MAC_C,p); }
void onSend(const uint8_t*,esp_now_send_status_t){}

void onRecv(const uint8_t* mac,const uint8_t* data,int len){
  MeshPacket p; memcpy(&p,data,sizeof(p));
  int idx=-1;
  if(strcmp(p.from,"B")==0) idx=1;
  if(strcmp(p.from,"C")==0) idx=2;
  if(idx>=0){ lastSeen[idx]=millis(); nodeOnline[idx]=true; }

  if(strcmp(p.type,"PING")==0) return;

  if(strcmp(p.type,"RELAY")==0){
    MeshPacket fwd=p; strncpy(fwd.type,"CHAT",11);
    strncpy(fwd.via,MY_NODE_NAME,3);
    if(strcmp(p.to,"B")==0) espSend(MAC_B,fwd);
    return;
  }
  if(strcmp(p.type,"SENSOR")==0){
    if(idx>=0){nodeTemp[idx]=p.temp;nodeHum[idx]=p.hum;}
    char buf[64]; snprintf(buf,64,"Temp:%.1f°C  Hum:%.1f%%",p.temp,p.hum);
    addHist(p.from,"ALL",buf,"",true,false,p.temp,p.hum);
    return;
  }
  if(strcmp(p.type,"GEMRES")==0){
    bool fu=strcmp(p.to,MY_NODE_NAME)==0||strcmp(p.to,"ALL")==0;
    if(fu) addHist("Gemini",p.to,p.msg,p.via,false,true);
    return;
  }
  if(strcmp(p.type,"CHAT")==0){
    bool fu=strcmp(p.to,MY_NODE_NAME)==0||strcmp(p.to,"ALL")==0;
    if(fu) addHist(p.from,p.to,p.msg,p.via,false,false);
  }
}


bool tryWifi(){
  Serial.println("[WiFi] Scanning...");
  int n=WiFi.scanNetworks();
  if(n<=0){Serial.println("[WiFi] None found");return false;}
  for(int i=0;i<n;i++){
    String sc=WiFi.SSID(i);
    for(int k=0;k<NUM_KNOWN;k++){
      if(sc==String(knownNets[k].ssid)){
        Serial.printf("[WiFi] Found %s, connecting...\n",knownNets[k].ssid);
        WiFi.begin(knownNets[k].ssid,knownNets[k].pass);
        unsigned long t0=millis();
        while(WiFi.status()!=WL_CONNECTED&&millis()-t0<10000) delay(250);
        if(WiFi.status()==WL_CONNECTED){
          connSSID=String(knownNets[k].ssid);
          Serial.printf("[WiFi] Connected! IP:%s\n",WiFi.localIP().toString().c_str());
          WiFi.scanDelete(); return true;
        }
      }
    }
  }
  WiFi.scanDelete(); return false;
}


void pushFirebase(){
  if(!wifiOK) return;
  HTTPClient http;
  String url=String(FIREBASE_URL)+"/nodes/A.json";
  if(strlen(FIREBASE_AUTH)>0) url+="?auth="+String(FIREBASE_AUTH);
  DynamicJsonDocument doc(256);
  doc["temp"]=myTemp; doc["hum"]=myHum;
  doc["ts"]=(unsigned long)millis()/1000;
  doc["node"]="A"; doc["wifi"]=connSSID;
  String body; serializeJson(doc,body);
  http.begin(url);
  http.addHeader("Content-Type","application/json");
  int code=http.PUT(body);
  Serial.printf("[Firebase] PUT -> %d\n",code);
  http.end();
}


String askGemini(const char* q){
  if(!wifiOK) return "No WiFi — Gemini unavailable.";
  HTTPClient http;
  http.begin(String(BACKEND_URL)+"/gemini");
  http.addHeader("Content-Type","application/json");
  http.setTimeout(25000);
  DynamicJsonDocument req(512);
  req["question"]=q; req["node"]=MY_NODE_NAME;
  req["temp"]=myTemp; req["hum"]=myHum;
  String body; serializeJson(req,body);
  int code=http.POST(body);
  String result="Backend error ("+String(code)+"). Is server running?";
  if(code==200){
    String resp=http.getString();
    DynamicJsonDocument res(2048);
    if(!deserializeJson(res,resp))
      result=res["answer"]|"No answer.";
  }
  http.end();
  return result;
}


void doSensor(){
  float t=dht.readTemperature(), h=dht.readHumidity();
  if(isnan(t)||isnan(h)){Serial.println("[DHT] Read fail");return;}
  myTemp=t; myHum=h; nodeTemp[0]=t; nodeHum[0]=h;
  char buf[64]; snprintf(buf,64,"Temp:%.1f°C  Hum:%.1f%%",t,h);
  addHist(MY_NODE_NAME,"ALL",buf,"",true,false,t,h);
  MeshPacket p; mkPkt(p,"SENSOR","ALL","",t,h);
  sendAll(p);
  Serial.printf("[Sensor] %.1f°C %.1f%%\n",t,h);
}


void hRoot(){ server.send(200,"text/html",buildHTML()); }

void hStatus(){
  unsigned long now=millis();
  for(int i=1;i<3;i++)
    if(nodeOnline[i]&&(now-lastSeen[i])>NODE_TIMEOUT) nodeOnline[i]=false;
  DynamicJsonDocument doc(700);
  doc["me"]=MY_NODE_NAME; doc["wifiOK"]=wifiOK;
  doc["wifiSSID"]=connSSID; doc["myTemp"]=myTemp; doc["myHum"]=myHum;
  doc["geminiOK"]=wifiOK;
  JsonArray arr=doc.createNestedArray("nodes");
  const char* nm[]={"A","B","C"};
  for(int i=0;i<3;i++){
    JsonObject n=arr.createNestedObject();
    n["name"]=nm[i]; n["online"]=nodeOnline[i];
    n["temp"]=nodeTemp[i]; n["hum"]=nodeHum[i];
  }
  String out; serializeJson(doc,out);
  server.send(200,"application/json",out);
}

void hMessages(){
  DynamicJsonDocument doc(8192);
  JsonArray arr=doc.createNestedArray("messages");
  for(int i=0;i<histCount;i++){
    JsonObject m=arr.createNestedObject();
    m["from"]=history[i].from; m["to"]=history[i].to;
    m["msg"]=history[i].msg;   m["via"]=history[i].via;
    m["broadcast"]=history[i].isBroadcast;
    m["sensor"]=history[i].isSensor;
    m["gemini"]=history[i].isGemini;
    m["temp"]=history[i].temp; m["hum"]=history[i].hum;
  }
  String out; serializeJson(doc,out);
  server.send(200,"application/json",out);
}

void hSend(){
  if(!server.hasArg("plain")){server.send(400,"text/plain","no body");return;}
  DynamicJsonDocument doc(512);
  deserializeJson(doc,server.arg("plain"));
  const char* to  =doc["to"]  |"ALL";
  const char* msg =doc["msg"] |"";
  bool gemQ       =doc["gemini"]|false;
  if(!strlen(msg)){server.send(200,"text/plain","ok");return;}

  if(gemQ){
    addHist("Me","Gemini",msg,"",false,false);
    String ans=askGemini(msg);
    addHist("Gemini","Me",ans.c_str(),"",false,true);
    MeshPacket p; mkPkt(p,"GEMRES","ALL",ans.c_str());
    sendAll(p);
  } else {
    addHist(MY_NODE_NAME,to,msg,"",false,false);
    MeshPacket p; mkPkt(p,"CHAT",to,msg);
    if(strcmp(to,"ALL")==0){ sendAll(p); }
    else if(strcmp(to,"B")==0){
      if(esp_now_send(MAC_B,(uint8_t*)&p,sizeof(p))!=ESP_OK){
        strncpy(p.type,"RELAY",11); espSend(MAC_C,p);
      }
    } else if(strcmp(to,"C")==0){ espSend(MAC_C,p); }
  }
  server.send(200,"text/plain","ok");
}

void hAddWifi(){
  if(!server.hasArg("plain")){server.send(400,"text/plain","no body");return;}
  DynamicJsonDocument doc(256);
  deserializeJson(doc,server.arg("plain"));
  const char* ssid=doc["ssid"]|"";
  const char* pass=doc["pass"]|"";
  if(!strlen(ssid)){server.send(200,"text/plain","empty ssid");return;}
  WiFi.begin(ssid,pass);
  unsigned long t0=millis();
  while(WiFi.status()!=WL_CONNECTED&&millis()-t0<10000) delay(200);
  if(WiFi.status()==WL_CONNECTED){
    wifiOK=true; connSSID=String(ssid);
    server.send(200,"text/plain","connected");
  } else {
    server.send(200,"text/plain","failed");
  }
}


void setup(){
  Serial.begin(115200); delay(500);
  Serial.println("\n=== MeshChat Node A ===");
  dht.begin();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID,AP_PASS);
  Serial.printf("AP: %s  IP: %s\n",AP_SSID,WiFi.softAPIP().toString().c_str());
  Serial.printf("STA MAC: %s\n",WiFi.macAddress().c_str());
  wifiOK=tryWifi();
  if(esp_now_init()!=ESP_OK){Serial.println("ESP-NOW FAIL");return;}
  esp_now_register_send_cb(onSend);
  esp_now_register_recv_cb(onRecv);
  auto addP=[](esp_now_peer_info_t& peer,uint8_t* mac){
    memset(&peer,0,sizeof(peer));
    memcpy(peer.peer_addr,mac,6);
    peer.channel=0; peer.encrypt=false;
    esp_now_add_peer(&peer);
  };
  addP(peerB,MAC_B); addP(peerC,MAC_C);
  server.on("/",        HTTP_GET, hRoot);
  server.on("/status",  HTTP_GET, hStatus);
  server.on("/messages",HTTP_GET, hMessages);
  server.on("/send",    HTTP_POST,hSend);
  server.on("/addwifi", HTTP_POST,hAddWifi);
  server.begin();
  Serial.println("Ready at http://192.168.4.1");
  lastSeen[0]=millis();
  delay(2000); doSensor();
}

void loop(){
  server.handleClient();
  unsigned long now=millis();
  if(now-tHB>HB_MS){ tHB=now;
    MeshPacket p; mkPkt(p,"PING","ALL",""); sendAll(p);
  }
  if(now-tSensor>SENSOR_MS){ tSensor=now; doSensor(); }
  if(wifiOK&&now-tFirebase>FIREBASE_MS){ tFirebase=now; pushFirebase(); }
  if(!wifiOK&&now-tWifiRetry>WIFI_RETRY){
    tWifiRetry=now; wifiOK=tryWifi();
  }
  if(WiFi.status()==WL_CONNECTED&&!wifiOK) wifiOK=true;
  if(WiFi.status()!=WL_CONNECTED&&wifiOK){ wifiOK=false; connSSID=""; }
}


String buildHTML(){
return R"HTML(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MeshChat</title>
<style>
:root{--bg:#0a0c10;--s1:#11141c;--s2:#181d28;--s3:#1f2535;--bd:#252d3d;
--ac:#3b82f6;--ac2:#22d3ee;--ok:#22c55e;--warn:#f59e0b;--err:#ef4444;
--gem:#a855f7;--tx:#e2e8f0;--mu:#64748b;--r:10px}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--tx);font-family:'Courier New',monospace;
min-height:100vh;display:flex;flex-direction:column}
header{background:var(--s1);border-bottom:1px solid var(--bd);
padding:10px 14px;display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.logo{font-size:16px;font-weight:700;letter-spacing:2px;color:var(--ac2)}
.logo span{color:var(--ok)}
.nb{background:var(--ac);color:#fff;padding:2px 10px;border-radius:20px;font-size:11px;font-weight:700}
.wb{font-size:10px;padding:2px 8px;border-radius:20px;background:var(--s3);border:1px solid var(--bd)}
.wb.on{border-color:var(--ok);color:var(--ok)}.wb.off{color:var(--mu)}
.sbar{display:flex;gap:5px;margin-left:auto;align-items:center;flex-wrap:wrap}
.np{display:flex;align-items:center;gap:4px;background:var(--s2);
border:1px solid var(--bd);padding:2px 7px;border-radius:20px;font-size:10px}
.dot{width:7px;height:7px;border-radius:50%;background:var(--err)}
.dot.on{background:var(--ok)}
.srow{display:flex;gap:6px;flex-wrap:wrap;padding:7px 14px;
background:var(--s1);border-bottom:1px solid var(--bd)}
.sc{background:var(--s2);border:1px solid var(--bd);padding:4px 10px;
border-radius:8px;font-size:11px;display:flex;gap:6px;align-items:center}
.sc .lbl{color:var(--mu);font-size:9px}
.main{flex:1;display:flex;flex-direction:column;max-width:680px;
margin:0 auto;width:100%;padding:10px;gap:8px}
#chat{overflow-y:auto;display:flex;flex-direction:column;gap:5px;
min-height:260px;max-height:52vh;padding:8px;
background:var(--s1);border:1px solid var(--bd);border-radius:var(--r)}
.msg{padding:7px 11px;border-radius:8px;font-size:12px;line-height:1.5;
max-width:88%;word-break:break-word}
.msg.mine{background:var(--ac);color:#fff;align-self:flex-end}
.msg.theirs{background:var(--s2);border:1px solid var(--bd);align-self:flex-start}
.msg.smsg{background:var(--s3);border:1px solid var(--warn);
border-left:3px solid var(--warn);align-self:center;text-align:center;
max-width:100%;font-size:11px}
.msg.gmsg{background:#1a0d2e;border:1px solid var(--gem);align-self:flex-start}
.meta{font-size:9px;opacity:.65;margin-bottom:3px;
display:flex;gap:5px;align-items:center;flex-wrap:wrap}
.bk{font-size:8px;padding:1px 5px;border-radius:10px;background:rgba(255,255,255,.1)}
.bk.bc{background:var(--warn);color:#000}
.bk.gm{background:var(--gem);color:#fff}
.bk.rv{background:var(--warn);color:#000}
.bk.sn{background:var(--warn);color:#000}
.comp{display:flex;flex-direction:column;gap:6px}
.trow{display:flex;gap:5px;flex-wrap:wrap;align-items:center}
.tb{padding:4px 11px;border-radius:20px;border:1px solid var(--bd);
background:var(--s2);color:var(--mu);cursor:pointer;font-size:11px;
font-family:inherit;transition:all .15s}
.tb.act{border-color:var(--ac);background:var(--ac);color:#fff}
.tb.gact{border-color:var(--gem);background:var(--gem);color:#fff;outline:2px solid #fff}
.irow{display:flex;gap:6px}
#mi{flex:1;background:var(--s2);border:1px solid var(--bd);border-radius:var(--r);
padding:9px 13px;color:var(--tx);font-family:inherit;font-size:13px;outline:none;transition:border .15s}
#mi:focus{border-color:var(--ac)}
.sbtn{background:var(--ac);color:#fff;border:none;border-radius:var(--r);
padding:9px 15px;cursor:pointer;font-weight:700;font-size:13px;
font-family:inherit;transition:opacity .15s}
.sbtn:hover{opacity:.85}.sbtn:disabled{opacity:.4}
.sbtn.g{background:var(--gem)}
.think{color:var(--gem);font-size:11px;display:none}
.think.show{display:block}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.3}}
.think.show{animation:blink 1s infinite}
.panel{background:var(--s2);border:1px solid var(--bd);
border-radius:var(--r);padding:10px}
.panel h4{font-size:10px;color:var(--mu);margin-bottom:7px;text-transform:uppercase;letter-spacing:.05em}
.wrow{display:flex;gap:6px;flex-wrap:wrap}
.wi{flex:1;min-width:100px;background:var(--s1);border:1px solid var(--bd);
border-radius:6px;padding:7px 10px;color:var(--tx);
font-family:inherit;font-size:12px;outline:none}
.wi:focus{border-color:var(--ac2)}
.wbtn2{background:var(--ac2);color:#000;border:none;border-radius:6px;
padding:7px 12px;cursor:pointer;font-size:12px;font-weight:700;font-family:inherit}
.wst{font-size:10px;margin-top:5px;color:var(--mu)}
.empty{text-align:center;color:var(--mu);padding:30px;font-size:12px}
</style></head><body>
<header>
  <div class="logo">MESH<span>CHAT</span></div>
  <div class="nb" id="myNode">NODE ?</div>
  <div class="wb off" id="wfBadge">WiFi: --</div>
  <div class="sbar" id="sbar"></div>
</header>
<div class="srow" id="srow">
  <span style="font-size:10px;color:var(--mu)">Loading sensors...</span>
</div>
<div class="main">
  <div id="chat"><div class="empty">No messages yet</div></div>
  <div class="think" id="think">Gemini is thinking...</div>
  <div class="comp">
    <div class="trow">
      <span style="font-size:10px;color:var(--mu)">TO:</span>
      <button class="tb act" onclick="setT('ALL',this)">Broadcast</button>
      <button class="tb" onclick="setT('A',this)">Node A</button>
      <button class="tb" onclick="setT('B',this)">Node B</button>
      <button class="tb" onclick="setT('C',this)">Node C</button>
      <button class="tb" id="gBtn" onclick="togGem(this)">Ask Gemini</button>
    </div>
    <div class="irow">
      <input id="mi" placeholder="Type a message..." maxlength="175"
             onkeydown="if(event.key==='Enter')doSend()">
      <button class="sbtn" id="sBtn" onclick="doSend()">SEND</button>
    </div>
  </div>
  <div class="panel">
    <h4>Connect to WiFi</h4>
    <div class="wrow">
      <input class="wi" id="wss" placeholder="WiFi SSID">
      <input class="wi" id="wps" placeholder="Password" type="password">
      <button class="wbtn2" onclick="joinWifi()">JOIN</button>
    </div>
    <div class="wst" id="wst"></div>
  </div>
</div>
<script>
let tgt='ALL',gem=false,me='?',lc=0;
function setT(t,el){
  if(gem)return;
  tgt=t;
  document.querySelectorAll('.tb:not(#gBtn)').forEach(b=>b.classList.remove('act'));
  el.classList.add('act');
}
function togGem(el){
  gem=!gem;
  el.className='tb'+(gem?' gact':'');
  document.getElementById('mi').placeholder=gem?'Ask Gemini anything...':'Type a message...';
  document.getElementById('sBtn').className='sbtn'+(gem?' g':'');
  if(!gem) setT('ALL',document.querySelector('.tb:not(#gBtn)'));
}
function doSend(){
  const v=document.getElementById('mi').value.trim();
  if(!v)return;
  const btn=document.getElementById('sBtn');
  btn.disabled=true;
  if(gem) document.getElementById('think').classList.add('show');
  fetch('/send',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({to:tgt,msg:v,gemini:gem})
  }).finally(()=>{
    btn.disabled=false;
    document.getElementById('think').classList.remove('show');
    document.getElementById('mi').value='';
  });
}
function joinWifi(){
  const s=document.getElementById('wss').value.trim();
  const p=document.getElementById('wps').value;
  if(!s)return;
  document.getElementById('wst').textContent='Connecting...';
  fetch('/addwifi',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid:s,pass:p})
  }).then(r=>r.text()).then(t=>{
    document.getElementById('wst').textContent=
      t==='connected'?'Connected to '+s:'Failed. Check credentials.';
  });
}
function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}
function poll(){
  fetch('/status').then(r=>r.json()).then(d=>{
    me=d.me;
    document.getElementById('myNode').textContent='NODE '+d.me;
    const wb=document.getElementById('wfBadge');
    wb.textContent=d.wifiOK?'WiFi: '+d.wifiSSID:'WiFi: offline';
    wb.className='wb '+(d.wifiOK?'on':'off');
    document.getElementById('sbar').innerHTML=d.nodes.map(n=>
      `<div class="np"><span class="dot ${n.online?'on':''}"></span>Node ${n.name}</div>`
    ).join('');
    let sr=d.nodes.filter(n=>n.online&&(n.temp>0||n.hum>0)).map(n=>
      `<div class="sc"><span class="lbl">Node ${n.name}</span>
       <b>${n.temp>0?n.temp.toFixed(1)+'°C':'--'}</b>
       <b>${n.hum>0?n.hum.toFixed(1)+'%':'--'}</b></div>`
    ).join('');
    document.getElementById('srow').innerHTML=sr||
      '<span style="font-size:10px;color:var(--mu)">No sensor data yet</span>';
    const gb=document.getElementById('gBtn');
    gb.title=d.geminiOK?'':'Needs WiFi';
    gb.style.opacity=d.geminiOK?'1':'0.5';
  }).catch(()=>{});
}
function pollMsgs(){
  fetch('/messages').then(r=>r.json()).then(d=>{
    if(d.messages.length===lc)return;
    lc=d.messages.length;
    const chat=document.getElementById('chat');
    if(!lc){chat.innerHTML='<div class="empty">No messages yet</div>';return;}
    chat.innerHTML=d.messages.map(m=>{
      const mine=m.from===me;
      const via=m.via?`<span class="bk rv">via ${m.via}</span>`:'';
      if(m.sensor) return `<div class="msg smsg">
        <span class="bk sn">SENSOR</span> Node <b>${m.from}</b> — ${esc(m.msg)}</div>`;
      if(m.gemini) return `<div class="msg gmsg">
        <div class="meta"><b>${m.from}</b><span class="bk gm">GEMINI</span>${via}</div>
        ${esc(m.msg)}</div>`;
      return `<div class="msg ${mine?'mine':'theirs'}">
        <div class="meta"><b>${m.from}</b>
        <span class="bk ${m.broadcast?'bc':''}">${m.broadcast?'BROADCAST':'&#8594; '+m.to}</span>
        ${via}</div>${esc(m.msg)}</div>`;
    }).join('');
    chat.scrollTop=chat.scrollHeight;
  }).catch(()=>{});
}
poll();pollMsgs();
setInterval(poll,3000);setInterval(pollMsgs,1000);
</script></body></html>
)HTML";
}
