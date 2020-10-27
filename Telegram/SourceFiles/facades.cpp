/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "facades.h"

#include "api/api_bot.h"
#include "info/info_memento.h"
#include "core/click_handler_types.h"
#include "core/application.h"
#include "media/clip/media_clip_reader.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "history/history_item_components.h"
#include "base/platform/base_platform_info.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "boxes/confirm_box.h"
#include "boxes/url_auth_box.h"
#include "ui/layers/layer_widget.h"
#include "lang/lang_keys.h"
#include "base/observer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/media/history_view_media.h"
#include "styles/style_history.h"
#include "data/data_session.h"

namespace {

[[nodiscard]] MainWidget *CheckMainWidget(not_null<Main::Session*> session) {
	if (const auto m = App::main()) { // multi good
		if (&m->session() == session) {
			return m;
		}
	}
	if (&Core::App().domain().active() != &session->account()) {
		Core::App().domain().activate(&session->account());
	}
	if (const auto m = App::main()) { // multi good
		if (&m->session() == session) {
			return m;
		}
	}
	return nullptr;
}

} // namespace

namespace App {

void sendBotCommand(
		not_null<PeerData*> peer,
		UserData *bot,
		const QString &cmd, MsgId replyTo) {
	if (const auto m = CheckMainWidget(&peer->session())) {
		m->sendBotCommand(peer, bot, cmd, replyTo);
	}
}

void hideSingleUseKeyboard(not_null<const HistoryItem*> message) {
	if (const auto m = CheckMainWidget(&message->history()->session())) {
		m->hideSingleUseKeyboard(message->history()->peer, message->id);
	}
}

bool insertBotCommand(const QString &cmd) {
	if (const auto m = App::main()) { // multi good
		return m->insertBotCommand(cmd);
	}
	return false;
}

void activateBotCommand(
		not_null<const HistoryItem*> msg,
		int row,
		int column) {
	const auto button = HistoryMessageMarkupButton::Get(
		&msg->history()->owner(),
		msg->fullId(),
		row,
		column);
	if (!button) {
		return;
	}

	using ButtonType = HistoryMessageMarkupButton::Type;
	switch (button->type) {
	case ButtonType::Default: {
		// Copy string before passing it to the sending method
		// because the original button can be destroyed inside.
		MsgId replyTo = (msg->id > 0) ? msg->id : 0;
		sendBotCommand(
			msg->history()->peer,
			msg->fromOriginal()->asUser(),
			QString(button->text),
			replyTo);
	} break;

	case ButtonType::Callback:
	case ButtonType::Game: {
		Api::SendBotCallbackData(
			const_cast<HistoryItem*>(msg.get()),
			row,
			column);
	} break;

	case ButtonType::CallbackWithPassword: {
		Api::SendBotCallbackDataWithPassword(
			const_cast<HistoryItem*>(msg.get()),
			row,
			column);
	} break;

	case ButtonType::Buy: {
		Ui::show(Box<InformBox>(tr::lng_payments_not_supported(tr::now)));
	} break;

	case ButtonType::Url: {
		auto url = QString::fromUtf8(button->data);
		auto skipConfirmation = false;
		if (const auto bot = msg->getMessageBot()) {
			if (bot->isVerified()) {
				skipConfirmation = true;
			}
		}
		if (skipConfirmation) {
			UrlClickHandler::Open(url);
		} else {
			HiddenUrlClickHandler::Open(url);
		}
	} break;

	case ButtonType::RequestLocation: {
		hideSingleUseKeyboard(msg);
		Ui::show(Box<InformBox>(
			tr::lng_bot_share_location_unavailable(tr::now)));
	} break;

	case ButtonType::RequestPhone: {
		hideSingleUseKeyboard(msg);
		const auto msgId = msg->id;
		const auto history = msg->history();
		Ui::show(Box<ConfirmBox>(tr::lng_bot_share_phone(tr::now), tr::lng_bot_share_phone_confirm(tr::now), [=] {
			Ui::showPeerHistory(history, ShowAtTheEndMsgId);
			auto action = Api::SendAction(history);
			action.clearDraft = false;
			action.replyTo = msgId;
			history->session().api().shareContact(
				history->session().user(),
				action);
		}));
	} break;

	case ButtonType::RequestPoll: {
		hideSingleUseKeyboard(msg);
		auto chosen = PollData::Flags();
		auto disabled = PollData::Flags();
		if (!button->data.isEmpty()) {
			disabled |= PollData::Flag::Quiz;
			if (button->data[0]) {
				chosen |= PollData::Flag::Quiz;
			}
		}
		if (const auto m = CheckMainWidget(&msg->history()->session())) {
			Window::PeerMenuCreatePoll(m->controller(), msg->history()->peer, chosen, disabled);
		}
	} break;

	case ButtonType::SwitchInlineSame:
	case ButtonType::SwitchInline: {
		const auto session = &msg->history()->session();
		if (const auto m = CheckMainWidget(session)) {
			if (const auto bot = msg->getMessageBot()) {
				const auto fastSwitchDone = [&] {
					auto samePeer = (button->type == ButtonType::SwitchInlineSame);
					if (samePeer) {
						Notify::switchInlineBotButtonReceived(session, QString::fromUtf8(button->data), bot, msg->id);
						return true;
					} else if (bot->isBot() && bot->botInfo->inlineReturnPeerId) {
						if (Notify::switchInlineBotButtonReceived(session, QString::fromUtf8(button->data))) {
							return true;
						}
					}
					return false;
				}();
				if (!fastSwitchDone) {
					m->inlineSwitchLayer('@' + bot->username + ' ' + QString::fromUtf8(button->data));
				}
			}
		}
	} break;

	case ButtonType::Auth:
		UrlAuthBox::Activate(msg, row, column);
		break;
	}
}

void searchByHashtag(const QString &tag, PeerData *inPeer) {
	const auto m = inPeer
		? CheckMainWidget(&inPeer->session())
		: App::main(); // multi good
	if (m) {
		if (m->controller()->openedFolder().current()) {
			m->controller()->closeFolder();
		}
		Ui::hideSettingsAndLayer();
		Core::App().hideMediaView();
		m->searchMessages(
			tag + ' ',
			(inPeer && !inPeer->isUser())
			? inPeer->owner().history(inPeer).get()
			: Dialogs::Key());
	}
}

void showSettings() {
	if (auto w = App::wnd()) {
		w->showSettings();
	}
}

} // namespace App

