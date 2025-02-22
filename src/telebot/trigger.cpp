#include "trigger.h"
#include "functions.h"
#include "group_chat.h"

#include "shared/break_point.h"
#include "shared/logger/logger.h"
#include "shared/logger/format.h"

#include <string>
#include <optional>
#include <stdexcept>

#define log_error_m   alog::logger().error  (alog_line_location, "Trigger")
#define log_warn_m    alog::logger().warn   (alog_line_location, "Trigger")
#define log_info_m    alog::logger().info   (alog_line_location, "Trigger")
#define log_verbose_m alog::logger().verbose(alog_line_location, "Trigger")
#define log_debug_m   alog::logger().debug  (alog_line_location, "Trigger")
#define log_debug2_m  alog::logger().debug2 (alog_line_location, "Trigger")

namespace tbot {

using namespace std;

atomic_int globalConfigParceErrors = {0};

struct trigger_logic_error : public std::logic_error
{
    explicit trigger_logic_error(const string& msg) : std::logic_error(msg) {}
};

void Trigger::assign(const Trigger& trigger)
{
    name           = trigger.name;
    type           = trigger.type;
    active         = trigger.active;
    description    = trigger.description;
    skipAdmins     = trigger.skipAdmins;
    whiteUsers     = trigger.whiteUsers;
    inverse        = trigger.inverse;
    checkBio       = trigger.checkBio;
    onlyBio        = trigger.onlyBio;
    reportSpam     = trigger.reportSpam;
    newUserBan     = trigger.newUserBan;
    premiumBan     = trigger.premiumBan;
    immediatelyBan = trigger.immediatelyBan;
}

void TriggerLinkBase::assign(const TriggerLinkBase& trigger)
{
    Trigger::assign(trigger);

    whiteList = trigger.whiteList;
    blackList = trigger.blackList;
}

bool TriggerLinkDisable::isActive(const Update& update, GroupChat* chat,
                                  const Text& /*text*/) const
{
    activationReasonMessage.clear();

    Message::Ptr message = update.message;
    if (message.empty())
        message = update.edited_message;

    if (message.empty())
        return false;

    auto goodEntitiy = [&](const QString& text, const MessageEntity& entity) -> bool
    {
        QString urlStr;
        if (entity.type == "url")
        {
            urlStr = text.mid(entity.offset, entity.length);
        }
        else if (entity.type == "text_link")
        {
            urlStr = entity.url;
        }
        else
            return true;

        activationReasonMessage = u8"\r\nссылка: " + urlStr;

        log_debug_m << log_format(
            "\"update_id\":%?. Chat: %?. Trigger '%?'. Input url: %?",
            update.update_id, chat->name(), name, urlStr);

        QUrl url = QUrl::fromEncoded(urlStr.toUtf8());
        if (url.scheme().isEmpty())
        {
            urlStr.prepend("https://");
            url = QUrl::fromEncoded(urlStr.toUtf8());
        }
        QString host = url.host();
        QString path = url.path();
        for (const ItemLink& item : whiteList)
            if (host.endsWith(item.host, Qt::CaseInsensitive))
            {
                if (item.paths.isEmpty())
                {
                    log_verbose_m << log_format(
                        "\"update_id\":%?. Chat: %?. Trigger '%?', link  skipped"
                        ". It belong to whitelist [host: %?; path: empty]",
                        update.update_id, chat->name(), name, item.host);
                    return true;
                }
                for (QString ipath: item.paths)
                {
                    if (ipath.isEmpty())
                        continue;

                    if (ipath[0] != QChar('/'))
                        ipath.prepend(QChar('/'));

                    if (path.startsWith(ipath, Qt::CaseInsensitive))
                    {
                        log_verbose_m << log_format(
                            "\"update_id\":%?. Chat: %?. Trigger '%?', link skipped"
                            ". It belong to whitelist [host: %?; path: %?]",
                            update.update_id, chat->name(), name, item.host, ipath);
                        return true;
                    }
                }
            }

        return false;
    };

    for (const MessageEntity& entity : message->caption_entities)
        if (!goodEntitiy(message->caption, entity))
        {
            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated",
                update.update_id, chat->name(), name);
            return true;
        }

    for (const MessageEntity& entity : message->entities)
        if (!goodEntitiy(message->text, entity))
        {
            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated",
                update.update_id, chat->name(), name);
            return true;
        }

    return false;
}

bool TriggerLinkEnable::isActive(const Update& update, GroupChat* chat,
                                 const Text& /*text*/) const
{
    activationReasonMessage.clear();

    Message::Ptr message = update.message;
    if (message.empty())
        message = update.edited_message;

    if (message.empty())
        return false;

    auto goodEntitiy = [&](const QString& text, const MessageEntity& entity) -> bool
    {
        QString urlStr;
        if (entity.type == "url")
        {
            urlStr = text.mid(entity.offset, entity.length);
        }
        else if (entity.type == "text_link")
        {
            urlStr = entity.url;
        }
        else
            return true;

        activationReasonMessage = u8"\r\nссылка: " + urlStr;

        log_debug_m << log_format(
            "\"update_id\":%?. Chat: %?. Trigger '%?'. Input url: %?",
            update.update_id, chat->name(), name, urlStr);

        QUrl url = QUrl::fromEncoded(urlStr.toUtf8());
        if (url.scheme().isEmpty())
        {
            urlStr.prepend("https://");
            url = QUrl::fromEncoded(urlStr.toUtf8());
        }
        QString host = url.host();
        QString path = url.path();
        for (const ItemLink& item : whiteList)
            if (host.endsWith(item.host, Qt::CaseInsensitive))
            {
                if (item.paths.isEmpty())
                {
                    log_verbose_m << log_format(
                        "\"update_id\":%?. Chat: %?. Trigger '%?', link  skipped"
                        ". It belong to whitelist [host: %?; path: empty]",
                        update.update_id, chat->name(), name, item.host);
                    return true;
                }
                for (QString ipath: item.paths)
                {
                    if (ipath.isEmpty())
                        continue;

                    if (ipath[0] != QChar('/'))
                        ipath.prepend(QChar('/'));

                    if (path.startsWith(ipath, Qt::CaseInsensitive))
                    {
                        log_verbose_m << log_format(
                            "\"update_id\":%?. Chat: %?. Trigger '%?', link skipped"
                            ". It belong to whitelist [host: %?; path: %?]",
                            update.update_id, chat->name(), name, item.host, ipath);
                        return true;
                    }
                }
            }

        for (const ItemLink& item : blackList)
            if (host.endsWith(item.host, Qt::CaseInsensitive))
            {
                if (item.paths.isEmpty())
                {
                    log_verbose_m << log_format(
                        "\"update_id\":%?. Chat: %?. Trigger '%?', link bad"
                        ". It belong to blacklist [host: %?; path: empty]",
                        update.update_id, chat->name(), name, item.host);
                    return false;
                }
                for (QString ipath: item.paths)
                {
                    if (ipath.isEmpty())
                        continue;

                    if (ipath[0] != QChar('/'))
                        ipath.prepend(QChar('/'));

                    if (path.startsWith(ipath, Qt::CaseInsensitive))
                    {
                        log_verbose_m << log_format(
                            "\"update_id\":%?. Chat: %?. Trigger '%?', link bad"
                            ". It belong to blacklist [host: %?; path: %?]",
                            update.update_id, chat->name(), name, item.host, ipath);
                        return false;
                    }
                }
            }

        return true;
    };

    for (const MessageEntity& entity : message->caption_entities)
        if (!goodEntitiy(message->caption, entity))
        {
            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated",
                update.update_id, chat->name(), name);
            return true;
        }

    for (const MessageEntity& entity : message->entities)
        if (!goodEntitiy(message->text, entity))
        {
            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated",
                update.update_id, chat->name(), name);
            return true;
        }

    return false;
}

