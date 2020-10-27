/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/type_traits.h"
#include "base/observer.h"
#include "base/call_delayed.h"
#include "mtproto/mtproto_proxy_data.h"

class History;

namespace Data {
struct FileOrigin;
} // namespace Data

namespace InlineBots {
namespace Layout {
class ItemBase;
} // namespace Layout
} // namespace InlineBots

namespace App {

template <typename Guard, typename Lambda>
[[nodiscard]] inline auto LambdaDelayed(int duration, Guard &&object, Lambda &&lambda) {
	auto guarded = crl::guard(
		std::forward<Guard>(object),
		std::forward<Lambda>(lambda));
	return [saved = std::move(guarded), duration] {
		auto copy = saved;
		base::call_delayed(duration, std::move(copy));
	};
}

void sendBotCommand(
	not_null<PeerData*> peer,
	UserData *bot,
	const QString &cmd,
	MsgId replyTo = 0);
bool insertBotCommand(const QString &cmd);
void activateBotCommand(
	not_null<const HistoryItem*> msg,
	int row,
	int column);
void searchByHashtag(const QString &tag, PeerData *inPeer);
void showSettings();

} // namespace App

namespace Ui {

// Legacy global methods.

void showPeerProfile(not_null<PeerData*> peer);
void showPeerProfile(not_null<const History*> history);

void showPeerHistoryAtItem(not_null<const HistoryItem*> item);
void showPeerHistory(not_null<const PeerData*> peer, MsgId msgId);
void showPeerHistory(not_null<const History*> history, MsgId msgId);
void showChatsList(not_null<Main::Session*> session);
PeerData *getPeerForMouseAction();

bool skipPaintEvent(QWidget *widget, QPaintEvent *event);

} // namespace Ui

enum ClipStopperType {
	ClipStopperMediaview,
	ClipStopperSavedGifsPanel,
};

namespace Notify {

bool switchInlineBotButtonReceived(
	not_null<Main::Session*> session,
	const QString &query,
	UserData *samePeerBot = nullptr,
	MsgId samePeerReplyTo = 0);

} // namespace Notify

#define DeclareReadOnlyVar(Type, Name) const Type &Name();
#define DeclareRefVar(Type, Name) DeclareReadOnlyVar(Type, Name) \
	Type &Ref##Name();
#define DeclareVar(Type, Name) DeclareRefVar(Type, Name) \
	void Set##Name(const Type &Name);

namespace Adaptive {

enum class WindowLayout {
	OneColumn,
	Normal,
	ThreeColumn,
};

enum class ChatLayout {
	Normal,
	Wide,
};

} // namespace Adaptive

namespace Global {

bool started();
void start();
void finish();

DeclareVar(bool, ScreenIsLocked);
DeclareVar(Adaptive::ChatLayout, AdaptiveChatLayout);
DeclareVar(Adaptive::WindowLayout, AdaptiveWindowLayout);
DeclareRefVar(base::Observable<void>, AdaptiveChanged);

DeclareVar(bool, NotificationsDemoIsShown);

DeclareVar(bool, TryIPv6);
DeclareVar(std::vector<MTP::ProxyData>, ProxiesList);
DeclareVar(MTP::ProxyData, SelectedProxy);
DeclareVar(MTP::ProxyData::Settings, ProxySettings);
DeclareVar(bool, UseProxyForCalls);
DeclareRefVar(base::Observable<void>, ConnectionTypeChanged);

DeclareVar(bool, LocalPasscode);
DeclareRefVar(base::Observable<void>, LocalPasscodeChanged);

DeclareRefVar(base::Variable<DBIWorkMode>, WorkMode);

DeclareRefVar(base::Observable<void>, PeerChooseCancel);

} // namespace Global

namespace Adaptive {

inline base::Observable<void> &Changed() {
	return Global::RefAdaptiveChanged();
}

inline bool OneColumn() {
	return Global::AdaptiveWindowLayout() == WindowLayout::OneColumn;
}

inline bool Normal() {
	return Global::AdaptiveWindowLayout() == WindowLayout::Normal;
}

inline bool ThreeColumn() {
	return Global::AdaptiveWindowLayout() == WindowLayout::ThreeColumn;
}

} // namespace Adaptive
