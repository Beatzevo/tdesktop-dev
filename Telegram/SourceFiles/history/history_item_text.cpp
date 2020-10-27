/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item_text.h"

#include "history/history_item.h"
#include "history/history_item_components.h"
#include "data/data_media_types.h"
#include "data/data_web_page.h"
#include "data/data_groups.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "ui/text_options.h"

TextForMimeData WrapAsReply(
		TextForMimeData &&text,
		not_null<HistoryItem*> to) {
	const auto name = to->author()->name;
	auto result = TextForMimeData();
	result.reserve(
		tr::lng_in_reply_to(tr::now).size()
		+ name.size()
		+ 4
		+ text.expanded.size());
	return result.append('['
	).append(tr::lng_in_reply_to(tr::now)
	).append(' '
	).append(name
	).append(qstr("]\n")
	).append(std::move(text));
}

TextForMimeData WrapAsForwarded(
		TextForMimeData &&text,
		not_null<HistoryMessageForwarded*> forwarded) {
	auto info = forwarded->text.toTextForMimeData();
	auto result = TextForMimeData();
	result.reserve(
		info.expanded.size() + 4 + text.expanded.size(),
		info.rich.entities.size() + text.rich.entities.size());
	return result.append('['
	).append(std::move(info)
	).append(qstr("]\n")
	).append(std::move(text));
}

TextForMimeData WrapAsItem(
		not_null<HistoryItem*> item,
		TextForMimeData &&result) {
	if (const auto reply = item->Get<HistoryMessageReply>()) {
		if (const auto message = reply->replyToMsg) {
			result = WrapAsReply(std::move(result), message);
		}
	}
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		result = WrapAsForwarded(std::move(result), forwarded);
	}
	return std::move(result);
}

TextForMimeData HistoryItemText(not_null<HistoryItem*> item) {
	const auto media = item->media();

	auto mediaResult = media ? media->clipboardText() : TextForMimeData();
	auto textResult = mediaResult.empty()
		? item->clipboardText()
		: TextForMimeData();
	auto logEntryOriginalResult = [&] {
		const auto entry = item->Get<HistoryMessageLogEntryOriginal>();
		if (!entry) {
			return TextForMimeData();
		}
		const auto title = TextUtilities::SingleLine(entry->page->title.isEmpty()
			? entry->page->author
			: entry->page->title);
		auto titleResult = TextForMimeData::Rich(
			TextUtilities::ParseEntities(
				title,
				Ui::WebpageTextTitleOptions().flags));
		auto descriptionResult = TextForMimeData::Rich(
			base::duplicate(entry->page->description));
		if (titleResult.empty()) {
			return descriptionResult;
		} else if (descriptionResult.empty()) {
			return titleResult;
		}
		titleResult.append('\n').append(std::move(descriptionResult));
		return titleResult;
	}();
	auto result = textResult;
	if (result.empty()) {
		result = std::move(mediaResult);
	} else if (!mediaResult.empty()) {
		result.append(qstr("\n\n")).append(std::move(mediaResult));
	}
	if (result.empty()) {
		result = std::move(logEntryOriginalResult);
	} else if (!logEntryOriginalResult.empty()) {
		result.append(qstr("\n\n")).append(std::move(logEntryOriginalResult));
	}
	return WrapAsItem(item, std::move(result));
}

TextForMimeData HistoryGroupText(not_null<const Data::Group*> group) {
	Expects(!group->items.empty());

	auto caption = [&] {
		auto &&nonempty = ranges::view::all(
			group->items
		) | ranges::view::filter([](not_null<HistoryItem*> item) {
			return !item->clipboardText().empty();
		}) | ranges::view::take(2);
		auto first = nonempty.begin();
		auto end = nonempty.end();
		if (first == end) {
			return TextForMimeData();
		}
		auto result = (*first)->clipboardText();
		return (++first == end) ? result : TextForMimeData();
	}();
	return WrapAsItem(group->items.back(), Data::WithCaptionClipboardText(
		tr::lng_in_dlg_album(tr::now),
		std::move(caption)));
}
