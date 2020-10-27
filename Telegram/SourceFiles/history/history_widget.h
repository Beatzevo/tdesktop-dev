/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_drag_area.h"
#include "ui/widgets/tooltip.h"
#include "mainwidget.h"
#include "chat_helpers/field_autocomplete.h"
#include "window/section_widget.h"
#include "ui/widgets/input_fields.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "mtproto/sender.h"
#include "base/flags.h"
#include "base/timer.h"

class RPCError;
struct FileLoadResult;
struct FileMediaInformation;
struct SendingAlbum;
enum class SendMediaType;
enum class CompressConfirm;
class MessageLinksParser;

namespace SendMenu {
enum class Type;
} // namespace SendMenu

namespace Api {
struct SendOptions;
} // namespace Api

namespace InlineBots {
namespace Layout {
class ItemBase;
class Widget;
} // namespace Layout
class Result;
} // namespace InlineBots

namespace Data {
struct Draft;
} // namespace Data

namespace Support {
class Autocomplete;
struct Contact;
} // namespace Support

namespace Ui {
class AbstractButton;
class InnerDropdown;
class DropdownMenu;
class PlainShadow;
class PopupMenu;
class IconButton;
class HistoryDownButton;
class EmojiButton;
class SendButton;
class SilentToggle;
class FlatButton;
class LinkButton;
class RoundButton;
namespace Toast {
class Instance;
} // namespace Toast
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace ChatHelpers {
class TabbedPanel;
class TabbedSection;
class TabbedSelector;
} // namespace ChatHelpers

namespace Storage {
enum class MimeDataState;
struct PreparedList;
struct UploadedPhoto;
struct UploadedDocument;
struct UploadedThumbDocument;
} // namespace Storage

namespace HistoryView {
class TopBarWidget;
class ContactStatus;
class Element;
} // namespace HistoryView

class DragArea;
class SendFilesBox;
class BotKeyboard;
class MessageField;
class HistoryInner;
struct HistoryMessageMarkupButton;

class HistoryWidget final : public Window::AbstractSectionWidget {
	Q_OBJECT

public:
	using FieldHistoryAction = Ui::InputField::HistoryAction;

	HistoryWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void start();

	void historyLoaded();

	// When resizing the widget with top edge moved up or down and we
	// want to add this top movement to the scroll position, so inner
	// content will not move.
	void setGeometryWithTopMoved(const QRect &newGeometry, int topDelta);

	void windowShown();
	[[nodiscard]] bool doWeReadServerHistory() const;
	[[nodiscard]] bool doWeReadMentions() const;
	bool skipItemRepaint();
	void checkHistoryActivation();

	void leaveToChildEvent(QEvent *e, QWidget *child) override;

	bool isItemCompletelyHidden(HistoryItem *item) const;
	void updateTopBarSelection();

	void loadMessages();
	void loadMessagesDown();
	void firstLoadMessages();
	void delayedShowAt(MsgId showAtMsgId);

	QRect historyRect() const;

	void updateFieldPlaceholder();
	void updateStickersByEmoji();

	bool confirmSendingFiles(const QStringList &files);
	bool confirmSendingFiles(not_null<const QMimeData*> data);
	void sendFileConfirmed(const std::shared_ptr<FileLoadResult> &file,
		const std::optional<FullMsgId> &oldId = std::nullopt);

	void updateControlsVisibility();
	void updateControlsGeometry();

	History *history() const;
	PeerData *peer() const;
	void setMsgId(MsgId showAtMsgId);
	MsgId msgId() const;

	bool hasTopBarShadow() const {
		return peer() != nullptr;
	}
	void showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params);
	void finishAnimating();

	void doneShow();

	QPoint clampMousePosition(QPoint point);

	void checkSelectingScroll(QPoint point);
	void noSelectingScroll();

	bool touchScroll(const QPoint &delta);

	void enqueueMessageHighlight(not_null<HistoryView::Element*> view);
	crl::time highlightStartTime(not_null<const HistoryItem*> item) const;

	MessageIdsList getSelectedItems() const;
	void itemEdited(not_null<HistoryItem*> item);

	void updateScrollColors();

	void replyToMessage(FullMsgId itemId);
	void replyToMessage(not_null<HistoryItem*> item);
	void editMessage(FullMsgId itemId);
	void editMessage(not_null<HistoryItem*> item);
	void pinMessage(FullMsgId itemId);
	void unpinMessage(FullMsgId itemId);

	MsgId replyToId() const;
	bool lastForceReplyReplied(const FullMsgId &replyTo) const;
	bool lastForceReplyReplied() const;
	bool cancelReply(bool lastKeyboardUsed = false);
	void cancelEdit();
	void updateForwarding();
	void updateForwardingTexts();