namespace Ui {

void showPeerProfile(not_null<PeerData*> peer) {
	if (const auto window = App::wnd()) { // multi good
		if (const auto controller = window->sessionController()) {
			if (&controller->session() == &peer->session()) {
				controller->showPeerInfo(peer);
				return;
			}
		}
		if (&Core::App().domain().active() != &peer->session().account()) {
			Core::App().domain().activate(&peer->session().account());
		}
		if (const auto controller = window->sessionController()) {
			if (&controller->session() == &peer->session()) {
				controller->showPeerInfo(peer);
			}
		}
	}
}

void showPeerProfile(not_null<const History*> history) {
	showPeerProfile(history->peer);
}

void showChatsList(not_null<Main::Session*> session) {
	if (const auto m = CheckMainWidget(session)) {
		m->ui_showPeerHistory(
			0,
			Window::SectionShow::Way::ClearStack,
			0);
	}
}

void showPeerHistoryAtItem(not_null<const HistoryItem*> item) {
	showPeerHistory(item->history()->peer, item->id);
}

void showPeerHistory(not_null<const History*> history, MsgId msgId) {
	showPeerHistory(history->peer, msgId);
}

void showPeerHistory(not_null<const PeerData*> peer, MsgId msgId) {
	if (const auto m = CheckMainWidget(&peer->session())) {
		m->ui_showPeerHistory(
			peer->id,
			Window::SectionShow::Way::ClearStack,
			msgId);
	}
}

PeerData *getPeerForMouseAction() {
	return Core::App().ui_getPeerForMouseAction();
}

bool skipPaintEvent(QWidget *widget, QPaintEvent *event) {
	if (auto w = App::wnd()) {
		if (w->contentOverlapped(widget, event)) {
			return true;
		}
	}
	return false;
}

} // namespace Ui

