/*****************************************************************************
  В модуле представлен список идентификаторов команд для коммуникации между
  клиентской и серверной частями приложения.
  В данном модуле представлен список команд персональный для этого приложения.

  Требование надежности коммуникаций: однажды назначенный идентификатор коман-
  ды не должен более меняться.
*****************************************************************************/

#pragma once

#include "shared/list.h"
#include "shared/clife_base.h"
#include "shared/clife_ptr.h"
#include "pproto/serialize/json.h"

namespace tbot {

using namespace pproto;

class Message;
typedef clife_ptr<Message> MessagePtr;

/**
  https://core.telegram.org/bots/api#update
*/

struct User : public clife_base
{
    typedef clife_ptr<User> Ptr;

    qint64  id                          = {0};
    bool    is_bot                      = {false}; // True, if this user is a bot
    QString first_name;                            // User's or bot's first name
    QString last_name;                             // Optional. User's or bot's last name
    QString username;                              // Optional. User's or bot's username
    QString language_code;                         // Optional. IETF language tag of the user's language (https://en.wikipedia.org/wiki/IETF_language_tag)
    bool    is_premium                  = {false}; // Optional. True, if this user is a Telegram Premium user
    bool    added_to_attachment_menu    = {false}; // Optional. True, if this user added the bot to the attachment menu
    bool    can_join_groups             = {false}; // Optional. True, if the bot can be invited to groups. Returned only in getMe.
    bool    can_read_all_group_messages = {false}; // Optional. True, if privacy mode is disabled for the bot. Returned only in getMe.
    bool    supports_inline_queries     = {false}; // Optional. True, if the bot supports inline queries. Returned only in getMe.

    J_SERIALIZE_BEGIN
        J_SERIALIZE_ITEM( id                          )
        J_SERIALIZE_ITEM( is_bot                      )
        J_SERIALIZE_ITEM( first_name                  )
        J_SERIALIZE_OPT ( last_name                   )
        J_SERIALIZE_OPT ( username                    )
        J_SERIALIZE_OPT ( language_code               )
        J_SERIALIZE_OPT ( is_premium                  )
        J_SERIALIZE_OPT ( added_to_attachment_menu    )
        J_SERIALIZE_OPT ( can_join_groups             )
        J_SERIALIZE_OPT ( can_read_all_group_messages )
        J_SERIALIZE_OPT ( supports_inline_queries     )
    J_SERIALIZE_END
};

struct Location : public clife_base
{
    typedef clife_ptr<Location> Ptr;

    float  longitude              = {0}; // Longitude as defined by sender
    float  latitude               = {0}; // Latitude as defined by sender
    float  horizontal_accuracy    = {0}; // Optional. The radius of uncertainty for the location, measured in meters; 0-1500
    qint32 live_period            = {0}; // Optional. Time relative to the message sending date, during which the location can be updated; in seconds. For active live locations only.
    qint32 heading                = {0}; // Optional. The direction in which user is moving, in degrees; 1-360. For active live locations only.
    qint32 proximity_alert_radius = {0}; // Optional. The maximum distance for proximity alerts about approaching another chat member, in meters. For sent live locations only.

    J_SERIALIZE_BEGIN
        J_SERIALIZE_ITEM( longitude              )
        J_SERIALIZE_ITEM( latitude               )
        J_SERIALIZE_OPT ( horizontal_accuracy    )
        J_SERIALIZE_OPT ( live_period            )
        J_SERIALIZE_OPT ( heading                )
        J_SERIALIZE_OPT ( proximity_alert_radius )
    J_SERIALIZE_END
};

struct ChatPhoto : public clife_base
{
    typedef clife_ptr<ChatPhoto> Ptr;

    QString small_file_id;        // File identifier of small (160x160) chat photo. This file_id can be used only for photo download and only for as long as the photo is not changed.
    QString small_file_unique_id; // Unique file identifier of small (160x160) chat photo, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.
    QString big_file_id;          // File identifier of big (640x640) chat photo. This file_id can be used only for photo download and only for as long as the photo is not changed.
    QString big_file_unique_id;   // Unique file identifier of big (640x640) chat photo, which is supposed to be the same over time and for different bots. Can't be used to download or reuse the file.

