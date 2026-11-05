# stb_image

- 版本：stb_image **v2.30**
- 来源：<https://github.com/nothings/stb>（2026-10-31 经 jsdelivr 镜像取得）
- 许可：public domain / MIT 双许可，全文在 `stb_image.h` 文件头
- 用途：`DoodleTextureLoader` 解码 TGA / PNG / JPG 像素

## 接入方式

- `stb_image.h`：只放声明，任何需要解码的文件包含它即可。
- `stb_image_impl.cpp`：唯一定义 `STB_IMAGE_IMPLEMENTATION` 的编译单元，
  避免同一份实现被多个翻译单元重复定义。
- 包含路径走 `Engine/ThirdParty`（CMake 已加），写作 `<stb/stb_image.h>`。

## 升级

替换 `stb_image.h` 整个文件即可；实现单元不需要改。新版本若改了宏名或
API 签名，先看文件头说明再改 `stb_image_impl.cpp` 与 loader。
