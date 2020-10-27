/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/value_ordering.h"
#include "ui/text/text.h" // For QFIXED_MAX

class HistoryItem;
using HistoryItemsList = std::vector<not_null<HistoryItem*>>;

class StorageImageLocation;
class WebFileLocation;
struct GeoPointLocation;

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache
} // namespace Storage

namespace Ui {
class InputField;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Images {
enum class Option;
using Options = base::flags<Option>;
} // namespace Images

namespace Data {

struct UploadState {
	UploadState(int size) : size(size) {
	}
	int offset = 0;
	int size = 0;
	bool waitingForAlbum = false;
};

Storage::Cache::Key DocumentCacheKey(int32 dcId, uint64 id);
Storage::Cache::Key DocumentThumbCacheKey(int32 dcId, uint64 id);
Storage::Cache::Key WebDocumentCacheKey(const WebFileLocation &location);
Storage::Cache::Key UrlCacheKey(const QString &location);
Storage::Cache::Key GeoPointCacheKey(const GeoPointLocation &location);

constexpr auto kImageCacheTag = uint8(0x01);
constexpr auto kStickerCacheTag = uint8(0x02);
constexpr auto kVoiceMessageCacheTag = uint8(0x03);
constexpr auto kVideoMessageCacheTag = uint8(0x04);
constexpr auto kAnimationCacheTag = uint8(0x05);

struct FileOrigin;

} // namespace Data

struct MessageGroupId {
	uint64 peer = 0;
	uint64 value = 0;

	MessageGroupId() = default;
	static MessageGroupId FromRaw(uint64 peer, uint64 value) {
		auto result = MessageGroupId();
		result.peer = peer;
		result.value = value;
		return result;
	}

	bool empty() const {
		return !value;
	}
	explicit operator bool() const {
		return !empty();
	}
	uint64 raw() const {
		return value;
	}

	friend inline std::pair<uint64, uint64> value_ordering_helper(MessageGroupId value) {
		return std::make_pair(value.value, value.peer);
	}

};

class PeerData;
class UserData;
class ChatData;
class ChannelData;
class BotCommand;
struct BotInfo;

namespace Data {
class Folder;
} // namespace Data

using UserId = int32;
using ChatId = int32;
using ChannelId = int32;
using FolderId = int32;
using FilterId = int32;

constexpr auto NoChannel = ChannelId(0);

using PeerId = uint64;

constexpr auto PeerIdMask         = PeerId(0xFFFFFFFFULL);
constexpr auto PeerIdTypeMask     = PeerId(0xF00000000ULL);
constexpr auto PeerIdUserShift    = PeerId(0x000000000ULL);
constexpr auto PeerIdChatShift    = PeerId(0x100000000ULL);
constexpr auto PeerIdChannelShift = PeerId(0x200000000ULL);
constexpr auto PeerIdFakeShift    = PeerId(0xF00000000ULL);

inline constexpr bool peerIsUser(const PeerId &id) {
	return (id & PeerIdTypeMask) == PeerIdUserShift;
}
inline constexpr bool peerIsChat(const PeerId &id) {
	return (id & PeerIdTypeMask) == PeerIdChatShift;
}
inline constexpr bool peerIsChannel(const PeerId &id) {
	return (id & PeerIdTypeMask) == PeerIdChannelShift;
}
inline constexpr PeerId peerFromUser(UserId user_id) {
	return PeerIdUserShift | uint64(uint32(user_id));
}
inline constexpr PeerId peerFromChat(ChatId chat_id) {
	return PeerIdChatShift | uint64(uint32(chat_id));
}
inline constexpr PeerId peerFromChannel(ChannelId channel_id) {
	return PeerIdChannelShift | uint64(uint32(channel_id));
}
inline constexpr PeerId peerFromUser(const MTPint &user_id) {
	return peerFromUser(user_id.v);
}
inline constexpr PeerId peerFromChat(const MTPint &chat_id) {
	return peerFromChat(chat_id.v);
}
inline constexpr PeerId peerFromChannel(const MTPint &channel_id) {
	return peerFromChannel(channel_id.v);
}
inline constexpr int32 peerToBareInt(const PeerId &id) {
	return int32(uint32(id & PeerIdMask));
}
inline constexpr UserId peerToUser(const PeerId &id) {
	return peerIsUser(id) ? peerToBareInt(id) : 0;
}
inline constexpr ChatId peerToChat(const PeerId &id) {
	return peerIsChat(id) ? peerToBareInt(id) : 0;
}
inline constexpr ChannelId peerToChannel(const PeerId &id) {
	return peerIsChannel(id) ? peerToBareInt(id) : NoChannel;
}
inline MTPint peerToBareMTPInt(const PeerId &id) {
	return MTP_int(peerToBareInt(id));
}
inline PeerId peerFromMTP(const MTPPeer &peer) {
	switch (peer.type()) {
	case mtpc_peerUser: return peerFromUser(peer.c_peerUser().vuser_id());
	case mtpc_peerChat: return peerFromChat(peer.c_peerChat().vchat_id());
	case mtpc_peerChannel: return peerFromChannel(peer.c_peerChannel().vchannel_id());
	}
	return 0;
}
inline MTPpeer peerToMTP(const PeerId &id) {
	if (peerIsUser(id)) {
		return MTP_peerUser(peerToBareMTPInt(id));
	} else if (peerIsChat(id)) {
		return MTP_peerChat(peerToBareMTPInt(id));
	} else if (peerIsChannel(id)) {
		return MTP_peerChannel(peerToBareMTPInt(id));
	}
	return MTP_peerUser(MTP_int(0));
}

using MsgId = int32;
constexpr auto StartClientMsgId = MsgId(-0x7FFFFFFF);
constexpr auto EndClientMsgId = MsgId(-0x40000000);
constexpr auto ShowAtTheEndMsgId = MsgId(-0x40000000);
constexpr auto SwitchAtTopMsgId = MsgId(-0x3FFFFFFF);
constexpr auto ShowAtProfileMsgId = MsgId(-0x3FFFFFFE);
constexpr auto ShowAndStartBotMsgId = MsgId(-0x3FFFFFD);
constexpr auto ShowAtGameShareMsgId = MsgId(-0x3FFFFFC);
constexpr auto ServerMaxMsgId = MsgId(0x3FFFFFFF);
constexpr auto ShowAtUnreadMsgId = MsgId(0);
constexpr inline bool IsClientMsgId(MsgId id) {
	return (id >= StartClientMsgId && id < EndClientMsgId);
}
constexpr inline bool IsServerMsgId(MsgId id) {
	return (id > 0 && id < ServerMaxMsgId);
}

struct MsgRange {
	MsgRange() = default;
	MsgRange(MsgId from, MsgId till) : from(from), till(till) {
	}

