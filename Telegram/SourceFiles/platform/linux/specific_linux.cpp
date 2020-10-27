/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/specific_linux.h"

#include "platform/linux/linux_libs.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_xcb_utilities_linux.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "platform/linux/linux_desktop_environment.h"
#include "platform/linux/file_utilities_linux.h"
#include "platform/platform_notifications_manager.h"
#include "storage/localstorage.h"
#include "core/crash_reports.h"
#include "core/update_checker.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>
#include <QtCore/QStandardPaths>
#include <QtCore/QProcess>
#include <QtCore/QVersionNumber>
#include <QtCore/QLibraryInfo>
#include <QtGui/QWindow>

#include <private/qwaylanddisplay_p.h>
#include <private/qwaylandwindow_p.h>
#include <private/qwaylandshellsurface_p.h>

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusError>
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#include <xcb/xcb.h>
#include <xcb/screensaver.h>

#if QT_VERSION < QT_VERSION_CHECK(5, 13, 0) && !defined DESKTOP_APP_QT_PATCHED
#include <wayland-client.h>
#endif // Qt < 5.13 && !DESKTOP_APP_QT_PATCHED

#include <glib.h>

extern "C" {
#undef signals
#include <gio/gio.h>
#define signals public
} // extern "C"

#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>

#include <iostream>

using namespace Platform;
using Platform::File::internal::EscapeShell;
using QtWaylandClient::QWaylandWindow;

namespace Platform {
namespace {

constexpr auto kDisableGtkIntegration = "TDESKTOP_DISABLE_GTK_INTEGRATION"_cs;
constexpr auto kIgnoreGtkIncompatibility = "TDESKTOP_I_KNOW_ABOUT_GTK_INCOMPATIBILITY"_cs;

constexpr auto kDesktopFile = ":/misc/telegramdesktop.desktop"_cs;
constexpr auto kIconName = "telegram"_cs;
constexpr auto kHandlerTypeName = "x-scheme-handler/tg"_cs;

constexpr auto kXDGDesktopPortalService = "org.freedesktop.portal.Desktop"_cs;
constexpr auto kXDGDesktopPortalObjectPath = "/org/freedesktop/portal/desktop"_cs;
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties"_cs;

constexpr auto kXCBFrameExtentsAtomName = "_GTK_FRAME_EXTENTS"_cs;

QStringList PlatformThemes;

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
void PortalAutostart(bool autostart, bool silent = false) {
	if (cExeName().isEmpty()) {
		return;
	}

	QVariantMap options;
	options["reason"] = tr::lng_settings_auto_start(tr::now);
	options["autostart"] = autostart;
	options["commandline"] = QStringList({
		cExeName(),
		qsl("-autostart")
	});
	options["dbus-activatable"] = false;

	auto message = QDBusMessage::createMethodCall(
		kXDGDesktopPortalService.utf16(),
		kXDGDesktopPortalObjectPath.utf16(),
		qsl("org.freedesktop.portal.Background"),
		qsl("RequestBackground"));

	message.setArguments({
		QString(),
		options
	});

	if (silent) {
		QDBusConnection::sessionBus().send(message);
	} else {
		const QDBusReply<void> reply = QDBusConnection::sessionBus().call(
			message);

		if (!reply.isValid()) {
			LOG(("Flatpak autostart error: %1").arg(reply.error().message()));
		}
	}
}

bool IsXDGDesktopPortalKDEPresent() {
	static const auto Result = QDBusInterface(
		qsl("org.freedesktop.impl.portal.desktop.kde"),
		kXDGDesktopPortalObjectPath.utf16()).isValid();

	return Result;
}

uint FileChooserPortalVersion() {
	static const auto Result = [&]() -> uint {
		auto message = QDBusMessage::createMethodCall(
			kXDGDesktopPortalService.utf16(),
			kXDGDesktopPortalObjectPath.utf16(),
			kPropertiesInterface.utf16(),
			qsl("Get"));

		message.setArguments({
			qsl("org.freedesktop.portal.FileChooser"),
			qsl("version")
		});

		const QDBusReply<uint> reply = QDBusConnection::sessionBus().call(
			message);

		if (reply.isValid()) {
			return reply.value();
		} else {
			LOG(("Error getting FileChooser portal version: %1")
				.arg(reply.error().message()));
		}

		return 0;
	}();

	return Result;
}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

QString FlatpakID() {
	static const auto Result = [] {
		if (!qEnvironmentVariableIsEmpty("FLATPAK_ID")) {
			return QString::fromLatin1(qgetenv("FLATPAK_ID"));
		} else {
			return GetLauncherBasename();
		}
	}();

	return Result;
}

QString ProcessNameByPID(const QString &pid) {
	constexpr auto kMaxPath = 1024;
	char result[kMaxPath] = { 0 };
	auto count = readlink("/proc/" + pid.toLatin1() + "/exe", result, kMaxPath);
	if (count > 0) {
		auto filename = QFile::decodeName(result);
		auto deletedPostfix = qstr(" (deleted)");
		if (filename.endsWith(deletedPostfix) && !QFileInfo(filename).exists()) {
			filename.chop(deletedPostfix.size());
		}
		return filename;
	}

	return QString();
}

QString RealExecutablePath(int argc, char *argv[]) {
	const auto processName = ProcessNameByPID(qsl("self"));

	// Fallback to the first command line argument.
	return !processName.isEmpty()
		? processName
		: argc
			? QFile::decodeName(argv[0])
			: QString();
}

bool RunShellCommand(const QString &program, const QStringList &arguments) {
	const auto result = QProcess::execute(program, arguments);

	const auto command = qsl("%1 %2")
		.arg(program)
		.arg(arguments.join(' '));

	if (result) {
		DEBUG_LOG(("App Error: command failed, code: %1, command: %2")
			.arg(result)
			.arg(command));

		return false;
	}

	DEBUG_LOG(("App Info: command succeeded, command: %1")
		.arg(command));

	return true;
}

bool GenerateDesktopFile(
		const QString &targetPath,
		const QString &args,
		bool silent = false) {
	if (targetPath.isEmpty() || cExeName().isEmpty()) {
		return false;
	}

	DEBUG_LOG(("App Info: placing .desktop file to %1").arg(targetPath));
	if (!QDir(targetPath).exists()) QDir().mkpath(targetPath);

	const auto sourceFile = kDesktopFile.utf16();
	const auto targetFile = targetPath + GetLauncherFilename();

	QString fileText;

	QFile source(sourceFile);
	if (source.open(QIODevice::ReadOnly)) {
		QTextStream s(&source);
		fileText = s.readAll();
		source.close();
	} else {
		if (!silent) {
			LOG(("App Error: Could not open '%1' for read").arg(sourceFile));
		}
		return false;
	}

	QFile target(targetFile);
	if (target.open(QIODevice::WriteOnly)) {
		if (IsStaticBinary() || InAppImage()) {
			fileText = fileText.replace(
				QRegularExpression(
					qsl("^TryExec=.*$"),
					QRegularExpression::MultilineOption),
				qsl("TryExec=")
					+ QFile::encodeName(cExeDir() + cExeName())
						.replace('\\', qsl("\\\\")));
			fileText = fileText.replace(
				QRegularExpression(
					qsl("^Exec=.*$"),
					QRegularExpression::MultilineOption),
				qsl("Exec=")
					+ EscapeShell(QFile::encodeName(cExeDir() + cExeName()))
						.replace('\\', qsl("\\\\"))
					+ (args.isEmpty() ? QString() : ' ' + args));
		} else {
			fileText = fileText.replace(
				QRegularExpression(
					qsl("^Exec=(.*) -- %u$"),
					QRegularExpression::MultilineOption),
				qsl("Exec=\\1")
					+ (args.isEmpty() ? QString() : ' ' + args));
		}

		target.write(fileText.toUtf8());
		target.close();

		if (IsStaticBinary()) {
			DEBUG_LOG(("App Info: removing old .desktop files"));
			QFile(qsl("%1telegram.desktop").arg(targetPath)).remove();
			QFile(qsl("%1telegramdesktop.desktop").arg(targetPath)).remove();
		}

		return true;
	} else {
		if (!silent) {
			LOG(("App Error: Could not open '%1' for write").arg(targetFile));
		}
		return false;
	}
}

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
bool GetImageFromClipboardSupported() {
	return (Libs::gtk_clipboard_wait_for_contents != nullptr)
		&& (Libs::gtk_clipboard_wait_for_image != nullptr)
		&& (Libs::gtk_selection_data_targets_include_image != nullptr)
		&& (Libs::gtk_selection_data_free != nullptr)
		&& (Libs::gdk_pixbuf_get_pixels != nullptr)
		&& (Libs::gdk_pixbuf_get_width != nullptr)
		&& (Libs::gdk_pixbuf_get_height != nullptr)
		&& (Libs::gdk_pixbuf_get_rowstride != nullptr)
		&& (Libs::gdk_pixbuf_get_has_alpha != nullptr)
		&& (Libs::gdk_atom_intern != nullptr);
}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

std::optional<crl::time> XCBLastUserInputTime() {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return std::nullopt;
	}

