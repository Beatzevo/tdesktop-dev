/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_specific.h"
#include "base/platform/win/base_windows_h.h"

namespace Data {
class LocationPoint;
} // namespace Data

namespace Platform {

inline void SetWatchingMediaKeys(bool watching) {
}

inline void IgnoreApplicationActivationRightNow() {
}

inline QImage GetImageFromClipboard() {
	return {};
}

inline bool StartSystemMove(QWindow *window) {
	return false;
}

inline bool StartSystemResize(QWindow *window, Qt::Edges edges) {
	return false;
}

inline bool TrayIconSupported() {
	return true;
}

inline bool SetWindowExtents(QWindow *window, const QMargins &extents) {
	return false;
}

inline bool UnsetWindowExtents(QWindow *window) {
	return false;
}

inline bool WindowsNeedShadow() {
	return false;
}

namespace ThirdParty {

void start();

inline void finish() {
}

} // namespace ThirdParty
} // namespace Platform

inline void psCheckLocalSocket(const QString &) {
}

void psWriteDump();

void psDeleteDir(const QString &dir);

QStringList psInitLogs();
void psClearInitLogs();

void psActivateProcess(uint64 pid = 0);
QString psLocalServerPrefix();
QString psAppDataPath();
QString psAppDataPathOld();
void psAutoStart(bool start, bool silent = false);
void psSendToMenu(bool send, bool silent = false);

QRect psDesktopRect();

int psCleanup();
int psFixPrevious();

void psNewVersion();

inline QByteArray psDownloadPathBookmark(const QString &path) {
	return QByteArray();
}
inline QByteArray psPathBookmark(const QString &path) {
	return QByteArray();
}
inline void psDownloadPathEnableAccess() {
}

class PsFileBookmark {
public:
	PsFileBookmark(const QByteArray &bookmark) {
	}
	bool check() const {
		return true;
	}
	bool enable() const {
		return true;
	}
	void disable() const {
	}
	const QString &name(const QString &original) const {
		return original;
	}
	QByteArray bookmark() const {
		return QByteArray();
	}

};

bool psLaunchMaps(const Data::LocationPoint &point);
