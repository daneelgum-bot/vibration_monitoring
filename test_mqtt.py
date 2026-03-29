import paho.mqtt.client as mqtt
import numpy as np
import matplotlib.pyplot as plt
import time

# ========== НАСТРОЙКИ ==========
BROKER = "192.168.1.50"
PORT = 1883
TOPIC = "/vibration/data"
QOS = 0

SAMPLES = 3200                 # отсчётов за пакет
FS = 3200                      # частота дискретизации

G_TO_MMPS2 = 9806.65

# Режимы фильтрации:
REMOVE_DC = False               # вычитать среднее (не используется, если включены фильтры)
USE_BUTTERWORTH_HP = True       # фильтр верхних частот 10 Гц
USE_BUTTERWORTH_LP = True       # фильтр нижних частот 1000 Гц

# Диапазон отображения спектра (Гц)
FREQ_MIN = 10
FREQ_MAX = 1000
# ================================

# Импортируем scipy, если нужны фильтры
if USE_BUTTERWORTH_HP or USE_BUTTERWORTH_LP:
    try:
        from scipy.signal import butter, filtfilt
    except ImportError:
        print("Для фильтров Баттерворта требуется scipy. Установите: pip install scipy")
        exit(1)

# Окно Ханна для спектрального анализа
window = np.hanning(SAMPLES)

# Инициализация графика
plt.ion()
fig, ax = plt.subplots(figsize=(10, 6))
line, = ax.semilogy([], [], lw=1)
ax.set_xlim(FREQ_MIN, FREQ_MAX)
ax.set_ylim(1e-4, 1e2)          # более разумные пределы (можно подстроить)
ax.set_xlabel('Частота, Гц')
ax.set_ylabel('Амплитуда скорости, мм/с')
ax.set_title('Спектр виброскорости')
ax.grid(True)

def butter_highpass(cutoff, fs, order=4):
    nyq = 0.5 * fs
    normal_cutoff = cutoff / nyq
    b, a = butter(order, normal_cutoff, btype='high', analog=False)
    return b, a

def highpass_filter(data, cutoff, fs, order=4):
    b, a = butter_highpass(cutoff, fs, order=order)
    y = filtfilt(b, a, data)
    return y

def butter_lowpass(cutoff, fs, order=4):
    nyq = 0.5 * fs
    normal_cutoff = cutoff / nyq
    b, a = butter(order, normal_cutoff, btype='low', analog=False)
    return b, a

def lowpass_filter(data, cutoff, fs, order=4):
    b, a = butter_lowpass(cutoff, fs, order=order)
    y = filtfilt(b, a, data)
    return y

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"Подключено к {BROKER}:{PORT}")
        client.subscribe(TOPIC, qos=QOS)
        print(f"Подписка на топик '{TOPIC}'")
    else:
        print(f"Ошибка подключения, код {rc}")

def on_message(client, userdata, msg):
    payload = msg.payload

    try:
        # Читаем массив float32 – модуль вектора в g
        magnitude_g = np.frombuffer(payload, dtype='<f4')
        accel = magnitude_g * G_TO_MMPS2

        # === ПРИМЕНЕНИЕ ФИЛЬТРАЦИИ К УСКОРЕНИЮ ===
        accel_filtered = accel.copy()
        
        if REMOVE_DC:
            accel_filtered = accel_filtered - np.mean(accel_filtered)
        
        if USE_BUTTERWORTH_HP:
            accel_filtered = highpass_filter(accel_filtered, cutoff=10.0, fs=FS, order=4)
        
        if USE_BUTTERWORTH_LP:
            accel_filtered = lowpass_filter(accel_filtered, cutoff=1000.0, fs=FS, order=4)

        # --- РАСЧЁТ СКОРОСТИ И ЕЁ СПЕКТРА ---
        fft_accel = np.fft.rfft(accel_filtered)
        freqs = np.fft.rfftfreq(SAMPLES, d=1/FS)

        # Интегрирование в частотной области: V = A / (j * 2πf)
        fft_vel = np.zeros_like(fft_accel, dtype=complex)
        fft_vel[0] = 0.0 + 0.0j               # обнуляем постоянную составляющую
        # Для положительных частот (f > 0)
        fft_vel[1:] = fft_accel[1:] / (2j * np.pi * freqs[1:])

        # Обратное БПФ для получения сигнала скорости во временной области
        velocity = np.fft.irfft(fft_vel, n=SAMPLES)
        rms_vel = np.sqrt(np.mean(velocity**2))

        # Для спектра скорости используем оконное преобразование, чтобы уменьшить утечки
        accel_windowed = accel_filtered * window
        fft_accel_win = np.fft.rfft(accel_windowed)
        fft_vel_win = np.zeros_like(fft_accel_win, dtype=complex)
        fft_vel_win[0] = 0.0 + 0.0j
        fft_vel_win[1:] = fft_accel_win[1:] / (2j * np.pi * freqs[1:])
        spectrum_vel = np.abs(fft_vel_win) * 2 / SAMPLES   # амплитудный спектр скорости

        # Отображаем только выбранный диапазон частот
        mask = (freqs >= FREQ_MIN) & (freqs <= FREQ_MAX)
        line.set_data(freqs[mask], spectrum_vel[mask])
        
        # Автоматически подбираем пределы Y, чтобы видеть спектр
        if np.any(spectrum_vel[mask] > 0):
            ymin = max(1e-9, spectrum_vel[mask][spectrum_vel[mask] > 0].min() * 0.5)
            ymax = spectrum_vel[mask].max() * 2
            ax.set_ylim(ymin, ymax)
        else:
            ax.set_ylim(1e-4, 1e2)  # запасной вариант

        ax.set_title(f'Спектр скорости {FREQ_MIN}-{FREQ_MAX} Гц (RMS = {rms_vel:.2f} мм/с)')
        plt.draw()
        plt.pause(0.001)

        print(f"[{time.strftime('%H:%M:%S')}] Спектр скорости обновлён, RMS={rms_vel:.2f} мм/с")

    except Exception as e:
        print(f"Ошибка обработки: {e}")

def main():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(BROKER, PORT, 60)
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nОстановлено пользователем")
        plt.ioff()
        plt.close('all')
        client.disconnect()
    except Exception as e:
        print(f"Критическая ошибка: {e}")

if __name__ == "__main__":
    main()