    J_SERIALIZE_BEGIN
        J_SERIALIZE_ITEM( small_file_id        )
        J_SERIALIZE_ITEM( small_file_unique_id )
        J_SERIALIZE_ITEM( big_file_id          )
        J_SERIALIZE_ITEM( big_file_unique_id   )
    J_SERIALIZE_END
};

struct ChatPermissions : public clife_base
{
    typedef clife_ptr<ChatPermissions> Ptr;

    bool can_send_messages         = {false}; // Optional. True, if the user is allowed to send text messages, contacts, locations and venues
    bool can_send_media_messages   = {false}; // Optional. True, if the user is allowed to send audios, documents, photos, videos, video notes and voice notes, implies can_send_messages
    bool can_send_polls            = {false}; // Optional. True, if the user is allowed to send polls, implies can_send_messages
    bool can_send_other_messages   = {false}; // Optional. True, if the user is allowed to send animations, games, stickers and use inline bots, implies can_send_media_messages
    bool can_add_web_page_previews = {false}; // Optional. True, if the user is allowed to add web page previews to their messages, implies can_send_media_messages
    bool can_change_info           = {false}; // Optional. True, if the user is allowed to change the chat title, photo and other settings. Ignored in public supergroups
    bool can_invite_users          = {false}; // Optional. True, if the user is allowed to invite new users to the chat
    bool can_pin_messages          = {false}; // Optional. True, if the user is allowed to pin messages. Ignored in public supergroups
    bool can_manage_topics         = {false}; // Optional. True, if the user is allowed to create forum topics. If omitted defaults to the value of can_pin_messages

    J_SERIALIZE_BEGIN
        J_SERIALIZE_OPT( can_send_messages         )
        J_SERIALIZE_OPT( can_send_media_messages   )
        J_SERIALIZE_OPT( can_send_polls            )
        J_SERIALIZE_OPT( can_send_other_messages   )
        J_SERIALIZE_OPT( can_add_web_page_previews )
        J_SERIALIZE_OPT( can_change_info           )
        J_SERIALIZE_OPT( can_invite_users          )
        J_SERIALIZE_OPT( can_pin_messages          )
        J_SERIALIZE_OPT( can_manage_topics         )
    J_SERIALIZE_END
};
typedef typename ChatPermissions::Ptr ChatPermissPtr;

struct ChatLocation : public clife_base
{
    typedef clife_ptr<ChatLocation> Ptr;

    Location::Ptr location; // The location to which the supergroup is connected. Can't be a live location.
    QString       address;  // Location address; 1-64 characters, as defined by the chat owner

    J_SERIALIZE_BEGIN
        J_SERIALIZE_ITEM( location )
        J_SERIALIZE_ITEM( address  )
    J_SERIALIZE_END
};
typedef typename ChatLocation::Ptr ChatLocatPtr;

struct MessageEntity /*: public clife_base*/
{
    //typedef clife_ptr<MessageEntity> Ptr;

    QString   type;            // Type of the entity. Currently, can be “mention” (@username), “hashtag” (#hashtag),
                               // “cashtag” ($USD), “bot_command” (/start@jobs_bot), “url” (https://telegram.org),
                               // “email” (do-not-reply@telegram.org), “phone_number” (+1-212-555-0123),
                               // “bold” (bold text), “italic” (italic text), “underline” (underlined text),
                               // “strikethrough” (strikethrough text), “spoiler” (spoiler message),
                               // “code” (monowidth string), “pre” (monowidth block),
                               // “text_link” (for clickable text URLs), “text_mention” (for users without usernames),
                               // “custom_emoji” (for inline custom emoji stickers).
    qint32    offset = {0};    // Offset in UTF-16 code units to the start of the entity.
    qint32    length = {0};    // Length of the entity in UTF-16 code units.
    QString   url;             // Optional. For “text_link” only, URL that will be opened after user taps on the text.
    User::Ptr user;            // Optional. For “text_mention” only, the mentioned user.
    QString   language;        // Optional. For “pre” only, the programming language of the entity text.
    QString   custom_emoji_id; // Optional. For “custom_emoji” only, unique identifier of the custom emoji.
                               // Use getCustomEmojiStickers to get full information about the sticker.