namespace Notify {

bool switchInlineBotButtonReceived(
		not_null<Main::Session*> session,
		const QString &query,
		UserData *samePeerBot,
		MsgId samePeerReplyTo) {
	if (const auto m = CheckMainWidget(session)) {
		return m->notify_switchInlineBotButtonReceived(
			query,
			samePeerBot,
			samePeerReplyTo);
	}
	return false;
}

} // namespace Notify

#define DefineReadOnlyVar(Namespace, Type, Name) const Type &Name() { \
	AssertCustom(Namespace##Data != nullptr, #Namespace "Data != nullptr in " #Namespace "::" #Name); \
	return Namespace##Data->Name; \
}
#define DefineRefVar(Namespace, Type, Name) DefineReadOnlyVar(Namespace, Type, Name) \
Type &Ref##Name() { \
	AssertCustom(Namespace##Data != nullptr, #Namespace "Data != nullptr in " #Namespace "::Ref" #Name); \
	return Namespace##Data->Name; \
}
#define DefineVar(Namespace, Type, Name) DefineRefVar(Namespace, Type, Name) \
void Set##Name(const Type &Name) { \
	AssertCustom(Namespace##Data != nullptr, #Namespace "Data != nullptr in " #Namespace "::Set" #Name); \
	Namespace##Data->Name = Name; \
}

namespace Global {
namespace internal {

struct Data {
	bool ScreenIsLocked = false;
	Adaptive::WindowLayout AdaptiveWindowLayout = Adaptive::WindowLayout::Normal;
	Adaptive::ChatLayout AdaptiveChatLayout = Adaptive::ChatLayout::Normal;
	base::Observable<void> AdaptiveChanged;

	bool NotificationsDemoIsShown = false;

	bool TryIPv6 = !Platform::IsWindows();
	std::vector<MTP::ProxyData> ProxiesList;
	MTP::ProxyData SelectedProxy;
	MTP::ProxyData::Settings ProxySettings = MTP::ProxyData::Settings::System;
	bool UseProxyForCalls = false;
	base::Observable<void> ConnectionTypeChanged;

	bool LocalPasscode = false;
	base::Observable<void> LocalPasscodeChanged;

	base::Variable<DBIWorkMode> WorkMode = { dbiwmWindowAndTray };

	base::Observable<void> PeerChooseCancel;
};

} // namespace internal
} // namespace Global

Global::internal::Data *GlobalData = nullptr;

namespace Global {

bool started() {
	return GlobalData != nullptr;
}

void start() {
	GlobalData = new internal::Data();
}

void finish() {
	delete GlobalData;
	GlobalData = nullptr;
}

DefineVar(Global, bool, ScreenIsLocked);
DefineVar(Global, Adaptive::WindowLayout, AdaptiveWindowLayout);
DefineVar(Global, Adaptive::ChatLayout, AdaptiveChatLayout);
DefineRefVar(Global, base::Observable<void>, AdaptiveChanged);

DefineVar(Global, bool, NotificationsDemoIsShown);

DefineVar(Global, bool, TryIPv6);
DefineVar(Global, std::vector<MTP::ProxyData>, ProxiesList);
DefineVar(Global, MTP::ProxyData, SelectedProxy);
DefineVar(Global, MTP::ProxyData::Settings, ProxySettings);
DefineVar(Global, bool, UseProxyForCalls);
DefineRefVar(Global, base::Observable<void>, ConnectionTypeChanged);

DefineVar(Global, bool, LocalPasscode);
DefineRefVar(Global, base::Observable<void>, LocalPasscodeChanged);

DefineRefVar(Global, base::Variable<DBIWorkMode>, WorkMode);

DefineRefVar(Global, base::Observable<void>, PeerChooseCancel);

} // namespace Global
