# Endfield SynchroFocus

English | [中文](README.md)

Splits the default "Short-press for Tactical Skill / Long-press for Ultimate Skill" mechanic in *Arknights: Endfield* into two independent short-press keybinds, with customizable Ultimate keys for all four squad slots.

## User Agreement and Disclaimer

<details>
<summary>Please read this agreement carefully before downloading, installing, or using this plugin. <b>By using this plugin, you acknowledge that you have read and fully understood this agreement and agree to comply with all of the following terms.</b></summary>

### 1. Open-Source License and End-User Rights

- This plugin is fully open source on GitHub under the **AGPL-3.0** license. Users may freely use, modify, and distribute its source code in accordance with that license.
- End users may use and distribute this plugin **without any restrictions**, provided they do not modify it. This right is unaffected by whether they violate this agreement.

### 2. Anti-Fraud Statement

- You **must not** openly sell **the plugin software itself** on online marketplaces without providing a link to the GitHub repository and after-sales support.
- This plugin is freely available and fully open source on GitHub. If you paid to obtain it, please be aware that it can be downloaded from GitHub for free.

### 3. Content Compliance and Conduct

- The plugin itself does not contain any game art assets. Users acknowledge and agree that the official animations, scenes, models, and other assets built into *Arknights: Endfield* are entirely copyrighted by Hypergryph and are not covered by AGPL-3.0. You **should not** use this plugin or the game's official assets to create, play, or distribute inappropriate actions or animations, including but not limited to pornographic, violent, politically sensitive, or other content that violates laws and regulations or causes discomfort within the community. Users are responsible for ensuring that they hold the copyright to any imported resources and comply with their licenses and terms of use.

### 4. Risks and Disclaimer

- This project is intended solely for learning, technical research, and discussion. The author is not responsible for how users use this plugin, including any misuse. All Arknights game data assets used in this plugin are copyrighted by Hypergryph. Using this tool may violate the game's Terms of Service and carries a risk of account suspension. **This project accepts no legal or financial liability** for any loss resulting directly or indirectly from the use of this plugin, including but not limited to account suspension or game data corruption. Users assume all risks and are strongly advised to use a test account.

</details>

## Download

Required plugin manager: [Applepie Manager](https://github.com/zonemodAE/ApplepieManager)

Download the latest release from [Releases](https://github.com/Sasye/EndfieldSynchroFocus/releases), or build it yourself from source.

## Installation

First install the required plugin manager, [Applepie Manager](https://github.com/zonemodAE/ApplepieManager), then copy the following file to the game directory (the folder containing `Endfield.exe`):

```
bin/synchro_focus.dll    → Game directory/plugin/synchro_focus.dll
```

> If you **have not installed this type of plugin before**, you may need to create the `plugin` folder yourself.

## Configuration

In the game settings, open **Applepie Manager → Plugins → Configuration** to change the enabled flag, debug logging, and Ultimate keys for slots 1–4. Changes save automatically. Click the current key name and press a key; `Esc` cancels, and Clear disables that shortcut.

New installations and Restore Defaults use `1/3/4/5`. Importing an old configuration preserves its existing bindings; the bundled legacy configuration uses `5/6/7/8`.

- Configuration: `plugin/config/applepie.synchro_focus.ini`. Once this file exists, the legacy configuration is no longer read. Exit the game before editing the file manually, then restart.
- Log: `plugin/synchro_focus_log.txt`.
