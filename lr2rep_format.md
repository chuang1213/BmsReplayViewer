# LR2 `.lr2rep` 回放格式 — 分析与实现说明

> 一份可直接照着写解析器的 LR2(LunaticRave2)回放格式文档。
>
> **可信度来源(三方交叉验证)**:
> ① 真实游玩产出的 `.lr2rep` 文件字节(数十个样本,直接读字节统计);
> ② **OpenLR2**(LR2 的开源 C 重写)的 `LR2_replay.cpp` 读写代码;
> ③ 原版 `LR2body.exe` 反汇编(判定窗口 / 时钟)。
> 三者对得上的标【确认】;来自代码、已在样本上验过的标【实测】。

---

## 0. 只想快速写解析器?先记这 4 条

1. **记录是 12 字节 = 3×int32 小端 `[time_ms, op, value]`。** 不是 8 字节。(OpenLR2 函数签名写 `uchar op, short value` 会骗你,实际落盘字段都是 int32。)
2. **randomseed 在 header 里 `op == 200` 那条记录的 `value`。** 没读它就没法做 random 置换 → keysound 乱、音符落错 lane、按键对不上音符。
3. **每个音符的判定真值在 `op == 210`。** 拿它当金标准:你从按键重算的判定,应逐音符等于 op210 序列。对得上 = 你全对。
4. **header 与 body 按 `op` 码切,别按 `time==0` 切**(开局第一个按键也可能在 time==0)。

---

## 1. 整体结构 【确认】

- **无压缩、无加密、无校验和。** 整块读、不解压。
- 文件 = 连续定长记录,**记录数 = 文件字节数 / 12**。
- 每条 = 12 字节 = 3 个小端 int32:

```
偏移   类型      字段       说明
+0     int32 LE  time_ms    毫秒时间戳。header 区全 = 0;body 区单调不减
+4     int32 LE  op         事件码(决定这条是什么)
+8     int32 LE  value      取值(含义随 op 变)
```

- **文件名 = 谱面 md5**(BMS 文件的 md5)。这是 replay ↔ 谱面的唯一关联键;`.lr2rep` 内部没有任何标题/曲名文本。

```c
// 读取
int records = fileSize / 12;
for (int i = 0; i < records; i++) {
    int32 time  = read_le_int32(buf + i*12 + 0);
    int32 op    = read_le_int32(buf + i*12 + 4);
    int32 value = read_le_int32(buf + i*12 + 8);
}
```

---

## 2. 头部 / 正文识别 【确认】

文件 = `[设置头部] + [游玩事件]`:

- **设置头部**:`time_ms == 0`,`op` 是设置码(≥40)。
- **游玩事件**:`time_ms` 单调不减,`op` 是输入(<40)或判定(≥210)。

⚠️ **按 op 码归类,别用 `time==0` 硬切**:开局第一个按键也可能落在 time==0;header 条数也不固定(取决于写了多少设置项,约 50 条)。规则:

- `op < 40` → 输入事件
- `op >= 40` → 设置 / 判定

没有 magic number、没有版本号字段。识别一个 `.lr2rep` 靠:扩展名 + `fileSize % 12 == 0` + 开头一段 `time==0` 设置记录。

---

## 3. op 码总表 【确认】

### 3.1 输入事件(`op < 40`)
```
op    = lane 索引(见 §5)
value = 1 按下 / 0 抬起
```
7K 单打里实际出现的输入 op:`0,1..7,10`(0/10 = 搓盘两个转向;1–7 = 键 1–7)。

### 3.2 randomseed + 模式(最关键)
| op | 含义 | value |
|---|---|---|
| **200** | **randomseed** | 种子(int);每文件恰好 1 条,实测值约 600 ~ 32700 |
| 103 / 153 | P1 / P2 **random 模式** | 0=OFF 1=MIRROR 2=RANDOM 3=S-RANDOM …(2=RANDOM 已确认) |