	if (!base::Platform::XCB::IsExtensionPresent(
			connection,
			&xcb_screensaver_id)) {
		return std::nullopt;
	}

	const auto root = base::Platform::XCB::GetRootWindowFromQt();
	if (!root.has_value()) {
		return std::nullopt;
	}

	const auto cookie = xcb_screensaver_query_info(
		connection,
		*root);

	auto reply = xcb_screensaver_query_info_reply(
		connection,
		cookie,
		nullptr);

	if (!reply) {
		return std::nullopt;
	}

	const auto idle = reply->ms_since_user_input;
	free(reply);

	return (crl::now() - static_cast<crl::time>(idle));
}

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
std::optional<crl::time> FreedesktopDBusLastUserInputTime() {
	static auto NotSupported = false;

	if (NotSupported) {
		return std::nullopt;
	}

	static const auto Message = QDBusMessage::createMethodCall(
		qsl("org.freedesktop.ScreenSaver"),
		qsl("/org/freedesktop/ScreenSaver"),
		qsl("org.freedesktop.ScreenSaver"),
		qsl("GetSessionIdleTime"));

	const QDBusReply<uint> reply = QDBusConnection::sessionBus().call(
		Message);

	static const auto NotSupportedErrors = {
		QDBusError::ServiceUnknown,
		QDBusError::NotSupported,
	};

	static const auto NotSupportedErrorsToLog = {
		QDBusError::Disconnected,
		QDBusError::AccessDenied,
	};

	if (reply.isValid()) {
		return (crl::now() - static_cast<crl::time>(reply.value()));
	} else if (ranges::contains(NotSupportedErrors, reply.error().type())) {
		NotSupported = true;
	} else {
		if (ranges::contains(NotSupportedErrorsToLog, reply.error().type())) {
			NotSupported = true;
		}

		LOG(("App Error: Unable to get last user input time "
			"from org.freedesktop.ScreenSaver: %1: %2")
			.arg(reply.error().name())
			.arg(reply.error().message()));
	}

	return std::nullopt;
}

std::optional<crl::time> MutterDBusLastUserInputTime() {
	static auto NotSupported = false;

	if (NotSupported) {
		return std::nullopt;
	}

	static const auto Message = QDBusMessage::createMethodCall(
		qsl("org.gnome.Mutter.IdleMonitor"),
		qsl("/org/gnome/Mutter/IdleMonitor/Core"),
		qsl("org.gnome.Mutter.IdleMonitor"),
		qsl("GetIdletime"));

	const QDBusReply<qulonglong> reply = QDBusConnection::sessionBus().call(
		Message);

	static const auto NotSupportedErrors = {
		QDBusError::ServiceUnknown,
	};

	static const auto NotSupportedErrorsToLog = {
		QDBusError::Disconnected,
		QDBusError::AccessDenied,
	};

	if (reply.isValid()) {
		return (crl::now() - static_cast<crl::time>(reply.value()));
	} else if (ranges::contains(NotSupportedErrors, reply.error().type())) {
		NotSupported = true;
	} else {
		if (ranges::contains(NotSupportedErrorsToLog, reply.error().type())) {
			NotSupported = true;
		}

		LOG(("App Error: Unable to get last user input time "
			"from org.gnome.Mutter.IdleMonitor: %1: %2")
			.arg(reply.error().name())
			.arg(reply.error().message()));
	}

	return std::nullopt;
}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

uint XCBMoveResizeFromEdges(Qt::Edges edges) {
	if (edges == (Qt::TopEdge | Qt::LeftEdge))
		return 0;
	if (edges == Qt::TopEdge)
		return 1;
	if (edges == (Qt::TopEdge | Qt::RightEdge))
		return 2;
	if (edges == Qt::RightEdge)
		return 3;
	if (edges == (Qt::RightEdge | Qt::BottomEdge))
		return 4;
	if (edges == Qt::BottomEdge)
		return 5;
	if (edges == (Qt::BottomEdge | Qt::LeftEdge))
		return 6;
	if (edges == Qt::LeftEdge)
		return 7;

	return 0;
}

#if QT_VERSION < QT_VERSION_CHECK(5, 13, 0) && !defined DESKTOP_APP_QT_PATCHED
enum wl_shell_surface_resize WlResizeFromEdges(Qt::Edges edges) {
	if (edges == (Qt::TopEdge | Qt::LeftEdge))
		return WL_SHELL_SURFACE_RESIZE_TOP_LEFT;
	if (edges == Qt::TopEdge)
		return WL_SHELL_SURFACE_RESIZE_TOP;
	if (edges == (Qt::TopEdge | Qt::RightEdge))
		return WL_SHELL_SURFACE_RESIZE_TOP_RIGHT;
	if (edges == Qt::RightEdge)
		return WL_SHELL_SURFACE_RESIZE_RIGHT;
	if (edges == (Qt::RightEdge | Qt::BottomEdge))
		return WL_SHELL_SURFACE_RESIZE_BOTTOM_RIGHT;
	if (edges == Qt::BottomEdge)
		return WL_SHELL_SURFACE_RESIZE_BOTTOM;
	if (edges == (Qt::BottomEdge | Qt::LeftEdge))
		return WL_SHELL_SURFACE_RESIZE_BOTTOM_LEFT;
	if (edges == Qt::LeftEdge)
		return WL_SHELL_SURFACE_RESIZE_LEFT;

