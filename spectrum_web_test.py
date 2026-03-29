import asyncio
import struct
import numpy as np
import matplotlib.pyplot as plt
from matplotlib import gridspec
import websockets

# Параметры
BUF_SIZE = 1024
SAMPLING_RATE = 3200.0
PACKET_SIZE = 4 + 4 + BUF_SIZE*4 + (BUF_SIZE//2)*4
PACKET_FORMAT = f'ff{BUF_SIZE}f{BUF_SIZE//2}f'
PACKET_STRUCT = struct.Struct(PACKET_FORMAT)

time_data = np.zeros(BUF_SIZE)
spectrum_data = np.zeros(BUF_SIZE // 2)
rms_value = 0.0

# Настройка графиков
plt.style.use('dark_background')
fig = plt.figure(figsize=(12, 8))
gs = gridspec.GridSpec(2, 1, height_ratios=[2, 1])

ax_time = plt.subplot(gs[0])
ax_spectrum = plt.subplot(gs[1])

line_time, = ax_time.plot([], [], 'cyan', lw=0.5)
line_spectrum, = ax_spectrum.plot([], [], 'lime', lw=1)

ax_time.set_title('Acceleration (mm/s²)')
ax_time.set_xlabel('Sample')
ax_time.set_ylabel('mm/s²')

ax_spectrum.set_title('Velocity Spectrum (mm/s)')
ax_spectrum.set_xlabel('Frequency (Hz)')
ax_spectrum.set_ylabel('Amplitude (mm/s)')
ax_spectrum.set_xscale('linear')
ax_spectrum.set_xlim(0, 1000)
ax_spectrum.grid(True, which='both', linestyle='--', alpha=0.5)

rms_text = ax_time.text(0.02, 0.95, '', transform=ax_time.transAxes,
                        color='white', fontsize=12)

freq_axis = np.linspace(0, SAMPLING_RATE/2, BUF_SIZE//2)

def update_plot():
    line_time.set_data(np.arange(BUF_SIZE), time_data)
    ax_time.relim()
    ax_time.autoscale_view(scalex=False, scaley=True)

    line_spectrum.set_data(freq_axis[1:], spectrum_data[1:])
    ax_spectrum.relim()
    ax_spectrum.autoscale_view(scalex=False, scaley=True)

    rms_text.set_text(f'RMS Velocity: {rms_value:.3f} mm/s')
    plt.draw()
    plt.pause(0.001)

async def websocket_handler(websocket):
    """Обработчик WebSocket (только один аргумент)."""
    print("Клиент подключился")
    try:
        async for message in websocket:
            if isinstance(message, bytes):
                if len(message) != PACKET_SIZE:
                    print(f"Некорректный размер пакета: {len(message)} != {PACKET_SIZE}")
                    continue

                unpacked = PACKET_STRUCT.unpack(message)
                sensor_id = unpacked[0]
                rms_value = unpacked[1]
                accel = np.array(unpacked[2:2+BUF_SIZE])
                spectrum = np.array(unpacked[2+BUF_SIZE:])

                global time_data, spectrum_data
                time_data = accel
                spectrum_data = spectrum

                update_plot()
                print(f"Пакет получен: sensor_id={sensor_id}, RMS={rms_value:.3f} мм/с")
            else:
                print("Получено текстовое сообщение, игнорируем")
    except websockets.exceptions.ConnectionClosed:
        print("Клиент отключился")
    except Exception as e:
        print(f"Ошибка: {e}")

async def main():
    async with websockets.serve(websocket_handler, "0.0.0.0", 8765):
        print("WebSocket сервер запущен на ws://0.0.0.0:8765")
        plt.ion()
        plt.show(block=False)
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())