	void clearReplyReturns();
	void pushReplyReturn(not_null<HistoryItem*> item);
	QList<MsgId> replyReturns();
	void setReplyReturns(PeerId peer, const QList<MsgId> &replyReturns);
	void calcNextReplyReturn();

	void updatePreview();
	void previewCancel();

	bool recordingAnimationCallback(crl::time now);
	void stopRecording(bool send);

	void escape();

	void sendBotCommand(
		not_null<PeerData*> peer,
		UserData *bot,
		const QString &cmd,
		MsgId replyTo);
	void hideSingleUseKeyboard(PeerData *peer, MsgId replyTo);
	bool insertBotCommand(const QString &cmd);

	bool eventFilter(QObject *obj, QEvent *e) override;

	// With force=true the markup is updated even if it is
	// already shown for the passed history item.
	void updateBotKeyboard(History *h = nullptr, bool force = false);

	void fastShowAtEnd(not_null<History*> history);
	void applyDraft(
		FieldHistoryAction fieldHistoryAction = FieldHistoryAction::Clear);
	void showHistory(const PeerId &peer, MsgId showAtMsgId, bool reload = false);
	void clearAllLoadRequests();
	void clearDelayedShowAtRequest();
	void clearDelayedShowAt();
	void saveFieldToHistoryLocalDraft();

	void applyCloudDraft(History *history);

	void updateHistoryDownPosition();
	void updateHistoryDownVisibility();
	void updateUnreadMentionsPosition();
	void updateUnreadMentionsVisibility();

	void updateFieldSubmitSettings();

	void setInnerFocus();

	void updateNotifyControls();

	bool contentOverlapped(const QRect &globalRect);

	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params);

	void forwardSelected();
	void confirmDeleteSelected();
	void clearSelected();

	bool sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options);
	bool sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options);

	void showInfoTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback);
	void hideInfoTooltip(anim::type animated);

	// Tabbed selector management.
	bool pushTabbedSelectorToThirdSection(
		not_null<PeerData*> peer,
		const Window::SectionShow &params) override;
	bool returnTabbedSelector() override;

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

	PeerData *ui_getPeerForMouseAction();

	bool notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo);

	~HistoryWidget();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

signals:
	void cancelled();

public slots:
	void onScroll();

	void activate();
	void onTextChange();

	void onFieldTabbed();

	void onWindowVisibleChanged();

	void onFieldFocused();
	void onFieldResize();
	void onCheckFieldAutocomplete();
	void onScrollTimer();

	void onDraftSaveDelayed();
	void onDraftSave(bool delayed = false);
	void onCloudDraftSave();

	void onUpdateHistoryItems();

	// checks if we are too close to the top or to the bottom
	// in the scroll area and preloads history if needed
	void preloadHistoryIfNeeded();

private slots:
	void onHashtagOrBotCommandInsert(QString str, FieldAutocomplete::ChooseMethod method);
	void onMentionInsert(UserData *user);
	void onInlineBotCancel();
	void onMembersDropdownShow();

