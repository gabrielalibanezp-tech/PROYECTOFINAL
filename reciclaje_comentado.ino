// ============================================================
//  SISTEMA DE CLASIFICACIÓN DE RESIDUOS EN BANDA TRANSPORTADORA
//  Recibe la clase (O/P/C) desde Python por Serial,
//  mueve la banda, activa el servo correcto y mide niveles.
// ============================================================


// ---------- LIBRERÍAS ----------
#include <Servo.h>              // Permite controlar servomotores con PWM
#include <Wire.h>               // Activa la comunicación I2C (necesaria para el LCD)
#include <LiquidCrystal_I2C.h> // Librería para manejar la pantalla LCD por I2C


// ---------- PANTALLA LCD ----------
// Crea el objeto lcd en la dirección I2C 0x27, con 16 columnas y 2 filas
LiquidCrystal_I2C lcd(0x27, 16, 2);


// ---------- PINES DEL MOTOR (L298N) ----------
int pinIN1 = 13;   // IN1 del L298N: controla la dirección del motor (junto con IN2)
int pinIN2 = 14;   // IN2 del L298N: controla la dirección del motor (junto con IN1)
int pinENA = 3;    // Enable A del L298N: recibe PWM para regular la velocidad


// ---------- PINES DE SENSORES IR ----------
int IR_O = 4;   // Sensor IR del contenedor Orgánico  → detecta si el objeto llegó
int IR_P = 5;   // Sensor IR del contenedor Plástico  → detecta si el objeto llegó
int IR_C = 22;  // Sensor IR del contenedor Cartón    → detecta si el objeto llegó
int IR_E = 2;   // Sensor IR de Entrada               → detecta si hay un objeto nuevo en la cinta


// ---------- PINES DE SERVOMOTORES ----------
const int PIN_SERVO_O = 44;  // Pin PWM del servo que empuja hacia Orgánicos
const int PIN_SERVO_P = 45;  // Pin PWM del servo que empuja hacia Plásticos
const int PIN_SERVO_C = 46;  // Pin PWM del servo que empuja hacia Cartón


// ---------- POSICIONES DE LOS SERVOS (en grados) ----------
const int SERVO_INACTIVO = 90;   // 90° = posición centrada = compuerta cerrada
const int SERVO_ACTIVO   = 140;  // 140° = abre la compuerta 50° para dejar caer el objeto


// ---------- PINES DE SENSORES ULTRASÓNICOS (HC-SR04) ----------
#define TRIG_C  7   // TRIG del ultrasónico del contenedor de Papel/Cartón
#define ECHO_C  8   // ECHO del ultrasónico del contenedor de Papel/Cartón
#define TRIG_P  9   // TRIG del ultrasónico del contenedor de Plásticos
#define ECHO_P  10  // ECHO del ultrasónico del contenedor de Plásticos
#define TRIG_O  11  // TRIG del ultrasónico del contenedor de Orgánicos
#define ECHO_O  12  // ECHO del ultrasónico del contenedor de Orgánicos


// ---------- UMBRALES DE NIVEL DE LLENADO ----------
// El sensor mide la distancia DESDE ARRIBA: vacío = distancia grande, lleno = distancia pequeña
#define DIST_MAX_CM   7.0   // Distancia en cm cuando el contenedor está completamente vacío
#define UMBRAL_CASI   0.60  // Si el llenado supera el 60%, muestra "CASI LLENO"
#define UMBRAL_LLENO  0.85  // Si el llenado supera el 85%, muestra "LLENO"
#define INTERVALO_US  3000UL  // Cada cuántos milisegundos se miden los ultrasónicos (3 segundos). UL = unsigned long


// ---------- TIEMPOS DE ACTUACIÓN (en milisegundos) ----------
const unsigned long TIEMPO_SERVO_ACTIVO     = 3000;  // El servo permanece abierto 3 segundos
const unsigned long TIEMPO_BANDA_TRAS_SERVO = 2000;  // La banda sigue 2 segundos más tras cerrar el servo


// ---------- VELOCIDAD DEL MOTOR ----------
const int VELOCIDAD = 100;  // Valor de 0 a 255 para analogWrite → aquí ≈ 39% de velocidad máxima


// ---------- OBJETOS SERVO ----------
Servo servoO;  // Objeto que controla el servo del contenedor Orgánico
Servo servoP;  // Objeto que controla el servo del contenedor Plástico
Servo servoC;  // Objeto que controla el servo del contenedor Cartón