bool TriggerWord::isActive(const Update& update, GroupChat* chat,
                           const Text& text_) const
{
    activationReasonMessage.clear();
    QString text = text_[TextType::Content].toString();

    if (text.isEmpty())
        return false;

    Qt::CaseSensitivity caseSens = (caseInsensitive)
                                   ? Qt::CaseInsensitive
                                   : Qt::CaseSensitive;
    for (const QString& word : wordList)
        if (text.contains(word, caseSens))
        {
            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                ". The word '%?' was found",
                update.update_id, chat->name(), name, word);

            activationReasonMessage = u8"\r\nслово: " + word;
            return true;
        }

    return false;
}

void TriggerWord::assign(const TriggerWord& trigger)
{
    Trigger::assign(trigger);

    caseInsensitive = trigger.caseInsensitive;
    wordList = trigger.wordList;
}

bool TriggerRegexp::isActive(const Update& update, GroupChat* chat,
                             const Text& text_) const
{
    QString text;
    activationReasonMessage.clear();

    if      (analyze == "content" ) text = text_[TextType::Content ].toString();
    else if (analyze == "username") text = text_[TextType::UserName].toString();
    else if (analyze == "filemime") text = text_[TextType::FileMime].toString();
    else if (analyze == "urllinks") text = text_[TextType::UrlLinks].toString();

    int textLen = text.length();
    if (textLen == 0)
        return false;

    for (const QRegularExpression& re : regexpRemove)
        text.remove(re);

    if (text.length() != textLen)
        log_verbose_m << log_format(
            "\"update_id\":%?. Chat: %?. Trigger '%?'"
            ". Text after rx-remove: %?",
            update.update_id, chat->name(), name, text);

    text = text.trimmed();
    if (text.isEmpty())
        return false;

    for (const QRegularExpression& re : regexpList)
    {
        const QRegularExpressionMatch match = re.match(text);
        if (match.hasMatch())
        {
            alog::Line logLine = log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                ". Regular expression '%?' matched. Captured text: ",
                update.update_id, chat->name(), name, re.pattern());
            for (const QString& cap : match.capturedTexts())
                logLine << cap << "; ";

            activationReasonMessage = u8"\r\nфраза: " + match.capturedTexts()[0];
            return true;
        }
        else
        {
            log_debug2_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?'"
                ". Regular expression pattern '%?' not match",
                update.update_id, chat->name(), name, re.pattern());
        }
    }
    return false;
}

void TriggerRegexp::assign(const TriggerRegexp& trigger)
{
    Trigger::assign(trigger);

    caseInsensitive = trigger.caseInsensitive;
    multiline       = trigger.multiline;
    analyze         = trigger.analyze;
    regexpRemove    = trigger.regexpRemove;
    regexpList      = trigger.regexpList;
}

bool TriggerTimeLimit::isActive(const Update& update, GroupChat* chat,
                                const Text& /*text*/) const
{
    activationReasonMessage.clear();

    if (timelimitInactiveChats().contains(chat->id))
    {
        log_verbose_m << log_format(
            "\"update_id\":%?. Chat: %?. Trigger '%?' skipped, it deactivated",
            update.update_id, chat->name(), name);

        return false;
    }

    if (update.edited_message)
    {
        log_verbose_m << log_format(
            "\"update_id\":%?. Chat: %?. Trigger '%?' skipped, message was edited",
            update.update_id, chat->name(), name);

        return false;
    }

    // Учитываем временной сдвиг UTC
    QDateTime dtime = QDateTime::currentDateTimeUtc().addSecs(utc * 60*60);
    int dayOfWeek = dtime.date().dayOfWeek();
    QTime curTime = dtime.time();

    Times times;
    timesRangeOfDay(dayOfWeek, times);
    for (const TimeRange& time : times)
    {
        QTime timeBegin = time.begin.isNull() ? QTime(0, 0) : time.begin;
        QTime timeEnd   = time.end.isNull()   ? QTime(23, 59, 59, 999) : time.end;

        if (timeInRange(timeBegin, curTime, timeEnd))
        {
            timeBegin = !time.begin.isNull() ? time.begin : time.hint;
            timeEnd   = !time.end.isNull()   ? time.end   : time.hint;

            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                ". Message in forbidden time range [%?÷%?]",
                update.update_id, chat->name(), name,
                timeBegin.toString("HH:mm"), timeEnd.toString("HH:mm"));

            activationReasonMessage = QString(u8": ограничение на публикацию с %1 до %2")
                                             .arg(timeBegin.toString("HH:mm"))
                                             .arg(timeEnd.toString("HH:mm"));
            activationTime = time;
            return true;
        }
    }
    return false;
}

void TriggerTimeLimit::assign(const TriggerTimeLimit& trigger)
{
    Trigger::assign(trigger);

    utc = trigger.utc;
    week = trigger.week;

    messageBegin = trigger.messageBegin;
    messageEnd = trigger.messageEnd;
    messageInfo = trigger.messageInfo;

    hideMessageBegin = trigger.hideMessageBegin;
    hideMessageEnd = trigger.hideMessageEnd;
}

void TriggerTimeLimit::timesRangeOfDay(int dayOfWeek, Times& times) const
{
    times.clear();
    for (const Day& day : week)
        if (day.daysOfWeek.contains(dayOfWeek))
        {
            times = day.times;
            return;
        }

    log_error_m << log_format(
        "Times range not found for day (%?) of week. Trigger '%?'",
        dayOfWeek, name);
}

