/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_instance.h"

#include "mtproto/mtproto_dh_utils.h"
#include "core/application.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "calls/calls_call.h"
#include "calls/calls_panel.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "media/audio/media_audio_track.h"
#include "platform/platform_specific.h"
#include "base/unixtime.h"
#include "mainwidget.h"
#include "mtproto/mtproto_config.h"
#include "boxes/rate_call_box.h"
#include "tgcalls/VideoCaptureInterface.h"
#include "app.h"

namespace Calls {
namespace {

constexpr auto kServerConfigUpdateTimeoutMs = 24 * 3600 * crl::time(1000);

} // namespace

Instance::Instance() = default;

Instance::~Instance() = default;

void Instance::startOutgoingCall(not_null<UserData*> user, bool video) {
	if (alreadyInCall()) { // Already in a call.
		_currentCallPanel->showAndActivate();
		return;
	}
	if (user->callsStatus() == UserData::CallsStatus::Private) {
		// Request full user once more to refresh the setting in case it was changed.
		user->session().api().requestFullPeer(user);
		Ui::show(Box<InformBox>(
			tr::lng_call_error_not_available(tr::now, lt_user, user->name)));
		return;
	}
	requestPermissionsOrFail(crl::guard(this, [=] {
		createCall(user, Call::Type::Outgoing, video);
	}));
}

void Instance::callFinished(not_null<Call*> call) {
	crl::on_main(call, [=] {
		destroyCall(call);
	});
}

void Instance::callFailed(not_null<Call*> call) {
	crl::on_main(call, [=] {
		destroyCall(call);
	});
}

void Instance::callRedial(not_null<Call*> call) {
	if (_currentCall.get() == call) {
		refreshDhConfig();
	}
}

void Instance::playSound(Sound sound) {
	switch (sound) {
	case Sound::Busy: {
		if (!_callBusyTrack) {
			_callBusyTrack = Media::Audio::Current().createTrack();
			_callBusyTrack->fillFromFile(
				Core::App().settings().getSoundPath(qsl("call_busy")));
		}
		_callBusyTrack->playOnce();
	} break;

	case Sound::Ended: {
		if (!_callEndedTrack) {
			_callEndedTrack = Media::Audio::Current().createTrack();
			_callEndedTrack->fillFromFile(
				Core::App().settings().getSoundPath(qsl("call_end")));
		}
		_callEndedTrack->playOnce();
	} break;

	case Sound::Connecting: {
		if (!_callConnectingTrack) {
			_callConnectingTrack = Media::Audio::Current().createTrack();
			_callConnectingTrack->fillFromFile(
				Core::App().settings().getSoundPath(qsl("call_connect")));
		}
		_callConnectingTrack->playOnce();
	} break;
	}
}

void Instance::destroyCall(not_null<Call*> call) {
	if (_currentCall.get() == call) {
		_currentCallPanel->closeBeforeDestroy();
		_currentCallPanel = nullptr;

		auto taken = base::take(_currentCall);
		_currentCallChanges.fire(nullptr);
		taken.reset();

		if (App::quitting()) {
			LOG(("Calls::Instance doesn't prevent quit any more."));
		}
		Core::App().quitPreventFinished();
	}
}

void Instance::createCall(not_null<UserData*> user, Call::Type type, bool video) {
	auto call = std::make_unique<Call>(getCallDelegate(), user, type, video);
	const auto raw = call.get();

	user->session().account().sessionChanges(
	) | rpl::start_with_next([=] {
		destroyCall(raw);
	}, raw->lifetime());

	if (_currentCall) {
		_currentCallPanel->replaceCall(raw);
		std::swap(_currentCall, call);
		call->hangup();
	} else {
		_currentCallPanel = std::make_unique<Panel>(raw);
		_currentCall = std::move(call);
	}
	_currentCallChanges.fire_copy(raw);
	refreshServerConfig(&user->session());
	refreshDhConfig();
}

void Instance::refreshDhConfig() {
	Expects(_currentCall != nullptr);

	const auto weak = base::make_weak(_currentCall);
	_currentCall->user()->session().api().request(MTPmessages_GetDhConfig(
		MTP_int(_dhConfig.version),
		MTP_int(MTP::ModExpFirst::kRandomPowerSize)
	)).done([=](const MTPmessages_DhConfig &result) {
		const auto call = weak.get();
		const auto random = updateDhConfig(result);
		if (!call) {
			return;
		}
		if (!random.empty()) {
			Assert(random.size() == MTP::ModExpFirst::kRandomPowerSize);
			call->start(random);
		} else {
			callFailed(call);
		}
	}).fail([=](const RPCError &error) {
		const auto call = weak.get();
		if (!call) {
			return;
		}
		callFailed(call);
	}).send();
}

bytes::const_span Instance::updateDhConfig(
		const MTPmessages_DhConfig &data) {
	const auto validRandom = [](const QByteArray & random) {
		if (random.size() != MTP::ModExpFirst::kRandomPowerSize) {
			return false;
		}
		return true;
	};
	return data.match([&](const MTPDmessages_dhConfig &data)
	-> bytes::const_span {
		auto primeBytes = bytes::make_vector(data.vp().v);
		if (!MTP::IsPrimeAndGood(primeBytes, data.vg().v)) {
			LOG(("API Error: bad p/g received in dhConfig."));
			return {};
		} else if (!validRandom(data.vrandom().v)) {
			return {};
		}
		_dhConfig.g = data.vg().v;
		_dhConfig.p = std::move(primeBytes);
		_dhConfig.version = data.vversion().v;
		return bytes::make_span(data.vrandom().v);
	}, [&](const MTPDmessages_dhConfigNotModified &data)
	-> bytes::const_span {
		if (!_dhConfig.g || _dhConfig.p.empty()) {
			LOG(("API Error: dhConfigNotModified on zero version."));
			return {};
		} else if (!validRandom(data.vrandom().v)) {
			return {};
		}
		return bytes::make_span(data.vrandom().v);
	});
}

void Instance::refreshServerConfig(not_null<Main::Session*> session) {
	if (_serverConfigRequestSession) {
		return;
	}
	if (_lastServerConfigUpdateTime
		&& ((crl::now() - _lastServerConfigUpdateTime)
			< kServerConfigUpdateTimeoutMs)) {
		return;
	}
	_serverConfigRequestSession = session;
	session->api().request(MTPphone_GetCallConfig(
	)).done([=](const MTPDataJSON &result) {
		_serverConfigRequestSession = nullptr;
		_lastServerConfigUpdateTime = crl::now();

		const auto &json = result.c_dataJSON().vdata().v;
		UpdateConfig(std::string(json.data(), json.size()));
	}).fail([=](const RPCError &error) {
		_serverConfigRequestSession = nullptr;
	}).send();
}

void Instance::handleUpdate(
		not_null<Main::Session*> session,
		const MTPUpdate &update) {
	update.match([&](const MTPDupdatePhoneCall &data) {
		handleCallUpdate(session, data.vphone_call());
	}, [&](const MTPDupdatePhoneCallSignalingData &data) {
		handleSignalingData(data);
	}, [](const auto &) {
		Unexpected("Update type in Calls::Instance::handleUpdate.");
	});
}

void Instance::showInfoPanel(not_null<Call*> call) {
	if (_currentCall.get() == call) {
		_currentCallPanel->showAndActivate();
	}
}

bool Instance::isQuitPrevent() {
	if (!_currentCall || _currentCall->isIncomingWaiting()) {
		return false;
	}
	_currentCall->hangup();
	if (!_currentCall) {
		return false;
	}
	LOG(("Calls::Instance prevents quit, hanging up a call..."));
	return true;
}

void Instance::handleCallUpdate(
		not_null<Main::Session*> session,
		const MTPPhoneCall &call) {
	if (call.type() == mtpc_phoneCallRequested) {
		auto &phoneCall = call.c_phoneCallRequested();
		auto user = session->data().userLoaded(phoneCall.vadmin_id().v);
		if (!user) {
			LOG(("API Error: User not loaded for phoneCallRequested."));
		} else if (user->isSelf()) {
			LOG(("API Error: Self found in phoneCallRequested."));
		}
		const auto &config = session->serverConfig();
		if (alreadyInCall() || !user || user->isSelf()) {
			const auto flags = phoneCall.is_video()
				? MTPphone_DiscardCall::Flag::f_video
				: MTPphone_DiscardCall::Flag(0);
			session->api().request(MTPphone_DiscardCall(
				MTP_flags(flags),
				MTP_inputPhoneCall(phoneCall.vid(), phoneCall.vaccess_hash()),
				MTP_int(0),
				MTP_phoneCallDiscardReasonBusy(),
				MTP_long(0)
			)).send();
		} else if (phoneCall.vdate().v + (config.callRingTimeoutMs / 1000)
			< base::unixtime::now()) {
			LOG(("Ignoring too old call."));
		} else {
			createCall(user, Call::Type::Incoming, phoneCall.is_video());
			_currentCall->handleUpdate(call);
		}
	} else if (!_currentCall || !_currentCall->handleUpdate(call)) {
		DEBUG_LOG(("API Warning: unexpected phone call update %1").arg(call.type()));
	}
}

void Instance::handleSignalingData(
		const MTPDupdatePhoneCallSignalingData &data) {
	if (!_currentCall || !_currentCall->handleSignalingData(data)) {
		DEBUG_LOG(("API Warning: unexpected call signaling data %1"
			).arg(data.vphone_call_id().v));
	}
}

bool Instance::alreadyInCall() {
	return (_currentCall && _currentCall->state() != Call::State::Busy);
}

Call *Instance::currentCall() const {
	return _currentCall.get();
}

rpl::producer<Call*> Instance::currentCallValue() const {
	return _currentCallChanges.events_starting_with(currentCall());
}

void Instance::requestPermissionsOrFail(Fn<void()> onSuccess) {
	using Type = Platform::PermissionType;
	requestPermissionOrFail(Type::Microphone, [=] {
		requestPermissionOrFail(Type::Camera, [=] {
			crl::on_main(onSuccess);
		});
	});
}

void Instance::requestPermissionOrFail(Platform::PermissionType type, Fn<void()> onSuccess) {
	using Status = Platform::PermissionStatus;
	const auto status = Platform::GetPermissionStatus(type);
	if (status == Status::Granted) {
		onSuccess();
	} else if (status == Status::CanRequest) {
		Platform::RequestPermission(type, crl::guard(this, [=](Status status) {
			if (status == Status::Granted) {
				crl::on_main(onSuccess);
			} else {
				if (_currentCall) {
					_currentCall->hangup();
				}
			}
		}));
	} else {
		if (alreadyInCall()) {
			_currentCall->hangup();
		}
		Ui::show(Box<ConfirmBox>(tr::lng_no_mic_permission(tr::now), tr::lng_menu_settings(tr::now), crl::guard(this, [=] {
			Platform::OpenSystemSettingsForPermission(type);
			Ui::hideLayer();
		})));
	}
}

std::shared_ptr<tgcalls::VideoCaptureInterface> Instance::getVideoCapture() {
	if (auto result = _videoCapture.lock()) {
		return result;
	}
	auto result = std::shared_ptr<tgcalls::VideoCaptureInterface>(
		tgcalls::VideoCaptureInterface::Create(
			Core::App().settings().callVideoInputDeviceId().toStdString()));
	_videoCapture = result;
	return result;
}

} // namespace Calls
