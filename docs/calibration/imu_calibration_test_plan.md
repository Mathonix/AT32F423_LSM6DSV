# IMU 六轴 / 九轴数据采集与校准测试计划

> 适用项目：AT32F423KCU7 + LSM6DSV + IST8310
>
> 目标：明确需要采集的原始数据、需要计算的校准参数、测试步骤、判定指标和 Flash 保存内容。
>
> 更新日期：2026-09-14

## 1. 术语和总体原则

### 1.1 六轴与九轴

- **六轴模式**：三轴陀螺仪 + 三轴加速度计。
- **九轴模式**：六轴 IMU + 三轴磁力计。
- 传感器物理校准、VQF 零偏估计和输出端 KF/AEKF 是三件不同的事情，不应混为一谈。

### 1.2 校准处理顺序

建议数据处理顺序如下：

```text
原始 ADC/寄存器数据
    ↓
轴方向映射
    ↓
传感器 bias 校准
    ↓
scale 或 3×3 矩阵校准
    ↓
温度补偿（如果已建立模型）
    ↓
VQF 融合
    ↓
输出端 KF/AEKF（只改善输出噪声）
```

### 1.3 重要原则

- 静置数据可以自动生成零偏、噪声和阈值建议。
- 低通截止频率、VQF 时间常数和动态 KF 参数不能只靠静置数据决定，必须配合运动测试。
- 不要把未通过质量检查的运行时 VQF `bias_x/y/z` 直接保存为长期参数。
- Flash 写入应采用 CRC、版本号和受控写入策略，避免频繁擦写。

---

## 2. 所有测试都应记录的通用字段

每条数据建议至少包含：

| 类别 | 字段 |
|---|---|
| 时间 | `timestamp_ms`、采样序号、实际采样间隔 |
| 温度 | `temperature_c`、温度原始值 |
| 陀螺仪原始值 | `gyr_raw_x/y/z` |
| 陀螺仪物理值 | `gx/gy/gz_dps` |
| 陀螺仪滤波值 | `gyr_lpf_x/y/z` |
| 加速度原始值 | `acc_raw_x/y/z` |
| 加速度物理值 | `ax/ay/az_g` 或 `m/s²` |
| 加速度模长 | `acc_norm_g` |
| 磁力计原始值 | `mag_raw_x/y/z` |
| 磁力计校准值 | `mag_cal_x/y/z` |
| 磁场模长 | `mag_norm` |
| 融合结果 | `yaw`、`pitch`、`roll` |
| VQF 状态 | `bias_x/y/z`、`rest_detected`、`rest_time` |
| 磁融合状态 | `mag_valid`、`mag_disturbed`、`mag_updates` |
| 输出滤波状态 | `KF_yaw`、`KF innovation`、`Q`、`R` |

推荐同时保存一份**完整原始 CSV**，不要只保存处理后的姿态角。

---

## 3. 传感器配置记录

每次正式校准前，必须记录当前硬件配置，否则不同配置下的数据不能直接比较。

```text
芯片型号和芯片 ID
固件版本 / 校准版本
MCU 主频
IMU ODR
陀螺仪量程
加速度计量程
磁力计 ODR
SPI 时钟频率
I2C 时钟频率
数字 LPF 配置
软件 LPF 截止频率
VQF 参数
KF/AEKF 参数
板卡安装方向
电源和外设状态
```

当前项目重点记录：

```c
APP_FUSION_HZ
APP_GYR_LPF_CUTOFF_HZ
APP_VQF_REST_GYR_DPS
APP_VQF_REST_ACC_MS2
APP_VQF_BIAS_SIGMA_REST_DPS
APP_VQF_BIAS_FORGETTING_TIME_S
APP_VQF_MOTION_BIAS_ENABLE
APP_MAG_FUSION_ENABLE
APP_MAG_VQF_UPDATE_DIV
```

---

# 4. 六轴：陀螺仪测试与校准

## 4.1 静置零偏采集

### 目的

获得启动时的三轴陀螺仪零偏，并验证当前噪声水平。

### 测试条件

```text
设备放在稳定平面
不要触碰设备和连接线
LED、USB、CAN 状态保持与实际工作状态一致
记录温度
建议采集 60 秒
```

### 计算参数

对每一轴计算：

```text
bias_x/y/z = mean(gx/gy/gz)
std_x/y/z
RMS_x/y/z
min_x/y/z
max_x/y/z
peak_to_peak_x/y/z
```