bool TriggerBlackUser::isActive(const Update& update, GroupChat* chat,
                                const Text& text_) const
{
    activationReasonMessage.clear();

    qint64 userId = text_[TextType::UserId].toLongLong();
    qint64 forwardUserId = text_[TextType::FrwdUserId].toLongLong();
    qint64 forwardChatId = text_[TextType::FrwdChatId].toLongLong();

    for (const Group& group : groups)
    {
        if (group.userIds.contains(userId))
        {
            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                ". The user id '%?' was found",
                update.update_id, chat->name(), name, userId);

            activationReasonMessage =
                QString(u8": пользователь %1 в черном списке ➞ (%2)")
                       .arg(userId).arg(group.description);
            return true;
        }
        if (group.userIds.contains(forwardUserId))
        {
            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                ". The forward-user id '%?' was found",
                update.update_id, chat->name(), name, forwardUserId);

            activationReasonMessage =
                QString(u8": forward-пользователь %1 в черном списке ➞ (%2)")
                       .arg(forwardUserId).arg(group.description);
            return true;
        }
        if (group.chatIds.contains(forwardChatId))
        {
            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                ". The forward-chat id '%?' was found",
                update.update_id, chat->name(), name, forwardChatId);

            activationReasonMessage =
                QString(u8": forward-чат/канал %1 в черном списке ➞ (%2)")
                       .arg(forwardChatId).arg(group.description);
            return true;
        }
    }
    return false;
}

void TriggerBlackUser::assign(const TriggerBlackUser& trigger)
{
    Trigger::assign(trigger);
    groups = trigger.groups;
}

bool TriggerEmptyText::isActive(const Update& update, GroupChat* chat,
                                const Text& text_) const
{
    activationReasonMessage.clear();

    qint64 userId  = text_[TextType::UserId].toLongLong();
    QString text   = text_[TextType::Content].toString();
    bool isPremium = text_[TextType::IsPremium].toBool();

    if (!text.isEmpty())
        return false;

    QString messageStr;

    if (userLimit.time > 0)
    {
        data::UserJoinTime::Ptr ujt = userJoinTimes().find(tuple{chat->id, userId});
        if (ujt.empty())
            return false;

        qint64 curTime = std::time(nullptr);
        qint64 limitTime = ujt->time + userLimit.time * 60*60;
        qint64 diffTime = limitTime - curTime;

        if (diffTime > 0)
        {
            int remainHours = 0;
            int remainMinutes = 0;
            if (diffTime > 60*60)
            {
                remainHours   = diffTime / (60*60);
                remainMinutes = diffTime % (60*60) / 60;
            }
            else
                remainMinutes = diffTime / 60;

            if (userLimit.threshId > 0)
            {
                if (userId >= userLimit.threshId)
                {
                    if (userLimit.premium)
                    {
                        if (isPremium)
                        {
                            messageStr =
                                u8": отсутствие текста."
                                u8"\r\nДополнительные ограничения на публикацию: "
                                u8"\r\n1. Время %1 часа(ов), истекает через %2 час. %3 мин;"
                                u8"\r\n2. Идентификатор пользователя превышает пороговое значение %4;"
                                u8"\r\n3. У пользователя премиум-аккаунт";
                            activationReasonMessage = messageStr.arg(userLimit.time)
                                                                .arg(remainHours)
                                                                .arg(remainMinutes)
                                                                .arg(userLimit.threshId);
                            log_verbose_m << log_format(
                                "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                                ". The empty message. User limits: time %?, thresh_id %?, premium true",
                                update.update_id, chat->name(), name, userLimit.time, userLimit.threshId);

                            return true;
                        }
                        return false;
                    }

                    messageStr =
                        u8": отсутствие текста."
                        u8"\r\nДополнительные ограничения на публикацию: "
                        u8"\r\n1. Время %1 часа(ов), истекает через %2 час. %3 мин;"
                        u8"\r\n2. Идентификатор пользователя превышает пороговое значение %4";
                    activationReasonMessage = messageStr.arg(userLimit.time)
                                                        .arg(remainHours)
                                                        .arg(remainMinutes)
                                                        .arg(userLimit.threshId);
                    log_verbose_m << log_format(
                        "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                        ". The empty message. User limits: time %?, thresh_id %?",
                        update.update_id, chat->name(), name, userLimit.time, userLimit.threshId);

                    return true;
                }
                return false;
            }
            if (userLimit.premium)
            {
                if (isPremium)
                {
                    messageStr =
                        u8": отсутствие текста."
                        u8"\r\nДополнительные ограничения на публикацию: "
                        u8"\r\n1. Время %1 часа(ов), истекает через %2 час. %3 мин;"
                        u8"\r\n2. У пользователя премиум-аккаунт";
                    activationReasonMessage = messageStr.arg(userLimit.time)
                                                        .arg(remainHours)
                                                        .arg(remainMinutes);
                    log_verbose_m << log_format(
                        "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                        ". The empty message. User limits: time %?, premium true",
                        update.update_id, chat->name(), name, userLimit.time);

                    return true;
                }
                return false;
            }

            messageStr =
                u8": отсутствие текста."
                u8"\r\nДополнительное ограничение на публикацию: "
                u8"время %1 часа(ов), истекает через %2 час. %3 мин";
            activationReasonMessage = messageStr.arg(userLimit.time)
                                                .arg(remainHours)
                                                .arg(remainMinutes);
            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                ". The empty message. User limits: time %?",
                update.update_id, chat->name(), name, userLimit.time);

            return true;
        }
        return false;
    }
    else if (userLimit.threshId > 0)
    {
        if (userId >= userLimit.threshId)
        {
            if (userLimit.premium)
            {
                if (isPremium)
                {
                    messageStr =
                        u8": отсутствие текста."
                        u8"\r\nДополнительные ограничения на публикацию: "
                        u8"\r\n1. Идентификатор пользователя превышает пороговое значение %1;"
                        u8"\r\n2. У пользователя премиум-аккаунт";
                    activationReasonMessage = messageStr.arg(userLimit.threshId);

                    log_verbose_m << log_format(
                        "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                        ". The empty message. User limits: thresh_id %?, premium true",
                        update.update_id, chat->name(), name, userLimit.threshId);

                    return true;
                }
                return false;
            }

            messageStr =
                u8": отсутствие текста."
                u8"\r\nДополнительное ограничение на публикацию: "
                u8"идентификатор пользователя превышает пороговое значение %1";

            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                ". The empty message. User limits: thresh_id %?",
                update.update_id, chat->name(), name, userLimit.threshId);

            activationReasonMessage = messageStr.arg(userLimit.threshId);
            return true;
        }
        return false;
    }
    else if (userLimit.premium)
    {
        if (isPremium)
        {
            activationReasonMessage =
                u8": отсутствие текста."
                u8"\r\nДополнительное ограничение на публикацию: "
                u8"у пользователя премиум-аккаунт";

            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                ". The empty message. User limits: premium true",
                update.update_id, chat->name(), name);

            return true;
        }
        return false;
    }

    log_verbose_m << log_format(
        "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
        ". The empty message",
        update.update_id, chat->name(), name);

    activationReasonMessage = u8": отсутствие текста";
    return true;
}