    J_SERIALIZE_BEGIN
        J_SERIALIZE_ITEM( type             )
        J_SERIALIZE_ITEM( offset           )
        J_SERIALIZE_ITEM( length           )
        J_SERIALIZE_OPT ( url              )
        J_SERIALIZE_OPT ( user             )
        J_SERIALIZE_OPT ( language         )
        J_SERIALIZE_OPT ( custom_emoji_id  )
    J_SERIALIZE_END
};

struct Chat : public clife_base
{
    typedef clife_ptr<Chat> Ptr;

    qint64         id = {0};
    QString        type;                               // Type of chat, can be either “private”, “group”, “supergroup” or “channel”
    QString        title;                              // Optional. Title, for supergroups, channels and group chats
    QString        username;                           // Optional. Username, for private chats, supergroups and channels if available
    QString        first_name;                         // Optional. First name of the other party in a private chat
    QString        last_name;                          // Optional. Last name of the other party in a private chat
    bool           is_forum = {true};                  // Optional. True, if the supergroup chat is a forum (has topics enabled)
    ChatPhoto::Ptr photo;                              // Optional. Chat photo. Returned only in getChat.
    QList<QString> active_usernames;                   // Optional. If non-empty, the list of all active chat usernames; for private chats, supergroups and channels. Returned only in getChat.
    QString        emoji_status_custom_emoji_id;       // Optional. Custom emoji identifier of emoji status of the other party in a private chat. Returned only in getChat.
    QString        bio;                                // Optional. Bio of the other party in a private chat. Returned only in getChat.
    bool           has_private_forwards     = {false}; // Optional. True, if privacy settings of the other party in the private chat allows to use tg://user?id=<user_id> links only in chats with the user. Returned only in getChat.
    bool           join_to_send_messages    = {false}; // Optional. True, if users need to join the supergroup before they can send messages. Returned only in getChat.
    bool           join_by_request          = {false}; // Optional. True, if all users directly joining the supergroup need to be approved by supergroup administrators. Returned only in getChat.
    bool           has_restricted_voice_and_video_messages = {true}; // Optional. True, if the privacy settings of the other party restrict sending voice and video note messages in the private chat. Returned only in getChat.
    QString        description;                        // Optional. Description, for groups, supergroups and channel chats. Returned only in getChat.
    QString        invite_link;                        // Optional. Primary invite link, for groups, supergroups and channel chats. Returned only in getChat.
    MessagePtr     pinned_message;                     // Optional. The most recent pinned message (by sending date). Returned only in getChat.
    ChatPermissPtr permissions;                        // Optional. Default chat member permissions, for groups and supergroups. Returned only in getChat.
    qint32         slow_mode_delay          = {0};     // Optional. For supergroups, the minimum allowed delay between consecutive messages sent by each unpriviledged user; in seconds. Returned only in getChat.
    qint32         message_auto_delete_time = {0};     // Optional. The time after which all messages sent to the chat will be automatically deleted; in seconds. Returned only in getChat.
    bool           has_protected_content    = {false}; // Optional. True, if messages from the chat can't be forwarded to other chats. Returned only in getChat.
    QString        sticker_set_name;                   // Optional. For supergroups, name of group sticker set. Returned only in getChat.
    bool           can_set_sticker_set      = {false}; // Optional. True, if the bot can change the group sticker set. Returned only in getChat.
    qint32         linked_chat_id;                     // Optional. Unique identifier for the linked chat, i.e. the discussion group identifier for a channel and vice versa; for supergroups and channel chats. This identifier may be greater than 32 bits and some programming languages may have difficulty/silent defects in interpreting it. But it is smaller than 52 bits, so a signed 64 bit integer or double-precision float type are safe for storing this identifier. Returned only in getChat.
  //ChatLocatPtr   location;                           // Optional. For supergroups, the location to which the supergroup is connected. Returned only in getChat.

