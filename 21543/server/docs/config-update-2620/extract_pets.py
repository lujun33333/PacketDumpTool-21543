"""Decode local pet captures using the field order in Server/pet.cpp.
Writes review candidates only; no runtime configuration is modified.
"""
from pathlib import Path
import copy
import hashlib
import json
import re
import yaml
from extract_configs import Reader, CAPTURE

OUT = Path(__file__).resolve().parent / 'pets'
PET = Path(r'F:\PacketDumpTool\21543\server\Server\bin\Pet')
U8, U16, U32, I16 = 'u1', 'u2', 'u4', 'i2'
def record(**fields): return ('record', fields)
def array(width, element, count=None): return ('array', width, element, count)
def string(width): return ('string', width)

SKILL = record(SkillID=U16, Status=U8, Active=U8, Value=U32)
LEVEL = record(Level=U32, SuccProb=U32, ConsCardNum=U32, ConsSuperMoney=U32,
    Skills=array(4, SKILL), Type=U8, **{k:U32 for k in
    ['HuanHuaItemPeriod','BeginDate','BeginTime','BonusProb','BonusProbBeginDate',
     'BonusProbBeginTime','BonusProbEndDate','BonusProbEndTime','BaojiBeginNum','BaojiInc']})
STRENGTH = record(ID=U32, Levels=array(4, LEVEL), HuanHuaItemID=U32, TailFlag1=U8, TailFlag2=U8)
SHOWINFO = record(SkillID=U32, SkillFullLevelValue=U32, StrengthFullLevelValue=U32)
SHOW = record(Base=record(ID=U32, FullLevel=U32, StrengthFullLevel=U32, CanRide=U16,
    RidePetType=U16, ShowBaseInfos=array(2, SHOWINFO)),
    Open=record(ID=U32, OpenNewSkills=array(2, record(OpenLevel=U32, SkillID=U16, AddValue=U32))))
SLOTS = record(ID=U32, SkillSlots=array(1, U16))
PKPET = record(ID=U32, ProfType=U8, Rarity=U8,
    Skills=array(4,record(SkillID=U32, TackEffectLevel=U16, Attach=string(2))),
    FightPower=U32, Attributes=array(0,U32,5), HasSpecialPowerSkill=U8, MaxFightPower=U32)
IMPROVE = record(Level=U32, ItemID=U32, ItemNum=U32, PetCoinNum=U32, Probability=U8)
def potential(key):
    return record(ProfType=U8, Rarities=array(0,record(Rarity=U8, **{key:array(0,I16,5)}),5))
EFFECT = record(ArraySeatEffectType=U8, Paras=array(1,U32),
                PetAttrEffectFactors=array(0,U32,5), MaxPetAttrEffectFactor=U32)
SEAT = record(SeatID=U8, Effects=array(1,EFFECT), RecomPetType=U8)
BATTLELEVEL = record(Level=U8, CostItems=array(1,record(ItemID=U32,ConsumeNum=U32)), Seats=array(1,SEAT))
BATTLEARRAY = record(ArrayID=U8, Name=string(1), Desc=string(2), Levels=array(1,BATTLELEVEL))
GLOBAL = record(ExpItems=array(1,record(ExpItemID=U32,PetCoinNum=U32)),
    StarLevels=array(2,IMPROVE), NormalSkillLevels=array(2,IMPROVE), PowerSkillLevels=array(2,IMPROVE),
    PetConfigs=array(2,PKPET), PetSkillConfigs=array(2,record(SkillID=U32,Type=U8,SkillStar=U16)),
    SkillWakeUp=record(ItemID=U32,ItemNum=U32,FightLevel=U16),
    AttributePotentials=array(0,potential('PotentialAttributes'),5),
    StarPotentials=array(0,record(Rarity=U8,StarAttributes=array(0,I16,2)),5),
    AttributeConfigs=array(0,potential('BaseAttributes'),5),
    Embattle=record(**{k:array(0,U32,10) for k in ['HealthAddition','FrontRowDefenseAddition',
        'BackRowAttackAddition','DamageTreatAddition']},EmbattlePetNum=U32),
    AttributeMotivateLevels=array(2,record(Level=U32,ItemID=U32,ItemNum=U32,PetCoinNum=U32,AttrInc=U32)),
    StarRefreshLevels=array(2,IMPROVE), BattleArrays=record(InitArrayID=U8,Arrays=array(1,BATTLEARRAY),
        BeginDate=U32,EndDate=U32,OpenTime=U32,CloseTime=U32))