void TriggerEmptyText::assign(const TriggerEmptyText& trigger)
{
    Trigger::assign(trigger);
    userLimit = trigger.userLimit;
}

const char* yamlTypeName(YAML::NodeType::value type)
{
    switch (int(type))
    {
        case YAML::NodeType::Scalar:   return "Scalar";
        case YAML::NodeType::Sequence: return "Sequence";
        case YAML::NodeType::Map:      return "Map";
        default:                       return "Undefined";
    }
}

Trigger::Ptr createTrigger(const YAML::Node& ytrigger, Trigger::List& triggers)
{
    auto checkFiedType = [](const YAML::Node& ynode, const string& field,
                            YAML::NodeType::value type)
    {
        if (ynode[field].IsNull())
            throw trigger_logic_error(
                "For 'trigger' node a field '" + field + "' can not be null");

        if (ynode[field].Type() != type)
            throw trigger_logic_error(
                "For 'trigger' node a field '" + field + "' "
                "must have type '" + yamlTypeName(type) + "'");
    };

    QString name;
    if (ytrigger["name"].IsDefined())
    {
        checkFiedType(ytrigger, "name", YAML::NodeType::Scalar);
        name = QString::fromStdString(ytrigger["name"].as<string>());
    }
    if (name.isEmpty())
        throw trigger_logic_error("In a 'trigger' node a field 'name' can not be empty");

    optional<bool> activeO;
    if (ytrigger["active"].IsDefined())
    {
        checkFiedType(ytrigger, "active", YAML::NodeType::Scalar);
        activeO = ytrigger["active"].as<bool>();
    }

    optional<QString> descriptionO;
    if (ytrigger["description"].IsDefined())
    {
        checkFiedType(ytrigger, "description", YAML::NodeType::Scalar);
        descriptionO = QString::fromStdString(ytrigger["description"].as<string>());
    }

    QString type;
    if (ytrigger["type"].IsDefined())
    {
        checkFiedType(ytrigger, "type", YAML::NodeType::Scalar);
        type = QString::fromStdString(ytrigger["type"].as<string>());
    }
    if (type != "link" // Синоним link_disable
        && type != "link_enable"
        && type != "link_disable"
        && type != "word"
        && type != "regexp"
        && type != "timelimit"
        && type != "blackuser"
        && type != "emptytext")
    {
        throw trigger_logic_error(
            "In a 'trigger' node a field 'type' can take one of the following "
            "values: link_enable, link_disable/link, word, regexp, timelimit, "
            "blackuser, emptytext. "
            "Current value: " + type.toStdString());
    }

    QString base;
    Trigger* baseTrigger = nullptr;
    if (ytrigger["base"].IsDefined())
    {
        checkFiedType(ytrigger, "base", YAML::NodeType::Scalar);
        base = QString::fromStdString(ytrigger["base"].as<string>());
    }
    if (!base.isEmpty())
    {
        baseTrigger = triggers.findItem(&base, {lst::BruteForce::Yes});
        if (!baseTrigger)
        {
            QString err = "Base trigger '%1' not found for trigger '%2'"
                          ". See parameter 'base'";
            err = err.arg(base).arg(name);
            throw trigger_logic_error(err.toStdString());
        }
        if (baseTrigger->type != type)
        {
            QString err = "Types mismatch for base/detived triggers: %1/%2"
                          ". See trigger '%3'";
            err = err.arg(baseTrigger->type).arg(type).arg(name);
            throw trigger_logic_error(err.toStdString());
        }
    }

    auto readWhiteBlackList = [&](const YAML::Node& ylist,
                                  TriggerLinkBase::LinkList& linkList)
    {
        for (const YAML::Node& yitem : ylist)
        {
            checkFiedType(yitem, "host", YAML::NodeType::Scalar);
            TriggerLinkBase::ItemLink itemLink;

            const YAML::Node yhost = yitem["host"];
            itemLink.host = QString::fromStdString(yhost.as<string>()).trimmed();

            if (itemLink.host.isEmpty())
                continue;

            const YAML::Node ypath_list = yitem["paths"];
            if (ypath_list.IsDefined())
            {
                checkFiedType(yitem, "paths", YAML::NodeType::Sequence);
                for (const YAML::Node& ypath : ypath_list)
                {
                    QString path = QString::fromStdString(ypath.as<string>()).trimmed();
                    if (path.isEmpty())
                        continue;
                    itemLink.paths.append(path);
                }
            }
            linkList.append(itemLink);
        }
    };

    optional<TriggerLinkBase::LinkList> linkWhiteListO;
    if (ytrigger["white_list"].IsDefined())
    {
        linkWhiteListO = TriggerLinkBase::LinkList();
        checkFiedType(ytrigger, "white_list", YAML::NodeType::Sequence);
        const YAML::Node& ywhite_list = ytrigger["white_list"];
        readWhiteBlackList(ywhite_list, *linkWhiteListO);
    }

    optional<TriggerLinkBase::LinkList> linkBlackListO;
    if (ytrigger["black_list"].IsDefined())
    {
        linkBlackListO = TriggerLinkBase::LinkList();
        checkFiedType(ytrigger, "black_list", YAML::NodeType::Sequence);
        const YAML::Node& yblack_list = ytrigger["black_list"];
        readWhiteBlackList(yblack_list, *linkBlackListO);
    }

    optional<QStringList> wordListO;
    if (ytrigger["word_list"].IsDefined())
    {
        wordListO = QStringList();
        checkFiedType(ytrigger, "word_list", YAML::NodeType::Sequence);
        const YAML::Node& yword_list = ytrigger["word_list"];
        for (const YAML::Node& yword : yword_list)
            wordListO->append(QString::fromStdString(yword.as<string>()));
    }

    optional<QStringList> regexpRemoveO;
    if (ytrigger["regexp_remove"].IsDefined())
    {
        regexpRemoveO = QStringList();
        checkFiedType(ytrigger, "regexp_remove", YAML::NodeType::Sequence);
        const YAML::Node& yregexp_remove = ytrigger["regexp_remove"];
        for (const YAML::Node& yregexp : yregexp_remove)
            regexpRemoveO->append(QString::fromStdString(yregexp.as<string>()));
    }

    optional<QStringList> regexpListO;
    if (ytrigger["regexp_list"].IsDefined())
    {
        regexpListO = QStringList();
        checkFiedType(ytrigger, "regexp_list", YAML::NodeType::Sequence);
        const YAML::Node& yregexp_list = ytrigger["regexp_list"];
        for (const YAML::Node& yregexp : yregexp_list)
            regexpListO->append(QString::fromStdString(yregexp.as<string>()));
    }

    optional<bool> caseInsensitiveO;
    if (ytrigger["case_insensitive"].IsDefined())
    {
        checkFiedType(ytrigger, "case_insensitive", YAML::NodeType::Scalar);
        caseInsensitiveO = ytrigger["case_insensitive"].as<bool>();
    }

    optional<bool> skipAdminsO;
    if (ytrigger["skip_admins"].IsDefined())
    {
        checkFiedType(ytrigger, "skip_admins", YAML::NodeType::Scalar);
        skipAdminsO = ytrigger["skip_admins"].as<bool>();
    }

    optional<QSet<qint64>> whiteUsersO;
    if (ytrigger["white_users"].IsDefined())
    {
        whiteUsersO = QSet<qint64>();
        checkFiedType(ytrigger, "white_users", YAML::NodeType::Sequence);
        const YAML::Node& ywhite_users = ytrigger["white_users"];
        for (const YAML::Node& ywhite : ywhite_users)
            whiteUsersO->insert(ywhite.as<int64_t>());
    }

    optional<bool> inverseO;
    if (ytrigger["inverse"].IsDefined())
    {
        checkFiedType(ytrigger, "inverse", YAML::NodeType::Scalar);
        inverseO = ytrigger["inverse"].as<bool>();
    }

    optional<bool> checkBioO;
    if (ytrigger["check_bio"].IsDefined())
    {
        checkFiedType(ytrigger, "check_bio", YAML::NodeType::Scalar);
        checkBioO = ytrigger["check_bio"].as<bool>();
    }

    optional<bool> onlyBioO;
    if (ytrigger["only_bio"].IsDefined())
    {
        checkFiedType(ytrigger, "only_bio", YAML::NodeType::Scalar);
        onlyBioO = ytrigger["only_bio"].as<bool>();
    }

    optional<bool> reportSpamO;
    if (ytrigger["report_spam"].IsDefined())
    {
        checkFiedType(ytrigger, "report_spam", YAML::NodeType::Scalar);
        reportSpamO = ytrigger["report_spam"].as<bool>();
    }

    optional<bool> premiumBanO;
    if (ytrigger["premium_ban"].IsDefined())
    {
        checkFiedType(ytrigger, "premium_ban", YAML::NodeType::Scalar);
        premiumBanO = ytrigger["premium_ban"].as<bool>();
    }

    optional<bool> immediatelyBanO;
    if (ytrigger["immediately_ban"].IsDefined())
    {
        checkFiedType(ytrigger, "immediately_ban", YAML::NodeType::Scalar);
        immediatelyBanO = ytrigger["immediately_ban"].as<bool>();
    }

    optional<bool> multilineO;
    if (ytrigger["multiline"].IsDefined())
    {
        checkFiedType(ytrigger, "multiline", YAML::NodeType::Scalar);
        multilineO = ytrigger["multiline"].as<bool>();
    }

    optional<QString> analyzeO;
    if (ytrigger["analyze"].IsDefined())
    {
        checkFiedType(ytrigger, "analyze", YAML::NodeType::Scalar);
        analyzeO = QString::fromStdString(ytrigger["analyze"].as<string>());

        // Параметр analyze может принимать значения: content, username, filemime,
        // urllinks. Значение параметра по умолчанию равно content
        if (analyzeO != "username"
            && analyzeO != "filemime"
            && analyzeO != "urllinks")
        {
            analyzeO = "content";
        }
    }

    optional<bool> newUserBanO;
    if (ytrigger["newuser_ban"].IsDefined())
    {
        checkFiedType(ytrigger, "newuser_ban", YAML::NodeType::Scalar);
        newUserBanO = ytrigger["newuser_ban"].as<bool>();
    }

    optional<int> timeUtcO;
    if (ytrigger["utc"].IsDefined())
    {
        checkFiedType(ytrigger, "utc", YAML::NodeType::Scalar);
        timeUtcO = qBound(-12, ytrigger["utc"].as<int>(), +12);
    }

    optional<TriggerTimeLimit::Week> timeWeekO;
    if (ytrigger["week"].IsDefined())
    {
        timeWeekO = TriggerTimeLimit::Week();
        checkFiedType(ytrigger, "week", YAML::NodeType::Sequence);
        const YAML::Node& ydays = ytrigger["week"];
        for (const YAML::Node& yday : ydays)
        {
            TriggerTimeLimit::Day day;
            if (yday["days"].IsDefined())
            {
                QSet<int> dayset;
                checkFiedType(yday, "days", YAML::NodeType::Sequence);
                const YAML::Node& ydayset = yday["days"];
                for (const YAML::Node& yds : ydayset)
                    dayset.insert(qBound(1, yds.as<int>(), 7));
                day.daysOfWeek = dayset;
            }
            if (yday["times"].IsDefined())
            {
                TriggerTimeLimit::Times times;
                checkFiedType(yday, "times", YAML::NodeType::Sequence);
                const YAML::Node& ytimes = yday["times"];
                for (const YAML::Node& ytime : ytimes)
                {
                    TriggerTimeLimit::TimeRange time;
                    if (ytime["begin"].IsDefined())
                    {
                        checkFiedType(ytime, "begin", YAML::NodeType::Scalar);
                        QString s = QString::fromStdString(ytime["begin"].as<string>());
                        if (s == "--:--")
                        {
                            time.begin = QTime();
                        }
                        else
                        {
                            QTime t = QTime::fromString(s, "HH:mm");
                            time.begin = t.isValid() ? t : QTime();
                        }
                    }
                    if (ytime["end"].IsDefined())
                    {
                        checkFiedType(ytime, "end", YAML::NodeType::Scalar);
                        QString s = QString::fromStdString(ytime["end"].as<string>());
                        if (s == "--:--")
                        {
                            time.end = QTime();
                        }
                        else
                        {
                            QTime t = QTime::fromString(s, "HH:mm");
                            time.end = t.isValid() ? t : QTime();
                        }
                    }
                    if (ytime["hint"].IsDefined())
                    {
                        checkFiedType(ytime, "hint", YAML::NodeType::Scalar);
                        QString s = QString::fromStdString(ytime["hint"].as<string>());
                        QTime t = QTime::fromString(s, "HH:mm");
                        time.hint = t.isValid() ? t : QTime();
                    }
                    times.append(time);
                }
                day.times = times;
            }
            timeWeekO->append(day);
        }
    }

    optional<QString> timeMsgBeginO;
    optional<QString> timeMsgEndO;
    optional<QString> timeMsgInfoO;
    optional<bool> hideMsgBeginO;
    optional<bool> hideMsgEndO;
    if (ytrigger["message"].IsDefined())
    {
        checkFiedType(ytrigger, "message", YAML::NodeType::Map);
        const YAML::Node ymessage = ytrigger["message"];

        if (ymessage["begin"].IsDefined())
        {
            checkFiedType(ymessage, "begin", YAML::NodeType::Scalar);
            timeMsgBeginO = QString::fromStdString(ymessage["begin"].as<string>()).trimmed();
        }
        if (ymessage["end"].IsDefined())
        {
            checkFiedType(ymessage, "end", YAML::NodeType::Scalar);
            timeMsgEndO = QString::fromStdString(ymessage["end"].as<string>()).trimmed();
        }
        if (ymessage["info"].IsDefined())
        {
            checkFiedType(ymessage, "info", YAML::NodeType::Scalar);
            timeMsgInfoO = QString::fromStdString(ymessage["info"].as<string>()).trimmed();
        }

        if (ymessage["hide"].IsDefined())
        {
            checkFiedType(ymessage, "hide", YAML::NodeType::Map);
            const YAML::Node yhide = ymessage["hide"];

            if (yhide["begin"].IsDefined())
            {
                checkFiedType(yhide, "begin", YAML::NodeType::Scalar);
                hideMsgBeginO = yhide["begin"].as<bool>();
            }
            if (yhide["end"].IsDefined())
            {
                checkFiedType(yhide, "end", YAML::NodeType::Scalar);
                hideMsgEndO = yhide["end"].as<bool>();
            }
        }
    }

    optional<TriggerBlackUser::Group::List> blackUserGroupsO;
    if (ytrigger["group_list"].IsDefined())
    {
        blackUserGroupsO = TriggerBlackUser::Group::List();
        checkFiedType(ytrigger, "group_list", YAML::NodeType::Sequence);
        const YAML::Node& ygroup_list = ytrigger["group_list"];
        for (const YAML::Node& ygroup : ygroup_list)
        {
            TriggerBlackUser::Group group;
            if (ygroup["description"].IsDefined())
            {
                checkFiedType(ygroup, "description", YAML::NodeType::Scalar);
                group.description =
                    QString::fromStdString(ygroup["description"].as<string>());
            }
            if (ygroup["user_list"].IsDefined())
            {
                checkFiedType(ygroup, "user_list", YAML::NodeType::Sequence);
                const YAML::Node& yuser_list = ygroup["user_list"];
                for (const YAML::Node& yuser : yuser_list)
                    group.userIds.insert(yuser.as<int64_t>());
            }
            if (ygroup["chat_list"].IsDefined())
            {
                checkFiedType(ygroup, "chat_list", YAML::NodeType::Sequence);
                const YAML::Node& ychat_list = ygroup["chat_list"];
                for (const YAML::Node& ychat : ychat_list)
                    group.chatIds.insert(ychat.as<int64_t>());
            }
            blackUserGroupsO->append(group);
        }
    }

    optional<TriggerEmptyText::UserLimit> userLimitO;
    if (ytrigger["user_limit"].IsDefined())
    {
        userLimitO = TriggerEmptyText::UserLimit();
        checkFiedType(ytrigger, "user_limit", YAML::NodeType::Map);
        const YAML::Node& yuserlimit = ytrigger["user_limit"];

        if (yuserlimit["time"].IsDefined())
        {
            checkFiedType(yuserlimit, "time", YAML::NodeType::Scalar);
            userLimitO->time = yuserlimit["time"].as<int>();
        }
        if (yuserlimit["thresh_id"].IsDefined())
        {
            checkFiedType(yuserlimit, "thresh_id", YAML::NodeType::Scalar);
            userLimitO->threshId = yuserlimit["thresh_id"].as<int64_t>();
        }
        if (yuserlimit["premium"].IsDefined())
        {
            checkFiedType(yuserlimit, "premium", YAML::NodeType::Scalar);
            userLimitO->premium = yuserlimit["premium"].as<bool>();
        }
    }

    auto assignValue = [](auto& dest, const auto& source)
    {
        if (source) dest = source.value();
    };

    Trigger::Ptr trigger;

    if ((type == "link") || (type == "link_disable"))
    {
        TriggerLinkDisable::Ptr triggerLinkD {new TriggerLinkDisable};

        if (TriggerLinkDisable* t = dynamic_cast<TriggerLinkDisable*>(baseTrigger))
            triggerLinkD->assign(*t);

        assignValue(triggerLinkD->whiteList, linkWhiteListO);
        trigger = triggerLinkD;
    }
    else if (type == "link_enable")
    {
        TriggerLinkEnable::Ptr triggerLinkE {new TriggerLinkEnable};

        if (TriggerLinkEnable* t = dynamic_cast<TriggerLinkEnable*>(baseTrigger))
            triggerLinkE->assign(*t);

        assignValue(triggerLinkE->whiteList, linkWhiteListO);
        assignValue(triggerLinkE->blackList, linkBlackListO);
        trigger = triggerLinkE;
    }
    else if (type == "word")
    {
        TriggerWord::Ptr triggerWord {new TriggerWord};

        if (TriggerWord* t = dynamic_cast<TriggerWord*>(baseTrigger))
            triggerWord->assign(*t);

        assignValue(triggerWord->caseInsensitive, caseInsensitiveO);
        assignValue(triggerWord->wordList, wordListO);
        trigger = triggerWord;
    }
    else if (type == "regexp")
    {
        TriggerRegexp::Ptr triggerRegexp {new TriggerRegexp};

        if (TriggerRegexp* t = dynamic_cast<TriggerRegexp*>(baseTrigger))
            triggerRegexp->assign(*t);

        assignValue(triggerRegexp->caseInsensitive, caseInsensitiveO);
        assignValue(triggerRegexp->multiline, multilineO);
        assignValue(triggerRegexp->analyze, analyzeO);

        QRegularExpression::PatternOptions patternOpt =
            {QRegularExpression::DotMatchesEverythingOption
            |QRegularExpression::UseUnicodePropertiesOption};

        if (triggerRegexp->caseInsensitive)
            patternOpt |= QRegularExpression::CaseInsensitiveOption;

        if (triggerRegexp->multiline)
            patternOpt |= QRegularExpression::MultilineOption;

        if (regexpRemoveO)
        {
            triggerRegexp->regexpRemove.clear();
            for (const QString& pattern : regexpRemoveO.value())
            {
                QRegularExpression re {pattern, patternOpt};
                if (!re.isValid())
                {
                    log_error_m << "Trigger '" << name << "'"
                                << ". Failed regular expression in regexp_remove"
                                << ". Pattren '" << pattern << "'"
                                << ". Error: " << re.errorString()
                                << ". Offset: " << re.patternErrorOffset();
                    ++globalConfigParceErrors;
                    continue;
                }
                triggerRegexp->regexpRemove.append(std::move(re));
            }
        }
        if (regexpListO)
        {
            triggerRegexp->regexpList.clear();
            for (const QString& pattern : regexpListO.value())
            {
                QRegularExpression re {pattern, patternOpt};
                if (!re.isValid())
                {
                    log_error_m << "Trigger '" << name << "'"
                                << ". Failed regular expression in regexp_list"
                                << ". Pattren '" << pattern << "'"
                                << ". Error: " << re.errorString()
                                << ". Offset: " << re.patternErrorOffset();
                    ++globalConfigParceErrors;
                    continue;
                }
                triggerRegexp->regexpList.append(std::move(re));
            }
        }
        trigger = triggerRegexp;
    }
    else if (type == "timelimit")
    {
        TriggerTimeLimit::Ptr triggerTimeLmt {new TriggerTimeLimit};

        if (TriggerTimeLimit* t = dynamic_cast<TriggerTimeLimit*>(baseTrigger))
            triggerTimeLmt->assign(*t);

        assignValue(triggerTimeLmt->utc, timeUtcO);
        assignValue(triggerTimeLmt->week, timeWeekO);
        assignValue(triggerTimeLmt->messageBegin, timeMsgBeginO);
        assignValue(triggerTimeLmt->messageEnd, timeMsgEndO);
        assignValue(triggerTimeLmt->messageInfo, timeMsgInfoO);
        assignValue(triggerTimeLmt->hideMessageBegin, hideMsgBeginO);
        assignValue(triggerTimeLmt->hideMessageEnd, hideMsgEndO);

        trigger = triggerTimeLmt;
    }
    else if (type == "blackuser")
    {
        TriggerBlackUser::Ptr triggerBlackUser {new TriggerBlackUser};

        if (TriggerBlackUser* t = dynamic_cast<TriggerBlackUser*>(baseTrigger))
            triggerBlackUser->assign(*t);

        assignValue(triggerBlackUser->groups, blackUserGroupsO);
        trigger = triggerBlackUser;
    }
    else if (type == "emptytext")
    {
        TriggerEmptyText::Ptr triggerEmptyText {new TriggerEmptyText};

        if (TriggerEmptyText* t = dynamic_cast<TriggerEmptyText*>(baseTrigger))
            triggerEmptyText->assign(*t);

        assignValue(triggerEmptyText->userLimit, userLimitO);
        trigger = triggerEmptyText;
    }

    if (trigger)
    {
        trigger->name = name;
        trigger->type = type;

        assignValue(trigger->active, activeO);
        assignValue(trigger->description, descriptionO);
        assignValue(trigger->skipAdmins, skipAdminsO);
        assignValue(trigger->whiteUsers, whiteUsersO);
        assignValue(trigger->inverse, inverseO);
        assignValue(trigger->checkBio, checkBioO);
        assignValue(trigger->onlyBio, onlyBioO);
        assignValue(trigger->reportSpam, reportSpamO);
        assignValue(trigger->newUserBan, newUserBanO);
        assignValue(trigger->premiumBan, premiumBanO);
        assignValue(trigger->immediatelyBan, immediatelyBanO);
    }
    return trigger;
}

