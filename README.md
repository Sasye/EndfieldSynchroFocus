# Endfield SynchroFocus

[English](README_EN.md) | 中文

将《明日方舟：终末地》原本的“短按战技 / 长按终结技”的按键绑定拆分为两个独立的短按键，让你能更精准、迅速地释放技能。

## 安装

将以下文件复制到游戏目录（`Endfield.exe` 所在文件夹）：

```
bin/synchro_focus.dll    → 游戏目录/plugin/synchro_focus.dll
synchro_focus_config.txt → 游戏目录/plugin/synchro_focus_config.txt
bin/vulkan-1.dll         → 游戏目录/vulkan-1.dll
bin/d3dcompiler_47.dll   → 游戏目录/d3dcompiler_47.dll
```

> **注意**：`d3dcompiler_47.dll`（DX环境）和 `vulkan-1.dll`（Vulkan环境）为代理加载器，二者放其一或全放均可。如果你同时在使用其他插件（如 AntiKick 或 FPS Unlocker），代理加载器是可以共用的，不需要重复覆盖。

## 配置

打开 `plugin/synchro_focus_config.txt`，你可以为 1~4 号位干员分别自定义**终结技**的专属释放快捷键。
默认情况下，终结技被绑定在键盘的 `1`、`3`、`4`、`5` 上（2号位默认预留给了别的按键所以用了3）。

**战技（Tactical）呢？**
战技继续使用你**在游戏内原生按键设置**里绑定的键（比如 `Q` / `2` / `R` / `C`）。
只要你按原生的键，就会照常释放战技；只要你按 `synchro_focus_config.txt` 里设置的键，就会瞬间释放终结技，再也不需要忍受长按的延迟了！

## 免责声明

本项目仅供学习和研究目的。使用本工具可能违反游戏服务条款，存在账号封禁风险。
请在测试账号上使用，风险自负。
