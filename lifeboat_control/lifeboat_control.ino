/*
 * ============================================================================
 *  열감지 추적 구명튜브 전달장치 — Arduino 제어 소스 (논문 게재 코드 재구성본)
 * ============================================================================
 *
 *  출처: 원본 .ino 파일이 남아 있지 않아, 졸업논문 "4) 아두이노 소스코드
 *  (Figure 16)"에 실린 코드를 텍스트로 옮겨 재구성했다. 로직·핀맵·상수는
 *  논문 원문 그대로이고, 추출 과정에서 깨진 줄바꿈·공백만 복원했다.
 *  상태: arduino:avr:uno 컴파일 확인 / 이 재구성본으로 실기 구동은 하지 않았다.
 *
 * ----------------------------------------------------------------------------
 *  회로 구성
 * ----------------------------------------------------------------------------
 *  주 제어부 : Arduino Uno
 *  주변장치  : 좌우 추진 DC 모터, 구명튜브 팽창용 DC 모터, 열화상 센서 AMG8833,
 *             RC 수신기(3채널), 모터 드라이버 L298N
 *  구동      : 좌우 모터는 L298N을 통해 PWM으로 속도·방향 제어
 *  전원      : 7.4V 리튬폴리머 배터리
 *
 *  핀 매핑
 *    receiver_pins[] = {8, 9, 10}    // RC 수신기 3채널 (조향/속도/특수기능)
 *    motor_pins[]    = {2,3,4,5,6,7} // {IN1,IN2,IN3,IN4,ENA,ENB} L298N 좌우 모터
 *    pin 11, 12                      // 구명튜브 팽창 DC 모터
 *    I2C (A4/A5)                     // AMG8833 (Wire)
 * ============================================================================
 */

#include <Wire.h>
#include <Adafruit_AMG88xx.h>

// Constants
#define PWM_MAX 255
#define PWM_MIN 0

Adafruit_AMG88xx amg;

// 핀 번호 설정
const int receiver_pins[] = {8, 9, 10};
const int motor_pins[] = {2, 3, 4, 5, 6, 7}; // {IN1, IN2, IN3, IN4, ENA, ENB}

// 열 센서 배열
float pixels[AMG88xx_PIXEL_ARRAY_SIZE];
float flipped_pixels[AMG88xx_PIXEL_ARRAY_SIZE];

// 동작 제어
enum Direction { SHARP_LEFT = 1, LEFT, STRAIGHT, RIGHT, SHARP_RIGHT };
Direction ans = STRAIGHT;

// 초기값 설정
float max_temperature = -1000.0;
int max_index = -1;

// 리시버 변수
unsigned long pulse_durations[3] = {0, 0, 0};

// 함수 정의
void setupPins() {
  for (int pin : receiver_pins) {
    pinMode(pin, INPUT);
  }
  for (int i = 0; i < 4; i++) { // IN1~IN4
    pinMode(motor_pins[i], OUTPUT);
    digitalWrite(motor_pins[i], LOW);
  }
  for (int i = 4; i < 6; i++) { // ENA, ENB
    pinMode(motor_pins[i], OUTPUT);
    analogWrite(motor_pins[i], PWM_MIN);
  }
}

void motorControl(int enA, int enB, bool forward = true) {
  digitalWrite(motor_pins[0], forward);  // IN1
  digitalWrite(motor_pins[1], !forward); // IN2
  digitalWrite(motor_pins[2], forward);  // IN3
  digitalWrite(motor_pins[3], !forward); // IN4
  analogWrite(motor_pins[4], enA);       // ENA
  analogWrite(motor_pins[5], enB);       // ENB
}

void processIRSensor() {
  amg.readPixels(pixels);

  // 열 센서 상하 반전 처리
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      flipped_pixels[(7 - row) * 8 + col] = pixels[row * 8 + col];
    }
  }

  // 각 열에서 최대 온도 찾기 (8x8 -> 1x8 차원 축소)
  float column_max[8] = {0};
  for (int col = 0; col < 8; col++) {
    for (int row = 0; row < 8; row++) {
      column_max[col] = max(column_max[col], flipped_pixels[row * 8 + col]);
    }
  }

  // 가장 큰 열의 인덱스 확인
  max_temperature = -1000.0;
  max_index = -1;
  for (int i = 0; i < 8; i++) {
    if (column_max[i] > max_temperature) {
      max_temperature = column_max[i];
      max_index = i;
    }
  }

  // 방향 결정 (5단계)
  if (max_index == 7) ans = SHARP_LEFT;
  else if (max_index >= 6) ans = LEFT;
  else if (max_index >= 4) ans = STRAIGHT;
  else if (max_index >= 2) ans = RIGHT;
  else ans = SHARP_RIGHT;
}

