"""Run the pinned shared fixtures and backend samples through both actual codecs."""
import json, os, subprocess, sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cpp,producer,java,javac=sys.argv[1:5]
kit=root/'third_party/monaka-protocol'
build=root/'build/cross';build.mkdir(parents=True,exist_ok=True)
cp=os.pathsep.join(str(p) for p in [build,*sorted((kit/'jvm/libs').glob('*.jar'))])
subprocess.run([javac,'-cp',cp,'-d',str(build),str(root/'tests/CodecConsumer.java')],check=True)
jvm=[java,'-Dfile.encoding=UTF-8','-cp',cp,'CodecConsumer']
def call(cmd,path):
    return subprocess.check_output([*cmd,str(path)],timeout=20).decode('utf-8').strip()
index=json.loads((kit/'fixtures/index.json').read_text(encoding='utf-8'))
cross=0
for item in index:
    path=kit/'fixtures'/item['file']
    results=[call([cpp],path),call(jvm,path)]
    if 'error' in item:
        assert results==['ERROR:'+item['error']['code']]*2,item['file']
    else:
        expected=item['decoded'];expected['version']['minor']=0
        for output,consumer in zip(results,[jvm,[cpp]]):
            assert json.loads(output)==expected,item['file']
            temp=build/'cross.json';temp.write_text(output,encoding='utf-8')
            assert json.loads(call(consumer,temp))==expected,item['file']
            cross+=1
samples=build/'vive-samples.jsonl'
subprocess.run([producer,'--emit',str(samples)],check=True)
for i,line in enumerate(samples.read_text().splitlines()):
    path=build/f'vive-{i}.json';path.write_text(line,encoding='utf-8')
    output=call(jvm,path);assert json.loads(output)==json.loads(line)
    path.write_text(output,encoding='utf-8');assert json.loads(call([cpp],path))==json.loads(line)
print(f'PASS: {len(index)} fixed fixtures, {cross} shared cross directions, 7 VIVE C++ -> JVM -> C++ samples')
