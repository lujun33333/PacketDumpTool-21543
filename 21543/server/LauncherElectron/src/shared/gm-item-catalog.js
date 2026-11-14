import { TextDecoder } from 'node:util';

const ITEM_ID_XOR = 0x4d10031e;
const STRING_XOR_KEY = Buffer.from('!G#&^* 9', 'ascii');
const ITEM_RECORD_HEADER_SIZE = 55;
const ITEM_INDEX_FOOTER_SIZE = 8;
const ITEM_INDEX_PAIR_SIZE = 8;
const MAX_ITEM_COUNT = 1_000_000;
const MAX_STRING_BYTES = 1024 * 1024;

const TYPE_CLASSIFICATION = new Map([
  // 人物服饰
  [257, ['服饰', '头饰']],
  [258, ['服饰', '护镜']],
  [259, ['服饰', '表情']],
  [261, ['服饰', '头饰']],
  [262, ['服饰', '上装']],
  [263, ['服饰', '手套']],
  [264, ['服饰', '刺青']],
  [265, ['服饰', '下装']],
  [266, ['服饰', '发饰']],
  [268, ['服饰', '面饰']],
  [271, ['服饰', '头饰']],
  [276, ['服饰', '面饰']],
  [277, ['服饰', '背饰']],
  [278, ['服饰', '脚底炫光']],
  [280, ['服饰', '尾饰']],
  [287, ['服饰', '手杖/手饰']],
  [288, ['服饰', '手杖']],
  [289, ['服饰', '上装光效']],
  [290, ['服饰', '发饰光效']],
  [293, ['服饰', '背部光效']],
  [295, ['服饰', '左手环']],
  [296, ['服饰', '右手环']],
  [300, ['服饰', '肩饰']],
  [301, ['服饰', '人物皮肤/肤色']],
  [303, ['服饰', '坐骑']],
  [310, ['服饰', '鞋子']],
  [317, ['服饰', '飞饰']],
  [1793, ['服饰', '套装']],

  // 社交和个人展示
  [269, ['社交展示', '前景']],
  [273, ['社交展示', '个人背景/配饰']],
  [275, ['社交展示', '挂饰']],
  [319, ['社交展示', '头像']],
  [320, ['社交展示', '头像框']],
  [324, ['社交展示', '名片']],
  [325, ['社交展示', '背景']],
  [326, ['社交展示', '全景名片']],
  [327, ['社交展示', '名片']],
  [330, ['社交展示', '开场展示']],
  [331, ['社交展示', '起点秀']],
  [332, ['社交展示', '冲线秀']],
  [333, ['社交展示', '颁奖台']],
  [334, ['社交展示', '驾驶舱面板']],
  [339, ['社交展示', '聊天气泡']],
  [340, ['社交展示', '炫彩昵称']],
  [341, ['社交展示', '社交底板']],
  [342, ['社交展示', '进房特效']],
  [2312, ['社交展示', '户外特效']],
  [2313, ['社交展示', '操纵魔法']],
  [2315, ['社交展示', '聊天动作']],
  [2318, ['社交展示', '车队徽章']],
  [2319, ['社交展示', '戒指']],
  [2322, ['社交展示', '锤子']],
  [2338, ['社交展示', '头像卡']],
  [2339, ['社交展示', '赠送道具']],
  [2354, ['社交展示', '名片夹']],
  [2387, ['社交展示', '面板场景']],
  [2391, ['社交展示', '语音包']],
  [3000, ['社交展示', '表情挂饰']],
  [3001, ['社交展示', '表情挂饰']],
  [3002, ['社交展示', '表情挂饰']],
  [3003, ['社交展示', '表情挂饰']],
  [3004, ['社交展示', '表情挂饰']],
  [3005, ['社交展示', '表情挂饰']],
  [3006, ['社交展示', '表情挂饰']],

  // 赛车装扮、改装和车载武器
  [272, ['赛车装扮', '车辆炫光']],
  [514, ['赛车装扮', '喷漆']],
  [515, ['赛车装扮', '车牌']],
  [516, ['赛车装扮', '尾挂/气球']],
  [521, ['赛车装扮', '车头']],
  [522, ['赛车装扮', '车尾/尾翼']],
  [523, ['赛车装扮', '侧翼']],
  [524, ['赛车装扮', '车轮/车胎']],
  [769, ['赛车装扮', '胎印']],
  [770, ['赛车装扮', '漂焰']],
  [2050, ['赛车装扮', 'G0级机枪']],
  [2051, ['赛车装扮', 'G1级机枪']],
  [2052, ['赛车装扮', 'G2级机枪']],
  [2053, ['赛车装扮', 'G3级机枪']],
  [2325, ['赛车装扮', '基础改装配件']],
  [2376, ['赛车装扮', '赛车觉醒经验卡']],
  [2384, ['赛车装扮', '改装加成卡']],
  [2824, ['赛车装扮', '进阶改装配件']],
  [2834, ['赛车装扮', '进阶改装碎片']],
  [2835, ['赛车装扮', '能量源']],

  // 宠物、宝宝和精灵
  [2328, ['宠物', '宠物']],
  [2329, ['宠物', '宠物道具']],
  [2348, ['宠物', '精灵卡']],
  [2349, ['宠物', '精灵道具']],
  [2355, ['宠物', '宠物头饰']],
  [2356, ['宠物', '宝宝头饰']],
  [2357, ['宠物', '宝宝炫光']],
  [2358, ['宠物', '宝宝服饰']],

  // 功能道具
  [1794, ['道具', '礼包']],
  [2305, ['道具', '功能道具']],
  [2306, ['道具', '经验/积分加成卡']],
  [2307, ['道具', '更名卡']],
  [2308, ['道具', '抽奖卡']],
  [2309, ['道具', '战绩清零卡']],
  [2310, ['道具', '积分清零卡']],
  [2311, ['道具', '临时驾照']],
  [2316, ['道具', '比赛道具']],
  [2320, ['道具', '倒计时加速卡']],
  [2321, ['道具', '自动防御卡']],
  [2323, ['道具', '宝箱']],
  [2324, ['道具', '钥匙']],
  [2326, ['道具', '砸蛋卡']],
  [2327, ['道具', '飞行卡']],
  [2331, ['道具', '装备类糖果']],
  [2332, ['道具', '时间类糖果']],
  [2333, ['道具', '任务类糖果']],
  [2340, ['道具', '兑换道具']],
  [2341, ['道具', '变形芯片/幻形卡']],
  [2342, ['道具', '高飞卡']],
  [2343, ['道具', '折扣卡']],
  [2351, ['道具', '奇幻卡片']],
  [2352, ['道具', '增值道具']],
  [2353, ['道具', '增值道具']],
  [2372, ['道具', '满减券']],
  [2375, ['道具', '代金券']],
  [2377, ['道具', '活跃度加成卡']],
  [2380, ['道具', '特殊宝箱']],
  [2382, ['道具', '排位双倍积分卡']],
  [2383, ['道具', '排位积分保护卡']],
  [2386, ['道具', '乐器卡']],

  // 水族系统
  [2334, ['水族', '鱼儿']],
  [2335, ['水族', '鱼苗']],
  [2336, ['水族', '鱼饲料']],

  // 宝石系统
  [2817, ['宝石', '圆形竞速宝珠']],
  [2818, ['宝石', '三角道具之钻']],
  [2820, ['宝石', '祝福石']],
  [2821, ['宝石', '幸运石']],
  [2822, ['宝石', '雕刻刀']],
  [2823, ['宝石', '宝石拆除刀']],

  // 客户端内部记录
  [771, ['系统/其他', '作废项']],
  [1281, ['系统/其他', '角色']],
  [1282, ['系统/其他', '角色']],
  [2049, ['系统/其他', '客户端内部项']]
]);

