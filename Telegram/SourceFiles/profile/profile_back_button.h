/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"

namespace Main {
class Session;
} // namespace Main

namespace Profile {

class BackButton final : public Ui::AbstractButton, private base::Subscriber {
public:
	BackButton(
		QWidget *parent,
		not_null<Main::Session*> session,
		const QString &text);

	void setText(const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;
	void onStateChanged(State was, StateChangeSource source) override;

private:
	void updateAdaptiveLayout();

	const not_null<Main::Session*> _session;

	rpl::lifetime _unreadBadgeLifetime;
	QString _text;

};

} // namespace Profile
