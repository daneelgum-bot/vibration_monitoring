import asyncio
import websockets
import numpy as np
import matplotlib.pyplot as plt
import time

# ========== НАСТРОЙКИ ==========
HOST = "0.0.0.0"
PORT = 8765
SAMPLES = 1600                    # отсчётов за пакет (должно совпадать с BUF_SIZE)
FS = 3200                         # частота дискретизации, Гц
G_TO_MMPS2 = 9806.65              # перевод из g в мм/с²

# ----- Фильтрация -----
USE_FILTERS = True                
LOW_CUT = 10.0                     # частота среза ФВЧ
HIGH_CUT = 1100.0                  # частота среза ФНЧ
REMOVE_DC = True                   # вычитать среднее (если фильтры отключены или как доп. опция)

# ----- Отображение -----
FREQ_MIN = 10
FREQ_MAX = 1000
AUTO_SCALE_Y = False                # автоматически подбирать пределы Y
Y_MAX_FIXED = 1                  # фиксированный верхний предел, если AUTO_SCALE_Y=False
# ================================

# Попытка импорта scipy для фильтрации
SCIPY_AVAILABLE = False
if USE_FILTERS and (LOW_CUT > 0 or HIGH_CUT > 0):
    try:
        from scipy.signal import butter, filtfilt
        SCIPY_AVAILABLE = True
    except ImportError:
        print("⚠️ SciPy не установлен. Фильтры Баттерворта отключены.")
        USE_FILTERS = False  # отключаем фильтры, оставляем только REMOVE_DC

# Окно Ханна для спектрального анализа
window = np.hanning(SAMPLES)

# Инициализация графика (ЛИНЕЙНЫЙ МАСШТАБ)
plt.ion()
fig, ax = plt.subplots(figsize=(10, 6))
line, = ax.plot([], [], lw=1)
ax.set_xlim(FREQ_MIN, FREQ_MAX)
ax.set_ylim(0, Y_MAX_FIXED)        # начальные пределы (позже могут быть изменены)
ax.set_xlabel('Частота, Гц')
ax.set_ylabel('Амплитуда скорости, мм/с')
ax.set_title('Спектр виброскорости')
ax.grid(True)

# Функции фильтрации
def butter_highpass(cutoff, fs, order=4):
    nyq = 0.5 * fs
    normal_cutoff = cutoff / nyq
    b, a = butter(order, normal_cutoff, btype='high', analog=False)
    return b, a

def butter_lowpass(cutoff, fs, order=4):
    nyq = 0.5 * fs
    normal_cutoff = cutoff / nyq
    b, a = butter(order, normal_cutoff, btype='low', analog=False)
    return b, a

def apply_filters(data, fs):
    """Применяет фильтры согласно настройкам LOW_CUT и HIGH_CUT (если SCIPY_AVAILABLE)"""
    if not SCIPY_AVAILABLE:
        # только удаление DC (если включено)
        if REMOVE_DC:
            return data - np.mean(data)
        return data

    filtered = data.copy()
    # ФВЧ
    if LOW_CUT > 0:
        b_hp, a_hp = butter_highpass(LOW_CUT, fs, order=4)
        filtered = filtfilt(b_hp, a_hp, filtered)
    # ФНЧ
    if HIGH_CUT > 0:
        b_lp, a_lp = butter_lowpass(HIGH_CUT, fs, order=4)
        filtered = filtfilt(b_lp, a_lp, filtered)
    # Дополнительное удаление DC, если указано (может быть избыточно после ФВЧ, но оставим для гибкости)
    if REMOVE_DC:
        filtered = filtered - np.mean(filtered)
    return filtered

async def handle_connection(websocket):
    print("✅ Клиент подключился")
    packet_count = 0
    try:
        async for message in websocket:
            if not isinstance(message, bytes):
                continue
            packet_count += 1
            # Интерпретируем как массив float32 (little-endian)
            accel_g = np.frombuffer(message, dtype='<f4')
            if len(accel_g) != SAMPLES:
                print(f"⚠️ Неожиданная длина: {len(accel_g)} (ожидалось {SAMPLES})")
                continue

            # Перевод в мм/с²
            accel = accel_g * G_TO_MMPS2

            # Применение фильтров
            accel_filtered = apply_filters(accel, FS)

            # Интегрирование в частотной области для получения скорости
            fft_accel = np.fft.rfft(accel_filtered)
            freqs = np.fft.rfftfreq(SAMPLES, d=1/FS)

            # Скорость: V = A / (j * 2πf)
            fft_vel = np.zeros_like(fft_accel, dtype=complex)
            fft_vel[0] = 0.0 + 0.0j
            fft_vel[1:] = fft_accel[1:] / (2j * np.pi * freqs[1:])

            # Обратное БПФ для сигнала скорости (RMS)
            velocity = np.fft.irfft(fft_vel, n=SAMPLES)
            rms_vel = np.sqrt(np.mean(velocity**2))

            # Спектр скорости с оконным преобразованием
            accel_windowed = accel_filtered * window
            fft_accel_win = np.fft.rfft(accel_windowed)
            fft_vel_win = np.zeros_like(fft_accel_win, dtype=complex)
            fft_vel_win[0] = 0.0 + 0.0j
            fft_vel_win[1:] = fft_accel_win[1:] / (2j * np.pi * freqs[1:])
            spectrum_vel = np.abs(fft_vel_win) * 2 / SAMPLES

            # Обновление графика (только выбранный диапазон)
            mask = (freqs >= FREQ_MIN) & (freqs <= FREQ_MAX)
            line.set_data(freqs[mask], spectrum_vel[mask])

            # Управление масштабом Y
            if AUTO_SCALE_Y and len(spectrum_vel[mask]) > 0:
                ymax = np.max(spectrum_vel[mask]) * 1.2
                ax.set_ylim(0, ymax if ymax > 0 else Y_MAX_FIXED)
            else:
                ax.set_ylim(0, Y_MAX_FIXED)

            ax.relim()
            ax.autoscale_view(scaley=False)
            ax.set_title(f'Спектр скорости (пакет {packet_count}, RMS = {rms_vel:.2f} мм/с)')

            plt.draw()
            plt.pause(0.001)

            print(f"[{time.strftime('%H:%M:%S')}] Пакет {packet_count}, RMS={rms_vel:.2f} мм/с")

    except websockets.exceptions.ConnectionClosed:
        print("🔌 Клиент отключился")
    except Exception as e:
        print(f"🔥 Ошибка: {e}")

async def main():
    async with websockets.serve(handle_connection, HOST, PORT,
                                max_size=2**20,
                                ping_interval=20,
                                ping_timeout=10):
        print(f"🚀 WebSocket сервер запущен на {HOST}:{PORT}")
        await asyncio.Future()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n🛑 Остановлено пользователем")
        plt.ioff()
        plt.close('all')