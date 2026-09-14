"""Real loopback UDP tests; no HID device is opened."""
import json, socket, subprocess, sys
exe=sys.argv[1]
receiver=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
receiver.bind(('127.0.0.1',0)); port=receiver.getsockname()[1]
receiver.close()
# A stopped consumer cannot stall the 1000-frame fixture producer.
subprocess.run([exe,'--send',str(port)],check=True,timeout=15)
for restart in range(2):
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as receiver:
        receiver.bind(('127.0.0.1',port)); receiver.settimeout(10)
        producer=subprocess.Popen([exe,'--send',str(port)])
        try:
            data,_=receiver.recvfrom(4096)
            value=json.loads(data)
            assert value['protocol']=='monaka.observation' and value['device_id']=='23:32:42:b7:82:d3'
            assert value['timestamp_kind']=='receive' and value['battery'] is None
            assert int(value['timestamp_ns'])<=int(value['sent_at_ns'])
            producer.wait(timeout=15)
            assert producer.returncode==0
        finally:
            if producer.poll() is None: producer.kill(); producer.wait()
print('PASS: consumer absent, receive, stop, receive again; producer remains bounded')