	MsgId from = 0;
	MsgId till = 0;
};
inline bool operator==(const MsgRange &a, const MsgRange &b) {
	return (a.from == b.from) && (a.till == b.till);
}
inline bool operator!=(const MsgRange &a, const MsgRange &b) {
	return !(a == b);
}

struct FullMsgId {
	constexpr FullMsgId() = default;
	constexpr FullMsgId(ChannelId channel, MsgId msg) : channel(channel), msg(msg) {
	}

	explicit operator bool() const {
		return msg != 0;
	}


	inline constexpr bool operator<(const FullMsgId &other) const {
		if (channel < other.channel) {
			return true;
		} else if (channel > other.channel) {
			return false;
		}
		return msg < other.msg;
	}
	inline constexpr bool operator>(const FullMsgId &other) const {
		return other < *this;
	}
	inline constexpr bool operator<=(const FullMsgId &other) const {
		return !(other < *this);
	}
	inline constexpr bool operator>=(const FullMsgId &other) const {
		return !(*this < other);
	}
	inline constexpr bool operator==(const FullMsgId &other) const {
		return (channel == other.channel) && (msg == other.msg);
	}
	inline constexpr bool operator!=(const FullMsgId &other) const {
		return !(*this == other);
	}

	ChannelId channel = NoChannel;
	MsgId msg = 0;

};

Q_DECLARE_METATYPE(FullMsgId);

using MessageIdsList = std::vector<FullMsgId>;

PeerId PeerFromMessage(const MTPmessage &message);
MTPDmessage::Flags FlagsFromMessage(const MTPmessage &message);
MsgId IdFromMessage(const MTPmessage &message);
TimeId DateFromMessage(const MTPmessage &message);

class DocumentData;
class PhotoData;
struct WebPageData;
struct GameData;
struct PollData;

class AudioMsgId;
class PhotoClickHandler;
class PhotoOpenClickHandler;
class PhotoSaveClickHandler;
class PhotoCancelClickHandler;
class DocumentClickHandler;
class DocumentSaveClickHandler;
class DocumentOpenClickHandler;
class DocumentCancelClickHandler;
class DocumentWrappedClickHandler;
class VoiceSeekClickHandler;

using PhotoId = uint64;
using VideoId = uint64;
using AudioId = uint64;
using DocumentId = uint64;
using WebPageId = uint64;
using GameId = uint64;
using PollId = uint64;
using WallPaperId = uint64;
constexpr auto CancelledWebPageId = WebPageId(0xFFFFFFFFFFFFFFFFULL);

struct PreparedPhotoThumb {
	QImage image;
	QByteArray bytes;
};
using PreparedPhotoThumbs = base::flat_map<char, PreparedPhotoThumb>;

// [0] == -1 -- counting, [0] == -2 -- could not count
using VoiceWaveform = QVector<signed char>;

enum LocationType {
	UnknownFileLocation = 0,
	// 1, 2, etc are used as "version" value in mediaKey() method.

