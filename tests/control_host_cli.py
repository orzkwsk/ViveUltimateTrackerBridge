"""Real native control framing and ownership failures, never opens HID."""
import json,subprocess,sys
exe=sys.argv[1]
def check(value,reason):
    if not value: raise RuntimeError(reason)
requests=[{'version':1,'id':'status','operation':'get_status'},
          {'version':1,'id':'connect','operation':'connect'},
          {'version':1,'id':'pair','operation':'begin_pairing','selector':-1,'timeoutSeconds':60},
          {'version':1,'id':'bad-slot','operation':'begin_pairing','selector':9,'timeoutSeconds':60},
          {'version':1,'id':'huge','operation':'begin_pairing','selector':4294967296,'timeoutSeconds':60},
          {'version':1,'id':'cancel','operation':'cancel_pairing'},
          {'version':1,'id':'future','operation':'future'},
          {'version':1,'id':'close','operation':'shutdown'}]
result=subprocess.run([exe,'--offline'],input='\n'.join(json.dumps(r) for r in requests)+'\n',text=True,capture_output=True,timeout=10)
check(result.returncode==0,result.stderr)
responses=[json.loads(line) for line in result.stdout.splitlines()]
check(len(responses)==len(requests),'one correlated reply per request')
check(responses[0]['devices']==[] and responses[0]['backend']=='Dongle unavailable','no fake devices')
check(not responses[1]['ok'] and responses[1]['code']=='Unavailable','offline connect never opens HID')
check(responses[2]['code']=='Unavailable' and responses[3]['code']=='InvalidArgument','unavailable/invalid pair')
check(responses[4]['code']=='InvalidArgument' and responses[5]['ok'] and responses[6]['code']=='Unsupported','cancel and future operation')
for frame in ['{','{"version":1,"version":1,"id":"x","operation":"connect"}',json.dumps({'version':2,'id':'x','operation':'connect'}),'x'*4097]:
    p=subprocess.run([exe,'--offline'],input=frame+'\n'+json.dumps(requests[-1])+'\n',text=True,capture_output=True,timeout=10)
    rows=[json.loads(line) for line in p.stdout.splitlines()]
    check(p.returncode==0 and rows[0]['code']=='InvalidRequest' and rows[-1]['ok'],'reject malformed and recover')
print('PASS native framing, duplicate/version/oversize rejection, offline ownership, explicit operations and close')

# Closing a slow reader releases a blocked output worker and ends offline setup.
p=subprocess.Popen([exe,'--offline'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
try:
    p.stdin.write((json.dumps(requests[0])+'\n').encode()*20);p.stdin.flush()
    import time
    time.sleep(0.15)
    p.stdout.close();p.stdin.close()
    check(p.wait(timeout=5)==0,'slow-reader close/shutdown')
finally:
    if p.poll() is None:p.kill();p.wait()
    p.stderr.close()
print('PASS slow-reader disconnect and bounded worker shutdown')
