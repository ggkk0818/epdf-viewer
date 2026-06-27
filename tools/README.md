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
- `tf/pdf/example/001.bin`：400×300 测试页

图标格式：4 字节文件头（width u16 LE, height u16 LE）+ 1bpp MSB-first 行序位图数据。
PDF 格式：无文件头，直接 1bpp MSB-first 行序位图（15000 字节）。

把整个 `tf/` 目录的目录结构原样拷贝到 TF 卡根目录即可。