SCHEMAS = {
    24426:record(Uin=U32,Pets=array(2,SHOW),HasMoreInfo=U8),
    24527:record(Uin=U32,Pets=array(2,SHOW),HasMoreInfo=U8),
    440:record(Pets=array(4,SLOTS)),
    20100:record(StrengthenCardID=U32,Pets=array(4,STRENGTH),HasMoreInfo=U8),
    28228:record(Pets=array(4,STRENGTH),HasMoreInfo=U8),
    24074:record(PetOrder=array(4,U32)),
    24073:record(StrengthenCardID=U32,Pets=array(4,STRENGTH)),
    21140:record(Uin=U32,ResultID=U16,Reason=string(2),Global=GLOBAL,HasMoreInfo=U8),
    21149:record(Pets=array(2,PKPET),HasMoreInfo=U8),
}

def decode(r,schema):
    if isinstance(schema,str):
        return int.from_bytes(r.take(int(schema[1:])), 'big', signed=schema[0]=='i')
    kind=schema[0]
    if kind=='record':
        child=r.sub();d={k:decode(child,s) for k,s in schema[1].items()};child.end();return d
    if kind=='array':
        _,width,element,count=schema
        n=count if count is not None else r.u(width)
        if n>len(r.data)-r.pos:raise ValueError('Invalid array count')
        return [decode(r,element) for _ in range(n)]
    if kind=='string':return r.take(r.u(schema[1])).decode('gb18030')
    raise ValueError(kind)

def encode(value,schema):
    if isinstance(schema,str):return value.to_bytes(int(schema[1:]),'big',signed=schema[0]=='i')
    kind=schema[0]
    if kind=='record':
        b=b''.join(encode(value[k],s) for k,s in schema[1].items());return (len(b)+2).to_bytes(2,'big')+b
    if kind=='array':
        _,width,element,count=schema
        if count is not None and len(value)!=count:raise ValueError('Fixed array mismatch')
        return (len(value).to_bytes(width,'big') if count is None else b'')+b''.join(encode(x,element) for x in value)
    b=value.encode('gb18030');return len(b).to_bytes(schema[1],'big')+b

def load_packets():
    packets={k:[] for k in SCHEMAS};evidence=[];negative=0
    for f in sorted((CAPTURE/'payloads').glob('*parser_queue_dispatch_body_msg*.bin'),key=lambda x:int(x.name.split('_')[3])):
        msg=int(re.search(r'_msg(\d+)_',f.name)[1])
        if msg not in SCHEMAS:continue
        raw=f.read_bytes();length=int.from_bytes(raw[:2],'big')
        if not 2<=length<=len(raw):raise ValueError('Invalid self length')
        b=raw[:length];r=Reader(b);d=decode(r,SCHEMAS[msg]);r.end()
        if encode(d,SCHEMAS[msg])!=b:raise ValueError('Byte roundtrip mismatch')
        try:decode(Reader(b[:-1]),SCHEMAS[msg])
        except ValueError:negative+=1
        else:raise ValueError('Truncated packet accepted')
        packets[msg].append(d)
        evidence.append(dict(file=f.name,msg=msg,body_length=length,
            sha256=hashlib.sha256(raw).hexdigest(),pets=len(d.get('Pets',d.get('PetOrder',[]))),
            has_more=d.get('HasMoreInfo')))
    return packets,evidence,negative

def single(packets,msg):
    rows=packets[msg]
    if not rows or any(x!=rows[0] for x in rows):raise ValueError(f'Missing/conflicting msg{msg}')
    return rows[0]

def batch(packets,msg):
    rows=[];sizes=[];completed=[]
    for p in packets[msg]:
        rows+=p['Pets'];sizes.append(len(p['Pets']))
        if p['HasMoreInfo']==0:completed.append((rows,sizes));rows=[];sizes=[]
    if rows or not completed or any(x!=completed[0] for x in completed):raise ValueError(f'Incomplete/conflicting msg{msg}')
    return completed[0]

def merge_rows(old,new):
    by_id={x['ID']:x for x in new}
    if len(by_id)!=len(new):raise ValueError('Duplicate captured ID')
    old_ids={x['ID'] for x in old}
    return [copy.deepcopy(by_id.get(x['ID'],x)) for x in old]+[copy.deepcopy(x) for x in new if x['ID'] not in old_ids]