### 3.3 判定输出(`op 210–217`)
LR2 把判定结果也写进回放(这是你的金标准自检通道):
| op | 含义 |
|---|---|
| **210** | **P1 SP 判定**(7K 单打只会出现这个,**每音符一条**) |
| 211 | P1 DP 判定 |
| 212 | P2 判定 |
| 213 | P2 DP 判定 |
| 214–217 | 地雷(MINE)判定 P1/P1dp/P2/P2dp |

`value` = 判定等级(见 §3.4)。
> 注:个别 LR2 版本/设置的回放里会额外出现一串每音符的 `op==213` 标记(value 多为 5),其确切用途未完全钉死;**解析不需要它**,只用 `op==210` 即可。

### 3.4 判定等级编码(`op 210` 的 value)【确认,三方吻合】
| value | 5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|
| 等级 | PGREAT | GREAT | GOOD | BAD | POOR | 空POOR / MISS |

EXSCORE = `判定数[PGREAT]*2 + 判定数[GREAT]`。

### 3.5 header 设置码(`time==0`)
**P1 用 100–107(0x64–0x6b),P2 用 150–157(0x96–0x9d),成对。**
| op P1 | op P2 | 含义 |
|---|---|---|
| 100 | 150 | hiSpeed(流速) |
| 101 | 151 | gaugeOption(血条) |
| 102 | 152 | lanecover(遮罩) |
| **103** | **153** | **random 模式** |
| 104 | 154 | HID/SUD |
| 105 | 155 | randFix |
| 106 | 156 | randSC(搓盘随机) |
| 107 | 157 | assist |

| op | 含义 |
|---|---|
| **200** | **randomseed** |
| 201 | battle |
| 202 | **isAutoplay**(=1 → 机器人 auto,不是人类游玩) |
| 203 | hsfix |
| 204 | is_extra |
| 205 | m_extra |
| 206 | dpflip |
| 40–43 | 音量:fx_on / master / key / BGM |
| 50–57 | EQ on + eq_gain[0..6] |
| 60–64 / 70–74 / 80–84 | 三组音效 FX(on/type/param×2/channel) |
| 90–92 | pitch:on / amount / type |

---

## 4. randomseed → lane pattern 【高可信,22.8 万种子验证过】

- **RNG = `LR2Random`:MT19937(1998 版)**,所有运算按 32 位:
  - 种子 scramble:`seed = 69069 * seed + 1`(逐格填高 16 位,再 OR 上一次迭代的高 16 位)
  - `nextInt(max) = (uint32(genrand()) * max) >>> 32`(64 位乘取**高 32 位**,**不是取模**)
  - tempering 掩码 `0x9D2C5680` / `0xEFC60000`,移位 11/7/15/18
  - ⚠️ **不是** `java.util.Random`,**不是** MT 2002 版(`init_genrand`)—— 用错序列全乱
- **洗牌**:对 1..L 个可玩键道(7K → L=7,**搓盘不参与置换**)做从左到右的 Fisher–Yates,再**求逆置换**,得到一串数字(每位 = 该显示 lane 对应的原始 lane 号)。
- 模式:OFF=不动;MIRROR=对称翻转(`lane i ↔ L+1-i`,不需 seed);RANDOM=上述;**S-RANDOM=逐行另随机**(不是单一排列,需按行推进 RNG);14K/DP=左右两侧各自洗牌。

伪代码见末尾 §11。

---

## 5. key / lane 映射 【确认结构】

输入事件:`op` = lane 索引,`value` = 1 按下 / 0 抬起。

**`op` 是"换位后的存储索引",不是物理键号。** LR2 内部按键索引与 replay 存的 op 之间会随 `scratchSide`(搓盘在左/右)做换位:

```c
// 读回(value==1 按下,抄自 OpenLR2 ReplayDataToInput)
if (op < 40) {
  if ((scratchSide==1||scratchSide==3) && 1<=op && op<=5)  internalKey = op + 2;
  else if ((scratchSide==1||scratchSide==3) && 6<=op && op<=7)  internalKey = op - 5;
  else if ((scratchSide==2||scratchSide==3) && 21<=op && op<=25) internalKey = op + 2;
  else if ((scratchSide==2||scratchSide==3) && 26<=op && op<=27) internalKey = op - 5;
  else internalKey = op;
}
```