	return WL_SHELL_SURFACE_RESIZE_NONE;
}
#endif // Qt < 5.13 && !DESKTOP_APP_QT_PATCHED

bool StartXCBMoveResize(QWindow *window, int edges) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return false;
	}

	const auto root = base::Platform::XCB::GetRootWindowFromQt();
	if (!root.has_value()) {
		return false;
	}

	const auto moveResizeAtom = base::Platform::XCB::GetAtom(
		connection,
		"_NET_WM_MOVERESIZE");

	if (!moveResizeAtom.has_value()) {
		return false;
	}

	const auto globalPos = QCursor::pos();

	xcb_client_message_event_t xev;
	xev.response_type = XCB_CLIENT_MESSAGE;
	xev.type = *moveResizeAtom;
	xev.sequence = 0;
	xev.window = window->winId();
	xev.format = 32;
	xev.data.data32[0] = globalPos.x();
	xev.data.data32[1] = globalPos.y();
	xev.data.data32[2] = (edges == 16)
		? 8 // move
		: XCBMoveResizeFromEdges(Qt::Edges(edges));
	xev.data.data32[3] = XCB_BUTTON_INDEX_1;
	xev.data.data32[4] = 0;

	xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
	xcb_send_event(
		connection,
		false,
		*root,
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
			| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
		reinterpret_cast<const char*>(&xev));

	return true;
}

bool StartWaylandMove(QWindow *window) {
	// There are startSystemMove on Qt 5.15
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0) && !defined DESKTOP_APP_QT_PATCHED
	if (const auto waylandWindow = static_cast<QWaylandWindow*>(
		window->handle())) {
		if (const auto seat = waylandWindow->display()->lastInputDevice()) {
			if (const auto shellSurface = waylandWindow->shellSurface()) {
				return shellSurface->move(seat);
			}
		}
	}
