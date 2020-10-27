/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/touchbar/mac_touchbar_manager.h"

#ifndef OS_OSX

#include "apiwrap.h" // ApiWrap::updateStickers()
#include "core/application.h"
#include "data/data_peer.h" // PeerData::canWrite()
#include "data/data_session.h"
#include "data/stickers/data_stickers.h" // Stickers::setsRef()
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mainwidget.h" // MainWidget::closeBothPlayers
#include "media/player/media_player_instance.h"
#include "platform/mac/touchbar/mac_touchbar_audio.h"
#include "platform/mac/touchbar/mac_touchbar_common.h"
#include "platform/mac/touchbar/mac_touchbar_main.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#import <AppKit/NSGroupTouchBarItem.h>

using namespace TouchBar::Main;

namespace {

const auto kMainItemIdentifier = @"touchbarMain";
const auto kAudioItemIdentifier = @"touchbarAudio";

} // namespace

@interface GroupTouchBarItem : NSGroupTouchBarItem
- (rpl::lifetime &)lifetime;
@end // @interface GroupTouchBarItem

@implementation GroupTouchBarItem {
	rpl::lifetime _lifetime;
}

- (rpl::lifetime &)lifetime {
	return _lifetime;
}

@end // GroupTouchBarItem

#pragma mark - RootTouchBar

@interface RootTouchBar()
@end // @interface RootTouchBar

@implementation RootTouchBar {
	Main::Session *_session;
	Window::Controller *_controller;

	rpl::producer<bool> _canApplyMarkdown;
	rpl::event_stream<> _touchBarSwitches;
	rpl::lifetime _lifetime;
}

- (id)init:(rpl::producer<bool>)canApplyMarkdown
		controller:(not_null<Window::Controller*>)controller
		domain:(not_null<Main::Domain*>)domain {
	self = [super init];
	if (!self) {
		return self;
	}
	self.delegate = self;
	TouchBar::CustomEnterToCocoaEventLoop([=] {
		self.defaultItemIdentifiers = @[];
	});
	_controller = controller;
	_canApplyMarkdown = std::move(canApplyMarkdown);

	auto sessionChanges = domain->activeSessionChanges(
	) | rpl::map([=](Main::Session *session) {
		if (session && session->data().stickers().setsRef().empty()) {
			session->api().updateStickers();
		}
		return session;
	});

	const auto type = AudioMsgId::Type::Song;
	auto audioPlayer = rpl::merge(
		Media::Player::instance()->stops(type) | rpl::map_to(false),
		Media::Player::instance()->startsPlay(type) | rpl::map_to(true)
	);

	rpl::combine(
		std::move(sessionChanges),
		rpl::single(false) | rpl::then(Core::App().passcodeLockChanges()),
		rpl::single(false) | rpl::then(std::move(audioPlayer))
	) | rpl::start_with_next([=](
			Main::Session *session,
			bool lock,
			bool audio) {
		TouchBar::CustomEnterToCocoaEventLoop([=] {
			_touchBarSwitches.fire({});
			if (!audio) {
				self.defaultItemIdentifiers = @[];
			}
			self.defaultItemIdentifiers = lock
				? @[]
				: audio
				? @[kAudioItemIdentifier]
				: session
				? @[kMainItemIdentifier]
				: @[];
		});
	}, _lifetime);

	return self;
}

- (NSTouchBarItem*)touchBar:(NSTouchBar*)touchBar
		makeItemForIdentifier:(NSTouchBarItemIdentifier)itemId {
	if (!touchBar || !_controller->sessionController()) {
		return nil;
	}
	const auto isEqual = [&](NSString *string) {
		return [itemId isEqualToString:string];
	};

	if (isEqual(kMainItemIdentifier)) {
		auto *item = [[GroupTouchBarItem alloc] initWithIdentifier:itemId];
		item.groupTouchBar =
			[[[TouchBarMain alloc]
				init:_controller
				touchBarSwitches:_touchBarSwitches.events()] autorelease];
		rpl::combine(
			rpl::single(false) | rpl::then(rpl::duplicate(_canApplyMarkdown)),
			rpl::single(
				false
			) | rpl::then(
				_controller->sessionController()->activeChatChanges(
				) | rpl::map([](Dialogs::Key k) {
					return k.peer() && k.history() && k.peer()->canWrite();
				})
			) | rpl::distinct_until_changed()
		) | rpl::start_with_next([=](
				bool canApplyMarkdown,
				bool hasActiveChat) {
			item.groupTouchBar.defaultItemIdentifiers = @[
				kPinnedPanelItemIdentifier,
				canApplyMarkdown
					? kPopoverInputItemIdentifier
					: hasActiveChat
					? kPopoverPickerItemIdentifier
					: @""];
		}, [item lifetime]);

		return [item autorelease];
	} else if (isEqual(kAudioItemIdentifier)) {
		auto *item = [[GroupTouchBarItem alloc] initWithIdentifier:itemId];
		auto *touchBar = [[[TouchBarAudioPlayer alloc] init]
			autorelease];
		item.groupTouchBar = touchBar;
		[touchBar closeRequests] | rpl::start_with_next([=] {
			if (const auto session = _controller->sessionController()) {
				session->content()->closeBothPlayers();
			}
		}, [item lifetime]);
		return [item autorelease];
	}

	return nil;
}

@end // @implementation RootTouchBar

#endif // OS_OSX