**internalKey → BMS channel**(7K):可玩 channel = `11 12 13 14 15 18 19`(**不连续,跳过 16/17**)+ scratch = `16`。最可能映射(用 §8 自检确认):
`键1→11, 键2→12, 键3→13, 键4→14, 键5→15, 键6→18, 键7→19, 搓盘→16`。

---

## 6. 时间戳 【确认】

- `time_ms` = int32 小端,毫秒,从演奏开始;header=0,body 单调不减。
- **时间基准 = 1ms 高精度时钟(QueryPerformanceCounter),亚帧,绝非帧锁。**
  实测:输入时戳奇偶 ms ≈ 50:50、`t mod 16/17` 残差均匀;若是 60fps 帧锁则只会落在 ~16.67ms 倍数。
- **无额外固定 offset**(判定时刻对最近音符的有符号均值 ≈ 0)。
- 对齐 / 重判定直接用这个 ms 单调时钟,**别用帧序号驱动**。

---

## 7. 判定模型 + 窗口 【确认,原版二进制】

```
gap    = 命中时间 − 音符绝对时间
absgap = abs(gap)            ← 原版用绝对值,early/late 完全对称
升序级联:absgap <= PG窗口 → PGREAT(5);else <= GR → GREAT(4);
          else <= GD → GOOD(3);else <= BD → BAD(2);else <= POOR → POOR(1)
```
窗口按谱面 `#RANK`(song.db 的 judge 列):

| #RANK | PG | GR | GD | BD | POOR |
|---|---|---|---|---|---|
| 0 VERYHARD | 12 | 24 | 60 | 200 | 1000 |
| 1 HARD | 15 | 30 | 80 | 200 | 1000 |
| 2 NORMAL | 18 | 40 | 100 | 200 | 1000 |
| 3 EASY | 21 | 60 | 120 | 200 | 1000 |

> 单位毫秒,来自原版 `LR2body.exe` 反汇编。注意:OpenLR2 重写把 VERYHARD / HARD-GD 写错了,**以上表为准**。

---

## 8. ★ 金标准自检(务必加上)★ 【确认】

回放自带 `op==210` 的逐音符判定真值。用 §7 模型从按键(op<40)重算判定,结果应**逐音符等于** op210 序列。

- **全等 ⇒ 你的解析(lane 映射 + random 置换 + 对齐)全部正确。**
- **对不上的音符 ⇒ 精确指出你哪里错了。**
- 也是反推 §5 lane 映射的最好办法:映射不确定时,试不同映射,选能让"重算判定 == op210"的那个。

---

## 9. keysound 还原 【模型】

回放**不存音**,keysound 要靠谱面 + 输入重建:
```
1. 谱面每个 note 绑定一个 #WAVxx;note 在原始 lane
2. 按 §4 的置换把 note(连同 #WAV)重排到物理 lane
3. replay 按键 (time, 物理lane) → 该 lane 上、判定窗内时间最近的 note → 播它的 #WAV
4. 漏按(超出 POOR 窗没按到)→ 不发声 / 空 POOR
5. 搓盘(op 0/10)对到 scratch 道(channel 16)的 note
```
keysound 正确性 100% 依赖 §5 lane 映射 + §4 置换;先用 §8 自检确认这两项对了,再接 keysound。

---

## 10. 常见解析错误对照