    DECLARE_J_SERIALIZE_FUNC
};

struct Message : public clife_base
{
    typedef MessagePtr Ptr;

    qint32       message_id = {-1};               // Unique message identifier inside this chat
    qint32       message_thread_id = {-1};        // Optional. Unique identifier of a message thread to which the message belongs; for supergroups only
    User::Ptr    from;                            // Optional. Sender of the message; empty for messages sent to channels. For backward compatibility,
                                                  // the field contains a fake sender user in non-channel chats, if the message was sent on behalf of a chat.
    Chat::Ptr    sender_chat;                     // Optional. Sender of the message, sent on behalf of a chat. For example, the channel itself for channel posts, the supergroup itself for messages from anonymous group administrators, the linked channel for messages automatically forwarded to the discussion group. For backward compatibility, the field from contains a fake sender user in non-channel chats, if the message was sent on behalf of a chat.
    qint32       date = {0};                      // Date the message was sent in Unix time
    Chat::Ptr    chat;                            // Conversation the message belongs to
    User::Ptr    forward_from;                    // Optional. For forwarded messages, sender of the original message
    Chat::Ptr    forward_from_chat;               // Optional. For messages forwarded from channels or from anonymous administrators, information about the original sender chat
    qint32       forward_from_message_id = {-1};  // Optional. For messages forwarded from channels, identifier of the original message in the channel
    QString      forward_signature;               // Optional. For forwarded messages that were originally sent in channels or by an anonymous chat administrator, signature of the message sender if present
    QString      forward_sender_name;             // Optional. Sender's name for messages forwarded from users who disallow adding a link to their account in forwarded messages
    qint32       forward_date          = {0};     // Optional. For forwarded messages, date the original message was sent in Unix time
    bool         is_topic_message      = {false}; // Optional. True, if the message is sent to a forum topic
    bool         is_automatic_forward  = {false}; // Optional. True, if the message is a channel post that was automatically forwarded to the connected discussion group
    Message::Ptr reply_to_message;                // Optional. For replies, the original message. Note that the Message object in this field will not contain further reply_to_message fields even if it itself is a reply.
    User::Ptr    via_bot;                         // Optional. Bot through which the message was sent
    qint32       edit_date             = {0};     // Optional. Date the message was last edited in Unix time
    bool         has_protected_content = {false}; // Optional. True, if the message can't be forwarded
    QString      media_group_id;                  // Optional. The unique identifier of a media message group this message belongs to
    QString      author_signature;                // Optional. Signature of the post author for messages in channels, or the custom title of an anonymous group administrator
    QString      text;                            // Optional. For text messages, the actual UTF-8 text of the message
    QList<MessageEntity> entities;                // Optional. For text messages, special entities like usernames, URLs, bot commands, etc. that appear in the text

//    animation 	Animation 	Optional. Message is an animation, information about the animation. For backward compatibility, when this field is set, the document field will also be set
//    audio 	Audio 	Optional. Message is an audio file, information about the file
//    document 	Document 	Optional. Message is a general file, information about the file
//    photo 	Array of PhotoSize 	Optional. Message is a photo, available sizes of the photo
//    sticker 	Sticker 	Optional. Message is a sticker, information about the sticker
//    video 	Video 	Optional. Message is a video, information about the video
//    video_note 	VideoNote 	Optional. Message is a video note, information about the video message
//    voice 	Voice 	Optional. Message is a voice message, information about the file