去除零偏后的噪声为：

```text
noise = gyro - bias
```

### 输出结果

```text
gyro_bias_dps[3]
gyro_std_dps[3]
gyro_rms_dps[3]
gyro_peak_to_peak_dps[3]
temperature_mean_c
temperature_min_c
temperature_max_c
```

### 判定项目

- `gx/gy/gz` 均值是否稳定；
- 三轴标准差是否有明显差异；
- 是否存在周期性尖峰；
- WS2812、USB、CAN 开启后噪声是否增加；
- 温度是否仍在快速变化；
- 静止时 `bias_z` 是否持续单向漂移。

---

## 4.2 启动零偏校准验证

当前启动流程建议验证：

```text
上电等待 1 秒
静置采集约 3 秒
计算三轴 gyro bias
将 bias 注入 VQF
```

需要同时记录：

```text
本次启动 bias
历史回退 bias
当前温度
VQF 初始 bias
VQF 稳定后的 bias
```

验证重点：

- 本次校准成功时是否使用本次值；
- 校准失败时是否按温度匹配历史值；
- Flash 无历史值时是否回退默认值；
- 本次校准失败是否被正确指示；
- VQF 初始化后是否重复错误叠加 bias。

---

## 4.3 陀螺仪长时间温漂测试

### 目的

建立零偏随温度变化的关系。

### 推荐测试

```text
设备静止 30 分钟～60 分钟
记录 gyro、temperature、VQF bias
每秒或每 100 ms 保存一次统计结果
```

更高质量的温度标定可以使用多个稳定温度点：

```text
约 10 ℃
约 25 ℃
约 40 ℃
约 50 ℃
```

每个温度点热稳定后静置 1～5 分钟。

### 拟合参数

一阶模型：

```text
bias_x(T) = b0_x + k_x × (T - T0)
bias_y(T) = b0_y + k_y × (T - T0)
bias_z(T) = b0_z + k_z × (T - T0)
```

保存：

```text
reference_temperature_c
bias0[3]
temperature_slope[3]
拟合残差
```

如果拟合残差较大，再考虑二阶模型或温度查表。

---

## 4.4 陀螺仪比例因子和轴向误差

### 比例因子

需要精密转台或已知角速度源。建议测试：

```text
+90 dps
-90 dps
+180 dps
-180 dps
```

计算：

```text
gyro_scale = theoretical_rate / measured_rate
```

### 轴间不正交

如果设备需要高精度，应拟合：

```text
gyro_corrected = M_gyr × (gyro_raw - bias_gyr)
```

其中 `M_gyr` 是 3×3 矩阵。

没有精密转台时，暂时只做：

```text
零偏校准
温度补偿
软件低通
```

不要使用手工旋转数据强行拟合完整陀螺仪矩阵。

---

## 4.5 陀螺仪噪声自动统计

MCU 可以在线使用 Welford 算法统计：

```text
样本数量 n
均值 mean
M2
最小值 min
最大值 max
```

最终计算：

```text
variance = M2 / (n - 1)
std = sqrt(variance)
peak_to_peak = max - min
```

不必保存全部样本即可生成基础噪声参数。

建议 MCU 输出以下建议值，但不要未经确认直接覆盖配置：

```text
suggested_rest_gyr_dps
suggested_rest_acc_ms2
suggested_kf_r_rest
noise_quality_score
```

---

# 5. 六轴：加速度计测试与校准

## 5.1 六面静态校准

### 六个姿态

```text
+X 朝上
-X 朝上
+Y 朝上
-Y 朝上
+Z 朝上
-Z 朝上
```

每个面需要：

```text
自动判断静止
连续稳定一段时间
采样 0.5～1 秒
记录原始加速度和温度
```

每个姿态记录：

```text
raw_ax/raw_ay/raw_az
ax/ay/az_g
acc_norm_g
gyr_x/y/z
temperature_c
sample_count
mean
std
```

### 轴对齐 bias/scale 拟合

```text
bias_x = (x_plus + x_minus) / 2
scale_x = 2 / (x_plus - x_minus)

bias_y = (y_plus + y_minus) / 2
scale_y = 2 / (y_plus - y_minus)

bias_z = (z_plus + z_minus) / 2
scale_z = 2 / (z_plus - z_minus)
```

校准公式：

```text
acc_corrected = (acc_raw - bias) × scale
```

