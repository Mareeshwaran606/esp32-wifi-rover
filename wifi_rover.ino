/*
 * ESP32 Three-Wheel Wi-Fi Rover  -  REFERENCE CODE
 * ------------------------------------------------
 * NOTE: This is a reference implementation written to match the
 * project design (ESP32 + motor driver + Wi-Fi mobile control).
 * Pin numbers assume an L298N-style dual H-bridge driver; change
 * them to match your own wiring.
 *
 * How it works:
 *  1. ESP32 creates its own Wi-Fi hotspot (Access Point).
 *  2. Phone connects to that hotspot and opens http://192.168.4.1
 *  3. Web page shows control buttons; each press sends a command.
 *  4. ESP32 drives the motors through the motor driver using PWM
 *     for speed control.
 *  5. Fail-safe: if no command arrives for 600 ms, rover stops.
 */

#include <WiFi.h>
#include <WebServer.h>

// ---------- Wi-Fi hotspot settings ----------
const char* AP_SSID = "ESP32-Rover";
const char* AP_PASS = "rover1234";      // minimum 8 characters

// ---------- Motor driver pins (change to your wiring) ----------
const int IN1 = 26;   // left motor direction
const int IN2 = 27;
const int IN3 = 14;   // right motor direction
const int IN4 = 12;
const int ENA = 25;   // left motor speed (PWM)
const int ENB = 33;   // right motor speed (PWM)

// ---------- PWM settings ----------
const int PWM_FREQ = 1000;
const int PWM_RES  = 8;               // 8-bit: 0 - 255

// ---------- State ----------
WebServer server(80);
int speedValue = 180;                 // 0 - 255
unsigned long lastCommandTime = 0;
const unsigned long FAILSAFE_MS = 600;

// ---------- PWM helper (works on ESP32 core 2.x and 3.x) ----------
void pwmSetup(int pin) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(pin, PWM_FREQ, PWM_RES);
#else
  static int channel = 0;
  ledcSetup(channel, PWM_FREQ, PWM_RES);
  ledcAttachPin(pin, channel);
  channel++;
#endif
}

void pwmWrite(int pin, int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, duty);
#else
  // core 2.x: channel 0 = ENA, channel 1 = ENB (attached in that order)
  ledcWrite(pin == ENA ? 0 : 1, duty);
#endif
}

// ---------- Motor control ----------
void setMotors(bool l1, bool l2, bool r1, bool r2) {
  digitalWrite(IN1, l1);
  digitalWrite(IN2, l2);
  digitalWrite(IN3, r1);
  digitalWrite(IN4, r2);
  pwmWrite(ENA, speedValue);
  pwmWrite(ENB, speedValue);
}

void stopRover()   { setMotors(LOW,  LOW,  LOW,  LOW ); }
void forward()     { setMotors(HIGH, LOW,  HIGH, LOW ); }
void backward()    { setMotors(LOW,  HIGH, LOW,  HIGH); }
void turnLeft()    { setMotors(LOW,  HIGH, HIGH, LOW ); }  // spin left
void turnRight()   { setMotors(HIGH, LOW,  LOW,  HIGH); }  // spin right

// ---------- Web page ----------
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
<title>ESP32 Rover</title>
<style>
  body{font-family:sans-serif;text-align:center;background:#0b1d3a;color:#fff;margin:0;padding:16px;
       -webkit-user-select:none;user-select:none;touch-action:manipulation}
  h2{margin:8px 0 16px}
  .pad{display:grid;grid-template-columns:repeat(3,90px);grid-gap:12px;justify-content:center;margin:16px 0}
  button{height:90px;font-size:28px;border:none;border-radius:16px;background:#1e6fd9;color:#fff}
  button:active{background:#f5a623;color:#000}
  .stop{background:#d93025}
  input[type=range]{width:80%}
</style></head><body>
<h2>ESP32 Wi-Fi Rover</h2>
<div class="pad">
  <div></div><button data-c="F">&#9650;</button><div></div>
  <button data-c="L">&#9664;</button><button data-c="S" class="stop">&#9632;</button><button data-c="R">&#9654;</button>
  <div></div><button data-c="B">&#9660;</button><div></div>
</div>
<p>Speed: <span id="sv">180</span></p>
<input type="range" id="sp" min="80" max="255" value="180">
<script>
let timer=null;
function send(c){fetch('/cmd?d='+c).catch(()=>{});}
function start(c){send(c); if(c!=='S'){timer=setInterval(()=>send(c),200);}}
function stop(){clearInterval(timer);timer=null;send('S');}
document.querySelectorAll('button').forEach(b=>{
  const c=b.dataset.c;
  b.addEventListener('touchstart',e=>{e.preventDefault();start(c);});
  b.addEventListener('touchend',e=>{e.preventDefault();stop();});
  b.addEventListener('mousedown',()=>start(c));
  b.addEventListener('mouseup',stop);
  b.addEventListener('mouseleave',()=>{if(timer)stop();});
});
const sp=document.getElementById('sp'),sv=document.getElementById('sv');
sp.addEventListener('input',()=>{sv.textContent=sp.value;fetch('/speed?v='+sp.value);});
</script></body></html>
)rawliteral";

// ---------- Web handlers ----------
void handleRoot() {
  server.send_P(200, "text/html", PAGE);
}

void handleCmd() {
  String d = server.arg("d");
  lastCommandTime = millis();
  if      (d == "F") forward();
  else if (d == "B") backward();
  else if (d == "L") turnLeft();
  else if (d == "R") turnRight();
  else               stopRover();
  server.send(200, "text/plain", "OK");
}

void handleSpeed() {
  speedValue = constrain(server.arg("v").toInt(), 0, 255);
  server.send(200, "text/plain", "OK");
}

// ---------- Setup & loop ----------
void setup() {
  Serial.begin(115200);

  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pwmSetup(ENA);
  pwmSetup(ENB);
  stopRover();

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Hotspot started. Open http://");
  Serial.println(WiFi.softAPIP());   // normally 192.168.4.1

  server.on("/",      handleRoot);
  server.on("/cmd",   handleCmd);
  server.on("/speed", handleSpeed);
  server.begin();
}

void loop() {
  server.handleClient();

  // Fail-safe: stop if the phone stops sending commands
  if (millis() - lastCommandTime > FAILSAFE_MS) {
    stopRover();
  }
}