    QString caption; // Optional. Caption for the animation, audio, document, photo, video or voice

//    caption_entities 	Array of MessageEntity 	Optional. For messages with a caption, special entities like usernames, URLs, bot commands, etc. that appear in the caption
//    contact 	Contact 	Optional. Message is a shared contact, information about the contact
//    dice 	Dice 	Optional. Message is a dice with random value
//    game 	Game 	Optional. Message is a game, information about the game. More about games »
//    poll 	Poll 	Optional. Message is a native poll, information about the poll
//    venue 	Venue 	Optional. Message is a venue, information about the venue. For backward compatibility, when this field is set, the location field will also be set
//    location 	Location 	Optional. Message is a shared location, information about the location
//    new_chat_members 	Array of User 	Optional. New members that were added to the group or supergroup and information about them (the bot itself may be one of these members)
//    left_chat_member 	User 	Optional. A member was removed from the group, information about them (this member may be the bot itself)
//    new_chat_title 	String 	Optional. A chat title was changed to this value
//    new_chat_photo 	Array of PhotoSize 	Optional. A chat photo was change to this value
//    delete_chat_photo 	True 	Optional. Service message: the chat photo was deleted
//    group_chat_created 	True 	Optional. Service message: the group has been created
//    supergroup_chat_created 	True 	Optional. Service message: the supergroup has been created. This field can't be received in a message coming through updates, because bot can't be a member of a supergroup when it is created. It can only be found in reply_to_message if someone replies to a very first message in a directly created supergroup.
//    channel_chat_created 	True 	Optional. Service message: the channel has been created. This field can't be received in a message coming through updates, because bot can't be a member of a channel when it is created. It can only be found in reply_to_message if someone replies to a very first message in a channel.
//    message_auto_delete_timer_changed 	MessageAutoDeleteTimerChanged 	Optional. Service message: auto-delete timer settings changed in the chat
//    migrate_to_chat_id 	Integer 	Optional. The group has been migrated to a supergroup with the specified identifier. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this identifier.
//    migrate_from_chat_id 	Integer 	Optional. The supergroup has been migrated from a group with the specified identifier. This number may have more than 32 significant bits and some programming languages may have difficulty/silent defects in interpreting it. But it has at most 52 significant bits, so a signed 64-bit integer or double-precision float type are safe for storing this identifier.
//    pinned_message 	Message 	Optional. Specified message was pinned. Note that the Message object in this field will not contain further reply_to_message fields even if it is itself a reply.
//    invoice 	Invoice 	Optional. Message is an invoice for a payment, information about the invoice. More about payments »
//    successful_payment 	SuccessfulPayment 	Optional. Message is a service message about a successful payment, information about the payment. More about payments »
//    connected_website 	String 	Optional. The domain name of the website on which the user has logged in. More about Telegram Login »
//    passport_data 	PassportData 	Optional. Telegram Passport data
//    proximity_alert_triggered 	ProximityAlertTriggered 	Optional. Service message. A user in the chat triggered another user's proximity alert while sharing Live Location.
//    forum_topic_created 	ForumTopicCreated 	Optional. Service message: forum topic created
//    forum_topic_closed 	ForumTopicClosed 	Optional. Service message: forum topic closed
//    forum_topic_reopened 	ForumTopicReopened 	Optional. Service message: forum topic reopened
//    video_chat_scheduled 	VideoChatScheduled 	Optional. Service message: video chat scheduled
//    video_chat_started 	VideoChatStarted 	Optional. Service message: video chat started
//    video_chat_ended 	VideoChatEnded 	Optional. Service message: video chat ended
//    video_chat_participants_invited 	VideoChatParticipantsInvited 	Optional. Service message: new participants invited to a video chat
//    web_app_data 	WebAppData 	Optional. Service message: data sent by a Web App
//    reply_markup 	InlineKeyboardMarkup 	Optional. Inline keyboard attached to the message. login_url buttons are represented as ordinary url buttons.