#endif // Qt < 5.15 && !DESKTOP_APP_QT_PATCHED

	return false;
}

bool StartWaylandResize(QWindow *window, Qt::Edges edges) {
	// There are startSystemResize on Qt 5.15
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0) && !defined DESKTOP_APP_QT_PATCHED
	if (const auto waylandWindow = static_cast<QWaylandWindow*>(
		window->handle())) {
		if (const auto seat = waylandWindow->display()->lastInputDevice()) {
			if (const auto shellSurface = waylandWindow->shellSurface()) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
				shellSurface->resize(seat, edges);
				return true;
#else // Qt >= 5.13
				shellSurface->resize(seat, WlResizeFromEdges(edges));
				return true;
#endif // Qt < 5.13
			}
		}
	}
#endif // Qt < 5.15 && !DESKTOP_APP_QT_PATCHED

	return false;
}

bool ShowWaylandWindowMenu(QWindow *window) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0) || defined DESKTOP_APP_QT_PATCHED
	if (const auto waylandWindow = static_cast<QWaylandWindow*>(
		window->handle())) {
		if (const auto seat = waylandWindow->display()->lastInputDevice()) {
			if (const auto shellSurface = waylandWindow->shellSurface()) {
				return shellSurface->showWindowMenu(seat);
			}
		}
	}
#endif // Qt >= 5.13 || DESKTOP_APP_QT_PATCHED

	return false;
}

bool XCBFrameExtentsSupported() {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return false;
	}

	const auto frameExtentsAtom = base::Platform::XCB::GetAtom(
		connection,
		kXCBFrameExtentsAtomName.utf16());

	if (!frameExtentsAtom.has_value()) {
		return false;
	}

	return ranges::contains(
		base::Platform::XCB::GetWMSupported(connection),
		*frameExtentsAtom);
}

bool SetXCBFrameExtents(QWindow *window, const QMargins &extents) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return false;
	}

	const auto frameExtentsAtom = base::Platform::XCB::GetAtom(
		connection,
		kXCBFrameExtentsAtomName.utf16());

	if (!frameExtentsAtom.has_value()) {
		return false;
	}

	const auto extentsVector = std::vector<uint>{
		uint(extents.left()),
		uint(extents.right()),
		uint(extents.top()),
		uint(extents.bottom()),
	};

	xcb_change_property(
		connection,
		XCB_PROP_MODE_REPLACE,
		window->winId(),
		*frameExtentsAtom,
		XCB_ATOM_CARDINAL,
		32,
		extentsVector.size(),
		extentsVector.data());

	return true;
}

bool UnsetXCBFrameExtents(QWindow *window) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return false;
	}

	const auto frameExtentsAtom = base::Platform::XCB::GetAtom(
		connection,
		kXCBFrameExtentsAtomName.utf16());

	if (!frameExtentsAtom.has_value()) {
		return false;
	}

	xcb_delete_property(
		connection,
		window->winId(),
		*frameExtentsAtom);

	return true;
}

Window::Control GtkKeywordToWindowControl(const QString &keyword) {
	if (keyword == qstr("minimize")) {
		return Window::Control::Minimize;
	} else if (keyword == qstr("maximize")) {
		return Window::Control::Maximize;
	} else if (keyword == qstr("close")) {
		return Window::Control::Close;
	}

	return Window::Control::Unknown;
}

} // namespace

void SetApplicationIcon(const QIcon &icon) {
	QApplication::setWindowIcon(icon);
}

bool InFlatpak() {
	static const auto Result = QFileInfo::exists(qsl("/.flatpak-info"));
	return Result;
}

bool InSnap() {
	static const auto Result = qEnvironmentVariableIsSet("SNAP");
	return Result;
}

bool InAppImage() {
	static const auto Result = qEnvironmentVariableIsSet("APPIMAGE");
	return Result;
}

bool IsStaticBinary() {
#ifdef DESKTOP_APP_USE_PACKAGED
		return false;
#else // DESKTOP_APP_USE_PACKAGED
		return true;
#endif // !DESKTOP_APP_USE_PACKAGED
}

bool UseGtkIntegration() {
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	static const auto Result = !qEnvironmentVariableIsSet(
		kDisableGtkIntegration.utf8());

	return Result;
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

	return false;
}

bool IsGtkIntegrationForced() {
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	static const auto Result = [&] {
		return PlatformThemes.contains(qstr("gtk3"), Qt::CaseInsensitive)
			|| PlatformThemes.contains(qstr("gtk2"), Qt::CaseInsensitive);
	}();

	return Result;
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

	return false;
}

bool IsQtPluginsBundled() {
#ifdef DESKTOP_APP_USE_PACKAGED_LAZY
	return true;
#else // DESKTOP_APP_USE_PACKAGED_LAZY
	return false;
#endif // !DESKTOP_APP_USE_PACKAGED_LAZY
}

bool IsXDGDesktopPortalPresent() {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	static const auto Result = QDBusInterface(
		kXDGDesktopPortalService.utf16(),
		kXDGDesktopPortalObjectPath.utf16()).isValid();

	return Result;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	return false;
}

