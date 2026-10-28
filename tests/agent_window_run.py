from pathlib import Path
import subprocess, sys, uuid
root=Path(sys.argv[2])/uuid.uuid4().hex
raise SystemExit(subprocess.run([sys.argv[1],'--test',str(root)],timeout=25).returncode)
