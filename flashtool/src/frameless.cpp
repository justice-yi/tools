#include "frameless.h"
#include <QEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QWindow>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <windows.h>
#include <windowsx.h>

/* ---- Windows：抹掉系统边框但保留厚框行为 ---- */
class WinFramelessFilter : public QAbstractNativeEventFilter {
public:
	explicit WinFramelessFilter(HWND hwnd) : m_hwnd(hwnd) {}

	bool nativeEventFilter(const QByteArray &ev, void *msg,
			       qintptr *res) override
	{
		Q_UNUSED(ev);
		MSG *m = static_cast<MSG *>(msg);
		if (m->hwnd != m_hwnd)
			return false;
		switch (m->message) {
		case WM_NCCALCSIZE:
			if (m->wParam) {
				if (IsZoomed(m->hwnd)) {
					/* 最大化时系统把窗口扩出屏幕一个
					 * 边框量，补回免得四边内容被裁 */
					auto *p = reinterpret_cast<
						NCCALCSIZE_PARAMS *>(m->lParam);
					int fx = GetSystemMetrics(SM_CXFRAME)
						+ GetSystemMetrics(
							  SM_CXPADDEDBORDER);
					int fy = GetSystemMetrics(SM_CYFRAME)
						+ GetSystemMetrics(
							  SM_CXPADDEDBORDER);
					p->rgrc[0].left += fx;
					p->rgrc[0].top += fy;
					p->rgrc[0].right -= fx;
					p->rgrc[0].bottom -= fy;
				}
				*res = 0;	/* 客户区铺满整个窗口 */
				return true;
			}
			break;
		case WM_NCHITTEST: {
			if (IsZoomed(m->hwnd))
				break;			/* 最大化无边可拉 */
			POINT pt{ GET_X_LPARAM(m->lParam),
				  GET_Y_LPARAM(m->lParam) };
			ScreenToClient(m->hwnd, &pt);
			RECT rc;
			GetClientRect(m->hwnd, &rc);
			const int e = 8;		/* 边缘热区 */
			bool L = pt.x < e, R = pt.x >= rc.right - e;
			bool T = pt.y < e, B = pt.y >= rc.bottom - e;
			if (T && L) { *res = HTTOPLEFT;     return true; }
			if (T && R) { *res = HTTOPRIGHT;    return true; }
			if (B && L) { *res = HTBOTTOMLEFT;  return true; }
			if (B && R) { *res = HTBOTTOMRIGHT; return true; }
			if (L)      { *res = HTLEFT;        return true; }
			if (R)      { *res = HTRIGHT;       return true; }
			if (T)      { *res = HTTOP;         return true; }
			if (B)      { *res = HTBOTTOM;      return true; }
			break;
		}
		}
		return false;
	}

	HWND m_hwnd;	/* 单窗口应用，filter 可被 apply 复用时改绑 */
};
#endif

/* ---- 通用：纯 Qt 的边缘缩放（非 Windows 平台；Windows 走上面的
 *      WM_NCHITTEST，光标/手感由系统提供，不装这个避免双重响应） ---- */
class EdgeResizeFilter : public QObject {
public:
	explicit EdgeResizeFilter(QObject *parent) : QObject(parent) {}

	static Qt::Edges edgesAt(QWidget *w, const QPoint &pos, int e)
	{
		Qt::Edges edges;
		if (pos.x() < e)		     edges |= Qt::LeftEdge;
		if (pos.x() >= w->width() - e)	     edges |= Qt::RightEdge;
		if (pos.y() < e)		     edges |= Qt::TopEdge;
		if (pos.y() >= w->height() - e)	     edges |= Qt::BottomEdge;
		return edges;
	}

	bool eventFilter(QObject *obj, QEvent *ev) override
	{
		auto *w = qobject_cast<QWidget *>(obj);
		if (!w || w->isMaximized())
			return false;
		const int e = 6;
		if (ev->type() == QEvent::MouseButtonPress) {
			auto *me = static_cast<QMouseEvent *>(ev);
			Qt::Edges edges = edgesAt(w, me->position().toPoint(), e);
			if (edges && w->window()->windowHandle()) {
				w->window()->windowHandle()->startSystemResize(
					edges);
				return true;
			}
		} else if (ev->type() == QEvent::MouseMove) {
			auto *me = static_cast<QMouseEvent *>(ev);
			Qt::Edges edges = edgesAt(w, me->position().toPoint(), e);
			Qt::CursorShape c = Qt::ArrowCursor;
			if ((edges & Qt::LeftEdge && edges & Qt::TopEdge)
			    || (edges & Qt::RightEdge && edges & Qt::BottomEdge))
				c = Qt::SizeFDiagCursor;
			else if ((edges & Qt::RightEdge && edges & Qt::TopEdge)
				 || (edges & Qt::LeftEdge && edges & Qt::BottomEdge))
				c = Qt::SizeBDiagCursor;
			else if (edges & (Qt::LeftEdge | Qt::RightEdge))
				c = Qt::SizeHorCursor;
			else if (edges & (Qt::TopEdge | Qt::BottomEdge))
				c = Qt::SizeVerCursor;
			w->setCursor(c);
		}
		return false;
	}
};

void Frameless::apply(QMainWindow *w)
{
	w->setWindowFlag(Qt::FramelessWindowHint, true);
	w->setMouseTracking(true);

#ifdef Q_OS_WIN
	/* Frameless 后 Qt 清掉 WS_CAPTION/WS_THICKFRAME；加回以保留 DWM 阴影、
	 * Aero 贴靠、任务栏最小化动画，可见边框由 WM_NCCALCSIZE 抹掉 */
	HWND hwnd = (HWND)w->winId();
	auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
	SetWindowLongPtrW(hwnd, GWL_STYLE,
			  style | WS_THICKFRAME | WS_CAPTION | WS_MINIMIZEBOX
			      | WS_MAXIMIZEBOX | WS_SYSMENU);
	SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
		     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
			 | SWP_FRAMECHANGED);
	static WinFramelessFilter *s_filter = nullptr;
	if (!s_filter) {
		s_filter = new WinFramelessFilter(hwnd);
		QCoreApplication::instance()->installNativeEventFilter(
			s_filter);
	} else {
		s_filter->m_hwnd = hwnd; /* 单窗口应用：直接改绑 */
	}
#else
	auto *resize = new EdgeResizeFilter(w);
	w->installEventFilter(resize);
#endif
}
