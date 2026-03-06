import asyncio
import websockets

BUF_SIZE = 3200
EXPECTED_BYTES = BUF_SIZE * 3 * 4

message_counter = 0
counter_lock = asyncio.Lock()

async def handle_connection(websocket):
    global message_counter
    print("Клиент подключился")
    try:
        async for message in websocket:
            if isinstance(message, bytes): 
                async with counter_lock:
                    message_counter += 1
                    current_num = message_counter
                print(f"Получено сообщение #{current_num}, размер: {len(message)} байт")
            else:
                print(f"Получен некорректный пакет: {len(message)} байт")
    except websockets.exceptions.ConnectionClosed:
        print("Клиент отключился")

async def main():
    async with websockets.serve(handle_connection, "0.0.0.0", 8765, max_size=2**20):
        print("WebSocket сервер запущен на порту 8765")
        await asyncio.Future()  

asyncio.run(main())
