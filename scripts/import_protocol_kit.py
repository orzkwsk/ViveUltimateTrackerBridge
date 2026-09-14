"""Import only the supplied, pinned Task 1 artifact; --verify checks every byte."""
import argparse, hashlib, json, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def digest(b): return hashlib.sha256(b).hexdigest()
def require(ok, message):
    if not ok: raise ValueError(message)
p = argparse.ArgumentParser()
p.add_argument('--zip', type=Path)
p.add_argument('--manifest', type=Path)
p.add_argument('--verify', action='store_true')
a = p.parse_args()
meta = json.loads((ROOT/'dependencies/monaka-protocol.handoff.json').read_text())
artifact = ROOT/'dependencies/artifacts'/meta['artifact']
raw = (artifact if a.verify else a.zip).read_bytes()
require(digest(raw) == meta['sha256'], 'ZIP SHA mismatch')
if not a.verify:
    external = a.manifest.read_bytes()
    require(json.loads(external) == meta, 'external handoff mismatch')
with zipfile.ZipFile(artifact if a.verify else a.zip) as z:
    data = {n: z.read(n) for n in z.namelist()}
    require(len(data) == len(z.namelist()), 'duplicate archive entries')
    for n in data:
        require(not Path(n).is_absolute() and '..' not in Path(n).parts and ':' not in n and '\\' not in n, 'unsafe path')
    require(digest(data['protocol.lock.json']) == meta['protocol_lock_sha256'], 'lock mismatch')
    lock = json.loads(data['protocol.lock.json'])
    for k in ('source_commit','schema_commit','contract_c1_sha256'):
        require(lock[k] == meta[k], k)
    require(digest(data['docs/C1.md']) == meta['contract_c1_sha256'], 'C1 mismatch')
    for group, values in lock.items():
        if group.endswith('_sha256') and isinstance(values,dict):
            for name, sha in values.items(): require(digest(data[name]) == sha, name)
    sums = dict(line.split('  ',1)[::-1] for line in data['SHA256SUMS'].decode().splitlines())
    require(set(sums) == set(data)-{'SHA256SUMS'}, 'sum coverage')
    for name, sha in sums.items(): require(digest(data[name]) == sha, name)
    kit = ROOT/'third_party/monaka-protocol'
    if a.verify:
        for name, content in data.items(): require((kit/name).read_bytes() == content, name)
        require((ROOT/'dependencies/monaka-protocol.lock.json').read_bytes() == data['protocol.lock.json'], 'retained lock')
    else:
        require(not kit.exists(), 'refuse overwrite')
        for name, content in data.items():
            target = kit/name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
        artifact.parent.mkdir(parents=True, exist_ok=True)
        artifact.write_bytes(raw)
        (ROOT/'dependencies/monaka-protocol.lock.json').write_bytes(data['protocol.lock.json'])
        (ROOT/'dependencies/task1-external-handoff.json').write_bytes(external)
print('PASS: fixed ZIP, external metadata, lock, C1 and all internal sums')
