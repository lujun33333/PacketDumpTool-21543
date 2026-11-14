# MsgID 223 / CSSafeCNotifyLoginMoreInfoPara alignment report

- dump root: `F:\PacketDumpTool\PacketDumpTool\x64\Release`
- all msg223 payloads: `725`
- recv_plain_full samples: `129`
- unique recv_plain_full samples: `66`
- body slice samples: `258`
- full[0x0E:] matched body slice samples: `66`

## Fixed expanded layout

| Name | Offset | Size | Type | Note |
|---|---:|---:|---|---|
| `m_ushItemNum` | `0x00000000` | `2` | `le:u16` | active item count |
| `m_astPlayerDBItemInfo[]` | `0x00000002` | `185150` | `array[8050] stride=23` | fixed item slots |
| `m_ushTaskNum` | `0x0002D340` | `2` | `le:u16` | active task count |
| `m_astPlayerDBTaskInfo[]` | `0x0002D342` | `26400` | `array[1100] stride=24` | fixed task slots |
| `m_usPetNum` | `0x00033A62` | `2` | `le:u16` | active pet count |
| `m_astPetInfo[]` | `0x00033A64` | `95500` | `array[500] stride=191` | fixed pet slots |
| `m_usMapNum` | `0x0004AF70` | `2` | `le:u16` | active map count |
| `m_astRecord[]` | `0x0004AF72` | `6000` | `array[500] stride=12` | fixed map records |
| `m_uchOverdueItemNum` | `0x0004C6E2` | `1` | `u8` | active overdue item count |
| `m_astOverdueItem[]` | `0x0004C6E3` | `1200` | `array[100] stride=12` | fixed overdue item slots |
| `m_iStoneKartNum` | `0x0004CB93` | `4` | `le:u32` | active stone-kart count |
| `m_astKartStoneGrooveInfo[]` | `0x0004CB97` | `386400` | `array[8050] stride=48` | fixed stone kart slots |
| `m_bHasNext` | `0x000AB0F7` | `1` | `u8` | more chunks flag |
| `DecodeCheckField` | `0x000AB0F8` | `4` | `le:u32` | special28346 decode check field; semantic not business |

## Count distributions

- `item_num`: `{198: 1, 200: 57, 199: 6, 99: 2}`
- `task_num`: `{0: 66}`
- `pet_num`: `{0: 57, 5: 9}`
- `map_num`: `{0: 66}`
- `overdue_num`: `{0: 66}`
- `stone_num`: `{0: 66}`
- `has_next`: `{0: 12, 1: 54}`
- `check_field`: `{'0x3A3A7C8A': 13, '0x3A3A67DE': 8, '0x3A3A5F62': 13, '0x3A3A4AD6': 11, '0x3A3ABD66': 8, '0x3A3A2A26': 8, '0x3A3A25BE': 5}`

## Evidence notes

- `recv_plain_full` / `special28346_after_full` length `700668` is the authoritative expanded full layout.
- `recv_plain_body` / `special28346_after_body` length `700654` equals `full[0x0E:]`; for Msg223 special decode this is a truncated view, not the structure start.
- Field order and fixed offsets are backed by `F:\new1\dll\new\21273\Network.dll.i64` export `sub_18032F740`; old server comments provide the task/map/overdue/stone field names.
- Unknown semantic tail fields are intentionally marked `UnknownName`; no names are invented from zero values.
