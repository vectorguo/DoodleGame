# IOS

预留目录，尚未实现。

一个平台要在这里落地需要三样东西：

1. 一个 `DoodleWindow` 子类 —— 实现 `../DoodleWindow.h` 的六个纯虚函数
2. 一个 `DoodleApplication` 子类 —— 回答「窗口从哪来」，即实现 `CreateWindow`。
   iOS 与 Android 同类（窗口会消失又回来），所以三个生命周期函数也要覆盖，
   形式对标 `../Android/DoodleAndroidApplication.h`；
   窗口与程序同寿的平台用基类的默认实现即可，`../MacOS/` 那份是例子
3. 一个入口函数 —— 形式对标 `../Android/DoodleAndroidMain.cpp`

三样都还没有，所以这里是空的。

本文件的作用只有一个：让 git 保留这个空目录（git 不跟踪空目录）。
