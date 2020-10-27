/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/toasts/common_toasts.h"

#include "ui/toast/toast.h"
#include "styles/style_td_common.h"

namespace Ui {

void ShowMultilineToast(MultilineToastArgs &&args) {
	Ui::Toast::Show(Ui::Toast::Config{
		.text = std::move(args.text),
		.st = &st::defaultMultilineToast,
		.multiline = true,
	});
}

} // namespace Ui
