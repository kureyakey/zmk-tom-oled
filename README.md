# zmk-tom-oled

`zmk-tom-oled` は、128x32 の横向き OLED 向け ZMK display shield です。

central 側は `zmk-dongle-display` のレイアウトをほぼ維持します。

- output status
- WPM bongo cat
- active modifiers
- highest active layer
- peripheral battery levels

peripheral 側は、split peripheral 上で取得できる範囲の情報を横向きで表示します。

- central との接続状態
- key press activity
- trackball activity animation
- disconnected animation

## Usage

`config/west.yml` にこの module を追加してから `west update` します。

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: pukuhei
      url-base: https://github.com/pukuhei
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-tom-oled
      remote: pukuhei
      revision: main
  self:
    path: config
```

左右両方の build target の shield list に `tom_oled` を追加します。

```yaml
---
include:
  - board: seeeduino_xiao_ble
    shield: your_keyboard_left tom_oled
  - board: seeeduino_xiao_ble
    shield: your_keyboard_right tom_oled
```

## Configuration

peripheral battery に加えて central/dongle の battery も表示します。

```conf
CONFIG_ZMK_TOM_OLED_DONGLE_BATTERY=y
```

macOS modifier symbols を使います。

```conf
CONFIG_ZMK_TOM_OLED_MAC_MODIFIERS=y
```

## Notes

この module は初期実装として `zmk-dongle-display` をベースにしており、central
側の status screen は意図的に近い構成にしています。peripheral 側は ZMK の
endpoint、layer、central 側 split battery 情報を同じようには持てないため、
接続状態、キー入力、トラックボール入力のような peripheral 側で取得できる情報に
絞った別 widget として実装しています。
