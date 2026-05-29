# ============================================================
#  CLASIFICADOR DE RESIDUOS — PYTHON
#  Captura imágenes de la ESP32-CAM, las clasifica con YOLO,
#  y envía la letra correspondiente al Arduino por Serial.
# ============================================================


# ---------- LIBRERÍAS ----------
from ultralytics import YOLO   # Framework para cargar y ejecutar el modelo YOLO de detección de objetos
import cv2                     # OpenCV: captura, decodifica y muestra imágenes/video
import numpy as np             # NumPy: convierte los bytes de la imagen en un arreglo numérico que OpenCV entiende
import urllib.request          # Permite hacer peticiones HTTP para obtener la foto de la ESP32-CAM
import serial                  # pySerial: abre el puerto COM y envía/recibe datos al Arduino
import time                    # Permite usar time.sleep() para pausas


# ============================================================
#  CONFIGURACIÓN
# ============================================================

model = YOLO("best.pt")   # Carga el modelo entrenado (best.pt es el archivo de pesos de tu red neuronal)

ESP32_URL      = "http://172.20.10.2/capture"  # Dirección IP de la ESP32-CAM; /capture toma una foto al consultarla
PUERTO_ARDUINO = "COM7"                         # Puerto serial donde está conectado el Arduino (Windows: COM#, Linux: /dev/ttyUSB#)
BAUDRATE       = 9600                           # Velocidad de comunicación serial — debe coincidir exactamente con el Arduino

# Diccionario que mapea el nombre de clase que devuelve YOLO → letra que entiende el Arduino
# Clave  = nombre de la clase tal como la entrenaste en el modelo
# Valor  = letra que se envía por Serial al Arduino (O, P o C)
CLASES = {
    "organicos":      "O",
    "plasticos":      "P",
    "papel o carton": "C"
}


# ============================================================
#  CONEXIÓN SERIAL CON EL ARDUINO
# ============================================================

try:
    # Intenta abrir el puerto serial con los parámetros dados
    # timeout=1 → si en 1 segundo no llega nada, readline() devuelve lo que tenga (no se queda esperando para siempre)
    arduino = serial.Serial(PUERTO_ARDUINO, BAUDRATE, timeout=1)
    time.sleep(2)   # Espera 2 segundos: el Arduino se reinicia automáticamente al abrirse el puerto serial y necesita este tiempo para arrancar
    print(f"[OK] Arduino conectado en {PUERTO_ARDUINO}")
except Exception as e:
    # Si falla la conexión (puerto incorrecto, Arduino desconectado, etc.) muestra el error y cierra Python
    print(f"[ERROR] No se pudo conectar al Arduino: {e}")
    exit()   # Termina el programa; sin Arduino no tiene sentido continuar


# ============================================================
#  VARIABLES DE ESTADO
# ============================================================

# Bandera booleana: True cuando el Arduino envió "CLASIFICAR" y está esperando que Python le mande una letra
# False mientras el Arduino está ocupado moviendo la banda/servo
esperando_clasificacion = False

ultima_clase  = ""   # Guarda el nombre de la última clase detectada (para mostrarlo en pantalla)
ultima_letra  = ""   # Guarda la última letra enviada al Arduino en este ciclo (para no enviarla dos veces)


# ============================================================
#  LOOP PRINCIPAL — se repite indefinidamente hasta presionar Q
# ============================================================

print("[PYTHON] Iniciado. Presiona Q para salir.")

