import paho.mqtt.client as mqtt

def on_message(client, userdata, msg):
    print(f"Received message: topic={msg.topic}, length={len(msg.payload)} bytes")

client = mqtt.Client()
client.on_message = on_message
client.connect("localhost", 1883, 60)
client.subscribe("/vibration/data")
client.loop_forever()
