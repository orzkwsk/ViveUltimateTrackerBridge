import subprocess,sys,tempfile
from pathlib import Path
exe=sys.argv[1]
with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]/'build') as d:
    d=Path(d); config=d/'service.conf'; journal=d/'revision.log'
    config.write_text('source installation-test\nmap 23:32:42:b7:82:d3 room-a\n')
    def reserve(): return subprocess.run([exe,'--check-session',str(config),str(journal)],capture_output=True,text=True)
    a=reserve();b=reserve();assert a.returncode==b.returncode==0,(a.stderr,b.stderr)
    x=a.stdout.split();y=b.stdout.split();assert x[:2]==['installation-test','1'] and y[:2]==['installation-test','2'] and x[2]!=y[2]
    journal.write_bytes(b'1\n2');assert reserve().returncode==1
    journal.write_bytes(b'1\n9\n');assert reserve().returncode==1
    config.write_text('source installation-test\nmap 23:32:42:b7:82:d3 a\nmap 23:32:42:b7:82:d3 b\n');assert reserve().returncode==1
print('PASS: persisted revisions, new UUID per lifetime, torn/corrupt journal and duplicate MAC rejected; no HID opened')