def main():
    packets,evidence,negative=load_packets()
    original={n:yaml.safe_load((PET/(n+'.yml')).read_text('utf-8-sig')) for n in ['PetBase','PetSkill','PetStrength','PetHuanHua','PetPK']}
    candidate=copy.deepcopy(original)
    first=single(packets,24426)
    if first['HasMoreInfo']!=1:raise ValueError('Unexpected show continuation')
    more,sizes=batch(packets,24527);show=first['Pets']+more
    slots={x['ID']:x['SkillSlots'] for x in single(packets,440)['Pets']}
    new_base=[];new_skill=[]
    for pet in show:
        b=pet['Base'];pid=b['ID'];o=pet['Open']
        if o['ID'] not in [0,pid]:raise ValueError('Open skill ID mismatch')
        new_base.append(dict(ID=pid,ShowConfig={k:b[k] for k in ['FullLevel','StrengthFullLevel','CanRide','RidePetType']}))
        new_skill.append(dict(ID=pid,SkillSlots=slots[pid],ShowBaseInfos=b['ShowBaseInfos'],OpenNewSkills=o['OpenNewSkills']))
    if set(slots)!={x['ID'] for x in new_base}:raise ValueError('Show and skill IDs differ')
    details,detail_sizes=batch(packets,28228)
    order=single(packets,24074)['PetOrder'];detail_ids=[x['ID'] for x in details];detail={x['ID']:x for x in details}
    if not set(detail)<=set(order):raise ValueError('Strength details outside ID list')
    strength=[]
    old_strength={x['ID']:x for x in original['PetStrength']['Pets']}
    for pid in order:
        fallback=old_strength.get(pid,dict(ID=pid,Levels=[],HuanHuaItemID=0,TailFlag1=0,TailFlag2=0))
        row=copy.deepcopy(detail.get(pid,fallback))
        if pid not in detail:row['Levels']=[]
        row['HasDetail']=pid in detail;strength.append(row)
    huan=single(packets,24073)
    pk,pk_sizes=batch(packets,21149);global_pk=single(packets,21140)
    header=single(packets,20100)
    if header['Pets'] or header['HasMoreInfo']!=1:raise ValueError('Unexpected strength header')
    referenced=set(order)|{x['ID'] for x in huan['Pets']}|{x['ID'] for x in pk}
    known={x['ID'] for x in new_base}
    new_base += [dict(ID=x,ShowConfig=None) for x in sorted(referenced-known)]
    updates=dict(PetBase=new_base,PetSkill=new_skill,PetStrength=strength,PetHuanHua=huan['Pets'],PetPK=pk)
    report={}
    for name,rows in updates.items():
        old={x['ID']:x for x in original[name]['Pets']};new={x['ID']:x for x in rows}
        report[name]=dict(existing=len(old),captured=len(new),added=[i for i in new if i not in old],
            changed=[i for i in new if i in old and new[i]!=old[i]],not_captured=[i for i in old if i not in new])
        candidate[name]['Pets']=merge_rows(original[name]['Pets'],rows)
        candidate[name]['SourceDump']=CAPTURE.name
        candidate[name]['GeneratedFrom']=[e['file'] for e in evidence]
    # Keep the loader's exact order contracts; preserve uncaptured custom rows.
    show_order=[x['ID'] for x in candidate['PetBase']['Pets'] if x.get('ShowConfig') is not None]
    sm={x['ID']:x for x in candidate['PetSkill']['Pets']}
    candidate['PetSkill']['Pets']=[sm[i] for i in show_order]
    def chunk(n,max_size):return [min(max_size,n-i) for i in range(0,n,max_size)]
    candidate['PetBase']['ShowPacketSizes']=chunk(len(show_order),200)
    candidate['PetStrength']['StrengthenCardID']=header['StrengthenCardID']
    candidate['PetStrength']['Pets']=sorted(candidate['PetStrength']['Pets'],key=lambda x:order.index(x['ID']) if x['ID'] in order else len(order))
    candidate['PetStrength']['PetOrder']=[x['ID'] for x in candidate['PetStrength']['Pets']]
    prior_details=candidate['PetStrength']['DetailPetOrder']
    candidate['PetStrength']['DetailPetOrder']=detail_ids+[i for i in prior_details if i not in detail_ids and i not in order]
    candidate['PetStrength']['DetailPacketSizes']=chunk(len(candidate['PetStrength']['DetailPetOrder']),30)
    candidate['PetHuanHua']['StrengthenCardID']=huan['StrengthenCardID']
    for key in ['Global','ResultID','Reason','HasMoreInfo']:candidate['PetPK'][key]=global_pk[key]
    candidate['PetPK']['PetPacketSizes']=chunk(len(candidate['PetPK']['Pets']),200)
    captured_pk=copy.deepcopy(candidate['PetPK'])
    # Configuration packets contain zero attributes for many existing pets.
    # Preserve local values until these zeroes are established as intended changes.
    old_pk={x['ID']:x for x in original['PetPK']['Pets']}
    preserved_pk=[]
    for row in candidate['PetPK']['Pets']:
        prior=old_pk.get(row['ID'])
        if prior and row['FightPower']==0 and not any(row['Attributes']) and (prior['FightPower'] or any(prior['Attributes'])):
            preserved_pk.append(row['ID'])
            row['FightPower']=prior['FightPower'];row['Attributes']=copy.deepcopy(prior['Attributes'])
            old_skills={(s['SkillID'],s['TackEffectLevel']):s for s in prior['Skills']}
            for skill in row['Skills']:
                prev=old_skills.get((skill['SkillID'],skill['TackEffectLevel']))
                if prev and not skill['Attach']:skill['Attach']=prev['Attach']
    for name in candidate:
        keys=set(candidate[name])|set(original[name])
        report[name]['changed_top_level']=[k for k in keys if k not in ['Pets','SourceDump','GeneratedFrom'] and candidate[name].get(k)!=original[name].get(k)]
    OUT.mkdir(exist_ok=True)
    (OUT/'captured-PetPK.yml').write_text(yaml.safe_dump(captured_pk,allow_unicode=True,sort_keys=False),encoding='utf-8')
    for name,doc in candidate.items():
        text=yaml.safe_dump(doc,allow_unicode=True,sort_keys=False,width=120)
        (OUT/(name+'.yml')).write_text(text,encoding='utf-8')
        if yaml.safe_load(text)!=doc:raise ValueError('YAML roundtrip mismatch')
    # The actual PK response sender reads Config/PetPK.yml, separately from Pet/PetPK.yml.
    # Produce a matching transport candidate, with a zeroed UIN patched by pet.cpp.
    transport_old=yaml.safe_load((PET.parent/'Config'/'PetPK.yml').read_text('utf-8-sig'))
    transport=copy.deepcopy(transport_old)
    transport['SourceDump']=CAPTURE.name+'; preserved local nonzero PK attributes'
    normalized_global=copy.deepcopy(global_pk);normalized_global['Uin']=0
    transport['Response21140Hex']=encode(normalized_global,SCHEMAS[21140]).hex()
    sizes=candidate['PetPK']['PetPacketSizes'];rows=candidate['PetPK']['Pets'];offset=0;hexes=[]
    for i,size in enumerate(sizes):
        body=dict(Pets=rows[offset:offset+size],HasMoreInfo=int(i+1<len(sizes)))
        hexes.append(encode(body,SCHEMAS[21149]).hex());offset+=size
    transport['Response21149HexList']=hexes
    (OUT/'Config').mkdir(exist_ok=True)
    (OUT/'Config'/'PetPK.yml').write_text(yaml.safe_dump(transport,allow_unicode=True,sort_keys=False),encoding='utf-8')
    result=dict(configs=report,packets=evidence,truncation_rejected=negative,
        packet_roundtrip_bytes=sum(e['body_length'] for e in evidence),
        retained_unverified_fields=['PetBase.LevelExperience'],
        preserved_local_pk_zero_snapshot_ids=preserved_pk,
        new_pk_ids_without_observed_attributes=[x['ID'] for x in pk if x['ID'] not in old_pk and not any(x['Attributes'])],
        strength_ids_without_detail=[x for x in order if x not in detail],
        original_sha256={n:hashlib.sha256((PET/(n+'.yml')).read_bytes()).hexdigest() for n in original})
    (OUT/'pet-diff.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
    print('Packet roundtrip:',len(evidence),'bytes:',result['packet_roundtrip_bytes'],'truncated rejected:',negative)

if __name__=='__main__':main()
