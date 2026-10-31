import subprocess,sys,tempfile
from pathlib import Path
exe=sys.argv[1]
test_root=Path(__file__).resolve().parents[1]/'Build'
test_root.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=test_root) as d:
    d=Path(d); config=d/'service.conf'; journal=d/'revision.log'
    config.write_text('source installation-test\nmap 23:32:42:b7:82:d3 room-a\n')
    def reserve(): return subprocess.run([exe,'--check-session',str(config),str(journal)],capture_output=True,text=True)
    a=reserve();b=reserve();assert a.returncode==b.returncode==0,(a.stderr,b.stderr)
    x=a.stdout.split();y=b.stdout.split();assert x[:2]==['installation-test','1'] and y[:2]==['installation-test','2'] and x[2]!=y[2]
    assert x[3]==y[3]=='strict'
    config.write_text('source installation-test\nreconnect_revision preserve\nmap 23:32:42:b7:82:d3 room-a\n')
    c=reserve();assert c.returncode==0,c.stderr
    z=c.stdout.split();assert z[1]=='3' and z[3]=='preserve'
    # A new process/config lifetime still advances; preserve is in-process only.
    d_run=reserve();assert d_run.returncode==0,d_run.stderr
    w=d_run.stdout.split();assert w[1]=='4' and w[2]!=z[2] and w[3]=='preserve'
    config.write_text('source installation-test\nreconnect_revision automatic\nmap 23:32:42:b7:82:d3 room-a\n')
    assert reserve().returncode==1
    config.write_text('source installation-test\nmap 23:32:42:b7:82:d3 room-a\n')
    journal.write_bytes(b'1\n2');assert reserve().returncode==1
    journal.write_bytes(b'1\n9\n');assert reserve().returncode==1
    config.write_text('source installation-test\nmap 23:32:42:b7:82:d3 a\nmap 23:32:42:b7:82:d3 b\n');assert reserve().returncode==1
print('PASS: strict default/preserve parsing, process-lifetime advance, new UUID, journal integrity and duplicate MAC; no HID opened')