bool UseXDGDesktopPortal() {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	static const auto Result = [&] {
		const auto envVar = qEnvironmentVariableIsSet("TDESKTOP_USE_PORTAL");
		const auto portalPresent = IsXDGDesktopPortalPresent();
		const auto neededForKde = DesktopEnvironment::IsKDE()
			&& IsXDGDesktopPortalKDEPresent();

		return (neededForKde || envVar) && portalPresent;
	}();

	return Result;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	return false;
}

bool CanOpenDirectoryWithPortal() {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	static const auto Result = [&] {
#ifdef DESKTOP_APP_QT_PATCHED
		return FileChooserPortalVersion() >= 3;
#else // DESKTOP_APP_QT_PATCHED
		return QLibraryInfo::version() >= QVersionNumber(5, 15, 0)
			&& FileChooserPortalVersion() >= 3;
#endif // !DESKTOP_APP_QT_PATCHED
	}();

	return Result;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	return false;
}

QString CurrentExecutablePath(int argc, char *argv[]) {
	if (InAppImage()) {
		const auto appimagePath = QString::fromUtf8(qgetenv("APPIMAGE"));
		const auto appimagePathList = appimagePath.split('/');

		if (qEnvironmentVariableIsSet("ARGV0")
			&& appimagePathList.size() >= 5
			&& appimagePathList[1] == qstr("run")
			&& appimagePathList[2] == qstr("user")
			&& appimagePathList[4] == qstr("appimagelauncherfs")) {
			return QString::fromUtf8(qgetenv("ARGV0"));
		}

		return appimagePath;
	}

	return RealExecutablePath(argc, argv);
}

QString AppRuntimeDirectory() {
	static const auto Result = [&] {
		auto runtimeDir = QStandardPaths::writableLocation(
			QStandardPaths::RuntimeLocation);

		if (InFlatpak()) {
			runtimeDir += qsl("/app/") + FlatpakID();
		}

		if (!QFileInfo::exists(runtimeDir)) { // non-systemd distros
			runtimeDir = QDir::tempPath();
		}

		if (runtimeDir.isEmpty()) {
			runtimeDir = qsl("/tmp/");
		}

		if (!runtimeDir.endsWith('/')) {
			runtimeDir += '/';
		}

		return runtimeDir;
	}();

	return Result;
}

QString SingleInstanceLocalServerName(const QString &hash) {
	const auto idealSocketPath = AppRuntimeDirectory()
		+ hash
		+ '-'
		+ cGUIDStr();

	if (idealSocketPath.size() >= 108) {
		return AppRuntimeDirectory() + hash;
	} else {
		return idealSocketPath;
	}
}

QString GetLauncherBasename() {
	static const auto Result = [&] {
		if ((IsStaticBinary() || InAppImage()) && !cExeName().isEmpty()) {
			const auto appimagePath = qsl("file://%1%2")
				.arg(cExeDir())
				.arg(cExeName())
				.toUtf8();

			char md5Hash[33] = { 0 };
			hashMd5Hex(
				appimagePath.constData(),
				appimagePath.size(),
				md5Hash);

			return qsl("appimagekit_%1-%2")
				.arg(md5Hash)
				.arg(AppName.utf16().replace(' ', '_'));
		}

		return qsl(MACRO_TO_STRING(TDESKTOP_LAUNCHER_BASENAME));
	}();

	return Result;
}

QString GetLauncherFilename() {
	static const auto Result = GetLauncherBasename()
		+ qsl(".desktop");
	return Result;
}

QString GetIconName() {
	static const auto Result = InFlatpak()
		? FlatpakID()
		: kIconName.utf16();
	return Result;
}

QImage GetImageFromClipboard() {
	QImage data;

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	if (!GetImageFromClipboardSupported() || !Libs::GtkClipboard()) {
		return data;
	}

	auto gsel = Libs::gtk_clipboard_wait_for_contents(
		Libs::GtkClipboard(),
		Libs::gdk_atom_intern("TARGETS", true));

	if (gsel) {
		if (Libs::gtk_selection_data_targets_include_image(gsel, false)) {
			auto img = Libs::gtk_clipboard_wait_for_image(
				Libs::GtkClipboard());

			if (img) {
				data = QImage(
					Libs::gdk_pixbuf_get_pixels(img),
					Libs::gdk_pixbuf_get_width(img),
					Libs::gdk_pixbuf_get_height(img),
					Libs::gdk_pixbuf_get_rowstride(img),
					Libs::gdk_pixbuf_get_has_alpha(img)
						? QImage::Format_RGBA8888
						: QImage::Format_RGB888).copy();

				g_object_unref(img);
			}
		}

		Libs::gtk_selection_data_free(gsel);
	}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

	return data;
}

