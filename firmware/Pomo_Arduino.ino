#include <Wire.h>
#include <rgb_lcd.h>
#include <DFRobot_LIS2DH12.h>
#include <DHT.h>

#define PICO_I2C_ADDR 0x42
#define PIN_BTN1 1
#define PIN_BTN2 2
#define PIN_BTN3 3
#define PIN_DHT  4
#define PIN_BUZZER 5
#define DHTTYPE DHT11

// --- CẤU HÌNH TRỤC GIA TỐC ---
#define ACCEL_THRESHOLD 500
enum Orientation { FLAT, SIDE_A, SIDE_B, UNKNOWN };

Orientation dynamicFocusOri = UNKNOWN;
Orientation dynamicBreakOri = UNKNOWN;

rgb_lcd lcd;
DFRobot_LIS2DH12 LIS(&Wire, 0x18);
DHT dht(PIN_DHT, DHTTYPE);

enum State { SETUP_FOCUS, SETUP_BREAK, STANDBY, FOCUS_RUN, FOCUS_PAUSE, BREAK_RUN, BREAK_PAUSE };
State currentState = SETUP_FOCUS;

int focusTimeSec = 25 * 60; // 25 phút
int breakTimeSec = 5 * 60;  // 5 phút
long currentRunMs = 0;
long totalRunMs = 0;
int grainsDropped = 0;
int cursorCol = 0; 
unsigned long lastTimerUpdate = 0;
unsigned long lastBlink = 0;
bool cursorBlink = true;

// Class nút bấm 
class TouchButton {
  public:
    uint8_t pin;
    bool lastState = LOW;
    unsigned long pressedTime = 0, releasedTime = 0, lastRepeatTime = 0;
    bool isPressed = false, isHeld = false;
    int clickCount = 0;
    const unsigned long holdTime = 800, doubleClickTime = 300, repeatInterval = 150;
    bool singleClick = false, doubleClick = false, repeat = false;

    TouchButton(uint8_t p) : pin(p) {}
    void begin() { pinMode(pin, INPUT_PULLDOWN); }
    void update() {
        singleClick = false; doubleClick = false; repeat = false;
        bool currentState = digitalRead(pin);
        unsigned long now = millis();

        if (currentState == HIGH && lastState == LOW) {
            pressedTime = now; isPressed = true; isHeld = false; lastRepeatTime = now + holdTime;
        } else if (currentState == LOW && lastState == HIGH) {
            releasedTime = now; isPressed = false;
            if (!isHeld) clickCount++; else isHeld = false;
        }

        if (isPressed) {
            if (now - pressedTime > holdTime) {
                isHeld = true;
                if (now - lastRepeatTime > repeatInterval) { repeat = true; lastRepeatTime = now; }
            }
        } else {
            if (clickCount > 0 && now - releasedTime > doubleClickTime) {
                if (clickCount == 1) singleClick = true;
                else if (clickCount >= 2) doubleClick = true;
                clickCount = 0;
            }
        }
        lastState = currentState;
    }
};

TouchButton btn1(PIN_BTN1);
TouchButton btn2(PIN_BTN2);
TouchButton btn3(PIN_BTN3);

void sendPicoCommand(uint8_t cmd) {
    Wire.beginTransmission(PICO_I2C_ADDR);
    Wire.write(cmd);
    Wire.endTransmission();
}

void resetFocus() {
    currentState = FOCUS_RUN;
    totalRunMs = focusTimeSec * 1000L;
    currentRunMs = totalRunMs;
    grainsDropped = 0;
    lastTimerUpdate = millis();
    sendPicoCommand(0x06); // Clear grid and refill top
    sendPicoCommand(0x01); // Focus Animation (16x2 normal)
    lcd.clear(); // Đảm bảo màn hình sạch sẽ
}

void resetBreak() {
    currentState = BREAK_RUN;
    totalRunMs = breakTimeSec * 1000L;
    currentRunMs = totalRunMs;
    grainsDropped = 0;
    lastTimerUpdate = millis();
    sendPicoCommand(0x06); // Clear grid and refill top
    sendPicoCommand(0x02); // Break Animation (16x2 upside down)
    lcd.clear(); 
}

