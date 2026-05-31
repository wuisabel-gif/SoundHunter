#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Microphone ADC pins
const int MIC_FRONT = 32;
const int MIC_REAR  = 33;
const int MIC_LEFT  = 34;
const int MIC_RIGHT = 35;

// OLED I2C pins
const int OLED_SDA = 21;
const int OLED_SCL = 22;

// Tune these after testing
const int SAMPLE_COUNT = 300;
const int SOUND_THRESHOLD = 35;
const float EMA_ALPHA = 0.18f;
const float DIRECTION_DEADBAND = 12.0f;
const unsigned long DISPLAY_REFRESH_MS = 120;

float readMicLevel(int pin) {
  long sum = 0;

  // First get average DC bias
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    sum += analogRead(pin);
  }

  float avg = sum / (float)SAMPLE_COUNT;

  // Then get sound energy around that average
  float energy = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++) {
    float v = analogRead(pin) - avg;
    energy += abs(v);
  }

  return energy / SAMPLE_COUNT;
}

struct DirectionResult {
  String label;
  float dx;
  float dy;
  float strength;
};

float smoothValue(float current, float incoming) {
  return (current * (1.0f - EMA_ALPHA)) + (incoming * EMA_ALPHA);
}

float maxAbsComponent(float horizontal, float vertical) {
  return max(fabs(horizontal), fabs(vertical));
}

DirectionResult classifyDirection(float horizontal, float vertical) {
  DirectionResult result;
  result.label = "QUIET";
  result.dx = 0.0f;
  result.dy = 0.0f;
  result.strength = maxAbsComponent(horizontal, vertical);

  if (result.strength < DIRECTION_DEADBAND) {
    return result;
  }

  float angle = atan2f(vertical, horizontal);
  const float sector = PI / 8.0f;

  if (angle >= -sector && angle < sector) {
    result.label = "RIGHT";
    result.dx = 1.0f;
    result.dy = 0.0f;
  } else if (angle >= sector && angle < 3.0f * sector) {
    result.label = "FRONT RIGHT";
    result.dx = 0.7f;
    result.dy = 0.7f;
  } else if (angle >= 3.0f * sector && angle < 5.0f * sector) {
    result.label = "FRONT";
    result.dx = 0.0f;
    result.dy = 1.0f;
  } else if (angle >= 5.0f * sector && angle < 7.0f * sector) {
    result.label = "FRONT LEFT";
    result.dx = -0.7f;
    result.dy = 0.7f;
  } else if (angle >= 7.0f * sector || angle < -7.0f * sector) {
    result.label = "LEFT";
    result.dx = -1.0f;
    result.dy = 0.0f;
  } else if (angle >= -7.0f * sector && angle < -5.0f * sector) {
    result.label = "REAR LEFT";
    result.dx = -0.7f;
    result.dy = -0.7f;
  } else if (angle >= -5.0f * sector && angle < -3.0f * sector) {
    result.label = "REAR";
    result.dx = 0.0f;
    result.dy = -1.0f;
  } else {
    result.label = "REAR RIGHT";
    result.dx = 0.7f;
    result.dy = -0.7f;
  }

  return result;
}

void drawCompass(const DirectionResult &direction) {
  const int centerX = 92;
  const int centerY = 32;
  const int radius = 20;

  display.drawCircle(centerX, centerY, radius, SSD1306_WHITE);
  display.drawCircle(centerX, centerY, radius - 1, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(centerX - 2, centerY - radius - 8);
  display.print("N");
  display.setCursor(centerX - 2, centerY + radius + 2);
  display.print("S");
  display.setCursor(centerX - radius - 8, centerY - 3);
  display.print("W");
  display.setCursor(centerX + radius + 4, centerY - 3);
  display.print("E");

  display.fillCircle(centerX, centerY, 2, SSD1306_WHITE);

  if (direction.label == "QUIET") {
    return;
  }

  int arrowX = centerX + (int)(direction.dx * (radius - 4));
  int arrowY = centerY - (int)(direction.dy * (radius - 4));
  display.drawLine(centerX, centerY, arrowX, arrowY, SSD1306_WHITE);
  display.fillCircle(arrowX, arrowY, 2, SSD1306_WHITE);
}

void showDirection(const DirectionResult &direction, float front, float rear, float left, float right, float totalSound) {
  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("SoundHunter");

  display.setCursor(0, 12);
  display.print("Dir ");
  display.println(direction.label);

  drawCompass(direction);

  display.setCursor(0, 26);
  display.print("Lvl ");
  display.print((int)totalSound);
  display.setTextSize(1);
  display.setCursor(0, 54);
  display.print("F:");
  display.print((int)front);
  display.print(" B:");
  display.print((int)rear);
  display.print(" L:");
  display.print((int)left);
  display.print(" R:");
  display.print((int)right);

  display.display();
}

float smoothFront = 0.0f;
float smoothRear = 0.0f;
float smoothLeft = 0.0f;
float smoothRight = 0.0f;
unsigned long lastDisplayUpdate = 0;

void setup() {
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Acoustic Compass");
  display.println("Starting...");
  display.display();

  delay(1000);
}

void loop() {
  float rawFront = readMicLevel(MIC_FRONT);
  float rawRear  = readMicLevel(MIC_REAR);
  float rawLeft  = readMicLevel(MIC_LEFT);
  float rawRight = readMicLevel(MIC_RIGHT);

  smoothFront = smoothValue(smoothFront, rawFront);
  smoothRear = smoothValue(smoothRear, rawRear);
  smoothLeft = smoothValue(smoothLeft, rawLeft);
  smoothRight = smoothValue(smoothRight, rawRight);

  float horizontal = smoothRight - smoothLeft;
  float vertical   = smoothFront - smoothRear;
  float totalSound = smoothFront + smoothRear + smoothLeft + smoothRight;

  DirectionResult direction;
  direction.label = "QUIET";
  direction.dx = 0.0f;
  direction.dy = 0.0f;
  direction.strength = 0.0f;

  if (totalSound > SOUND_THRESHOLD) {
    direction = classifyDirection(horizontal, vertical);
  }

  unsigned long now = millis();
  if (now - lastDisplayUpdate >= DISPLAY_REFRESH_MS) {
    showDirection(direction, smoothFront, smoothRear, smoothLeft, smoothRight, totalSound);
    lastDisplayUpdate = now;
  }

  delay(40);
}