std::optional<crl::time> LastUserInputTime() {
	if (!IsWayland()) {
		const auto xcbResult = XCBLastUserInputTime();
		if (xcbResult.has_value()) {
			return xcbResult;
		}
	}

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	const auto freedesktopResult = FreedesktopDBusLastUserInputTime();
	if (freedesktopResult.has_value()) {
		return freedesktopResult;
	}

	const auto mutterResult = MutterDBusLastUserInputTime();
	if (mutterResult.has_value()) {
		return mutterResult;
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	return std::nullopt;
}

std::optional<bool> IsDarkMode() {
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	if (Libs::GtkSettingSupported() && Libs::GtkLoaded()) {
		if (Libs::gtk_check_version != nullptr
			&& !Libs::gtk_check_version(3, 0, 0)
			&& Libs::GtkSetting<gboolean>(
				"gtk-application-prefer-dark-theme")) {
			return true;
		}

		const auto themeName = Libs::GtkSetting("gtk-theme-name").toLower();

		if (themeName.endsWith(qsl("-dark"))) {
			return true;
		}

		return false;
	}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

	return std::nullopt;
}

bool AutostartSupported() {
	// snap sandbox doesn't allow creating files
	// in folders with names started with a dot
	// and doesn't provide any api to add an app to autostart
	// thus, autostart isn't supported in snap
	return !InSnap();
}

bool TrayIconSupported() {
	return App::wnd()
		? App::wnd()->trayAvailable()
		: false;
}

bool StartSystemMove(QWindow *window) {
	if (IsWayland()) {
		return StartWaylandMove(window);
	} else {
		return StartXCBMoveResize(window, 16);
	}
}

bool StartSystemResize(QWindow *window, Qt::Edges edges) {
	if (IsWayland()) {
		return StartWaylandResize(window, edges);
	} else {
		return StartXCBMoveResize(window, edges);
	}
}

bool ShowWindowMenu(QWindow *window) {
	if (IsWayland()) {
		return ShowWaylandWindowMenu(window);
	}

	return false;
}

bool SetWindowExtents(QWindow *window, const QMargins &extents) {
	if (!IsWayland()) {
		return SetXCBFrameExtents(window, extents);
	}

	return false;
}

bool UnsetWindowExtents(QWindow *window) {
	if (!IsWayland()) {
		return UnsetXCBFrameExtents(window);
	}

	return false;
}

bool WindowsNeedShadow() {
	if (!IsWayland() && XCBFrameExtentsSupported()) {
		return true;
	}

	return false;
}

Window::ControlsLayout WindowControlsLayout() {
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	if (Libs::GtkSettingSupported()
		&& Libs::GtkLoaded()
		&& Libs::gtk_check_version != nullptr
		&& !Libs::gtk_check_version(3, 12, 0)) {
		const auto decorationLayout = Libs::GtkSetting(
			"gtk-decoration-layout").split(':');

		std::vector<Window::Control> controlsLeft;
		ranges::transform(
			decorationLayout[0].split(','),
			ranges::back_inserter(controlsLeft),
			GtkKeywordToWindowControl
		);

		std::vector<Window::Control> controlsRight;
		if (decorationLayout.size() > 1) {
			ranges::transform(
				decorationLayout[1].split(','),
				ranges::back_inserter(controlsRight),
				GtkKeywordToWindowControl
			);
		}

		Window::ControlsLayout controls;
		controls.left = controlsLeft;
		controls.right = controlsRight;

		return controls;
	}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

	Window::ControlsLayout controls;

	if (DesktopEnvironment::IsUnity()) {
		controls.left = {
			Window::Control::Close,
			Window::Control::Minimize,
			Window::Control::Maximize,
		};
	} else {
		controls.right = {
			Window::Control::Minimize,
			Window::Control::Maximize,
			Window::Control::Close,
		};
	}

	return controls;
}

} // namespace Platform

namespace {

QRect _monitorRect;
auto _monitorLastGot = 0LL;

} // namespace

QRect psDesktopRect() {
	auto tnow = crl::now();
	if (tnow > _monitorLastGot + 1000LL || tnow < _monitorLastGot) {
		_monitorLastGot = tnow;
		_monitorRect = QApplication::desktop()->availableGeometry(App::wnd());
	}
	return _monitorRect;
}

void psWriteDump() {
}

bool _removeDirectory(const QString &path) { // from http://stackoverflow.com/questions/2256945/removing-a-non-empty-directory-programmatically-in-c-or-c
	QByteArray pathRaw = QFile::encodeName(path);
	DIR *d = opendir(pathRaw.constData());
	if (!d) return false;

	while (struct dirent *p = readdir(d)) {
		/* Skip the names "." and ".." as we don't want to recurse on them. */
		if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;

		QString fname = path + '/' + p->d_name;
		QByteArray fnameRaw = QFile::encodeName(fname);
		struct stat statbuf;
		if (!stat(fnameRaw.constData(), &statbuf)) {
			if (S_ISDIR(statbuf.st_mode)) {
				if (!_removeDirectory(fname)) {
					closedir(d);
					return false;
				}
			} else {
				if (unlink(fnameRaw.constData())) {
					closedir(d);
					return false;
				}
			}
		}
	}
	closedir(d);

	return !rmdir(pathRaw.constData());
}

void psDeleteDir(const QString &dir) {
	_removeDirectory(dir);
}

void psActivateProcess(uint64 pid) {
//	objc_activateProgram();
}

namespace {

QString getHomeDir() {
	const auto home = QString(g_get_home_dir());

	if (!home.isEmpty() && !home.endsWith('/')) {
		return home + '/';
	}

	return home;
}

} // namespace

QString psAppDataPath() {
	// Previously we used ~/.TelegramDesktop, so look there first.
	// If we find data there, we should still use it.
	auto home = getHomeDir();
	if (!home.isEmpty()) {
		auto oldPath = home + qsl(".TelegramDesktop/");
		auto oldSettingsBase = oldPath + qsl("tdata/settings");
		if (QFile(oldSettingsBase + '0').exists()
			|| QFile(oldSettingsBase + '1').exists()
			|| QFile(oldSettingsBase + 's').exists()) {
			return oldPath;
		}
	}

	return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + '/';
}

void psDoCleanup() {
	try {
		psAutoStart(false, true);
		psSendToMenu(false, true);
	} catch (...) {
	}
}