    J_SERIALIZE_BEGIN
        J_SERIALIZE_ITEM( message_id              )
        J_SERIALIZE_OPT ( message_thread_id       )
        J_SERIALIZE_OPT ( from                    )
        J_SERIALIZE_OPT ( sender_chat             )
        J_SERIALIZE_OPT ( date                    )
        J_SERIALIZE_OPT ( chat                    )
        J_SERIALIZE_OPT ( forward_from            )
        J_SERIALIZE_OPT ( forward_from_chat       )
        J_SERIALIZE_OPT ( forward_from_message_id )
        J_SERIALIZE_OPT ( forward_signature       )
        J_SERIALIZE_OPT ( forward_sender_name     )
        J_SERIALIZE_OPT ( forward_date            )
        J_SERIALIZE_OPT ( is_topic_message        )
        J_SERIALIZE_OPT ( is_automatic_forward    )
        J_SERIALIZE_OPT ( reply_to_message;       )
        J_SERIALIZE_OPT ( via_bot                 )
        J_SERIALIZE_OPT ( edit_date               )
        J_SERIALIZE_OPT ( has_protected_content   )
        J_SERIALIZE_OPT ( media_group_id          )
        J_SERIALIZE_OPT ( author_signature        )
        J_SERIALIZE_OPT ( text                    )
        J_SERIALIZE_OPT ( entities                )
        J_SERIALIZE_OPT ( caption                 )
    J_SERIALIZE_END
};

struct Update : public clife_base
{
    typedef clife_ptr<Update> Ptr;

    qint32       update_id = {-1};    // The update's unique identifier. Update identifiers start from a certain positive number and increase
                                      // sequentially. This ID becomes especially handy if you're using webhooks, since it allows you to ignore
                                      // repeated updates or to restore the correct update sequence, should they get out of order. If there are
                                      // no new updates for at least a week, then identifier of the next update will be chosen randomly instead
                                      // of sequentially.
    Message::Ptr message;             // Optional. New incoming message of any kind - text, photo, sticker, etc.
    Message::Ptr edited_message;      // Optional. New version of a message that is known to the bot and was edited
    Message::Ptr channel_post;        // Optional. New incoming channel post of any kind - text, photo, sticker, etc.
    Message::Ptr edited_channel_post; // Optional. New version of a channel post that is known to the bot and was edited

    //User::Ptr    from;

    J_SERIALIZE_BEGIN
        J_SERIALIZE_ITEM( update_id           )
        J_SERIALIZE_OPT ( message             )
        J_SERIALIZE_OPT ( edited_message      )
        J_SERIALIZE_OPT ( channel_post        )
        J_SERIALIZE_OPT ( edited_channel_post )
    J_SERIALIZE_END
};

struct HttpResult //: public clife_base
{
    //typedef clife_ptr<HttpResult> Ptr;

    bool       ok = {false};
    QByteArray result;
    qint32     error_code = {0};
    QString    description;

    J_SERIALIZE_BEGIN
        J_SERIALIZE_ITEM( ok          )
        J_SERIALIZE_OPT ( result      )
        J_SERIALIZE_OPT ( error_code  )
        J_SERIALIZE_OPT ( description )
    J_SERIALIZE_END
};

struct ChatMemberAdministrator
{
    QString   status;                           // The member's status in the chat, always “administrator”
    User::Ptr user;                             // Information about the user
    bool      can_be_edited          = {false}; // True, if the bot is allowed to edit administrator privileges of that user
    bool      is_anonymous           = {false}; // True, if the user's presence in the chat is hidden
    bool      can_manage_chat        = {false}; // True, if the administrator can access the chat event log, chat statistics, message statistics in channels, see channel members, see anonymous administrators in supergroups and ignore slow mode. Implied by any other administrator privilege
    bool      can_delete_messages    = {false}; // True, if the administrator can delete messages of other users
    bool      can_manage_video_chats = {false}; // True, if the administrator can manage video chats
    bool      can_restrict_members   = {false}; // True, if the administrator can restrict, ban or unban chat members
    bool      can_promote_members    = {false}; // True, if the administrator can add new administrators with a subset of their own privileges or demote administrators that he has promoted, directly or indirectly (promoted by administrators that were appointed by the user)
    bool      can_change_info        = {false}; // True, if the user is allowed to change the chat title, photo and other settings
    bool      can_invite_users       = {false}; // True, if the user is allowed to invite new users to the chat
    bool      can_post_messages      = {false}; // Optional. True, if the administrator can post in the channel; channels only
    bool      can_edit_messages      = {false}; // Optional. True, if the administrator can edit messages of other users and can pin messages; channels only
    bool      can_pin_messages       = {false}; // Optional. True, if the user is allowed to pin messages; groups and supergroups only
    bool      can_manage_topics      = {false}; // Optional. True, if the user is allowed to create, rename, close, and reopen forum topics; supergroups only
    QString   custom_title;                     // Optional. Custom title for this user