const RACER_TYPES = new Map([
  [1537, '特殊赛车'],
  [1538, '测试/任务赛车'],
  [1539, 'A级赛车'],
  [1540, 'B级赛车'],
  [1541, 'C级赛车'],
  [1542, 'D级赛车'],
  [1543, 'S级赛车'],
  [1544, 'M级赛车'],
  [1545, 'M0级赛车'],
  [1546, 'M1级摩托/赛车'],
  [1547, 'M2级摩托/赛车'],
  [1548, 'M3级摩托'],
  [1549, 'M4级摩托'],
  [1550, 'L0级滑板'],
  [1551, 'L1级滑板'],
  [1552, 'L2级滑板'],
  [1553, 'L3级滑板'],
  [1554, 'L4级滑板'],
  [1555, 'T0机甲赛车'],
  [1556, 'T1机甲赛车'],
  [1557, 'T2机甲赛车'],
  [1558, 'T3机甲赛车'],
  [1559, 'T4机甲赛车'],
  [1560, 'K0级轮滑'],
  [1561, 'K1级轮滑'],
  [1562, 'K2级轮滑'],
  [1565, 'R级赛车'],
  [1567, '特殊机甲赛车']
]);

const gb18030Decoder = new TextDecoder('gb18030');

export function decodeOfficialGender(sexType) {
  if (sexType === 0) return '通用';
  if (sexType === 1) return '男';
  if (sexType === 2) return '女';
  return '未知';
}

export function classifyOfficialItem(itemType, garageType = 0) {
  if (RACER_TYPES.has(itemType)) {
    return { category: '赛车', subcategory: RACER_TYPES.get(itemType) };
  }
  if (itemType === 1566) {
    return { category: '赛车皮肤', subcategory: '赛车皮肤' };
  }

  const direct = TYPE_CLASSIFICATION.get(itemType);
  if (direct) {
    return { category: direct[0], subcategory: direct[1] };
  }

  if ((garageType & 0x00400000) !== 0) {
    return { category: '赛车皮肤', subcategory: '赛车皮肤' };
  }

  if ((garageType & 0x00020000) !== 0) {
    return { category: '宝石', subcategory: '其他宝石材料' };
  }
  if ((garageType & 0x00001000) !== 0) {
    return { category: '水族', subcategory: '其他水族道具' };
  }
  if ((garageType & 0x01000000) !== 0) {
    return { category: '社交展示', subcategory: '其他展示项' };
  }
  if ((garageType & 0x10) !== 0) {
    return { category: '宠物', subcategory: '其他宠物相关' };
  }
  if ((garageType & 0x08) !== 0) {
    return { category: '服饰', subcategory: '其他服饰' };
  }
  if ((garageType & 0x01) !== 0) {
    return { category: '赛车装扮', subcategory: '其他赛车相关' };
  }
  if ((garageType & 0x40) !== 0) {
    return { category: '道具', subcategory: '其他道具' };
  }
  return { category: '系统/其他', subcategory: '未分类' };
}