int psCleanup() {
	psDoCleanup();
	return 0;
}

void psDoFixPrevious() {
}

int psFixPrevious() {
	psDoFixPrevious();
	return 0;
}

namespace Platform {

void start() {
	PlatformThemes = QString::fromUtf8(qgetenv("QT_QPA_PLATFORMTHEME"))
		.split(':', QString::SkipEmptyParts);

	LOG(("Launcher filename: %1").arg(GetLauncherFilename()));

	qputenv("PULSE_PROP_application.name", AppName.utf8());
	qputenv("PULSE_PROP_application.icon_name", GetIconName().toLatin1());

	// if gtk integration and qgtk3/qgtk2 platformtheme (or qgtk2 style)
	// is used at the same time, the app will crash
	if (UseGtkIntegration()
		&& !IsStaticBinary()
		&& !qEnvironmentVariableIsSet(
			kIgnoreGtkIncompatibility.utf8())) {
		g_warning(
			"Unfortunately, GTK integration "
			"conflicts with qgtk2 platformtheme and style. "
			"Therefore, QT_QPA_PLATFORMTHEME "
			"and QT_STYLE_OVERRIDE will be unset.");

		g_message(
			"This can be ignored by setting %s environment variable "
			"to any value, however, if qgtk2 theme or style is used, "
			"this will lead to a crash.",
			kIgnoreGtkIncompatibility.utf8().constData());

		g_message(
			"GTK integration can be disabled by setting %s to any value. "
			"Keep in mind that this will lead to clipboard issues "
			"and tdesktop will be unable to get settings from GTK "
			"(such as decoration layout, dark mode & more).",
			kDisableGtkIntegration.utf8().constData());

		qunsetenv("QT_QPA_PLATFORMTHEME");
		qunsetenv("QT_STYLE_OVERRIDE");

		// Don't allow qgtk3 to init gtk earlier than us
		if (DesktopEnvironment::IsGtkBased()) {
			QApplication::setDesktopSettingsAware(false);
		}
	}

	if (!UseGtkIntegration()) {
		g_warning(
			"GTK integration was disabled on build or in runtime. "
			"This will lead to clipboard issues and a lack of some features "
			"(like Auto-Night Mode or system window controls layout).");
	}

#ifdef DESKTOP_APP_USE_PACKAGED_RLOTTIE
	g_warning(
		"Application has been built with foreign rlottie, "
		"animated emojis won't be colored to the selected pack.");
#endif // DESKTOP_APP_USE_PACKAGED_RLOTTIE

#ifdef DESKTOP_APP_USE_PACKAGED_FONTS
	g_warning(
		"Application was built without embedded fonts, "
		"this may lead to font issues.");
#endif // DESKTOP_APP_USE_PACKAGED_FONTS

	if (IsQtPluginsBundled()) {
		qputenv("QT_WAYLAND_DECORATION", "material");
	}

	if ((IsStaticBinary()
		|| InAppImage()
		|| IsQtPluginsBundled())
		// it is handled by Qt for flatpak and snap
		&& !InFlatpak()
		&& !InSnap()) {
		LOG(("Checking for XDG Desktop Portal..."));
		// this can give us a chance to use
		// a proper file dialog for current session
		if (IsXDGDesktopPortalPresent()) {
			LOG(("XDG Desktop Portal is present!"));
			if (UseXDGDesktopPortal()) {
				LOG(("Using XDG Desktop Portal."));
				qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
			} else {
				LOG(("Not using XDG Desktop Portal."));
			}
		} else {
			LOG(("XDG Desktop Portal is not present :("));
		}
	}
}

void finish() {
}

void InstallLauncher() {
	static const auto DisabledByEnv = qEnvironmentVariableIsSet(
		"TDESKTOP_DISABLE_DESKTOP_FILE_GENERATION");

	// don't update desktop file for alpha version or if updater is disabled
	if (cAlphaVersion() || Core::UpdaterDisabled() || DisabledByEnv)
		return;

	const auto applicationsPath = QStandardPaths::writableLocation(
		QStandardPaths::ApplicationsLocation) + '/';

	GenerateDesktopFile(applicationsPath, qsl("-- %u"));

	const auto icons = QStandardPaths::writableLocation(
		QStandardPaths::GenericDataLocation) + qsl("/icons/");

	if (!QDir(icons).exists()) QDir().mkpath(icons);

	const auto icon = icons + qsl("telegram.png");
	auto iconExists = QFile(icon).exists();
	if (Local::oldSettingsVersion() < 10021 && iconExists) {
		// Icon was changed.
		if (QFile(icon).remove()) {
			iconExists = false;
		}
	}
	if (!iconExists) {
		if (QFile(qsl(":/gui/art/logo_256.png")).copy(icon)) {
			DEBUG_LOG(("App Info: Icon copied to '%1'").arg(icon));
		}
	}

	RunShellCommand("update-desktop-database", {
		applicationsPath
	});
}

void RegisterCustomScheme(bool force) {
	if (cExeName().isEmpty()) {
		return;
	}

	GError *error = nullptr;

	const auto neededCommandlineBuilder = qsl("%1 --")
		.arg((IsStaticBinary() || InAppImage())
			? cExeDir() + cExeName()
			: cExeName());

	const auto neededCommandline = qsl("%1 %u")
		.arg(neededCommandlineBuilder);

	auto currentAppInfo = g_app_info_get_default_for_type(
		kHandlerTypeName.utf8(),
		true);

	if (currentAppInfo) {
		const auto currentCommandline = QString(
			g_app_info_get_commandline(currentAppInfo));

		g_object_unref(currentAppInfo);

		if (currentCommandline == neededCommandline) {
			return;
		}
	}

	auto registeredAppInfoList = g_app_info_get_recommended_for_type(
		kHandlerTypeName.utf8());

	for (auto l = registeredAppInfoList; l != nullptr; l = l->next) {
		const auto currentRegisteredAppInfo = reinterpret_cast<GAppInfo*>(
			l->data);

		const auto currentAppInfoId = QString(
			g_app_info_get_id(currentRegisteredAppInfo));

		const auto currentCommandline = QString(
			g_app_info_get_commandline(currentRegisteredAppInfo));

		if (currentCommandline == neededCommandline
			&& currentAppInfoId.startsWith(qsl("userapp-"))) {
			g_app_info_delete(currentRegisteredAppInfo);
		}
	}

	if (registeredAppInfoList) {
		g_list_free_full(registeredAppInfoList, g_object_unref);
	}

	auto newAppInfo = g_app_info_create_from_commandline(
		neededCommandlineBuilder.toUtf8(),
		AppName.utf8(),
		G_APP_INFO_CREATE_SUPPORTS_URIS,
		&error);

	if (newAppInfo) {
		g_app_info_set_as_default_for_type(
			newAppInfo,
			kHandlerTypeName.utf8(),
			&error);

		g_object_unref(newAppInfo);
	}

	if (error) {
		LOG(("App Error: %1").arg(error->message));
		g_error_free(error);
	}
}

PermissionStatus GetPermissionStatus(PermissionType type) {
	return PermissionStatus::Granted;
}

void RequestPermission(PermissionType type, Fn<void(PermissionStatus)> resultCallback) {
	resultCallback(PermissionStatus::Granted);
}

void OpenSystemSettingsForPermission(PermissionType type) {
}

bool OpenSystemSettings(SystemSettingsType type) {
	if (type == SystemSettingsType::Audio) {
		auto options = std::vector<QString>();
		const auto add = [&](const char *option) {
			options.emplace_back(option);
		};
		if (DesktopEnvironment::IsUnity()) {
			add("unity-control-center sound");
		} else if (DesktopEnvironment::IsKDE()) {
			add("kcmshell5 kcm_pulseaudio");
			add("kcmshell4 phonon");
		} else if (DesktopEnvironment::IsGnome()) {
			add("gnome-control-center sound");
		} else if (DesktopEnvironment::IsCinnamon()) {
			add("cinnamon-settings sound");
		} else if (DesktopEnvironment::IsMATE()) {
			add("mate-volume-control");
		}
		add("pavucontrol-qt");
		add("pavucontrol");
		add("alsamixergui");
		return ranges::any_of(options, [](const QString &command) {
			return QProcess::startDetached(command);
		});
	}
	return true;
}

namespace ThirdParty {

void start() {
	DEBUG_LOG(("Icon theme: %1").arg(QIcon::themeName()));
	DEBUG_LOG(("Fallback icon theme: %1").arg(QIcon::fallbackThemeName()));

	Libs::start();
	MainWindow::LibsLoaded();
}

void finish() {
}

} // namespace ThirdParty

} // namespace Platform

