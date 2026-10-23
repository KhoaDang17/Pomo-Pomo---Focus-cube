#include <Wire.h>
#include "pico/stdlib.h"

#define I2C_ADDR 0x42

const int rowPins[8] = {21, 20, 19, 18, 17, 16, 15, 14};
const int colPins[8] = {2, 3, 4, 5, 6, 7, 8, 9};

uint8_t layout[8][8] = {
  {1, 0, 0, 0, 0, 0, 0, 1},
  {1, 0, 0, 0, 0, 0, 0, 1},
  {3, 1, 0, 0, 0, 0, 1, 3},
  {3, 3, 1, 0, 0, 1, 3, 3},
  {3, 3, 1, 0, 0, 1, 3, 3},
  {3, 1, 0, 0, 0, 0, 1, 3},
  {1, 0, 0, 0, 0, 0, 0, 1},
  {1, 0, 0, 0, 0, 0, 0, 1} 
};

volatile uint8_t grid[8][8];
volatile bool isUpsideDown = false; 

void displayGrid() {
    for (int r = 0; r < 8; r++) {
        for (int i = 0; i < 8; i++) {
            digitalWrite(rowPins[i], LOW); digitalWrite(colPins[i], HIGH);
        }
        delayMicroseconds(50);
        int renderR = isUpsideDown ? (7 - r) : r;
        for (int c = 0; c < 8; c++) {
            int renderC = isUpsideDown ? (7 - c) : c;
            if (grid[r][c] == 1 || grid[r][c] == 2) digitalWrite(colPins[renderC], LOW);  
            else digitalWrite(colPins[renderC], HIGH); 
        }
        digitalWrite(rowPins[renderR], HIGH);
        delay(2);
    }
}

volatile uint8_t animState = 0; // 0=Stop, 1=Focus, 2=Break, 3=Pause(Left), 4=Pause(Right)
volatile int pendingDrops = 0;

void resetGrid() {
    for(int r=0; r<8; r++) {
        for(int c=0; c<8; c++) grid[r][c] = layout[r][c];
    }
    for(int r=0; r<=2; r++) {
        for(int c=0; c<8; c++) {
            if(grid[r][c] == 0) grid[r][c] = 2; // Khởi tạo lại 16 hạt cát ở trên
        }
    }
}

void receiveEvent(int bytes) {
    while (Wire.available()) { 
        uint8_t cmd = Wire.read();
        if (cmd == 0x05) pendingDrops++;
        else if (cmd == 0x06) {
            resetGrid();
            pendingDrops = 0;
        }
        else {
            animState = cmd;
            if (cmd == 1) isUpsideDown = false;
            else if (cmd == 2) isUpsideDown = true;
        }
    }
}

void setup() {
    for(int i=0; i<8; i++) {
        pinMode(rowPins[i], OUTPUT); pinMode(colPins[i], OUTPUT);
        digitalWrite(rowPins[i], LOW); digitalWrite(colPins[i], HIGH);
    }
    Wire.setSDA(0); Wire.setSCL(1);
    Wire.begin(I2C_ADDR);
    Wire.onReceive(receiveEvent);
    resetGrid();
}

// THUẬT TOÁN CÁT TRÀN SANG BÊN VÁCH KÍNH KHI KHỐI NẰM NGANG
void settleSidewaysCA(int dc) {
    int cStart = (dc == 1) ? 6 : 1;
    int cEnd   = (dc == 1) ? -1 : 8;
    int cInc   = (dc == 1) ? -1 : 1;

    for (int c = cStart; c != cEnd; c += cInc) {
        int rows[8] = {0,1,2,3,4,5,6,7};
        for(int i=0; i<8; i++) { int s=random(8); int t=rows[i]; rows[i]=rows[s]; rows[s]=t; }
        
        for (int i = 0; i < 8; i++) {
            int r = rows[i];
            if (grid[r][c] == 2) {
                if (grid[r][c+dc] == 0) {
                    grid[r][c] = 0; grid[r][c+dc] = 2;
                } else {
                    bool canUp = (r > 0 && grid[r-1][c+dc] == 0);
                    bool canDown = (r < 7 && grid[r+1][c+dc] == 0);
                    if (canUp && canDown) {
                        if (random(2) == 0) { grid[r][c] = 0; grid[r-1][c+dc] = 2; }
                        else { grid[r][c] = 0; grid[r+1][c+dc] = 2; }
                    } else if (canUp) { grid[r][c] = 0; grid[r-1][c+dc] = 2; } 
                    else if (canDown) { grid[r][c] = 0; grid[r+1][c+dc] = 2; }
                }
            }
        }
    }
}

