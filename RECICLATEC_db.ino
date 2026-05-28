
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

//pantalla LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

//motor
int pinIN1 = 13;
int pinIN2 = 14;
int pinENA = 3;

//sensores ir
int IR_O = 4;
int IR_P = 5;
int IR_C = 22;
int IR_E = 2;

//servomotores
const int PIN_SERVO_O = 44;
const int PIN_SERVO_P = 45;
const int PIN_SERVO_C = 46;

//posiciones de servo
const int SERVO_INACTIVO = 90;
const int SERVO_ACTIVO   = 140;

//ultrsónicos
#define TRIG_C  7  //papel o carton
#define ECHO_C  8
#define TRIG_P  9  //plásticos
#define ECHO_P  10
#define TRIG_O  11  //Orgánicos
#define ECHO_O  12

// umbrales para niveles de llenado
#define DIST_MAX_CM   7.0
#define UMBRAL_CASI   0.60
#define UMBRAL_LLENO  0.85
#define INTERVALO_US  3000UL

//tiempos de servos
const unsigned long TIEMPO_SERVO_ACTIVO     = 3000;
const unsigned long TIEMPO_BANDA_TRAS_SERVO = 2000;

//velocidad del motor
const int VELOCIDAD = 100;


Servo servoO, servoP, servoC;

//estados del sistma
enum Estado {
  ESPERANDO_IR_E,
  ESPERANDO_CLASIF,
  BANDA_ON,
  SERVO_ABIERTO,
  SERVO_CERRANDO,
  PAUSA_FINAL
};

Estado        estado        = ESPERANDO_IR_E;
int           sensorDestino = -1;
int           servoActual   = -1;
unsigned long tEstado       = 0;
unsigned long tUltrasonico  = 0;

void setup() {
  Serial.begin(9600);

  pinMode(pinIN1, OUTPUT);
  pinMode(pinIN2, OUTPUT);
  pinMode(pinENA, OUTPUT);

  pinMode(IR_O, INPUT);
  pinMode(IR_P, INPUT);
  pinMode(IR_C, INPUT);
  pinMode(IR_E, INPUT_PULLUP);

  // Ultrasónicos
  pinMode(TRIG_C, OUTPUT); pinMode(ECHO_C, INPUT);
  pinMode(TRIG_P, OUTPUT); pinMode(ECHO_P, INPUT);
  pinMode(TRIG_O, OUTPUT); pinMode(ECHO_O, INPUT);

  servoO.attach(PIN_SERVO_O);
  servoP.attach(PIN_SERVO_P);
  servoC.attach(PIN_SERVO_C);
  servoO.write(SERVO_INACTIVO);
  servoP.write(SERVO_INACTIVO);
  servoC.write(SERVO_INACTIVO);

  motorOFF();

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(3, 0);
  lcd.print("Reciclaje");
  lcd.setCursor(2, 1);
  lcd.print("Bienvenido!");

  Serial.println("[SISTEMA] Listo. Esperando IR_E...");
}