bool loadTriggers(Trigger::List& triggers, const YamlConfig& config)
{
    auto locker {config.locker()}; (void) locker;

    bool result = false;
    try
    {
        YAML::Node ytriggers = config.nodeGet("triggers");
        if (!ytriggers.IsDefined() || ytriggers.IsNull())
            return false;

        if (!ytriggers.IsSequence())
            throw std::logic_error("'triggers' node must have sequence type");

        for (const YAML::Node& ytrigger : ytriggers)
            try
            {
                if (Trigger::Ptr t = createTrigger(ytrigger, triggers))
                    triggers.add(t.detach());
            }
            catch (trigger_logic_error& e)
            {
                log_error_m << "Trigger configure error. Detail: " << e.what()
                            << ". Config file: " << config.filePath();
                ++globalConfigParceErrors;
            }

        result = true;
    }
    catch (YAML::ParserException& e)
    {
        triggers.clear();
        log_error_m << "YAML error. Detail: " << e.what()
                    << ". Config file: " << config.filePath();
        ++globalConfigParceErrors;
    }
    catch (std::exception& e)
    {
        triggers.clear();
        log_error_m << "Configuration error. Detail: " << e.what()
                    << ". Config file: " << config.filePath();
        ++globalConfigParceErrors;
    }
    catch (...)
    {
        triggers.clear();
        log_error_m << "Unknown error"
                    << ". Config file: " << config.filePath();
        ++globalConfigParceErrors;
    }
    return result;
}