	DocumentFileLocation = 0x4e45abe9, // mtpc_inputDocumentFileLocation
	AudioFileLocation = 0x74dc404d, // mtpc_inputAudioFileLocation
	VideoFileLocation = 0x3d0364ec, // mtpc_inputVideoFileLocation
	SecureFileLocation = 0xcbc7ee28, // mtpc_inputSecureFileLocation
};

enum FileStatus {
	FileDownloadFailed = -2,
	FileUploadFailed = -1,
	FileReady = 1,
};

// Don't change the values. This type is used for serialization.
enum DocumentType {
	FileDocument = 0,
	VideoDocument = 1,
	SongDocument = 2,
	StickerDocument = 3,
	AnimatedDocument = 4,
	VoiceDocument = 5,
	RoundVideoDocument = 6,
	WallPaperDocument = 7,
};

inline constexpr auto kStickerSideSize = 512;

[[nodiscard]] inline bool GoodStickerDimensions(int width, int height) {
	return (width > 0 && width <= kStickerSideSize)
		&& (height > 0 && height <= kStickerSideSize)
		&& (width == kStickerSideSize || height == kStickerSideSize);
}

using MediaKey = QPair<uint64, uint64>;

class AudioMsgId {
public:
	enum class Type {
		Unknown,
		Voice,
		Song,
		Video,
	};

	AudioMsgId() = default;
	AudioMsgId(
		not_null<DocumentData*> audio,
		FullMsgId msgId,
		uint32 externalPlayId = 0)
	: _audio(audio)
	, _contextId(msgId)
	, _externalPlayId(externalPlayId) {
		setTypeFromAudio();
	}

	[[nodiscard]] static uint32 CreateExternalPlayId();
	[[nodiscard]] static AudioMsgId ForVideo();

	[[nodiscard]] Type type() const {
		return _type;
	}
	[[nodiscard]] DocumentData *audio() const {
		return _audio;
	}
	[[nodiscard]] FullMsgId contextId() const {
		return _contextId;
	}
	[[nodiscard]] uint32 externalPlayId() const {
		return _externalPlayId;
	}
	[[nodiscard]] explicit operator bool() const {
		return (_audio != nullptr) || (_externalPlayId != 0);
	}

private:
	void setTypeFromAudio();

	DocumentData *_audio = nullptr;
	Type _type = Type::Unknown;
	FullMsgId _contextId;
	uint32 _externalPlayId = 0;

};

inline bool operator<(const AudioMsgId &a, const AudioMsgId &b) {
	if (quintptr(a.audio()) < quintptr(b.audio())) {
		return true;
	} else if (quintptr(b.audio()) < quintptr(a.audio())) {
		return false;
	} else if (a.contextId() < b.contextId()) {
		return true;
	} else if (b.contextId() < a.contextId()) {
		return false;
	}
	return (a.externalPlayId() < b.externalPlayId());
}

inline bool operator==(const AudioMsgId &a, const AudioMsgId &b) {
	return (a.audio() == b.audio())
		&& (a.contextId() == b.contextId())
		&& (a.externalPlayId() == b.externalPlayId());
}

inline bool operator!=(const AudioMsgId &a, const AudioMsgId &b) {
	return !(a == b);
}

struct MessageCursor {
	MessageCursor() = default;
	MessageCursor(int position, int anchor, int scroll)
	: position(position)
	, anchor(anchor)
	, scroll(scroll) {
	}
	MessageCursor(not_null<const Ui::InputField*> field) {
		fillFrom(field);
	}

	void fillFrom(not_null<const Ui::InputField*> field);
	void applyTo(not_null<Ui::InputField*> field);

	int position = 0;
	int anchor = 0;
	int scroll = QFIXED_MAX;

};

inline bool operator==(
		const MessageCursor &a,
		const MessageCursor &b) {
	return (a.position == b.position)
		&& (a.anchor == b.anchor)
		&& (a.scroll == b.scroll);
}

class FileClickHandler : public LeftButtonClickHandler {
public:
	FileClickHandler(
		not_null<Main::Session*> session,
		FullMsgId context)
	: _session(session)
	, _context(context) {
	}

	[[nodiscard]] Main::Session &session() const {
		return *_session;
	}

	void setMessageId(FullMsgId context) {
		_context = context;
	}

	[[nodiscard]] FullMsgId context() const {
		return _context;
	}

protected:
	HistoryItem *getActionItem() const;

private:
	const not_null<Main::Session*> _session;
	FullMsgId _context;

};