void psNewVersion() {
	Platform::InstallLauncher();
	Platform::RegisterCustomScheme();
}

bool psShowOpenWithMenu(int x, int y, const QString &file) {
	return false;
}

void psAutoStart(bool start, bool silent) {
	if (InFlatpak()) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
		PortalAutostart(start, silent);
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	} else {
		const auto autostart = QStandardPaths::writableLocation(
			QStandardPaths::GenericConfigLocation)
			+ qsl("/autostart/");

		if (start) {
			GenerateDesktopFile(autostart, qsl("-autostart"), silent);
		} else {
			QFile::remove(autostart + GetLauncherFilename());
		}
	}
}

void psSendToMenu(bool send, bool silent) {
}

bool linuxMoveFile(const char *from, const char *to) {
	FILE *ffrom = fopen(from, "rb"), *fto = fopen(to, "wb");
	if (!ffrom) {
		if (fto) fclose(fto);
		return false;
	}
	if (!fto) {
		fclose(ffrom);
		return false;
	}
	static const int BufSize = 65536;
	char buf[BufSize];
	while (size_t size = fread(buf, 1, BufSize, ffrom)) {
		fwrite(buf, 1, size, fto);
	}

	struct stat fst; // from http://stackoverflow.com/questions/5486774/keeping-fileowner-and-permissions-after-copying-file-in-c
	//let's say this wont fail since you already worked OK on that fp
	if (fstat(fileno(ffrom), &fst) != 0) {
		fclose(ffrom);
		fclose(fto);
		return false;
	}
	//update to the same uid/gid
	if (fchown(fileno(fto), fst.st_uid, fst.st_gid) != 0) {
		fclose(ffrom);
		fclose(fto);
		return false;
	}
	//update the permissions
	if (fchmod(fileno(fto), fst.st_mode) != 0) {
		fclose(ffrom);
		fclose(fto);
		return false;
	}

	fclose(ffrom);
	fclose(fto);

	if (unlink(from)) {
		return false;
	}

	return true;
}

bool psLaunchMaps(const Data::LocationPoint &point) {
	return false;
}
