from ultralytics import YOLO
import cv2
import numpy as np
import urllib.request
import serial
import time

# ============================================================
# CONFIGURACIÓN
# ============================================================

model = YOLO("best.pt")

ESP32_URL      = "http://172.20.10.2/capture"
PUERTO_ARDUINO = "COM7"
BAUDRATE       = 9600

CLASES = {
    "organicos":      "O",
    "plasticos":      "P",
    "papel o carton": "C"
}

# ============================================================
# CONEXIÓN SERIAL
# ============================================================

try:
    arduino = serial.Serial(PUERTO_ARDUINO, BAUDRATE, timeout=1)
    time.sleep(2)
    print(f"[OK] Arduino conectado en {PUERTO_ARDUINO}")
except Exception as e:
    print(f"[ERROR] No se pudo conectar al Arduino: {e}")
    exit()

# ============================================================
# VARIABLES
# ============================================================

# True  = Arduino mandó "CLASIFICAR" y espera letra
# False = Arduino procesando, no enviar nada
esperando_clasificacion = False

ultima_clase  = ""   # Para mostrar en pantalla
ultima_letra  = ""

# ============================================================
# LOOP PRINCIPAL
# ============================================================

print("[PYTHON] Iniciado. Presiona Q para salir.")

while True:
    try:

        # ====================================================
        # LEER MENSAJES DEL ARDUINO
        # ====================================================

        while arduino.in_waiting > 0:
            msg = arduino.readline().decode("utf-8", errors="ignore").strip()
            if msg:
                print(f"[ARDUINO] {msg}")

                # Arduino pide clasificación → Python debe enviar letra
                if "CLASIFICAR" in msg:
                    esperando_clasificacion = True
                    ultima_letra = ""
                    print("[PYTHON] Arduino listo. Clasificando...")

        # ====================================================
        # SI ARDUINO NO PIDE CLASIFICACIÓN, SOLO MOSTRAR VIDEO
        # ====================================================

        img_resp = urllib.request.urlopen(ESP32_URL, timeout=5)
        img_np   = np.array(bytearray(img_resp.read()), dtype=np.uint8)
        frame    = cv2.imdecode(img_np, -1)

        if frame is None:
            continue

        # ====================================================
        # INFERENCIA YOLO
        # ====================================================

        results = model.predict(
            source=frame,
            imgsz=640,
            conf=0.70,
            verbose=False
        )

        detecciones     = results[0].boxes
        letra           = None
        clase_detectada = ""
        confianza       = 0

        if len(detecciones) > 0:
            confianzas  = detecciones.conf.tolist()
            clases      = detecciones.cls.tolist()
            nombres     = results[0].names

            mejor_idx       = confianzas.index(max(confianzas))
            clase_detectada = nombres[int(clases[mejor_idx])]
            confianza       = confianzas[mejor_idx]

            print(f"[YOLO] {clase_detectada} ({confianza:.0%})")

            for clave, valor in CLASES.items():
                if clave.lower() in clase_detectada.lower():
                    letra = valor
                    break

            # ================================================
            # ENVIAR SOLO CUANDO ARDUINO LO PIDIÓ ("CLASIFICAR")
            # Y AÚN NO SE HA ENVIADO EN ESTE CICLO
            # ================================================

            if letra and esperando_clasificacion and ultima_letra == "":
                arduino.write(letra.encode())
                ultima_letra  = letra
                ultima_clase  = clase_detectada
                esperando_clasificacion = False   # Bloquear hasta próximo "CLASIFICAR"
                print(f"[PYTHON] Enviando '{letra}' ({clase_detectada})")

        # ====================================================
        # MOSTRAR VIDEO
        # ====================================================

        annotated_frame = results[0].plot()

        # Detección
        if clase_detectada:
            cv2.putText(annotated_frame, f"{clase_detectada} {confianza:.0%}",
                        (10, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)
        else:
            cv2.putText(annotated_frame, "Sin deteccion",
                        (10, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 0, 255), 2)

        # Estado
        if esperando_clasificacion:
            estado_txt = "ESPERANDO OBJETO FRENTE A CAMARA"
            color = (0, 165, 255)
        elif ultima_letra:
            estado_txt = f"PROCESANDO: {ultima_clase}"
            color = (255, 255, 0)
        else:
            estado_txt = "EN ESPERA"
            color = (200, 200, 200)

        cv2.putText(annotated_frame, estado_txt,
                    (10, 80), cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)

        cv2.imshow("ESP32-CAM - YOLO CAD", annotated_frame)

        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    except KeyboardInterrupt:
        break
    except Exception as e:
        print(f"[ERROR] {e}")
        time.sleep(1)

# ============================================================
arduino.close()
cv2.destroyAllWindows()