// ---------- MÁQUINA DE ESTADOS ----------
// Define los 6 estados posibles del sistema con nombres legibles
enum Estado {
  ESPERANDO_IR_E,    // Estado 0: Sistema en reposo, esperando que llegue un objeto
  ESPERANDO_CLASIF,  // Estado 1: Objeto detectado, esperando que Python envíe la clase
  BANDA_ON,          // Estado 2: Motor encendido, objeto viajando hacia su destino
  SERVO_ABIERTO,     // Estado 3: Objeto llegó al destino, servo abriendo compuerta
  SERVO_CERRANDO,    // Estado 4: Servo volviendo a posición cerrada (90°)
  PAUSA_FINAL        // Estado 5: Motor apagado, pequeña pausa antes de reiniciar
};

Estado        estado        = ESPERANDO_IR_E;  // Estado actual del sistema (arranca en reposo)
int           sensorDestino = -1;              // Pin del sensor IR al que debe llegar el objeto (-1 = ninguno)
int           servoActual   = -1;              // Índice del servo a activar (0=O, 1=P, 2=C, -1=ninguno)
unsigned long tEstado       = 0;              // Marca de tiempo del último cambio de estado
unsigned long tUltrasonico  = 0;              // Marca de tiempo de la última lectura ultrasónica


// ============================================================
//  SETUP — Se ejecuta UNA sola vez al encender el Arduino
// ============================================================
void setup() {
  Serial.begin(9600);  // Abre la comunicación serial a 9600 baudios (misma velocidad que Python)

  // Configura los pines del motor como salidas
  pinMode(pinIN1, OUTPUT);
  pinMode(pinIN2, OUTPUT);
  pinMode(pinENA, OUTPUT);

  // Configura los sensores IR de destino como entradas simples
  pinMode(IR_O, INPUT);
  pinMode(IR_P, INPUT);
  pinMode(IR_C, INPUT);
  // IR_E usa INPUT_PULLUP: activa resistencia interna → el pin lee HIGH en reposo y LOW al detectar objeto
  pinMode(IR_E, INPUT_PULLUP);

  // Configura los pines de los sensores ultrasónicos
  pinMode(TRIG_C, OUTPUT); pinMode(ECHO_C, INPUT);  // TRIG envía el pulso, ECHO lo recibe
  pinMode(TRIG_P, OUTPUT); pinMode(ECHO_P, INPUT);
  pinMode(TRIG_O, OUTPUT); pinMode(ECHO_O, INPUT);

  // Conecta cada servo a su pin y lo coloca en posición cerrada (90°)
  servoO.attach(PIN_SERVO_O);
  servoP.attach(PIN_SERVO_P);
  servoC.attach(PIN_SERVO_C);
  servoO.write(SERVO_INACTIVO);  // Centra el servo O al arrancar
  servoP.write(SERVO_INACTIVO);  // Centra el servo P al arrancar
  servoC.write(SERVO_INACTIVO);  // Centra el servo C al arrancar

  motorOFF();  // Asegura que la banda esté detenida al encender

  // Inicializa y muestra el mensaje de bienvenida en el LCD
  lcd.init();        // Inicializa la pantalla
  lcd.backlight();   // Enciende la luz de fondo
  lcd.clear();       // Borra cualquier contenido previo
  lcd.setCursor(3, 0);       // Mueve el cursor a columna 3, fila 0
  lcd.print("Reciclaje");    // Escribe "Reciclaje" en la fila superior
  lcd.setCursor(2, 1);       // Mueve el cursor a columna 2, fila 1
  lcd.print("Bienvenido!");  // Escribe "Bienvenido!" en la fila inferior

  Serial.println("[SISTEMA] Listo. Esperando IR_E...");  // Aviso por Serial para depuración
}