function readOfficialString(buffer, initialOffset, recordLimit) {
  if (initialOffset + 4 > recordLimit) {
    throw new Error(`Item string length exceeds record boundary at ${initialOffset}`);
  }
  const byteLength = buffer.readInt32LE(initialOffset);
  if (byteLength < 0 || byteLength > MAX_STRING_BYTES) {
    throw new Error(`Invalid item string length ${byteLength} at ${initialOffset}`);
  }
  const dataOffset = initialOffset + 4;
  const nextOffset = dataOffset + byteLength;
  if (nextOffset > recordLimit) {
    throw new Error(`Item string exceeds record boundary at ${initialOffset}`);
  }

  const decoded = Buffer.allocUnsafe(byteLength);
  for (let index = 0; index < byteLength; index += 1) {
    decoded[index] = buffer[dataOffset + index] ^ STRING_XOR_KEY[index % STRING_XOR_KEY.length];
  }
  return {
    value: gb18030Decoder.decode(decoded),
    nextOffset
  };
}

export function parseOfficialItemList(input) {
  const buffer = Buffer.isBuffer(input) ? input : Buffer.from(input);
  if (buffer.length < 4 + ITEM_INDEX_FOOTER_SIZE) {
    throw new Error('Official itemlist is too small');
  }

  const version = buffer.readUInt32LE(0);
  const indexOffset = buffer.readUInt32LE(buffer.length - ITEM_INDEX_FOOTER_SIZE);
  if (indexOffset < 4 || indexOffset + 4 > buffer.length - ITEM_INDEX_FOOTER_SIZE) {
    throw new Error(`Invalid official item index offset ${indexOffset}`);
  }

  const count = buffer.readUInt32LE(indexOffset);
  if (count > MAX_ITEM_COUNT) {
    throw new Error(`Official item count ${count} exceeds the supported limit`);
  }
  const indexEnd = indexOffset + 4 + count * ITEM_INDEX_PAIR_SIZE;
  if (indexEnd > buffer.length - ITEM_INDEX_FOOTER_SIZE) {
    throw new Error('Official item index exceeds file boundary');
  }

  const indexRows = [];
  for (let index = 0; index < count; index += 1) {
    const pairOffset = indexOffset + 4 + index * ITEM_INDEX_PAIR_SIZE;
    const id = buffer.readUInt32LE(pairOffset);
    const recordOffset = buffer.readUInt32LE(pairOffset + 4);
    if (recordOffset < 4 || recordOffset + ITEM_RECORD_HEADER_SIZE > indexOffset) {
      throw new Error(`Invalid record offset ${recordOffset} for item ${id}`);
    }
    indexRows.push({ id, recordOffset });
  }

  const recordLimits = new Map();
  const sortedOffsets = [...indexRows]
    .sort((left, right) => left.recordOffset - right.recordOffset);
  for (let index = 0; index < sortedOffsets.length; index += 1) {
    recordLimits.set(
      sortedOffsets[index].recordOffset,
      index + 1 < sortedOffsets.length ? sortedOffsets[index + 1].recordOffset : indexOffset
    );
  }

  const items = indexRows.map(({ id, recordOffset }) => {
    const decodedId = (buffer.readUInt32LE(recordOffset) ^ ITEM_ID_XOR) >>> 0;
    if (decodedId !== id) {
      throw new Error(`Item ID mismatch at ${recordOffset}: index=${id}, record=${decodedId}`);
    }

    const typeCode = buffer.readUInt32LE(recordOffset + 8);
    const garageType = buffer.readUInt32LE(recordOffset + 20);
    const sexType = buffer.readUInt8(recordOffset + 24);
    const recordLimit = recordLimits.get(recordOffset);
    let stringOffset = recordOffset + ITEM_RECORD_HEADER_SIZE;
    let name = '';
    for (let index = 0; index < 4; index += 1) {
      const decoded = readOfficialString(buffer, stringOffset, recordLimit);
      if (index === 3) name = decoded.value;
      stringOffset = decoded.nextOffset;
    }

    const classification = classifyOfficialItem(typeCode, garageType);
    return {
      id,
      name,
      category: classification.category,
      subcategory: classification.subcategory,
      gender: decodeOfficialGender(sexType),
      typeCode
    };
  });

  return { version, items };
}