// THUẬT TOÁN CÁT RƠI DỌC QUA CỔ CHAI KHI ĐANG ĐẾM NGƯỢC
void updateHourglassCA() {
    for (int r = 6; r >= 0; r--) {
        int cols[8] = {0,1,2,3,4,5,6,7};
        for (int i = 0; i < 8; i++) {
            int swapIdx = random(8);
            int t = cols[i]; cols[i] = cols[swapIdx]; cols[swapIdx] = t;
        }

        for (int i = 0; i < 8; i++) {
            int c = cols[i];
            if (grid[r][c] == 2) {
                if (r == 2) {
                    if (c == 3 || c == 4) {
                        if (pendingDrops > 0 && grid[r+1][c] == 0) {
                            grid[r][c] = 0; grid[r+1][c] = 2; pendingDrops--;
                        }
                    } else {
                        int targetC = (c < 3) ? c + 1 : c - 1;
                        if (grid[r][targetC] == 0) {
                            grid[r][c] = 0; grid[r][targetC] = 2;
                        }
                    }
                    continue; 
                }

                if (grid[r+1][c] == 0) { grid[r][c] = 0; grid[r+1][c] = 2; }
                else { 
                    bool canLeft = (c > 0 && grid[r+1][c-1] == 0);
                    bool canRight = (c < 7 && grid[r+1][c+1] == 0);
                    
                    if (canLeft && canRight) {
                        if (random(2) == 0) { grid[r][c] = 0; grid[r+1][c-1] = 2; } 
                        else { grid[r][c] = 0; grid[r+1][c+1] = 2; }
                    } 
                    else if (canLeft) { grid[r][c] = 0; grid[r+1][c-1] = 2; } 
                    else if (canRight) { grid[r][c] = 0; grid[r+1][c+1] = 2; }
                    else {
                        bool slideLeft = (c > 0 && grid[r][c-1] == 0 && grid[r+1][c-1] == 0);
                        bool slideRight = (c < 7 && grid[r][c+1] == 0 && grid[r+1][c+1] == 0);
                        if (slideLeft && slideRight) {
                            if (random(2) == 0) { grid[r][c] = 0; grid[r][c-1] = 2; }
                            else { grid[r][c] = 0; grid[r][c+1] = 2; }
                        } else if (slideLeft) { grid[r][c] = 0; grid[r][c-1] = 2;
                        } else if (slideRight) { grid[r][c] = 0; grid[r][c+1] = 2; }
                    }
                }
            }
        }
    }
}

unsigned long lastSandMove = 0;
void loop() {
    if (animState == 1 || animState == 2) {
        if (millis() - lastSandMove > 50) { 
            lastSandMove = millis();
            updateHourglassCA(); 
        }
    } else if (animState == 3) {
        if (millis() - lastSandMove > 50) { 
            lastSandMove = millis();
            settleSidewaysCA(-1); // Dàn cát sang trái vách kính
        }
    } else if (animState == 4) {
        if (millis() - lastSandMove > 50) { 
            lastSandMove = millis();
            settleSidewaysCA(1);  // Dàn cát sang phải vách kính
        }
    }
    
    displayGrid();
}