while True:
    try:

        # ====================================================
        #  PASO 1: LEER MENSAJES QUE LLEGARON DEL ARDUINO
        # ====================================================

        # arduino.in_waiting devuelve cuántos bytes hay en el buffer de entrada sin leer todavía
        # El while lee TODOS los mensajes acumulados antes de continuar (puede haber más de uno)
        while arduino.in_waiting > 0:
            # readline() lee hasta encontrar '\n' y lo decodifica como texto UTF-8
            # errors="ignore" descarta bytes que no sean UTF-8 válido (evita crashes por ruido en el cable)
            # .strip() elimina espacios, '\n' y '\r' del inicio y el final
            msg = arduino.readline().decode("utf-8", errors="ignore").strip()
            if msg:   # Solo procesa si el mensaje no quedó vacío después del strip
                print(f"[ARDUINO] {msg}")

                # Si el mensaje contiene la palabra "CLASIFICAR", el Arduino está listo para recibir una letra
                if "CLASIFICAR" in msg:
                    esperando_clasificacion = True   # Activa la bandera para que el próximo frame con detección envíe la letra
                    ultima_letra = ""                # Resetea la letra anterior para permitir un nuevo envío
                    print("[PYTHON] Arduino listo. Clasificando...")


        # ====================================================
        #  PASO 2: CAPTURAR IMAGEN DESDE LA ESP32-CAM
        # ====================================================

        # Hace una petición HTTP GET a la ESP32-CAM; timeout=5 evita que se quede colgado si la cámara no responde
        img_resp = urllib.request.urlopen(ESP32_URL, timeout=5)
        # Lee todos los bytes de la respuesta HTTP (el JPEG de la foto) y los convierte en un arreglo NumPy de bytes
        img_np   = np.array(bytearray(img_resp.read()), dtype=np.uint8)
        # cv2.imdecode decodifica el arreglo de bytes JPEG y lo convierte en una imagen BGR (arreglo 3D de píxeles)
        # -1 significa "usa el modo de color original de la imagen" (en este caso BGR de 3 canales)
        frame    = cv2.imdecode(img_np, -1)

        if frame is None:
            continue   # Si la imagen llegó corrupta o vacía, salta esta iteración y vuelve al inicio del while


        # ====================================================
        #  PASO 3: INFERENCIA CON YOLO
        # ====================================================

        results = model.predict(
            source=frame,    # La imagen capturada de la ESP32
            imgsz=640,       # Redimensiona internamente la imagen a 640×640 antes de pasarla por la red neuronal
            conf=0.70,       # Umbral de confianza: solo acepta detecciones con ≥70% de certeza; descarta el resto
            verbose=False    # Silencia los prints internos de YOLO para no llenar la consola
        )

        detecciones     = results[0].boxes   # Lista de todas las cajas detectadas en este frame
        letra           = None               # Letra a enviar al Arduino (se calcula más abajo)
        clase_detectada = ""                 # Nombre de la clase con mayor confianza
        confianza       = 0                  # Valor de confianza de la mejor detección

        if len(detecciones) > 0:   # Solo procesa si YOLO encontró al menos un objeto
            confianzas  = detecciones.conf.tolist()   # Lista de confianzas de cada caja detectada (valores 0.0–1.0)
            clases      = detecciones.cls.tolist()    # Lista de índices de clase de cada caja detectada
            nombres     = results[0].names            # Diccionario {índice: nombre_de_clase} del modelo

            # Encuentra el índice de la detección con la mayor confianza
            mejor_idx       = confianzas.index(max(confianzas))
            # Obtiene el nombre de esa clase usando su índice
            clase_detectada = nombres[int(clases[mejor_idx])]
            # Guarda su confianza
            confianza       = confianzas[mejor_idx]

            print(f"[YOLO] {clase_detectada} ({confianza:.0%})")   # Ej: [YOLO] plasticos (87%)

            # Busca en el diccionario CLASES si el nombre detectado contiene alguna de las claves
            # .lower() hace la comparación insensible a mayúsculas/minúsculas
            for clave, valor in CLASES.items():
                if clave.lower() in clase_detectada.lower():
                    letra = valor   # Asigna la letra correspondiente ("O", "P" o "C")
                    break           # Sale del for en cuanto encuentra la primera coincidencia


            # ================================================
            #  PASO 4: ENVIAR LETRA AL ARDUINO (solo si fue pedida y no se envió ya)
            # ================================================

            if letra and esperando_clasificacion and ultima_letra == "":
                # letra         → YOLO identificó una clase válida del diccionario
                # esperando_clasificacion → el Arduino mandó "CLASIFICAR" y está esperando
                # ultima_letra == "" → aún no se envió ninguna letra en este ciclo (evita duplicados)
                arduino.write(letra.encode())           # Convierte la letra a bytes y la envía por Serial
                ultima_letra  = letra                   # Registra que ya se envió esta letra
                ultima_clase  = clase_detectada         # Guarda el nombre para mostrarlo en pantalla
                esperando_clasificacion = False         # Bloquea más envíos hasta que el Arduino pida otro "CLASIFICAR"
                print(f"[PYTHON] Enviando '{letra}' ({clase_detectada})")


        # ====================================================
        #  PASO 5: MOSTRAR VIDEO EN PANTALLA
        # ====================================================

        # Dibuja las cajas de detección de YOLO sobre el frame (con colores y etiquetas automáticas)
        annotated_frame = results[0].plot()

        # ---- Texto superior: clase detectada y su confianza ----
        if clase_detectada:
            # Escribe el nombre de la clase y el porcentaje de confianza en verde
            cv2.putText(annotated_frame, f"{clase_detectada} {confianza:.0%}",
                        (10, 40),                    # Posición (x=10, y=40) en píxeles desde la esquina superior izquierda
                        cv2.FONT_HERSHEY_SIMPLEX,    # Tipo de fuente
                        0.9,                         # Escala del texto
                        (0, 255, 0),                 # Color en BGR: verde
                        2)                           # Grosor del trazo en píxeles
        else:
            # Si YOLO no detectó nada, muestra "Sin deteccion" en rojo
            cv2.putText(annotated_frame, "Sin deteccion",
                        (10, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 0, 255), 2)

        # ---- Texto inferior: estado actual del sistema ----
        if esperando_clasificacion:
            estado_txt = "ESPERANDO OBJETO FRENTE A CAMARA"
            color = (0, 165, 255)    # Naranja BGR: Arduino listo pero aún sin objeto con suficiente confianza
        elif ultima_letra:
            estado_txt = f"PROCESANDO: {ultima_clase}"
            color = (255, 255, 0)    # Amarillo BGR: letra enviada, Arduino moviendo la banda
        else:
            estado_txt = "EN ESPERA"
            color = (200, 200, 200)  # Gris BGR: sistema inactivo, esperando que el Arduino pida clasificación

        # Escribe el texto de estado debajo del texto de detección
        cv2.putText(annotated_frame, estado_txt,
                    (10, 80), cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)

        # Muestra la imagen anotada en una ventana con el título dado
        cv2.imshow("ESP32-CAM - YOLO CAD", annotated_frame)

        # waitKey(1) espera 1 ms y devuelve la tecla presionada
        # & 0xFF es una máscara de bits para obtener solo el byte menos significativo (compatibilidad con algunos sistemas)
        # ord('q') es el código ASCII de 'q' (113); si coincide, sale del while
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    except KeyboardInterrupt:
        break   # Si el usuario presiona Ctrl+C en la terminal, sale limpiamente del loop

    except Exception as e:
        # Captura cualquier otro error (timeout de cámara, pérdida de conexión, etc.) sin cerrar el programa
        print(f"[ERROR] {e}")
        time.sleep(1)   # Espera 1 segundo antes de reintentar para no saturar la consola de errores


# ============================================================
#  CIERRE LIMPIO AL SALIR
# ============================================================
arduino.close()          # Cierra el puerto serial correctamente (libera el recurso del sistema operativo)
cv2.destroyAllWindows()  # Cierra todas las ventanas de OpenCV que estaban abiertas