void loop() {
  unsigned long ahora = millis();

  //lecturas ultrasónicas periódicas
  if (ahora - tUltrasonico >= INTERVALO_US) {
    tUltrasonico = ahora;
    leerNiveles();
  }

  //LEE LA LETRA DESDE PYTHON
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') return;
    c = toupper(c);

    if (estado == ESPERANDO_CLASIF && (c == 'O' || c == 'P' || c == 'C')) {
      asignarDestino(c);
      motorON();
      estado = BANDA_ON;
      lcd.clear();
      lcd.setCursor(3, 0);
      lcd.print("Reciclaje");
      lcd.setCursor(0, 1);
      lcd.print("Clase: ");
      if      (c == 'O') lcd.print("Organico");
      else if (c == 'P') lcd.print("Plastico");
      else if (c == 'C') lcd.print("Carton");
      Serial.print("[OK] Clasificacion recibida: "); Serial.println(c);
    }
  }

  switch (estado) {

    case ESPERANDO_IR_E:
      if (digitalRead(IR_E) == LOW) {
        Serial.println("[IR_E] Objeto detectado.");
        lcd.clear();
        lcd.setCursor(3, 0);
        lcd.print("Reciclaje");
        lcd.setCursor(2, 1);
        lcd.print("En proceso...");
        Serial.println("CLASIFICAR");
        estado = ESPERANDO_CLASIF;
      }
      break;

    case ESPERANDO_CLASIF:
      break;

    case BANDA_ON:
      if (sensorDestino != -1 && digitalRead(sensorDestino) == LOW) {
        Serial.println("[IR-DEST] Objeto en destino. Abriendo servo...");
        abrirServo();
        tEstado = ahora;
        estado  = SERVO_ABIERTO;
      }
      break;

    case SERVO_ABIERTO:
      if (ahora - tEstado >= TIEMPO_SERVO_ACTIVO) {
        cerrarServo();
        tEstado = ahora;
        estado  = SERVO_CERRANDO;
        Serial.println("[SERVO] Cerrado. Banda para en 2 s...");
      }
      break;

    case SERVO_CERRANDO:
      if (ahora - tEstado >= TIEMPO_BANDA_TRAS_SERVO) {
        motorOFF();
        tEstado = ahora;
        estado  = PAUSA_FINAL;
        Serial.println("[MOTOR] Banda detenida.");
      }
      break;

    case PAUSA_FINAL:
      if (ahora - tEstado >= 500) {
        sensorDestino = -1;
        servoActual   = -1;
        estado        = ESPERANDO_IR_E;
        lcd.clear();
        lcd.setCursor(3, 0);
        lcd.print("Reciclaje");
        lcd.setCursor(2, 1);
        lcd.print("Bienvenido!");
        Serial.println("CLASIFICAR");
        Serial.println("[SISTEMA] Ciclo completo. Esperando IR_E...");
      }
      break;
  }
}

//acciones
void asignarDestino(char c) {
  if      (c == 'O') { sensorDestino = IR_O; servoActual = 0; }
  else if (c == 'P') { sensorDestino = IR_P; servoActual = 1; }
  else if (c == 'C') { sensorDestino = IR_C; servoActual = 2; }
}

void abrirServo() {
  if      (servoActual == 0) { servoO.write(SERVO_ACTIVO); Serial.println("[SERVO O] ABIERTO"); }
  else if (servoActual == 1) { servoP.write(SERVO_ACTIVO); Serial.println("[SERVO P] ABIERTO"); }
  else if (servoActual == 2) { servoC.write(SERVO_ACTIVO); Serial.println("[SERVO C] ABIERTO"); }
}

void cerrarServo() {
  if      (servoActual == 0) { servoO.write(SERVO_INACTIVO); Serial.println("[SERVO O] CERRADO"); }
  else if (servoActual == 1) { servoP.write(SERVO_INACTIVO); Serial.println("[SERVO P] CERRADO"); }
  else if (servoActual == 2) { servoC.write(SERVO_INACTIVO); Serial.println("[SERVO C] CERRADO"); }
}

void motorON() {
  digitalWrite(pinIN1, HIGH);
  digitalWrite(pinIN2, LOW);
  analogWrite(pinENA, VELOCIDAD);
  Serial.println("[MOTOR] ENCENDIDO");
}

void motorOFF() {
  digitalWrite(pinIN1, LOW);
  digitalWrite(pinIN2, LOW);
  analogWrite(pinENA, 0);
  Serial.println("[MOTOR] APAGADO");
}

// ULTRASÓNICOS


long medirDistancia(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long duracion = pulseIn(echo, HIGH, 30000UL);
  return duracion * 0.034 / 2;
}

const char* nivelTexto(long distCm) {
  if (distCm <= 0 || distCm > 400) return "ERROR";
  float pct = 1.0 - ((float)distCm / DIST_MAX_CM);
  if (pct < 0) pct = 0;
  if (pct > 1) pct = 1;
  if (pct >= UMBRAL_LLENO) return "LLENO";
  if (pct >= UMBRAL_CASI)  return "CASI LLENO";
  return "DISPONIBLE";
}

void leerNiveles() {
  long dC = medirDistancia(TRIG_C, ECHO_C);
  long dP = medirDistancia(TRIG_P, ECHO_P);
  long dO = medirDistancia(TRIG_O, ECHO_O);

  Serial.print("[US] CARTON:");  Serial.print(nivelTexto(dC));
  Serial.print(" | PLASTICO:"); Serial.print(nivelTexto(dP));
  Serial.print(" | ORGANICO:"); Serial.println(nivelTexto(dO));
}