void setup() {
    Serial.begin(115200);
    Wire.begin(); 
    btn1.begin(); btn2.begin(); btn3.begin();
    pinMode(PIN_BUZZER, OUTPUT);

    lcd.begin(16, 2);
    dht.begin();
    
    while(!LIS.begin()) { delay(100); }
    LIS.setRange(DFRobot_LIS2DH12::eLIS2DH12_2g);
    LIS.setAcquireRate(DFRobot_LIS2DH12::eLowPower_10Hz);
    
    lcd.setRGB(0, 255, 0);
    lcd.print("Focus Cube V3");
    delay(1000);
    lcd.clear();
}

void adjustTime(int &totalSec, int col, int amount) {
    int h = totalSec / 3600;
    int m = (totalSec % 3600) / 60;
    int s = totalSec % 60;
    if (col == 0) h = (h + amount + 2) % 2; 
    else if (col == 1) m = (m + amount + 60) % 60;
    else if (col == 2) s = (s + amount + 60) % 60;
    totalSec = constrain(h * 3600 + m * 60 + s, 5, 7199); 
}

void handleSetState(int &timeSec, State nextState) {
    if (btn1.singleClick || btn1.repeat) adjustTime(timeSec, cursorCol, 1);
    if (btn3.singleClick || btn3.repeat) adjustTime(timeSec, cursorCol, -1);
    if (btn2.singleClick) cursorCol = (cursorCol + 1) % 3;
    if (btn2.doubleClick) {
        cursorCol = 0;
        currentState = nextState;
        lcd.clear();
    }
}

// Đọc và chống nhiễu trục gia tốc
Orientation getOrientation() {
    float y = LIS.readAccY();
    float z = LIS.readAccZ();
    
    // Nằm ngang đọc được (Trục Y hướng lên/xuống)
    if (abs(y) > ACCEL_THRESHOLD) return FLAT; 
    // Trục Z hướng xuống mặt đất (Xoay sang 1 trong 2 bên)
    if (z > ACCEL_THRESHOLD) return SIDE_A; 
    if (z < -ACCEL_THRESHOLD) return SIDE_B; 
    
    return UNKNOWN;
}

