# 吉里吉里 SDL2 / Kirikiri SDL2

## 项目说明 / About this fork

本项目 fork 自 [krkrsdl2/krkrsdl2](https://github.com/krkrsdl2/krkrsdl2)，在原项目基础上修改而来，主要新增了 **Emscripten/WASM 浏览器部署**（游戏库主页 + 游玩页 + Python 服务端）。

> **声明**：本仓库的修改部分由 AI（Claude）辅助完成。

- 上游原项目：https://github.com/krkrsdl2/krkrsdl2
- 本仓库：https://github.com/q1096247897-star/krkrsdl2_web
- 浏览器部署文档见 [web-deploy-README.md](web-deploy-README.md) 与 [docker/README.md](docker/README.md)

---

吉里吉里 SDL2 是 [吉里吉里 Z](https://krkrz.github.io/) 的移植版，可在支持 [SDL2](https://www.libsdl.org/) 的平台上运行，例如 macOS、Linux，以及通过 Emscripten/WASM 在浏览器中运行。

详情请访问：https://krkrsdl2.github.io/krkrsdl2/

---

吉里吉里SDL2は、macOSやLinuxなど、[SDL2](https://www.libsdl.org/)をサポートするプラットフォームで実行できる[吉里吉里Z](https://krkrz.github.io/)の移植版です。

詳細については、次の Web ページを参照してください: https://krkrsdl2.github.io/krkrsdl2/

---

Kirikiri SDL2 is a port of [Kirikiri Z](https://krkrz.github.io/) that can be run on platforms supporting [SDL2](https://www.libsdl.org/), such as macOS and Linux.

Please visit the following webpage for more information: https://krkrsdl2.github.io/krkrsdl2/en/

## 运行商业游戏的注意事项 / 商用ゲームの実行に関する注意 / A note on running commercial games

本项目不支持运行未修改的商业游戏。请改用 [Wine](https://www.winehq.org/) 或 [Kirikiroid2](https://play.google.com/store/apps/details?id=org.tvp.kirikiri2)。

---

このプロジェクトを使用して変更されていない商用ゲームを実行することはサポートされていません。
代わりに[Wine](https://www.winehq.org/)または[Kirikiroid2](https://play.google.com/store/apps/details?id=org.tvp.kirikiri2)を使用してください。

---

Running unmodified commercial games using this project is not supported.
Please use [Wine](https://www.winehq.org/) or [Kirikiroid2](https://play.google.com/store/apps/details?id=org.tvp.kirikiri2) instead.

## 许可证 / ライセンス / License

吉里吉里 SDL2 源代码（`src` 目录内）在 MIT 许可证下授权。详情请阅读 `LICENSE`。
本项目包含第三方组件（未在 GPL 下授权），详情请参阅各组件的许可证文件。

---

吉里吉里SDL2ソース（`src`ディレクトリ内）のコードは、MITライセンスの下でライセンスされています。 詳細については、`LICENSE`をお読みください。
このプロジェクトには、サードパーティのコンポーネントが含まれています (GPL に基づいてライセンスされていません)。詳細については、各コンポーネントのライセンスファイルを参照してください。

---

The code of the Kirikiri SDL2 source (inside the `src` directory) is licensed under the MIT license. Please read `LICENSE` for more information.
This project contains third-party components (not licensed under the GPL). Please view the license file in each component for more information.
