"""Offline extraction of captured 317/714 skin configuration; never changes runtime files."""
from pathlib import Path
import json
import hashlib
import re
import struct
import yaml

CAPTURE = Path(r'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_2620')
CONFIG = Path(r'F:\PacketDumpTool\21543\server\Server\bin\Config')
OUT = Path(__file__).resolve().parent

class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0
    def take(self, count):
        if count < 0 or self.pos + count > len(self.data):
            raise ValueError(f'Truncated field at {self.pos}, length {count}')
        value = self.data[self.pos:self.pos + count]
        self.pos += count
        return value
    def u(self, count):
        return int.from_bytes(self.take(count), 'big')
    def string(self):
        return self.take(self.u(1)).decode('gb18030')
    def sub(self):
        return Reader(self.take(self.u(2) - 2))
    def zero(self, count=1):
        if self.u(count) != 0:
            raise ValueError('Nonzero field unsupported by current server schema')
    def end(self):
        if self.pos != len(self.data):
            raise ValueError(f'Unparsed bytes: {self.data[self.pos:].hex()}')

def effect(r):
    e = r.sub()
    d = dict(EffectID=e.u(2), Level=e.u(1), Value1=e.u(4), Value2=e.u(4),
             Resource=e.string(), EffectRes=e.string(), Grade=e.u(1))
    e.end()
    return d

def suit(r):
    e = r.sub()
    d = dict(KartID=e.u(4), KarNameLen=e.string())
    d['RefitItemList'] = [e.u(4) for _ in range(e.u(1))]
    d['RefitEffects'] = [effect(e) for _ in range(e.u(1))]
    for key in ['Grade', 'DressPoint', 'CarLibraryCollectValue']:
        d[key] = e.u(2)
    e.zero(2)
    d['ShapeSuitID'] = e.u(4)
    d['RefitAdditionList'] = [e.u(4) for _ in range(e.u(1))]
    d['GradeResourceLen'] = e.string()
    e.zero(2)
    d['SimilarShapeList'] = [e.u(4) for _ in range(e.u(1))]
    e.end()
    return d

def consum(r):
    e = r.sub()
    d = dict(ItemID=e.u(4), ItemNum=e.u(4), ItemPeriod=int.from_bytes(e.take(4), 'big', signed=True))
    e.end()
    return d

def level(r):
    e = r.sub()
    d = dict(Level=e.u(1))
    e.zero()
    d['RefitEffectsInfo'] = [effect(e) for _ in range(e.u(1))]
    d['DressPoint'], d['CarLibraryCollectValue'] = e.u(2), e.u(2)
    e.zero(2)
    d['ShapeSuitID'] = e.u(4)
    d['ConsumInfo'] = [consum(e) for _ in range(e.u(1))]
    e.zero(2)
    e.end()
    return d

def legend(r):
    e = r.sub()
    d = dict(KartID=e.u(4), KarName=e.string(), Grade=e.u(2), GradeResource=e.string())
    d['LevelInfo'] = [level(e) for _ in range(e.u(1))]
    d['ShapeSuitID'] = e.u(4)
    d['MaxConsumInfo'] = consum(e)
    d['LegendSkinType'] = e.u(1)
    e.end()
    return d

def color(r):
    e = r.sub()
    d = dict(MainShapeSuitID=e.u(4), ColorShapeList=[])
    for _ in range(e.u(2)):
        v = e.sub()
        d['ColorShapeList'].append(dict(ShapeSuitID=v.u(4), KarName=v.string(), Grade=v.u(2),
                                       GradeResource=v.string(), CarLibraryCollectValue=v.u(2)))
        v.end()
    e.end()
    return d

def load_config(name):
    data = (CONFIG / name).read_bytes()
    for encoding in ['utf-8-sig', 'gb18030']:
        try:
            return yaml.safe_load(data.decode(encoding)), encoding
        except UnicodeError:
            continue
    raise ValueError(f'Unknown encoding: {name}')

def write_yaml(name, value, encoding='gb18030'):
    # Match existing GBK-family files: the C++ writer copies string bytes directly.
    text = yaml.safe_dump(value, allow_unicode=True, sort_keys=False, width=120)
    (OUT / name).write_bytes(text.encode(encoding))
    if yaml.safe_load(text) != value:
        raise ValueError('YAML roundtrip mismatch')

