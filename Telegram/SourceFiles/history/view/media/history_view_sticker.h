/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media_unwrapped.h"
#include "base/weak_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {
struct FileOrigin;
class DocumentMedia;
} // namespace Data

namespace Lottie {
class SinglePlayer;
struct ColorReplacements;
} // namespace Lottie

namespace HistoryView {

class Sticker final
	: public UnwrappedMedia::Content
	, public base::has_weak_ptr {
public:
	Sticker(
		not_null<Element*> parent,
		not_null<DocumentData*> data,
		Element *replacing = nullptr,
		const Lottie::ColorReplacements *replacements = nullptr);
	~Sticker();

	void initSize();
	QSize size() override;
	void draw(Painter &p, const QRect &r, bool selected) override;
	ClickHandlerPtr link() override {
		return _link;
	}

	DocumentData *document() override {
		return _data;
	}
	void stickerClearLoopPlayed() override {
		_lottieOncePlayed = false;
	}
	std::unique_ptr<Lottie::SinglePlayer> stickerTakeLottie(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

	void refreshLink() override;

	void setDiceIndex(const QString &emoji, int index);
	[[nodiscard]] bool atTheEnd() const {
		return _atTheEnd;
	}
	[[nodiscard]] bool readyToDrawLottie();

	[[nodiscard]] static QSize GetAnimatedEmojiSize(
		not_null<Main::Session*> session);
	[[nodiscard]] static QSize GetAnimatedEmojiSize(
		not_null<Main::Session*> session,
		QSize documentSize);

private:
	[[nodiscard]] bool isEmojiSticker() const;
	void paintLottie(Painter &p, const QRect &r, bool selected);
	void paintPixmap(Painter &p, const QRect &r, bool selected);
	[[nodiscard]] QPixmap paintedPixmap(bool selected) const;

	void ensureDataMediaCreated() const;
	void dataMediaCreated() const;

	void setupLottie();
	void lottieCreated();
	void unloadLottie();

	const not_null<Element*> _parent;
	const not_null<DocumentData*> _data;
	const Lottie::ColorReplacements *_replacements = nullptr;
	std::unique_ptr<Lottie::SinglePlayer> _lottie;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	ClickHandlerPtr _link;
	QSize _size;
	QImage _lastDiceFrame;
	QString _diceEmoji;
	int _diceIndex = -1;
	mutable bool _lottieOncePlayed = false;
	mutable bool _atTheEnd = false;
	mutable bool _nextLastDiceFrame = false;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