### 判定指标

校准后检查：

```text
每个面 acc_norm 是否接近 1g
静止时各轴标准差
六面拟合残差
bias 和 scale 是否处于合理范围
```

如果需要补偿轴间不正交或安装误差，继续采集 20～100 个均匀分布姿态，拟合：

```text
acc_corrected = M_acc × (acc_raw - bias_acc)
```

---

# 6. 六轴融合测试

## 6.1 静置姿态稳定性

测试至少：

```text
静置 1 分钟
静置 5 分钟
静置 30 分钟
```

记录：

```text
yaw / pitch / roll
bias_x/y/z
rest_detected
rest_time
temperature
```

重点观察：

- pitch/roll 是否回到稳定值；
- yaw 是否单向漂移；
- 设备静止后 `bias_z` 是否继续变化；
- 温度变化是否与 yaw 漂移相关；
- VQF 拒绝运动状态时是否错误更新 bias。

## 6.2 静止到快速旋转

测试动作：

```text
静止 5 秒
快速绕 Z 轴旋转
停止并保持静止 5 秒
重复 5～10 次
```

记录：

```text
vqf_yaw
KF_yaw
gz
bias_z
rest_detected
rest_time
KF innovation
```

检查：

- 快速旋转时 yaw 是否跟手；
- 停止后是否出现 0.1～0.3° 的迟滞或跳变；
- `bias_z` 是否在运动阶段变化；
- KF 是否过度平滑；
- `rest_detected` 是否提前触发。

---

# 7. 九轴：磁力计测试与校准

## 7.1 磁力计原始数据采集

必须记录：

```text
mag_raw_x/y/z
mag_cal_x/y/z
mag_norm
mag_temperature
mag_valid
mag_disturbed
mag_updates
```

同时记录：

```text
yaw
pitch
roll
```

建议使用多姿态采集，而不是只做水平旋转。

---

## 7.2 磁力计坐标轴映射

先确认 IST8310 到 LSM6DSV/机体坐标系的映射：

```text
IST X → 机体哪个轴，是否取反
IST Y → 机体哪个轴，是否取反
IST Z → 机体哪个轴，是否取反
```

可以使用：

```text
轴交换
正负号变换
3×3 轴映射矩阵
```

轴映射错误时，即使硬铁和软铁校准正确，yaw 仍然会异常。

---

## 7.3 磁力计硬铁校准

### 数据采集

设备在无明显磁干扰的环境中做完整三维旋转，覆盖：

```text
水平旋转
俯仰旋转
滚转旋转
多个空间方向
```

### 计算

硬铁偏置近似为磁场点云的球心偏移：

```text
mag_corrected = mag_raw - hard_iron_bias
```

保存：

```text
hard_iron_bias[3]
```

---

## 7.4 磁力计软铁校准

如果校准后磁场点云仍为椭球，需要拟合：

```text
mag_corrected = M_mag × (mag_raw - hard_iron_bias)
```

其中：

```text
M_mag 为 3×3 软铁校准矩阵
```

最低限度可以先使用三轴 scale，但高精度九轴模式建议使用完整矩阵。

---

## 7.5 跨姿态磁场模长验证

校准后计算：

```text
mag_norm = sqrt(mx² + my² + mz²)
```

判定：

```text
不同姿态下 mag_norm 应基本稳定
旋转过程中不应频繁大幅跳变
```

如果波动明显，检查：

```text
硬铁校准
软铁校准
轴映射
附近金属或磁性器件
供电电流变化
电机、扬声器、磁铁
```

---

# 8. 九轴 VQF 融合测试

## 8.1 磁融合开启前

确认：

```text
六轴加速度校准已完成
陀螺仪零偏稳定
磁力计轴映射正确
硬铁/软铁校准已加载
mag_norm 稳定
```

## 8.2 磁拒绝测试

记录：

```text
mag_valid
mag_disturbed
mag_updates
vqf_yaw
bias_z
```

故意靠近磁性物体，检查：

```text
磁场异常时是否停止磁更新
yaw 是否不会突然大幅跳变
九轴状态灯是否切换到拒磁提示
磁场恢复后是否重新融合
```

## 8.3 纯 Z 轴旋转测试

步骤：

```text
设备水平放置
绕 Z 轴旋转 360°
停止后保持 5～10 秒
再转回起始方向
```

检查：

