#pragma once
class QMainWindow;

/* 无边框窗口适配层。
 * UI/业务代码只调这一个纯 Qt 签名的函数，平台差异全部封装在
 * frameless.cpp 内部（mainwindow.cpp 零 ifdef）：
 *   - 通用（纯 Qt）：FramelessWindowHint + startSystemMove 拖拽 +
 *     startSystemResize 边缘缩放 + 双击最大化
 *   - Windows：WS_THICKFRAME|WS_CAPTION 加回（保 DWM 阴影/Aero 贴靠/
 *     任务栏动画）+ WM_NCCALCSIZE 抹掉可见边框 + WM_NCHITTEST 边缘命中 */
namespace Frameless {
void apply(QMainWindow *w);
}