// ============================================================
//  LOOP — Se repite continuamente mientras el Arduino esté encendido
// ============================================================
void loop() {
  unsigned long ahora = millis();  // Guarda el tiempo actual en ms desde el arranque

  // ----- LECTURA PERIÓDICA DE ULTRASÓNICOS -----
  // Si han pasado 3 segundos desde la última lectura, mide los niveles de los contenedores
  if (ahora - tUltrasonico >= INTERVALO_US) {
    tUltrasonico = ahora;  // Reinicia el contador de tiempo para el próximo ciclo
    leerNiveles();         // Llama a la función que mide y reporta los tres contenedores
  }

  // ----- LECTURA DE CLASIFICACIÓN DESDE PYTHON -----
  if (Serial.available() > 0) {        // Si hay datos esperando en el buffer serial...
    char c = Serial.read();            // Lee el siguiente byte como carácter
    if (c == '\n' || c == '\r') return; // Ignora los terminadores de línea que Python envía al final
    c = toupper(c);                    // Convierte a mayúscula por si Python envía 'o', 'p' o 'c' en minúscula

    // Solo procesa la clasificación si el sistema está esperándola Y el carácter es válido
    if (estado == ESPERANDO_CLASIF && (c == 'O' || c == 'P' || c == 'C')) {
      asignarDestino(c);  // Asigna el sensor IR y el índice de servo según la clase recibida
      motorON();          // Enciende la banda transportadora
      estado = BANDA_ON;  // Avanza al siguiente estado
      // Actualiza el LCD con la clase recibida
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

  // ----- MÁQUINA DE ESTADOS -----
  switch (estado) {

    case ESPERANDO_IR_E:
      // Si el sensor de entrada detecta un objeto (pin baja a LOW por el pull-up)...
      if (digitalRead(IR_E) == LOW) {
        Serial.println("[IR_E] Objeto detectado.");
        // Actualiza el LCD para indicar que está procesando
        lcd.clear();
        lcd.setCursor(3, 0);
        lcd.print("Reciclaje");
        lcd.setCursor(2, 1);
        lcd.print("En proceso...");
        Serial.println("CLASIFICAR");  // Avisa a Python que debe tomar la foto y clasificar
        estado = ESPERANDO_CLASIF;     // Pasa al estado de espera de clasificación
      }
      break;

    case ESPERANDO_CLASIF:
      // No hace nada aquí: la acción ocurre arriba en la lectura Serial
      break;

    case BANDA_ON:
      // Mientras la banda está en movimiento, revisa si el objeto llegó al destino
      if (sensorDestino != -1 && digitalRead(sensorDestino) == LOW) {
        // El sensor IR de destino detectó el objeto (LOW = hay algo frente al sensor)
        Serial.println("[IR-DEST] Objeto en destino. Abriendo servo...");
        abrirServo();       // Abre la compuerta del contenedor correspondiente
        tEstado = ahora;    // Guarda el momento en que se abrió el servo
        estado  = SERVO_ABIERTO;  // Pasa al estado de servo abierto
      }
      break;

    case SERVO_ABIERTO:
      // Espera 3 segundos con el servo abierto para que el objeto caiga al contenedor
      if (ahora - tEstado >= TIEMPO_SERVO_ACTIVO) {
        cerrarServo();           // Cierra la compuerta (vuelve el servo a 90°)
        tEstado = ahora;         // Guarda el momento en que se cerró
        estado  = SERVO_CERRANDO;
        Serial.println("[SERVO] Cerrado. Banda para en 2 s...");
      }
      break;

    case SERVO_CERRANDO:
      // Espera 2 segundos más para que la banda arrastre cualquier resto
      if (ahora - tEstado >= TIEMPO_BANDA_TRAS_SERVO) {
        motorOFF();           // Apaga la banda
        tEstado = ahora;      // Guarda el momento en que se apagó
        estado  = PAUSA_FINAL;
        Serial.println("[MOTOR] Banda detenida.");
      }
      break;

    case PAUSA_FINAL:
      // Espera 500 ms antes de reiniciar, para estabilizar el sistema
      if (ahora - tEstado >= 500) {
        sensorDestino = -1;          // Resetea el sensor de destino
        servoActual   = -1;          // Resetea el índice de servo
        estado        = ESPERANDO_IR_E;  // Vuelve al estado inicial
        // Muestra el mensaje de bienvenida nuevamente
        lcd.clear();
        lcd.setCursor(3, 0);
        lcd.print("Reciclaje");
        lcd.setCursor(2, 1);
        lcd.print("Bienvenido!");
        Serial.println("CLASIFICAR");  // Avisa a Python que puede empezar a clasificar de nuevo
        Serial.println("[SISTEMA] Ciclo completo. Esperando IR_E...");
      }
      break;
  }
}


// ============================================================
//  FUNCIONES AUXILIARES
// ============================================================

// Asigna el pin del sensor IR de destino y el índice de servo según la clase recibida
void asignarDestino(char c) {
  if      (c == 'O') { sensorDestino = IR_O; servoActual = 0; }  // Orgánico → sensor IR_O, servo índice 0
  else if (c == 'P') { sensorDestino = IR_P; servoActual = 1; }  // Plástico → sensor IR_P, servo índice 1
  else if (c == 'C') { sensorDestino = IR_C; servoActual = 2; }  // Cartón   → sensor IR_C, servo índice 2
}

// Abre el servo correspondiente al índice asignado (lo mueve a 140°)
void abrirServo() {
  if      (servoActual == 0) { servoO.write(SERVO_ACTIVO); Serial.println("[SERVO O] ABIERTO"); }
  else if (servoActual == 1) { servoP.write(SERVO_ACTIVO); Serial.println("[SERVO P] ABIERTO"); }
  else if (servoActual == 2) { servoC.write(SERVO_ACTIVO); Serial.println("[SERVO C] ABIERTO"); }
}

// Cierra el servo correspondiente (lo devuelve a 90°)
void cerrarServo() {
  if      (servoActual == 0) { servoO.write(SERVO_INACTIVO); Serial.println("[SERVO O] CERRADO"); }
  else if (servoActual == 1) { servoP.write(SERVO_INACTIVO); Serial.println("[SERVO P] CERRADO"); }
  else if (servoActual == 2) { servoC.write(SERVO_INACTIVO); Serial.println("[SERVO C] CERRADO"); }
}

// Enciende el motor de la banda en dirección hacia adelante a la velocidad definida
void motorON() {
  digitalWrite(pinIN1, HIGH);        // IN1=HIGH, IN2=LOW → el motor gira hacia adelante
  digitalWrite(pinIN2, LOW);
  analogWrite(pinENA, VELOCIDAD);    // Aplica 100/255 ≈ 39% del voltaje máximo al motor
  Serial.println("[MOTOR] ENCENDIDO");
}

// Apaga el motor completamente (frena la banda)
void motorOFF() {
  digitalWrite(pinIN1, LOW);   // IN1=LOW, IN2=LOW → sin corriente al motor
  digitalWrite(pinIN2, LOW);
  analogWrite(pinENA, 0);      // PWM a 0 = sin voltaje al motor
  Serial.println("[MOTOR] APAGADO");
}


// ============================================================
//  FUNCIONES DE SENSORES ULTRASÓNICOS
// ============================================================

// Mide la distancia en cm usando un sensor HC-SR04 dado su pin TRIG y ECHO
long medirDistancia(int trig, int echo) {
  digitalWrite(trig, LOW);          // Asegura que TRIG empieza en LOW
  delayMicroseconds(2);             // Espera 2 µs para estabilizar
  digitalWrite(trig, HIGH);         // Envía un pulso ultrasónico de 10 µs
  delayMicroseconds(10);            // Mantiene el pulso 10 µs (requerido por el HC-SR04)
  digitalWrite(trig, LOW);          // Termina el pulso
  // Mide cuántos microsegundos tarda en volver el eco (timeout de 30 ms = ~5 m máx)
  long duracion = pulseIn(echo, HIGH, 30000UL);
  // Convierte tiempo a distancia: distancia = (tiempo × velocidad_sonido) / 2
  // velocidad_sonido ≈ 0.034 cm/µs; se divide entre 2 porque el sonido hace ida y vuelta
  return duracion * 0.034 / 2;
}

// Convierte una distancia en cm al texto del nivel de llenado del contenedor
const char* nivelTexto(long distCm) {
  if (distCm <= 0 || distCm > 400) return "ERROR";  // Descarta lecturas imposibles o timeout
  // Calcula el porcentaje de llenado: vacío=0.0, lleno=1.0
  // Se invierte porque a mayor llenado, menor distancia medida
  float pct = 1.0 - ((float)distCm / DIST_MAX_CM);
  if (pct < 0) pct = 0;  // Clamping: si la distancia supera DIST_MAX_CM, fija en 0% (vacío)
  if (pct > 1) pct = 1;  // Clamping: si la distancia es casi 0, fija en 100% (lleno)
  if (pct >= UMBRAL_LLENO) return "LLENO";       // ≥85% de llenado
  if (pct >= UMBRAL_CASI)  return "CASI LLENO";  // ≥60% de llenado
  return "DISPONIBLE";                           // <60% de llenado → hay espacio
}

// Lee los tres sensores ultrasónicos y reporta el nivel de cada contenedor por Serial
void leerNiveles() {
  long dC = medirDistancia(TRIG_C, ECHO_C);  // Distancia del contenedor de Cartón
  long dP = medirDistancia(TRIG_P, ECHO_P);  // Distancia del contenedor de Plástico
  long dO = medirDistancia(TRIG_O, ECHO_O);  // Distancia del contenedor de Orgánico

  // Imprime los tres niveles en una sola línea separados por " | "
  Serial.print("[US] CARTON:");  Serial.print(nivelTexto(dC));
  Serial.print(" | PLASTICO:"); Serial.print(nivelTexto(dP));
  Serial.print(" | ORGANICO:"); Serial.println(nivelTexto(dO));
}