void printTriggers(Trigger::List& triggers)
{
    log_info_m << "--- Bot triggers ---";

    bool nextCommaVal;
    auto nextComma = [&nextCommaVal]()
    {
        if (nextCommaVal)
            return ", ";

        nextCommaVal = true;
        return "";
    };

    bool nextCommaVal2;
    auto nextComma2 = [&nextCommaVal2]()
    {
        if (nextCommaVal2)
            return ", ";

        nextCommaVal2 = true;
        return "";
    };

    for (Trigger* trigger : triggers)
    {
        alog::Line logLine = log_info_m << "Trigger : ";
        logLine << "name: " << trigger->name;

        auto printWhiteBlackList = [&](const TriggerLinkBase::LinkList& linkList)
        {
            for (const TriggerLinkBase::ItemLink& item : linkList)
            {
                logLine << nextComma() << "{host: " << item.host;
                if (!item.paths.isEmpty())
                {
                    nextCommaVal2 = false;
                    logLine << ", paths: [";
                    for (const QString& path : item.paths)
                        logLine << nextComma2() << path;
                    logLine << "]";
                }
                logLine << "}";
            }
        };
        if (TriggerLinkDisable* triggerLinkD = dynamic_cast<TriggerLinkDisable*>(trigger))
        {
            logLine << "; type: link_disable"
                    << "; active: " << triggerLinkD->active;

            nextCommaVal = false;
            logLine << "; white_list: [";
            printWhiteBlackList(triggerLinkD->whiteList);
            logLine << "]";
        }
        else if (TriggerLinkEnable* triggerLinkE = dynamic_cast<TriggerLinkEnable*>(trigger))
        {
            logLine << "; type: link_enable"
                    << "; active: " << triggerLinkE->active;

            nextCommaVal = false;
            logLine << "; white_list: [";
            printWhiteBlackList(triggerLinkE->whiteList);
            logLine << "]";

            nextCommaVal = false;
            logLine << "; black_list: [";
            printWhiteBlackList(triggerLinkE->blackList);
            logLine << "]";
        }
        else if (TriggerWord* triggerWord = dynamic_cast<TriggerWord*>(trigger))
        {
            logLine << "; type: word"
                    << "; active: " << triggerWord->active
                    << "; case_insensitive: " << triggerWord->caseInsensitive;

            nextCommaVal = false;
            logLine << "; word_list: [";
            for (const QString& item : triggerWord->wordList)
                logLine << nextComma() << item;
            logLine << "]";
        }
        else if (TriggerRegexp* triggerRegexp = dynamic_cast<TriggerRegexp*>(trigger))
        {
            logLine << "; type: regexp"
                    << "; active: " << triggerRegexp->active
                    << "; case_insensitive: " << triggerRegexp->caseInsensitive
                    << "; multiline: " << triggerRegexp->multiline
                    << "; analyze: " << triggerRegexp->analyze;

            nextCommaVal = false;
            logLine << "; regexp_remove: [";
            for (const QRegularExpression& re : triggerRegexp->regexpRemove)
                logLine << nextComma() << "'" << re.pattern() << "'";
            logLine << "]";

            nextCommaVal = false;
            logLine << "; regexp_list: [";
            for (const QRegularExpression& re : triggerRegexp->regexpList)
                logLine << nextComma() << "'" << re.pattern() << "'";
            logLine << "]";
        }
        else if (TriggerTimeLimit* triggerTimeLmt = dynamic_cast<TriggerTimeLimit*>(trigger))
        {
            logLine << "; type: timelimit"
                    << "; active: " << triggerTimeLmt->active
                    << "; utc: " << triggerTimeLmt->utc;

            nextCommaVal = false;
            logLine << "; week: [";
            for (const TriggerTimeLimit::Day& day : triggerTimeLmt->week)
            {
                logLine << nextComma();

                nextCommaVal2 = false;
                logLine << "{days: [";
                QList<int> dayset = day.daysOfWeek.toList();
                qSort(dayset.begin(), dayset.end());
                for (int ds : dayset)
                    logLine << nextComma2() << ds;
                logLine << "], ";

                nextCommaVal2 = false;
                logLine << "times: [";
                for (const TriggerTimeLimit::TimeRange& time : day.times)
                {
                    logLine << nextComma2();
                    QString timeBegin = !time.begin.isNull()
                                        ? time.begin.toString("HH:mm")
                                        : QString("--:--");

                    QString timeEnd = !time.end.isNull()
                                      ? time.end.toString("HH:mm")
                                      : QString("--:--");

                    logLine << log_format("{begin: %?, end: %?",
                                          timeBegin, timeEnd);

                    if (time.hint.isValid())
                        logLine << ", hint: " << time.hint.toString("HH:mm");

                    logLine << "}";
                }
                logLine << "]}";
            }
            logLine << "]";

            logLine << log_format("; message: {begin: '%?', end: '%?', info: '%?'"
                                  ", hide: {begin: %?, end: %?}}",
                                  triggerTimeLmt->messageBegin,
                                  triggerTimeLmt->messageEnd,
                                  triggerTimeLmt->messageInfo,
                                  triggerTimeLmt->hideMessageBegin,
                                  triggerTimeLmt->hideMessageEnd);
        }
        else if (TriggerBlackUser* triggerBlackUsr = dynamic_cast<TriggerBlackUser*>(trigger))
        {
            logLine << "; type: blackuser"
                    << "; active: " << triggerBlackUsr->active;

            nextCommaVal = false;
            logLine << "; group_list: [";
            for (const TriggerBlackUser::Group& group : triggerBlackUsr->groups)
            {
                logLine << nextComma();
                logLine << log_format("{description: %?", group.description);

                nextCommaVal2 = false;
                logLine << ", user_list: [";
                for (qint64 id : group.userIds)
                {
                    logLine << nextComma2();
                    logLine << id;
                }
                logLine << "]";

                nextCommaVal2 = false;
                logLine << ", chat_list: [";
                for (qint64 id : group.chatIds)
                {
                    logLine << nextComma2();
                    logLine << id;
                }
                logLine << "]}";
            }
            logLine << "]";
        }
        else if (TriggerEmptyText* triggerEmptyTxt = dynamic_cast<TriggerEmptyText*>(trigger))
        {
            logLine << "; type: emptytext"
                    << "; active: " << triggerEmptyTxt->active;

            logLine << log_format("; user_limit: {time: %?, thresh_id: %?, premium: %?}",
                                  triggerEmptyTxt->userLimit.time,
                                  triggerEmptyTxt->userLimit.threshId,
                                  triggerEmptyTxt->userLimit.premium);
        }

        logLine << "; skip_admins: " << trigger->skipAdmins;

        nextCommaVal = false;
        logLine << "; white_users: [";
        for (qint64 item : trigger->whiteUsers)
            logLine << nextComma() << item;
        logLine << "]";

        logLine << "; inverse: " << trigger->inverse
                << "; check_bio: " << trigger->checkBio
                << "; only_bio: " << trigger->onlyBio
                << "; report_spam: " << trigger->reportSpam
                << "; newuser_ban: " << trigger->newUserBan
                << "; premium_ban: " << trigger->premiumBan
                << "; immediately_ban: " << trigger->immediatelyBan;

        if (!trigger->description.isEmpty())
            logLine << "; description: " << trigger->description;
    }
    log_info_m << "---";
}

Trigger::List triggers(Trigger::List* list)
{
    static QMutex mutex;
    static Trigger::List triggers;

    QMutexLocker locker {&mutex}; (void) locker;

    if (list)
        triggers.swap(*list);

//    Trigger::List retTriggers;
//    for (Trigger* t : triggers)
//    {
//        t->add_ref();
//        retTriggers.add(t);
//    }
//    return retTriggers;

    return triggers;
}

bool timeInRange(const QTime& begin, const QTime& time, const QTime& end)
{
    if (begin <= end)
    {
        if ((begin <= time) && (time <= end))
            return true;
    }
    else // begin > end
    {
        if ((begin <= time) && (time <= QTime(23, 59, 59, 999)))
            return true;

        if ((QTime(0, 0) <= time) && (time <= end))
            return true;
    }
    return false;
}

} // namespace tbot
