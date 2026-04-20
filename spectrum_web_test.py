import asyncio
import struct
import numpy as np
import matplotlib.pyplot as plt
from matplotlib import gridspec
import websockets

# Параметры (должны совпадать с прошивкой)
BUF_SIZE = 1024
SAMPLING_RATE = 1/0.000313
PACKET_FORMAT = f'If{BUF_SIZE}f{BUF_SIZE//2}f'
PACKET_STRUCT = struct.Struct(PACKET_FORMAT)
PACKET_SIZE = PACKET_STRUCT.size

# Хранилище данных для двух датчиков
sensor_data = {
    1: {'accel': np.zeros(BUF_SIZE), 'spectrum': np.zeros(BUF_SIZE // 2), 'rms': 0.0},
    2: {'accel': np.zeros(BUF_SIZE), 'spectrum': np.zeros(BUF_SIZE // 2), 'rms': 0.0}
}

# ------------------------------------------------------------
# Окно для датчика 1
# ------------------------------------------------------------
fig1 = plt.figure(figsize=(10, 6))
fig1.canvas.manager.set_window_title('Sensor 1')
gs1 = gridspec.GridSpec(2, 1, height_ratios=[2, 1], figure=fig1)
ax_time1 = fig1.add_subplot(gs1[0])
ax_spec1 = fig1.add_subplot(gs1[1])

ax_time1.set_title('Sensor 1 - Acceleration (g)')
ax_time1.set_xlabel('Sample')
ax_time1.set_ylabel('g')
ax_time1.grid(True, alpha=0.3)
line_time1, = ax_time1.plot([], [], 'cyan', lw=0.5)

ax_spec1.set_title('Sensor 1 - Velocity Spectrum (mm/s)')
ax_spec1.set_xlabel('Frequency (Hz)')
ax_spec1.set_ylabel('mm/s')
ax_spec1.set_xlim(0, 1000)
ax_spec1.grid(True, linestyle='--', alpha=0.5)
line_spec1, = ax_spec1.plot([], [], 'lime', lw=1)
rms_text1 = ax_time1.text(0.02, 0.95, '', transform=ax_time1.transAxes,
                          color='white', fontsize=10, bbox=dict(facecolor='black', alpha=0.5))

# ------------------------------------------------------------
# Окно для датчика 2
# ------------------------------------------------------------
fig2 = plt.figure(figsize=(10, 6))
fig2.canvas.manager.set_window_title('Sensor 2')
gs2 = gridspec.GridSpec(2, 1, height_ratios=[2, 1], figure=fig2)
ax_time2 = fig2.add_subplot(gs2[0])
ax_spec2 = fig2.add_subplot(gs2[1])

ax_time2.set_title('Sensor 2 - Acceleration (g)')
ax_time2.set_xlabel('Sample')
ax_time2.set_ylabel('g')
ax_time2.grid(True, alpha=0.3)
line_time2, = ax_time2.plot([], [], 'magenta', lw=0.5)

ax_spec2.set_title('Sensor 2 - Velocity Spectrum (mm/s)')
ax_spec2.set_xlabel('Frequency (Hz)')
ax_spec2.set_ylabel('mm/s')
ax_spec2.set_xlim(0, 1000)
ax_spec2.grid(True, linestyle='--', alpha=0.5)
line_spec2, = ax_spec2.plot([], [], 'orange', lw=1)
rms_text2 = ax_time2.text(0.02, 0.95, '', transform=ax_time2.transAxes,
                          color='white', fontsize=10, bbox=dict(facecolor='black', alpha=0.5))

freq_axis = np.linspace(0, SAMPLING_RATE / 2, BUF_SIZE // 2)

def update_plots():
    """Обновление обоих окон."""
    # Датчик 1
    line_time1.set_data(np.arange(BUF_SIZE), sensor_data[1]['accel'])
    ax_time1.relim()
    ax_time1.autoscale_view(scalex=False, scaley=True)
    line_spec1.set_data(freq_axis[1:], sensor_data[1]['spectrum'][1:])
    ax_spec1.relim()
    ax_spec1.autoscale_view(scalex=False, scaley=True)
    rms_text1.set_text(f'RMS: {sensor_data[1]["rms"]:.3f} mm/s')

    # Датчик 2
    line_time2.set_data(np.arange(BUF_SIZE), sensor_data[2]['accel'])
    ax_time2.relim()
    ax_time2.autoscale_view(scalex=False, scaley=True)
    line_spec2.set_data(freq_axis[1:], sensor_data[2]['spectrum'][1:])
    ax_spec2.relim()
    ax_spec2.autoscale_view(scalex=False, scaley=True)
    rms_text2.set_text(f'RMS: {sensor_data[2]["rms"]:.3f} mm/s')

    fig1.canvas.draw_idle()
    fig2.canvas.draw_idle()
    plt.pause(0.001)

async def websocket_handler(websocket):
    """Обработчик WebSocket соединения."""
    print("Клиент подключился")
    try:
        async for message in websocket:
            if isinstance(message, bytes):
                if len(message) != PACKET_SIZE:
                    print(f"Неверный размер пакета: {len(message)} ожидалось {PACKET_SIZE}")
                    continue

                unpacked = PACKET_STRUCT.unpack(message)
                sensor_id = int(unpacked[0])
                if sensor_id not in (1, 2):
                    print(f"Неизвестный sensor_id: {sensor_id}")
                    continue

                sensor_data[sensor_id]['rms'] = unpacked[1]
                sensor_data[sensor_id]['accel'] = np.array(unpacked[2:2+BUF_SIZE])
                sensor_data[sensor_id]['spectrum'] = np.array(unpacked[2+BUF_SIZE:])

                update_plots()
                print(f"Датчик {sensor_id}: RMS={unpacked[1]:.3f} мм/с")
            else:
                print("Текстовое сообщение проигнорировано")
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