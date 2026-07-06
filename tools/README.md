# EPDF Viewer 工具脚本

## 环境安装（仅首次）

需要 Python 3.9+，已存在的 platformio 自带 Python 3.11 可用：

```bash
/c/Users/ggkk2/.platformio/python3/python.exe -m pip install --upgrade pip
/c/Users/ggkk2/.platformio/python3/python.exe -m pip install cairosvg pillow
```

如果系统已装 Python 3，直接：

```bash
pip install cairosvg pillow
```

> Windows 上 `cairosvg` 还依赖 GTK runtime（用于 cairo 库）。如果安装失败，可改用 `svglib + reportlab`：
>
> ```bash
> pip install svglib reportlab pillow
> ```
>
> 然后修改 `tools/svg_to_bin.py` 中的 `render_svg_to_rgba` 函数，改用 `svglib.svglib.svg2rlg` + `renderPM.drawToFile`。

## 使用

在项目根目录执行：

```bash
# 全部生成（图标 + 测试 PDF 页）
python tools/svg_to_bin.py all

# 仅生成图标（tf/sys/icon/*.bin）
python tools/svg_to_bin.py icons

# 仅生成测试 PDF 页（tf/pdf/example/001.bin）
python tools/svg_to_bin.py pdf
```

## 输入

- 图标源 SVG：`C:/Users/ggkk2/Downloads/{doc,bluetoothon,bluetooth_on,bluetooth_off,battery (1),settings}.svg`
- PDF 测试页：脚本内置生成的占位内容（边框+标题条+文字行模拟）

## 输出

- `tf/sys/icon/{doc,ble,settings}.bin`：48×48 图标
- `tf/sys/icon/{ble_on,ble_off,battery}.bin`：16×16 状态栏图标
- `tf/pdf/example/001.bin`：300×380 测试页（与设备视口一致）

## 文件格式

位图位序约定（所有格式通用）：**1bpp，MSB-first，行序**；bit=1 表示黑色像素（`GxEPD_BLACK`），bit=0 表示白色。

### 图标格式（4 字节文件头）

```
偏移  长度  字段
0     u16   width  (LE)
2     u16   height (LE)
4     ...   1bpp MSB-first row-major bitmap
```

### PDF 页格式（8 字节文件头）

```
偏移  长度  字段
0     u8    magic     = 0xE5
1     u8    version   = 0x01
2     u16   width     (LE)
4     u16   height    (LE)
6     u16   reserved  = 0（留作未来扩展）
8     ...   1bpp MSB-first row-major bitmap
```

设备端读取（`src/modules/PdfStore.cpp`）流程：

1. 读取 8 字节文件头，校验 magic=0xE5 / version=0x01。
2. 取出 `width × height` 作为页面位图尺寸。
3. 与设备视口 `cfg::display::CONTENT_W × CONTENT_H`（300×380）比较：
   - 页面 ≥ 视口：**裁剪左上角**，逐行从 SD 读取仅覆盖视口的字节范围，多余部分不读取也不渲染。
   - 页面 < 视口：**居中显示**，按页面尺寸读取完整内容，余白填白。
4. 视口缓冲区填好后，由 `DocViewPage::render` 直接 `drawBitmap` 一次渲染。

页宽上限 ≈ 2048 像素（`MAX_PAGE_ROW_BYTES = 256`），超过会被拒绝。

> ⚠️ 新格式不再兼容历史的无文件头 PDF bin。如果你仍持有旧版的 `001.bin`（15000 字节、400×300、无文件头），请用新版脚本重新生成。

把整个 `tf/` 目录的目录结构原样拷贝到 TF 卡根目录即可。