void autonomousDrive() {
  switch (ans) {
    case SHARP_LEFT:
      motorControl(PWM_MIN, PWM_MAX);
      break;
    case LEFT:
      motorControl(200, PWM_MAX);
      break;
    case STRAIGHT:
      motorControl(PWM_MAX, PWM_MAX);
      break;
    case RIGHT:
      motorControl(PWM_MAX, 200);
      break;
    case SHARP_RIGHT:
      motorControl(PWM_MAX, PWM_MIN);
      break;
  }
}

void manualDrive() {
  unsigned long pulse1 = pulse_durations[0]; // 조향
  unsigned long pulse2 = pulse_durations[1]; // 속도
  unsigned long pulse3 = pulse_durations[2]; // 특수기능(구명튜브 팽창)

  if (pulse2 >= 5000) {
    motorControl(PWM_MIN, PWM_MIN);
  } else if (pulse2 >= 1730 && pulse2 <= 2100) {
    int speed = map(pulse2, 1730, 2220, 160, PWM_MAX);
    if (pulse1 >= 1600 && pulse1 <= 1800) {
      motorControl(speed, speed);       // 직진
    } else if (pulse1 < 1300) {
      motorControl(PWM_MIN, speed);     // 좌회전
    } else if (pulse1 > 2220) {
      motorControl(speed, PWM_MIN);     // 우회전
    }
  } else if (pulse2 >= 2220) {
    motorControl(PWM_MAX, PWM_MAX);     // 최고속 전진
  } else {
    motorControl(PWM_MIN, PWM_MIN);     // 정지
  }

  // 특수기능: 구명튜브 팽창 시퀀스 후 정지 락(while(1))
  if (pulse3 >= 1500) {
    analogWrite(11, 0);
    analogWrite(12, 255);
    delay(3000);
    analogWrite(11, 255);
    analogWrite(12, 0);
    delay(3000);
    analogWrite(11, 0);
    analogWrite(12, 0);
    while (1);
  }
}

void setup() {
  Serial.begin(9600);
  if (!amg.begin()) {
    Serial.println("IR sensor initialization failed!");
    while (true);
  }
  // 구명보트 DC모터
  pinMode(11, OUTPUT);
  pinMode(12, OUTPUT);
}

void loop() {
  // 리시버 데이터 읽기
  for (int i = 0; i < 3; i++) {
    pulse_durations[i] = pulseIn(receiver_pins[i], HIGH);
  }

  // 자율 주행 또는 수동 주행
  if (pulse_durations[0] == 0 && pulse_durations[1] == 0 && pulse_durations[2] == 0) {
    processIRSensor();
    autonomousDrive();
  } else {
    manualDrive();
  }

  // 디버깅용 출력
  Serial.print("Pulse1: "); Serial.print(pulse_durations[0]);
  Serial.print(", Pulse2: "); Serial.print(pulse_durations[1]);
  Serial.print(", Pulse3: "); Serial.println(pulse_durations[2]);
  Serial.print(", max_index: "); Serial.println(max_index);
  delay(100);
}

/*
 * ============================================================================
 *  알려진 특이점 (논문 코드 그대로 두고 기록만 함)
 * ============================================================================
 *  1. ENB(핀 7)는 Uno에서 PWM 핀이 아니어서 analogWrite(200)과 (255)가 모두
 *     HIGH로 나간다 -> RIGHT(255,200)와 STRAIGHT(255,255)의 출력이 같아,
 *     방향 5단계 중 실제로 구분되는 출력은 4가지다.
 *  2. 자율/수동 분기는 3채널 pulseIn 결과가 모두 0일 때 자율이다. 0은
 *     timeout 결과이므로 무신호일 때 채널마다 최대 1초씩 지연될 수 있다.
 *  3. setupPins()가 setup()에서 호출되지 않는다(논문 원문 그대로).
 *  4. 구명튜브 팽창 시퀀스(pulse3 >= 1500)는 while(1)에 들어가기 전에 좌우
 *     추진 모터를 정지시키지 않는다. 추진 정지 경로는 pulse2 >= 5000뿐이다.
 *  5. manualDrive의 속도 판정 범위(1730~2100)와 map 상한(2220)이 서로 다르다.
 * ============================================================================
 */
