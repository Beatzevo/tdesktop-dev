/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/notifications_manager.h"

namespace Platform {
namespace Notifications {

[[nodiscard]] bool SkipAudio();
[[nodiscard]] bool SkipToast();
[[nodiscard]] bool SkipFlashBounce();

[[nodiscard]] bool Supported();
[[nodiscard]] std::unique_ptr<Window::Notifications::Manager> Create(
	Window::Notifications::System *system);

} // namespace Notifications
} // namespace Platform

// Platform dependent implementations.

#ifdef Q_OS_MAC
#include "platform/mac/notifications_manager_mac.h"
#elif defined Q_OS_UNIX // Q_OS_MAC
#include "platform/linux/notifications_manager_linux.h"
#elif defined Q_OS_WIN // Q_OS_MAC || Q_OS_UNIX
#include "platform/win/notifications_manager_win.h"
#endif // Q_OS_MAC || Q_OS_UNIX || Q_OS_WIN