private:
	using TabbedPanel = ChatHelpers::TabbedPanel;
	using TabbedSelector = ChatHelpers::TabbedSelector;
	struct PinnedBar {
		PinnedBar(MsgId msgId, HistoryWidget *parent);
		~PinnedBar();

		MsgId msgId = 0;
		HistoryItem *msg = nullptr;
		Ui::Text::String text;
		object_ptr<Ui::IconButton> cancel;
		object_ptr<Ui::PlainShadow> shadow;
	};
	enum ScrollChangeType {
		ScrollChangeNone,

		// When we toggle a pinned message.
		ScrollChangeAdd,

		// When loading a history part while scrolling down.
		ScrollChangeNoJumpToBottom,
	};
	struct ScrollChange {
		ScrollChangeType type;
		int value;
	};
	enum class TextUpdateEvent {
		SaveDraft = (1 << 0),
		SendTyping = (1 << 1),
	};
	using TextUpdateEvents = base::flags<TextUpdateEvent>;
	friend inline constexpr bool is_flag_type(TextUpdateEvent) { return true; };

	void initTabbedSelector();
	void refreshTabbedPanel();
	void createTabbedPanel();
	void setTabbedPanel(std::unique_ptr<TabbedPanel> panel);
	void updateField();

	void requestMessageData(MsgId msgId);
	void messageDataReceived(ChannelData *channel, MsgId msgId);

	void send(Api::SendOptions options);
	void sendWithModifiers(Qt::KeyboardModifiers modifiers);
	void sendSilent();
	void sendScheduled();
	[[nodiscard]] SendMenu::Type sendMenuType() const;
	[[nodiscard]] SendMenu::Type sendButtonMenuType() const;
	void handlePendingHistoryUpdate();
	void fullPeerUpdated(PeerData *peer);
	void toggleTabbedSelectorMode();
	void recountChatWidth();
	void historyDownClicked();
	void showNextUnreadMention();
	void handlePeerUpdate();
	void setMembersShowAreaActive(bool active);
	void handleHistoryChange(not_null<const History*> history);
	void showAboutTopPromotion();
	void unreadCountUpdated();

	[[nodiscard]] int computeMaxFieldHeight() const;
	void toggleMuteUnmute();
	void toggleKeyboard(bool manual = true);
	void startBotCommand();
	void hidePinnedMessage();
	void cancelFieldAreaState();
	void unblockUser();
	void sendBotStartCommand();
	void joinChannel();

	void supportInitAutocomplete();
	void supportInsertText(const QString &text);
	void supportShareContact(Support::Contact contact);

	void highlightMessage(MsgId universalMessageId);
	void checkNextHighlight();
	void updateHighlightedMessage();
	void clearHighlightMessages();
	void stopMessageHighlight();

	auto computeSendButtonType() const;

	void animationCallback();
	void updateOverStates(QPoint pos);
	void recordDone(QByteArray result, VoiceWaveform waveform, int samples);
	void recordUpdate(ushort level, int samples);
	void recordStartCallback();
	void recordStopCallback(bool active);
	void recordUpdateCallback(QPoint globalPos);
	void chooseAttach();
	void historyDownAnimationFinish();
	void unreadMentionsAnimationFinish();
	void sendButtonClicked();
	void unreadMessageAdded(not_null<HistoryItem*> item);

	bool canSendFiles(not_null<const QMimeData*> data) const;
	bool confirmSendingFiles(
		const QStringList &files,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		not_null<const QMimeData*> data,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		Storage::PreparedList &&list,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool showSendingFilesError(const Storage::PreparedList &list) const;

	void uploadFile(const QByteArray &fileContent, SendMediaType type);

	void uploadFilesAfterConfirmation(
		Storage::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		MsgId replyTo,
		Api::SendOptions options,
		std::shared_ptr<SendingAlbum> album = nullptr);

	void itemRemoved(not_null<const HistoryItem*> item);

	// Updates position of controls around the message field,
	// like send button, emoji button and others.
	void moveFieldControls();
	void updateFieldSize();

	bool canWriteMessage() const;
	std::optional<QString> writeRestriction() const;
	void orderWidgets();

	void clearInlineBot();
	void inlineBotChanged();

	// Look in the _field for the inline bot and query string.
	void updateInlineBotQuery();

	// Request to show results in the emoji panel.
	void applyInlineBotQuery(UserData *bot, const QString &query);

	void cancelReplyAfterMediaSend(bool lastKeyboardUsed);
	bool replyToPreviousMessage();
	bool replyToNextMessage();
	[[nodiscard]] bool showSlowmodeError();

	void hideSelectorControlsAnimated();
	int countMembersDropdownHeightMax() const;

	void updateReplyToName();
	void checkForwardingInfo();
	bool editingMessage() const {
		return _editMsgId != 0;
	}
	bool jumpToDialogRow(const Dialogs::RowDescriptor &to);

	void setupShortcuts();
	bool showNextChat();
	bool showPreviousChat();

	void handlePeerMigration();

	void updateReplyEditTexts(bool force = false);
	void updateReplyEditText(not_null<HistoryItem*> item);

	void updatePinnedBar(bool force = false);
	bool pinnedMsgVisibilityUpdated();
	void destroyPinnedBar();

	void sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot,
		Api::SendOptions options);

	void drawField(Painter &p, const QRect &rect);
	void paintEditHeader(
		Painter &p,
		const QRect &rect,
		int left,
		int top) const;
	void drawRecording(Painter &p, float64 recordActive);
	void drawPinnedBar(Painter &p);
	void drawRestrictedWrite(Painter &p, const QString &error);
	bool paintShowAnimationFrame();

	void updateMouseTracking();

	// destroys _history and _migrated unread bars
	void destroyUnreadBar();
	void destroyUnreadBarOnClose();
	void createUnreadBarIfBelowVisibleArea(int withScrollTop);
	[[nodiscard]] bool insideJumpToEndInsteadOfToUnread() const;
	void createUnreadBarAndResize();

	void saveEditMsg();

	void checkPreview();
	void requestPreview();
	void gotPreview(QString links, const MTPMessageMedia &media, mtpRequestId req);
	void messagesReceived(PeerData *peer, const MTPmessages_Messages &messages, int requestId);
	void messagesFailed(const RPCError &error, int requestId);
	void addMessagesToFront(PeerData *peer, const QVector<MTPMessage> &messages);
	void addMessagesToBack(PeerData *peer, const QVector<MTPMessage> &messages);

	static void UnpinMessage(not_null<PeerData*> peer);

	void updateHistoryGeometry(bool initial = false, bool loadedDown = false, const ScrollChange &change = { ScrollChangeNone, 0 });
	void updateListSize();

	// Does any of the shown histories has this flag set.
	bool hasPendingResizedItems() const;

	// Counts scrollTop for placing the scroll right at the unread
	// messages bar, choosing from _history and _migrated unreadBar.
	std::optional<int> unreadBarTop() const;
	int itemTopForHighlight(not_null<HistoryView::Element*> view) const;
	void scrollToCurrentVoiceMessage(FullMsgId fromId, FullMsgId toId);

	// Scroll to current y without updating the _lastUserScrolled time.
	// Used to distinguish between user scrolls and syntetic scrolls.
	// This one is syntetic.
	void synteticScrollToY(int y);

	void writeDrafts(Data::Draft **localDraft, Data::Draft **editDraft);
	void writeDrafts(History *history);
	void setFieldText(
		const TextWithTags &textWithTags,
		TextUpdateEvents events = 0,
		FieldHistoryAction fieldHistoryAction = FieldHistoryAction::Clear);
	void clearFieldText(
		TextUpdateEvents events = 0,
		FieldHistoryAction fieldHistoryAction = FieldHistoryAction::Clear);

	HistoryItem *getItemFromHistoryOrMigrated(MsgId genericMsgId) const;
	void animatedScrollToItem(MsgId msgId);
	void animatedScrollToY(int scrollTo, HistoryItem *attachTo = nullptr);

	// when scroll position or scroll area size changed this method
	// updates the boundings of the visible area in HistoryInner
	void visibleAreaUpdated();
	int countInitialScrollTop();
	int countAutomaticScrollTop();
	void preloadHistoryByScroll();
	void checkReplyReturns();
	void scrollToAnimationCallback(FullMsgId attachToId, int relativeTo);

	bool readyToForward() const;
	bool hasSilentToggle() const;

	void handleSupportSwitch(not_null<History*> updated);

	void inlineBotResolveDone(const MTPcontacts_ResolvedPeer &result);
	void inlineBotResolveFail(const RPCError &error, const QString &username);

	bool isBotStart() const;
	bool isBlocked() const;
	bool isJoinChannel() const;
	bool isMuteUnmute() const;
	bool updateCmdStartShown();
	void updateSendButtonType();
	bool showRecordButton() const;
	bool showInlineBotCancel() const;
	void refreshSilentToggle();

	void setupScheduledToggle();
	void refreshScheduledToggle();

	MTP::Sender _api;
	MsgId _replyToId = 0;
	Ui::Text::String _replyToName;
	int _replyToNameVersion = 0;

	HistoryItemsList _toForward;
	Ui::Text::String _toForwardFrom, _toForwardText;
	int _toForwardNameVersion = 0;

	MsgId _editMsgId = 0;

	HistoryItem *_replyEditMsg = nullptr;
	Ui::Text::String _replyEditMsgText;
	mutable base::Timer _updateEditTimeLeftDisplay;

	object_ptr<Ui::IconButton> _fieldBarCancel;

	std::unique_ptr<PinnedBar> _pinnedBar;

	mtpRequestId _saveEditMsgRequestId = 0;

	QStringList _parsedLinks;
	QString _previewLinks;
	WebPageData *_previewData = nullptr;
	typedef QMap<QString, WebPageId> PreviewCache;
	PreviewCache _previewCache;
	mtpRequestId _previewRequest = 0;
	Ui::Text::String _previewTitle;
	Ui::Text::String _previewDescription;
	base::Timer _previewTimer;
	bool _previewCancelled = false;

	bool _replyForwardPressed = false;

	HistoryItem *_replyReturn = nullptr;
	QList<MsgId> _replyReturns;

	PeerData *_peer = nullptr;

	ChannelId _channel = NoChannel;
	bool _canSendMessages = false;
	MsgId _showAtMsgId = ShowAtUnreadMsgId;

	int _firstLoadRequest = 0; // Not real mtpRequestId.
	int _preloadRequest = 0; // Not real mtpRequestId.
	int _preloadDownRequest = 0; // Not real mtpRequestId.

	MsgId _delayedShowAtMsgId = -1;
	int _delayedShowAtRequest = 0; // Not real mtpRequestId.

	object_ptr<HistoryView::TopBarWidget> _topBar;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<HistoryInner> _list;
	History *_migrated = nullptr;
	History *_history = nullptr;
	// Initial updateHistoryGeometry() was called.
	bool _historyInited = false;
	// If updateListSize() was called without updateHistoryGeometry().
	bool _updateHistoryGeometryRequired = false;
	int _addToScroll = 0;

	int _lastScrollTop = 0; // gifs optimization
	crl::time _lastScrolled = 0;
	QTimer _updateHistoryItems;

	crl::time _lastUserScrolled = 0;
	bool _synteticScrollEvent = false;
	Ui::Animations::Simple _scrollToAnimation;

	Ui::Animations::Simple _historyDownShown;
	bool _historyDownIsShown = false;
	object_ptr<Ui::HistoryDownButton> _historyDown;

	Ui::Animations::Simple _unreadMentionsShown;
	bool _unreadMentionsIsShown = false;
	object_ptr<Ui::HistoryDownButton> _unreadMentions;

	object_ptr<FieldAutocomplete> _fieldAutocomplete;
	object_ptr<Support::Autocomplete> _supportAutocomplete;
	std::unique_ptr<MessageLinksParser> _fieldLinksParser;

	UserData *_inlineBot = nullptr;
	QString _inlineBotUsername;
	bool _inlineLookingUpBot = false;
	mtpRequestId _inlineBotResolveRequestId = 0;
	bool _isInlineBot = false;

	std::unique_ptr<HistoryView::ContactStatus> _contactStatus;

	object_ptr<Ui::SendButton> _send;
	object_ptr<Ui::FlatButton> _unblock;
	object_ptr<Ui::FlatButton> _botStart;
	object_ptr<Ui::FlatButton> _joinChannel;
	object_ptr<Ui::FlatButton> _muteUnmute;
	object_ptr<Ui::IconButton> _attachToggle;
	object_ptr<Ui::EmojiButton> _tabbedSelectorToggle;
	object_ptr<Ui::IconButton> _botKeyboardShow;
	object_ptr<Ui::IconButton> _botKeyboardHide;
	object_ptr<Ui::IconButton> _botCommandStart;
	object_ptr<Ui::SilentToggle> _silent = { nullptr };
	object_ptr<Ui::IconButton> _scheduled = { nullptr };
	bool _cmdStartShown = false;
	object_ptr<Ui::InputField> _field;
	bool _recording = false;
	bool _inField = false;
	bool _inReplyEditForward = false;
	bool _inPinnedMsg = false;
	bool _inClickable = false;
	int _recordingSamples = 0;
	int _recordCancelWidth;
	rpl::lifetime _recordingLifetime;

	// This can animate for a very long time (like in music playing),
	// so it should be a Basic, not a Simple animation.
	Ui::Animations::Basic _recordingAnimation;
	anim::value _recordingLevel;

	bool kbWasHidden() const;

	bool _kbShown = false;
	HistoryItem *_kbReplyTo = nullptr;
	object_ptr<Ui::ScrollArea> _kbScroll;
	QPointer<BotKeyboard> _keyboard;

	object_ptr<Ui::InnerDropdown> _membersDropdown = { nullptr };
	QTimer _membersDropdownShowTimer;

	object_ptr<InlineBots::Layout::Widget> _inlineResults = { nullptr };
	std::unique_ptr<TabbedPanel> _tabbedPanel;

	DragArea::Areas _attachDragAreas;

	Fn<void()> _raiseEmojiSuggestions;

	bool _nonEmptySelection = false;

	TextUpdateEvents _textUpdateEvents = (TextUpdateEvents() | TextUpdateEvent::SaveDraft | TextUpdateEvent::SendTyping);

	QString _confirmSource;

	Ui::Animations::Simple _a_show;
	Window::SlideDirection _showDirection;
	QPixmap _cacheUnder, _cacheOver;

	QTimer _scrollTimer;
	int32 _scrollDelta = 0;

	MsgId _highlightedMessageId = 0;
	std::deque<MsgId> _highlightQueue;
	base::Timer _highlightTimer;
	crl::time _highlightStart = 0;

	crl::time _saveDraftStart = 0;
	bool _saveDraftText = false;
	QTimer _saveDraftTimer, _saveCloudDraftTimer;

	base::weak_ptr<Ui::Toast::Instance> _topToast;

	object_ptr<Ui::PlainShadow> _topShadow;
	bool _inGrab = false;

	int _topDelta = 0;

};