```text
yaw 是否连续
yaw 正负方向是否正确
转回原方向的误差
磁场模长是否稳定
是否频繁拒磁
```

---

# 9. VQF、KF 和输出参数的调校范围

## 9.1 VQF 参数

| 参数 | 作用 | 推荐调节方法 |
|---|---|---|
| `APP_VQF_TAU_ACC` | 加速度计姿态修正时间常数 | 用动态倾斜和线性加速度测试 |
| `APP_VQF_TAU_MAG` | 磁力计航向修正时间常数 | 用纯 Z 轴旋转和磁干扰测试 |
| `APP_VQF_REST_GYR_DPS` | 静止检测阈值 | 用静态噪声和轻微振动测试 |
| `APP_VQF_REST_ACC_MS2` | 加速度静止阈值 | 用加速度模长统计生成建议值 |
| `APP_VQF_REST_MIN_SECONDS` | 连续静止确认时间 | 防止刚停止就更新 bias |
| `APP_VQF_BIAS_SIGMA_REST_DPS` | 静止 bias 更新信任程度 | 观察 bias 收敛和噪声追踪 |
| `APP_VQF_BIAS_FORGETTING_TIME_S` | 长期 bias 变化速度 | 需要长时间温漂数据 |
| `APP_VQF_MOTION_BIAS_ENABLE` | 运动时是否估计 bias | 必须通过动态测试决定 |

## 9.2 输出端 KF/AEKF 参数

静置数据可以估计：

```text
R_rest ≈ var(yaw_noise)
```

但运动状态的 `Q`、`R` 需要结合：

```text
快速旋转
突然停止
振动
线性加速度
磁干扰
```

KF 只能降低输出噪声，不能修复：

```text
真实零偏变化
温度漂移
磁力计轴映射错误
加速度计校准错误
```

---

# 10. 建议的 Flash 校准数据结构

六轴基础版本建议保存：

```c
typedef struct
{
    float gyro_bias_dps[3];
    float gyro_temp_coeff_dps_per_c[3];
    float acc_bias_g[3];
    float acc_scale[3];
    float calibration_temperature_c;
    uint32_t calibration_count;
    uint32_t version;
    uint32_t crc;
} imu6_calibration_t;
```

九轴扩展版本增加：

```c
typedef struct
{
    float mag_hard_iron[3];
    float mag_soft_iron[3][3];
    float mag_to_imu[3][3];
    float magnetic_declination_deg;
} mag_calibration_t;
```

保存策略：

```text
本次校准成功且质量合格后才写入
写入前校验范围
使用 magic/version/CRC
保存失败时保留旧记录
限制 Flash 擦写次数
```

---

# 11. 测试结果记录模板

每次测试建议记录：

```text
测试日期：
固件版本：
硬件版本：
IMU 序列号：
供电方式：
USB/CAN/WS2812 状态：
环境温度：
传感器量程：
ODR：
LPF：
VQF 配置：
KF 配置：

测试项目：
采集时长：
动作说明：
原始数据文件：
分析脚本：

结果：
校准是否成功：
质量评分：
gyro bias：
acc bias：
acc scale：
mag hard iron：
mag soft iron：
温漂结果：
yaw 漂移：
pitch 漂移：
roll 漂移：
是否写入 Flash：
存在的问题：
下一步：
```

---

# 12. 推荐执行顺序

```text
1. SPI/I2C 通信和 WHO_AM_I 检查
2. 记录当前传感器配置
3. 陀螺仪 60 秒静置噪声统计
4. 上电 1 秒 + 静置 3 秒校准验证
5. 陀螺仪 30 分钟温漂测试
6. 加速度计自动六面校准
7. 六轴静置和快速旋转验证
8. 确认 IST8310 坐标轴映射
9. 磁力计多姿态原始数据采集
10. 硬铁校准
11. 软铁/椭球校准
12. 跨姿态磁场模长验证
13. 开启九轴 VQF
14. 纯 Z 轴旋转测试
15. 磁拒绝和磁干扰测试
16. 最后再调 VQF 和 KF 参数
```

## 最终判定原则

```text
先修正原始传感器数据
再验证六轴姿态
再校准磁力计
最后开启九轴融合
最后调输出滤波
```

不要通过增大 KF 平滑来掩盖：

```text
陀螺仪零偏错误
温度漂移
加速度计 bias/scale 错误
磁力计轴映射错误
磁场干扰
```