| 错误 | 现象 | 正解 |
|---|---|---|
| 当压缩文件解压 | 失败 / 乱码 | 无压缩,直接读 12B 记录 |
| 记录用 8 字节(被函数签名误导) | header op 乱值、time 跳变 | **12 字节 = 3×int32** |
| 大端读 | 数值全是天文数字 | 小端 |
| 用 time==0 硬切 header | 吞开局按键 / 设置当按键 | 按 op 码切(op<40=输入) |
| **没读 op200 seed、没做 random 置换** | keysound 乱、note 落错 lane、按键对不上 | 读 op200 → LR2Random → 重排谱面 |
| op 当 BMS channel 直接用 | note 丢失/错位(channel 是 11-19 跳着的) | op→内部键→channel(§5) |
| 把 op210 当输入事件 | 多出一堆"按键"、combo 错 | 210 是判定输出,用来自检 |

---

## 11. 实测佐证(一个完整样本)

某 7K RANDOM 谱(1800 音符):
- header:`op103=2`(RANDOM)、`op200=24332`(seed)、`op202=0`(人类游玩)。
- body:`op210` 判定 ≈ 1800 条(完整通关时与音符数几乎 1:1),验证 **op210 = 逐音符判定真值**。
- 判定时刻对谱面最近音符的偏差中位 ≈ 15ms、无系统性 offset → 时间轴对齐无需额外补偿。
- 用 §4 的 LR2Random(seed=24332)算出 7 位置换 → 把谱面 note 重排到物理 lane → 按 op<40 输入逐键查最近 note → keysound 从头到尾正确,§8 自检逐音符吻合。

---

## 12. 参考伪代码:LR2Random + lane pattern

```js
// MT19937(1998 版),32 位语义。逢大整数语言每步掩码 |0 / >>>0。
const N=624, M=397, MATRIX_A=0x9908B0DF;       // 有符号 int32 即 -1727483681
function setSeed(seed){
  for(let i=0;i<624;){
    mt[i] = (seed & 0xFFFF0000)|0;
    seed = (Math.imul(69069,seed)+1)|0;          // 32 位回绕
    mt[i] = (mt[i] | ((seed & 0xFFFF0000)>>>16))|0;  i++;
    seed = (Math.imul(69069,seed)+1)|0;
  }
  generateMT();
}
function generateMT(){                            // 一次性算满 624 个 + tempering
  const mag01=[0,MATRIX_A]; let y,kk;
  for(kk=0;kk<N-M;++kk){ y=((mt[kk]&0x80000000)|(mt[kk+1]&0x7FFFFFFF))|0;
    mt[kk]=(mt[kk+M]^(y>>>1)^mag01[y&1])|0; }
  mt[624]=mt[0];
  for(;kk<624;++kk){ y=((mt[kk]&0x80000000)|(mt[kk+1]&0x7FFFFFFF))|0;
    mt[kk]=(mt[kk-(N-M)]^(y>>>1)^mag01[y&1])|0; }
  for(kk=0;kk<624;++kk){ y=mt[kk];
    y^=y>>>11; y^=(y<<7)&0x9D2C5680; y^=(y<<15)&0xEFC60000; y^=y>>>18;
    mtr[kk]=y|0; }
  mti=0;
}
function randMT(){ if(mti>=624) generateMT(); return mtr[mti++]|0; }
function nextInt(max){ const r=randMT()>>>0;            // 当成 64 位无符号
  return Number((BigInt(r)*BigInt(max))>>32n); }        // 取高 32 位

// seed -> 显示排列(L = 键道数,7K→7,搓盘不参与)
function lr2LanePattern(seed, L){
  setSeed(seed|0);
  const a=[], inv=[];
  for(let i=0;i<=L;++i) a[i]=i;                          // 恒等,1-indexed
  for(let i=1;i<L;++i){ const j=i+nextInt(L-i+1);        // 从左 Fisher–Yates
    const t=a[i]; a[i]=a[j]; a[j]=t; }
  for(let i=1;i<=L;++i) inv[a[i]]=i;                     // 求逆置换
  let s=""; for(let i=1;i<=L;++i) s+=String.fromCharCode(48+inv[i]);
  return s;                                              // 例 "3271645"
}
```

---

*本文基于对原版 `LR2body.exe` + 真实 `.lr2rep` 字节 + OpenLR2 源码的交叉逆向整理。`op210` 金标准自检是验证一切的最快路径。*
