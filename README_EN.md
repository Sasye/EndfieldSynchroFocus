# Endfield SynchroFocus

English | [中文](README.md)

Splits the default "Short-press for Tactical Skill / Long-press for Ultimate Skill" mechanic in *Arknights: Endfield* into two independent short-press keybinds, allowing for faster and more precise skill execution.

## Installation

Copy the following files to the game directory (same folder as `Endfield.exe`):

```
bin/synchro_focus.dll    → Game Directory/plugin/synchro_focus.dll
synchro_focus_config.txt → Game Directory/plugin/synchro_focus_config.txt
bin/vulkan-1.dll         → Game Directory/vulkan-1.dll
bin/d3dcompiler_47.dll   → Game Directory/d3dcompiler_47.dll
```

> **Note**: `d3dcompiler_47.dll` (DirectX) and `vulkan-1.dll` (Vulkan) are proxy loaders. You only need the one matching your rendering API, or both. If you are using other plugins (like AntiKick or FPS Unlocker), these proxy loaders are shared and do not need to be overwritten if they already exist.

## Configuration

Open `plugin/synchro_focus_config.txt` to customize the **Ultimate Skill** keybinds for characters 1 through 4.
By default, the ultimate skills are bound to the `1`, `3`, `4`, and `5` keys.

**What about Tactical Skills?**
Tactical skills continue to use your **in-game native keybindings** (e.g., `Q` / `2` / `R` / `C`).
Press the native keys to cast tactical skills instantly, and press your `synchro_focus_config.txt` configured keys to cast ultimate skills instantly. No more long-press delays!

## Disclaimer

This project is for educational and research purposes only. Use of this tool may violate the game's Terms of Service and carries a risk of account suspension. Use at your own risk.
