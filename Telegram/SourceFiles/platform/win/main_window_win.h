/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_main_window.h"
#include "ui/platform/win/ui_window_shadow_win.h"
#include "base/platform/win/base_windows_h.h"
#include "base/flags.h"

#include <QtCore/QTimer>

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Platform {

class MainWindow : public Window::MainWindow {
public:
	explicit MainWindow(not_null<Window::Controller*> controller);

	HWND psHwnd() const;
	HMENU psMenu() const;

	void psInitSysMenu();
	void updateSystemMenu(Qt::WindowState state);
	void updateCustomMargins();

	void updateWindowIcon() override;

	void psRefreshTaskbarIcon();

	virtual QImage iconWithCounter(int size, int count, style::color bg, style::color fg, bool smallIcon) = 0;

	static UINT TaskbarCreatedMsgId() {
		return _taskbarCreatedMsgId;
	}
	static void TaskbarCreated();

	// Custom shadows.
	void shadowsActivate();
	void shadowsDeactivate();
	void shadowsUpdate(
		Ui::Platform::WindowShadow::Changes changes,
		WINDOWPOS *position = nullptr);

	int deltaLeft() const {
		return _deltaLeft;
	}
	int deltaTop() const {
		return _deltaTop;
	}

	void psShowTrayMenu();

	~MainWindow();

protected:
	void initHook() override;
	int32 screenNameChecksum(const QString &name) const override;
	void unreadCounterChangedHook() override;

	void initShadows() override;
	void firstShadowsUpdate() override;
	void stateChangedHook(Qt::WindowState state) override;

	bool hasTrayIcon() const override {
		return trayIcon;
	}

	QSystemTrayIcon *trayIcon = nullptr;
	Ui::PopupMenu *trayIconMenu = nullptr;

	void psTrayMenuUpdated();
	void psSetupTrayIcon();
	virtual void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) = 0;

	void showTrayTooltip() override;

	void workmodeUpdated(DBIWorkMode mode) override;

	QTimer psUpdatedPositionTimer;

private:
	void setupNativeWindowFrame();
	void updateIconCounters();
	QMargins computeCustomMargins();
	void validateWindowTheme(bool native, bool night);
	void psDestroyIcons();
	void fixMaximizedWindow();

	static UINT _taskbarCreatedMsgId;

	std::optional<Ui::Platform::WindowShadow> _shadow;

	bool _themeInited = false;
	bool _inUpdateMargins = false;
	bool _wasNativeFrame = false;
	bool _hasActiveFrame = false;

	HWND ps_hWnd = nullptr;
	HWND ps_tbHider_hWnd = nullptr;
	HMENU ps_menu = nullptr;
	HICON ps_iconBig = nullptr;
	HICON ps_iconSmall = nullptr;
	HICON ps_iconOverlay = nullptr;

	int _deltaLeft = 0;
	int _deltaTop = 0;
	int _deltaRight = 0;
	int _deltaBottom = 0;

};

} // namespace Platform