class RpcReader(Reader):
    def __init__(self, data):
        super().__init__(data)
        self.strings = []
    def value(self):
        tag = self.u(1)
        if tag == 0xF6:
            return struct.unpack('<d', self.take(8))[0]
        if tag == 0xFA:
            value = self.string()
            self.strings.append(value)
            return value
        if tag == 0xFB:
            return self.strings[self.u(1)]
        if tag in (0xF8, 0xF9):
            return tag == 0xF8
        if tag == 0xFD:
            size = self.u(4)
            end = self.pos + size
            if end > len(self.data):
                raise ValueError('Truncated RPC table')
            value = {}
            while self.pos < end:
                key = self.value()
                value[key] = self.value()
            if self.pos != end:
                raise ValueError('RPC table length mismatch')
            return value
        raise ValueError(f'Unsupported RPC tag {tag:02x}')

def extract_maps():
    configured, _ = load_config('Map.yml')
    names = {x['ID']: x['Name'] for x in configured}
    captured = {}
    marker = b'DispatchCoResponse\0\0'
    for f in (CAPTURE / 'payloads').glob('*special28346_after_body_msg24251*'):
        raw = f.read_bytes()
        if b'MapID' not in raw and b'newMapEntryFunc' not in raw:
            continue
        offset = raw.find(marker)
        if offset < 0:
            continue
        r = RpcReader(raw[offset + len(marker):])
        context, result, value = r.value(), r.value(), r.value()
        if result != 0:
            raise ValueError('RPC returned failure')
        if 'mapinfo' in value:
            for row in value['mapinfo'].values():
                map_id = int(row['MapID'])
                if map_id != row['MapID']:
                    raise ValueError('Non-integer MapID')
                info = captured.setdefault(map_id, dict(ID=map_id, Name=names.get(map_id),
                    NameSource='existing Map.yml' if map_id in names else 'unknown',
                    ExistsInMapYaml=map_id in names, Evidence=[]))
                info['Evidence'].append(dict(file=f.name, context_id=context,
                    sha256=hashlib.sha256(raw).hexdigest()))
        if 'newMapEntryFunc' in value:
            print('New map entry:', value['newMapEntryFunc'])
    write_yaml('captured-map-ids.yml', list(captured.values()))
    write_yaml('existing-map-catalog.yml', [dict(ID=x['ID'], Name=x['Name']) for x in configured])
    print('Captured map IDs:', [(x['ID'], x['Name']) for x in captured.values()])
    tips = {}
    for f in (CAPTURE / 'payloads').glob('*parser_queue_dispatch_body_msg822_*.bin'):
        raw = f.read_bytes()
        wire_len = int.from_bytes(raw[:2], 'big')
        if wire_len > len(raw):
            raise ValueError('Truncated 822')
        body = raw[:wire_len]
        # Locate the length-prefixed NewMapTipsCfg array through its text.
        # Validate count, every record length, WordLen, and contiguous array bounds.
        hits = []
        for pos in range(4, len(body) - 8):
            length, map_id, word_len = struct.unpack_from('>HIH', body, pos)
            if length != 8 + word_len or pos + length > len(body) or not 0 < map_id < 65536:
                continue
            text_bytes = body[pos + 8:pos + length]
            if '新赛道'.encode('gb18030') not in text_bytes:
                continue
            word = text_bytes.decode('gb18030')
            match = re.search(r'【([^】]+)】', word)
            if match:
                hits.append((pos, length, map_id, match[1], word))
        if not hits:
            continue
        count = int.from_bytes(body[hits[0][0] - 4:hits[0][0]], 'big')
        if count != len(hits) or any(a[0] + a[1] != b[0] for a, b in zip(hits, hits[1:])):
            raise ValueError('NewMapTipsCfg array not contiguous or count mismatch')
        for pos, length, map_id, name, word in hits:
            previous = tips.setdefault(map_id, dict(ID=map_id, Name=name, WordTxt=word,
                ExistsInMapYaml=map_id in names, Evidence=[]))
            if previous['Name'] != name or previous['WordTxt'] != word:
                raise ValueError('Conflicting map tip records')
            previous['Evidence'].append(dict(file=f.name, offset=pos, record_hex=body[pos:pos+length].hex(' '),
                                            sha256=hashlib.sha256(raw).hexdigest()))
    rows = sorted(tips.values(), key=lambda x: x['ID'])
    write_yaml('new-map-ids.yml', [dict(ID=x['ID'], Name=x['Name']) for x in rows if not x['ExistsInMapYaml']])
    _, map_encoding = load_config('AMapConfig.yml')
    write_yaml('AMapConfig.NewMaps.fragment.yml', {'NewMaps': [dict(MapID=x['ID'], WordTxt=x['WordTxt']) for x in rows]}, map_encoding)
    (OUT / 'map-evidence.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
    print('New maps:', [(x['ID'], x['Name'], len(x['Evidence'])) for x in rows])
    return list(captured.values())

def main():
    suits, legends, colors, evidence = [], [], [], []
    files = sorted((CAPTURE / 'payloads').glob('*parser_queue_dispatch_body_msg*.bin'),
                   key=lambda p: int(p.name.split('_')[3]))
    for f in files:
        match = re.search(r'_msg(317|714)_', f.name)
        if not match:
            continue
        msg = int(match[1])
        raw = f.read_bytes()
        body_len = int.from_bytes(raw[:2], 'big')
        if body_len > len(raw):
            raise ValueError('Incomplete packet')
        r = Reader(raw[:body_len])
        r.u(2)
        r.zero(2)
        ns = r.u(2)
        batch = [suit(r) for _ in range(ns)]
        nl = r.u(2)
        legend_batch = [legend(r) for _ in range(nl)]
        more = r.u(1)
        origins = r.u(2)
        for _ in range(origins):
            r.sub()  # Not consumed by the three skin YAML loaders.
        color_batch = [color(r) for _ in range(r.u(2))] if msg == 317 else []
        buffers = r.u(2)
        for _ in range(buffers):
            r.sub()
        # New client fields beyond the current server's three skin schemas.
        extension = r.take(len(r.data) - r.pos)
        r.end()
        suits.extend(batch)
        legends.extend(legend_batch)
        colors.extend(color_batch)
        evidence.append(dict(file=f.name, sha256=hashlib.sha256(raw).hexdigest(), msg=msg,
                             body_len=body_len, captured_len=len(raw), suits=ns, legends=nl,
                             colors=len(color_batch), has_more=more, origin_count=origins,
                             buffer_count=buffers, unparsed_extension_hex=extension.hex(),
                             prefix=raw[:12].hex(' ')))
    if not evidence or evidence[0]['msg'] != 317 or evidence[-1]['has_more'] != 0:
        raise ValueError('Incomplete configuration sequence')
    if any(e['has_more'] != 1 for e in evidence[:-1]):
        raise ValueError('Unexpected continuation sequence')
    report = {'packets': evidence, 'configs': {}}
    for name, rows, key, root in [
        ('ShapeRefitCfg.yml', suits, lambda x: (x['KartID'], x['ShapeSuitID']), 'ShapeRefitLefend'),
        ('LegendarySkin.yml', legends, lambda x: (x['KartID'], x['ShapeSuitID']), None),
        ('ShapeColorCfg.yml', colors, lambda x: x['MainShapeSuitID'], None),
    ]:
        old, encoding = load_config(name)
        previous = old[root] if root else old
        prior = {key(x): x for x in previous}
        current = {key(x): x for x in rows}
        if len(current) != len(rows):
            raise ValueError(f'Duplicate captured key in {name}')
        added = [x for x in rows if key(x) not in prior]
        changed = [x for x in rows if key(x) in prior and x != prior[key(x)]]
        missing = [x for x in previous if key(x) not in current]
        wrap = lambda x: {root: x} if root else x
        write_yaml('captured-' + name, wrap(rows))
        write_yaml('added-' + name, wrap(added))
        # Preserve every existing/custom row; append captured keys absent locally.
        write_yaml('merged-additions-' + name, wrap(previous + added))
        report['configs'][name] = dict(captured=len(rows), existing=len(previous), encoding=encoding,
            added=added, changed=[dict(before=prior[key(x)], after=x) for x in changed],
            existing_not_captured=[key(x) for x in missing])
    (OUT / 'skin-evidence.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    for name, info in report['configs'].items():
        print(name, 'captured', info['captured'], 'existing', info['existing'],
              'added', len(info['added']), 'changed', len(info['changed']),
              'not captured', len(info['existing_not_captured']))
        for row in info['added']:
            print('  ', row.get('KartID'), row.get('ShapeSuitID'), row.get('KarNameLen', row.get('KarName')))
    print('Skin packet counts:', [(e['msg'], e['suits'], e['legends'], e['has_more']) for e in evidence])
    extract_maps()

if __name__ == '__main__':
    main()