void loop() {
    btn1.update(); btn2.update(); btn3.update();
    
    // Đọc gia tốc kế có Debounce (Chỉ đổi trạng thái khi khối giữ im 0.5s)
    Orientation ori = getOrientation();
    static Orientation stableOri = UNKNOWN;
    static unsigned long lastOriChange = 0;
    if (ori != UNKNOWN && ori != stableOri) {
        if (millis() - lastOriChange > 500) stableOri = ori;
    } else {
        lastOriChange = millis();
    }

    // STATE MACHINE CHÍNH DỰA VÀO ĐỘ TRỌNG LỰC
    switch(currentState) {
        case SETUP_FOCUS: handleSetState(focusTimeSec, SETUP_BREAK); break;
        case SETUP_BREAK: handleSetState(breakTimeSec, STANDBY); break;
        
        case STANDBY:
            // Mặt ĐẦU TIÊN mà người dùng lật sang sẽ luôn được gán là mặt Focus!
            if (stableOri == SIDE_A || stableOri == SIDE_B) {
                dynamicFocusOri = stableOri;
                dynamicBreakOri = (stableOri == SIDE_A) ? SIDE_B : SIDE_A;
                resetFocus();
            }
            break;
            
        case FOCUS_RUN:
            if (stableOri == FLAT) {
                currentState = FOCUS_PAUSE;
                noTone(PIN_BUZZER);
                sendPicoCommand(0x03); // Báo Pico cát dàn sang trái
                lcd.clear();
            } else if (stableOri == dynamicBreakOri) {
                noTone(PIN_BUZZER);
                resetBreak(); // Lật thẳng sang Break -> Huỷ Focus và bắt đầu Break
            }
            break;
            
        case FOCUS_PAUSE:
            if (stableOri == dynamicFocusOri) {
                currentState = FOCUS_RUN;
                lastTimerUpdate = millis();
                sendPicoCommand(0x01); // Báo Pico tiếp tục chạy
                lcd.clear();
            } else if (stableOri == dynamicBreakOri) {
                resetBreak();
            }
            break;
            
        case BREAK_RUN:
            if (stableOri == FLAT) {
                currentState = BREAK_PAUSE;
                noTone(PIN_BUZZER);
                sendPicoCommand(0x03); // Báo Pico cát dàn sang trái
                lcd.clear();
            } else if (stableOri == dynamicFocusOri) {
                noTone(PIN_BUZZER);
                resetFocus(); 
            }
            break;
            
        case BREAK_PAUSE:
            if (stableOri == dynamicBreakOri) {
                currentState = BREAK_RUN;
                lastTimerUpdate = millis();
                sendPicoCommand(0x02); // Báo Pico tiếp tục chạy
                lcd.clear();
            } else if (stableOri == dynamicFocusOri) {
                resetFocus();
            }
            break;
    }
    
    // XỬ LÝ THỜI GIAN KHI ĐANG RUN
    if (currentState == FOCUS_RUN || currentState == BREAK_RUN) {
        unsigned long dt = millis() - lastTimerUpdate;
        lastTimerUpdate = millis();
        currentRunMs -= dt;
        
        if (currentRunMs <= 0) {
            currentRunMs = 0;
            // Bíp bíp liên tục khi hết giờ, đợi người dùng lật khối
            if (millis() % 1000 < 300) tone(PIN_BUZZER, 2000); else noTone(PIN_BUZZER);
        } else {
            noTone(PIN_BUZZER);
            int expectedGrains = 16 - (16L * currentRunMs / totalRunMs);
            if (expectedGrains > grainsDropped && grainsDropped < 16) {
                sendPicoCommand(0x05); 
                grainsDropped++;
            }
        }
    }
    
    // LOGIC NÚT BẤM HIỂN THỊ NHIỆT ĐỘ (OVERLAY)
    bool currentShowTemp = false;
    if (currentState >= FOCUS_RUN && btn2.isPressed && (millis() - btn2.pressedTime > 500)) {
        currentShowTemp = true; // Giữ nút 2 > 0.5s ở bất kỳ mode nào cũng hiện Nhiệt độ
    }
    
    static bool lastShowTemp = false;
    if (currentShowTemp != lastShowTemp) {
        lcd.clear(); // Xoá sạch tàn dư màn hình cũ khi vừa chuyển đổi
        lastShowTemp = currentShowTemp;
    }
    
    // HIỂN THỊ LCD
    if (currentState == SETUP_FOCUS || currentState == SETUP_BREAK) {
        lcd.setCursor(0, 0);
        lcd.print(currentState == SETUP_FOCUS ? "Set Focus Time  " : "Set Break Time  ");
        lcd.setCursor(4, 1);
        int t = (currentState == SETUP_FOCUS) ? focusTimeSec : breakTimeSec;
        char buf[9]; sprintf(buf, "%02d:%02d:%02d", t/3600, (t%3600)/60, t%60);
        
        if (millis() - lastBlink > 400) { cursorBlink = !cursorBlink; lastBlink = millis(); }
        if (cursorBlink) {
            if (cursorCol == 0) { buf[0]=' '; buf[1]=' '; }
            else if (cursorCol == 1) { buf[3]=' '; buf[4]=' '; }
            else if (cursorCol == 2) { buf[6]=' '; buf[7]=' '; }
        }
        lcd.print(buf);
    } 
    else if (currentState == STANDBY) {
        lcd.setCursor(0, 0); lcd.print(" Ready to Start ");
        lcd.setCursor(0, 1); lcd.print(" Rotate to Run  ");
    }
    else {
        if (currentShowTemp) {
            float t = dht.readTemperature();
            float h = dht.readHumidity();
            lcd.setCursor(0, 0);
            if (isnan(t)) {
                lcd.print("Temp: Error     ");
                lcd.setCursor(0, 1);
                lcd.print("Hum: Error      ");
            } else {
                lcd.print("Temp: "); lcd.print(t, 1); lcd.print(" C      ");
                lcd.setCursor(0, 1);
                lcd.print("Hum:  "); lcd.print(h, 1); lcd.print(" %      ");
            }
        } else {
            lcd.setCursor(0, 0);
            if (currentState == FOCUS_RUN) lcd.print("Focus Running   ");
            else if (currentState == FOCUS_PAUSE) lcd.print("Focus Paused    ");
            else if (currentState == BREAK_RUN) lcd.print("Break Running   ");
            else if (currentState == BREAK_PAUSE) lcd.print("Break Paused    ");
            
            // Dùng 16 ký tự để ghi đè, chặn đứng lỗi hiển thị chồng chéo
            lcd.setCursor(0, 1);
            int displaySec = currentRunMs / 1000;
            char buf[17]; 
            sprintf(buf, "    %02d:%02d:%02d    ", displaySec/3600, (displaySec%3600)/60, displaySec%60);
            lcd.print(buf);
        }
    }
    
    delay(10);
}