    J_SERIALIZE_BEGIN
        J_SERIALIZE_ITEM( status                 )
        J_SERIALIZE_ITEM( user                   )
        J_SERIALIZE_OPT ( can_be_edited          )
        J_SERIALIZE_OPT ( is_anonymous           )
        J_SERIALIZE_OPT ( can_manage_chat        )
        J_SERIALIZE_OPT ( can_delete_messages    )
        J_SERIALIZE_OPT ( can_manage_video_chats )
        J_SERIALIZE_OPT ( can_restrict_members   )
        J_SERIALIZE_OPT ( can_promote_members    )
        J_SERIALIZE_OPT ( can_change_info        )
        J_SERIALIZE_OPT ( can_invite_users       )
        J_SERIALIZE_OPT ( can_post_messages      )
        J_SERIALIZE_OPT ( can_edit_messages      )
        J_SERIALIZE_OPT ( can_pin_messages       )
        J_SERIALIZE_OPT ( can_manage_topics      )
        J_SERIALIZE_OPT ( custom_title           )
    J_SERIALIZE_END
};

/**
  Результат вызова функции getMe()
*/
struct GetMe_Result
{
    User user;
    J_SERIALIZE_MAP_ONE( "result", user )
};

/**
  Результат вызова функции getChat()
*/
struct GetChat_Result
{
    Chat chat;
    J_SERIALIZE_MAP_ONE( "result", chat )
};

/**
  Результат вызова функции getChatAdministrators()
*/
struct GetChatAdministrators_Result
{
    QList<ChatMemberAdministrator> items;
    J_SERIALIZE_MAP_ONE( "result", items )
};

//--- struct Chat ---
J_SERIALIZE_EXTERN_BEGIN(Chat)
    J_SERIALIZE_ITEM( id                           )
    J_SERIALIZE_ITEM( type                         )
    J_SERIALIZE_OPT ( title                        )
    J_SERIALIZE_OPT ( username                     )
    J_SERIALIZE_OPT ( first_name                   )
    J_SERIALIZE_OPT ( last_name                    )
    J_SERIALIZE_OPT ( is_forum                     )
    J_SERIALIZE_OPT ( photo                        )
    J_SERIALIZE_OPT ( active_usernames             )
    J_SERIALIZE_OPT ( emoji_status_custom_emoji_id )
    J_SERIALIZE_OPT ( bio                          )
    J_SERIALIZE_OPT ( has_private_forwards         )
    J_SERIALIZE_OPT ( join_to_send_messages        )
    J_SERIALIZE_OPT ( join_by_request              )
    J_SERIALIZE_OPT ( has_restricted_voice_and_video_messages )
    J_SERIALIZE_OPT ( description                  )
    J_SERIALIZE_OPT ( invite_link                  )
    J_SERIALIZE_OPT ( pinned_message               )
    J_SERIALIZE_OPT ( permissions                  )
    J_SERIALIZE_OPT ( slow_mode_delay              )
    J_SERIALIZE_OPT ( message_auto_delete_time     )
    J_SERIALIZE_OPT ( has_protected_content        )
    J_SERIALIZE_OPT ( sticker_set_name             )
    J_SERIALIZE_OPT ( can_set_sticker_set          )
    J_SERIALIZE_OPT ( linked_chat_id               )
  //J_SERIALIZE_OPT ( location                     )
J_SERIALIZE_END

} // namespace tbot
