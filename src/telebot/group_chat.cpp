#include "group_chat.h"

#include "shared/break_point.h"
#include "shared/logger/logger.h"
#include "shared/logger/format.h"

#include <string>
#include <stdexcept>

#define log_error_m   alog::logger().error  (alog_line_location, "GroupChat")
#define log_warn_m    alog::logger().warn   (alog_line_location, "GroupChat")
#define log_info_m    alog::logger().info   (alog_line_location, "GroupChat")
#define log_verbose_m alog::logger().verbose(alog_line_location, "GroupChat")
#define log_debug_m   alog::logger().debug  (alog_line_location, "GroupChat")
#define log_debug2_m  alog::logger().debug2 (alog_line_location, "GroupChat")

namespace tbot {

extern atomic_int globalConfigParceErrors;

struct group_logic_error : public std::logic_error
{
    explicit group_logic_error(const string& msg) : std::logic_error(msg) {}
};

QString GroupChat::name() const
{
    QMutexLocker locker {&_lock}; (void) locker;
    return _name;
}

void GroupChat::setName(const QString& val)
{
    QMutexLocker locker {&_lock}; (void) locker;
    _name = val;
}

QSet<qint64> GroupChat::adminIds() const
{
    QMutexLocker locker {&_lock}; (void) locker;
    return _adminIds;
}

void GroupChat::setAdminIds(const QSet<qint64>& val)
{
    QMutexLocker locker {&_lock}; (void) locker;
    _adminIds = val;
}

QSet<qint64> GroupChat::ownerIds() const
{
    QMutexLocker locker {&_lock}; (void) locker;
    return _ownerIds;
}

void GroupChat::setOwnerIds(const QSet<qint64>& val)
{
    QMutexLocker locker {&_lock}; (void) locker;
    _ownerIds = val;
}

QStringList GroupChat::adminNames() const
{
    QMutexLocker locker {&_lock}; (void) locker;
    return _adminNames;
}

void GroupChat::setAdminNames(const QStringList& val)
{
    QMutexLocker locker {&_lock}; (void) locker;
    _adminNames = val;
}

ChatMemberAdministrator::Ptr GroupChat::botInfo() const
{
    QMutexLocker locker {&_lock}; (void) locker;
    return _botInfo;
}

void GroupChat::setBotInfo(const ChatMemberAdministrator::Ptr& val)
{
    QMutexLocker locker {&_lock}; (void) locker;
    _botInfo = val;
}

GroupChat::Ptr createGroupChat(const YAML::Node& ychat)
{
    auto checkFiedType = [](const YAML::Node& ynode, const string& field,
                            YAML::NodeType::value type)
    {
        if (ynode[field].IsNull())
            throw group_logic_error(
                "For 'group_chats' node a field '" + field + "' can not be null");

        if (ynode[field].Type() != type)
            throw group_logic_error(
                "For 'group_chats' node a field '" + field + "' "
                "must have type '" + yamlTypeName(type) + "'");
    };

    qint64 id = {0};
    if (ychat["id"].IsDefined())
    {
        checkFiedType(ychat, "id", YAML::NodeType::Scalar);
        id = ychat["id"].as<int64_t>();
    }
    if (id == 0)
        throw group_logic_error("In a 'group_chats' node a field 'id' can not be empty");

    QString name;
    if (ychat["name"].IsDefined())
    {
        checkFiedType(ychat, "name", YAML::NodeType::Scalar);
        name = QString::fromStdString(ychat["name"].as<string>());
    }

    QStringList triggerNames;
    if (ychat["triggers"].IsDefined())
    {
        checkFiedType(ychat, "triggers", YAML::NodeType::Sequence);
        const YAML::Node& ytriggers = ychat["triggers"];
        for (const YAML::Node& ytrigger : ytriggers)
            triggerNames.append(QString::fromStdString(ytrigger.as<string>()));
    }

    bool skipAdmins = true;
    if (ychat["skip_admins"].IsDefined())
    {
        checkFiedType(ychat, "skip_admins", YAML::NodeType::Scalar);
        skipAdmins = ychat["skip_admins"].as<bool>();
    }

    bool premiumBan = false;
    if (ychat["premium_ban"].IsDefined())
    {
        checkFiedType(ychat, "premium_ban", YAML::NodeType::Scalar);
        premiumBan = ychat["premium_ban"].as<bool>();
    }

    bool checkBio = false;
    if (ychat["check_bio"].IsDefined())
    {
        checkFiedType(ychat, "check_bio", YAML::NodeType::Scalar);
        checkBio = ychat["check_bio"].as<bool>();
    }

    GroupChat::WhiteUser::List whiteUsers;
    if (ychat["white_users"].IsDefined())
    {
        checkFiedType(ychat, "white_users", YAML::NodeType::Sequence);
        const YAML::Node& ywhite_users = ychat["white_users"];
        for (const YAML::Node& ywhite : ywhite_users)
        {
            checkFiedType(ywhite, "id", YAML::NodeType::Scalar);
            GroupChat::WhiteUser* wu = whiteUsers.add();
            wu->userId = ywhite["id"].as<int64_t>();

            if (ywhite["info"].IsDefined())
            {
                checkFiedType(ywhite, "info", YAML::NodeType::Scalar);
                wu->info = QString::fromStdString(ywhite["info"].as<string>());
            }
        }
    }
    whiteUsers.sort();

    qint32 userSpamLimit = 5;
    if (ychat["user_spam_limit"].IsDefined())
    {
        checkFiedType(ychat, "user_spam_limit", YAML::NodeType::Scalar);
        userSpamLimit = ychat["user_spam_limit"].as<int>();
    }

    QVector<qint32> userRestricts;
    if (ychat["user_restricts"].IsDefined())
    {
        checkFiedType(ychat, "user_restricts", YAML::NodeType::Sequence);
        const YAML::Node& yuser_restricts = ychat["user_restricts"];
        for (const YAML::Node& yrestrict : yuser_restricts)
            userRestricts.append(yrestrict.as<int32_t>());
    }

    qint32 newUserMute = -1;
    if (ychat["newuser_mute"].IsDefined())
    {
        checkFiedType(ychat, "newuser_mute", YAML::NodeType::Scalar);
        newUserMute = ychat["newuser_mute"].as<int>();
    }

    bool anonymousAsAdmin = true;
    if (ychat["anonymous_as_admin"].IsDefined())
    {
        checkFiedType(ychat, "anonymous_as_admin", YAML::NodeType::Scalar);
        anonymousAsAdmin = ychat["anonymous_as_admin"].as<bool>();
    }

    GroupChat::JoinViaChatFolder joinViaChatFolder;
    if (ychat["join_via_chat_folder"].IsDefined())
    {
        checkFiedType(ychat, "join_via_chat_folder", YAML::NodeType::Map);
        const YAML::Node yjoin = ychat["join_via_chat_folder"];

        if (yjoin["restrict"].IsDefined())
        {
            checkFiedType(yjoin, "restrict", YAML::NodeType::Scalar);
            joinViaChatFolder.restrict_ = yjoin["restrict"].as<bool>();
        }
        if (yjoin["mute"].IsDefined())
        {
            checkFiedType(yjoin, "mute", YAML::NodeType::Scalar);
            joinViaChatFolder.mute = yjoin["mute"].as<bool>();
        }
        if (yjoin["report_spam"].IsDefined())
        {
            checkFiedType(yjoin, "report_spam", YAML::NodeType::Scalar);
            joinViaChatFolder.reportSpam = yjoin["report_spam"].as<bool>();
        }
    }

    GroupChat::AntiRaid antiRaid;
    if (ychat["anti_raid"].IsDefined())
    {
        checkFiedType(ychat, "anti_raid", YAML::NodeType::Map);
        const YAML::Node yantiraid = ychat["anti_raid"];

        if (yantiraid["active"].IsDefined())
        {
            checkFiedType(yantiraid, "active", YAML::NodeType::Scalar);
            antiRaid.active = yantiraid["active"].as<bool>();
        }
        if (yantiraid["time_frame"].IsDefined())
        {
            checkFiedType(yantiraid, "time_frame", YAML::NodeType::Scalar);
            antiRaid.timeFrame = yantiraid["time_frame"].as<int>();
        }
        if (yantiraid["users_limit"].IsDefined())
        {
            checkFiedType(yantiraid, "users_limit", YAML::NodeType::Scalar);
            antiRaid.usersLimit = yantiraid["users_limit"].as<int>();
        }
        if (yantiraid["duration"].IsDefined())
        {
            checkFiedType(yantiraid, "duration", YAML::NodeType::Scalar);
            antiRaid.duration = yantiraid["duration"].as<int>();
        }
    }

    GroupChat::Ptr chat {new GroupChat};
    chat->id = id;
    chat->setName(name);
    chat->skipAdmins = skipAdmins;
    chat->premiumBan = premiumBan;
    chat->checkBio = checkBio;
    chat->whiteUsers = std::move(whiteUsers);
    chat->userSpamLimit = userSpamLimit;
    chat->userRestricts = userRestricts;
    chat->newUserMute = newUserMute;
    chat->anonymousAsAdmin = anonymousAsAdmin;
    chat->joinViaChatFolder = joinViaChatFolder;
    chat->antiRaid = antiRaid;

    Trigger::List triggers = tbot::triggers();
    for (const QString& triggerName : triggerNames)
    {
        lst::FindResult fr = triggers.findRef(triggerName, {lst::BruteForce::Yes});
        if (fr.success())
        {
            Trigger* t = triggers.item(fr.index());
            t->add_ref();
            chat->triggers.add(t);
        }
        else
        {
            log_error_m << log_format("Group chat id: %?. Trigger '%?' not found",
                                      chat->id, triggerName);
            ++globalConfigParceErrors;
        }
    }
    return chat;
}

bool loadGroupChats(GroupChat::List& chats, const YamlConfig& config)
{
    auto locker {config.locker()}; (void) locker;

    bool result = false;
    try
    {
        YAML::Node ychats = config.nodeGet("group_chats");
        if (!ychats.IsDefined() || ychats.IsNull())
            return false;

        if (!ychats.IsSequence())
            throw std::logic_error("'group_chats' node must have sequence type");

        for (const YAML::Node& ychat : ychats)
            try
            {
                if (GroupChat::Ptr c = createGroupChat(ychat))
                    chats.add(c.detach());
            }
            catch (group_logic_error& e)
            {
                log_error_m << "Group configure error. Detail: " << e.what()
                            << ". Config file: " << config.filePath();
                ++globalConfigParceErrors;
            }

        chats.sort();
        result = true;
    }
    catch (YAML::ParserException& e)
    {
        log_error_m << "YAML error. Detail: " << e.what()
                    << ". Config file: " << config.filePath();
        ++globalConfigParceErrors;
    }
    catch (std::exception& e)
    {
        log_error_m << "Configuration error. Detail: " << e.what()
                    << ". Config file: " << config.filePath();
        ++globalConfigParceErrors;
    }
    catch (...)
    {
        log_error_m << "Unknown error"
                    << ". Config file: " << config.filePath();
        ++globalConfigParceErrors;
    }
    return result;
}

void printGroupChats(GroupChat::List& chats)
{
    log_info_m << "--- Bot group chats ---";

    bool nextCommaVal;
    auto nextComma = [&nextCommaVal]()
    {
        if (nextCommaVal)
            return ", ";

        nextCommaVal = true;
        return "";
    };

    for (GroupChat* chat : chats)
    {
        alog::Line logLine = log_info_m << "Chat : ";
        logLine << "id: " << chat->id;
        logLine << "; name: " << chat->name();

        nextCommaVal = false;
        logLine << "; triggers: [";
        for (Trigger* trigger : chat->triggers)
            logLine << nextComma() << trigger->name;
        logLine << "]";

        logLine << "; skip_admins: " << chat->skipAdmins;
        logLine << "; premium_ban: " << chat->premiumBan;
        logLine << "; check_bio: "   << chat->checkBio;

        nextCommaVal = false;
        logLine << "; white_users: [";
        for (GroupChat::WhiteUser* wu : chat->whiteUsers)
            logLine << nextComma() << wu->userId;
        logLine << "]";

        logLine << "; user_spam_limit: " << chat->userSpamLimit;

        nextCommaVal = false;
        logLine << "; user_restricts: [";
        for (qint32 item : chat->userRestricts)
            logLine << nextComma() << item;
        logLine << "]";

        logLine << "; newuser_mute: " << chat->newUserMute;
        logLine << "; anonymous_as_admin: " << chat->anonymousAsAdmin;

        logLine << log_format("; join_via_chat_folder: {restrict: %?, mute: %?"
                              ", report_spam: %?}",
                              chat->joinViaChatFolder.restrict_,
                              chat->joinViaChatFolder.mute,
                              chat->joinViaChatFolder.reportSpam);

        logLine << log_format("; anti_raid: {active: %?, time_frame: %?"
                              ", users_limit: %?, duration: %?}",
                              chat->antiRaid.active,
                              chat->antiRaid.timeFrame,
                              chat->antiRaid.usersLimit,
                              chat->antiRaid.duration);
    }
    log_info_m << "---";
}

GroupChat::List groupChats(GroupChat::List* list)
{
    static QMutex mutex;
    static GroupChat::List chats;

    QMutexLocker locker {&mutex}; (void) locker;

    if (list)
    {
        chats.swap(*list);

        // После создания нового списка групп, они (группы) некоторое  время
        // остаются без актуального списка админов и владельцев. Если в этот
        // момент придет сообщение от админа, оно может быть  удалено  ботом.
        // Чтобы не оставлять группу с пустым списком  админов,  переприсваи-
        // ваем его из "старых" групп. Новый список админов будет получен и
        // обновлен через 1-2 секунды
        for (GroupChat* oldChat : *list)
        {
            QString       chatName               = oldChat->name();
            QSet<qint64>  adminIds               = oldChat->adminIds();
            QSet<qint64>  ownerIds               = oldChat->ownerIds();
            QStringList   adminNames             = oldChat->adminNames();
            bool          antiRaidTurnOn         = oldChat->antiRaidTurnOn;
            ChatMemberAdministrator::Ptr botInfo = oldChat->botInfo();

            if (GroupChat* newChat = chats.findItem(&oldChat->id))
            {
                if (oldChat == newChat)
                {
                    log_debug_m << "oldChat == newChat";
                    continue;
                }

                if (!chatName.isEmpty())
                    newChat->setName(chatName);

                if (!adminIds.isEmpty())
                {
                    if (newChat->anonymousAsAdmin)
                        adminIds.insert(GROUP_ANONYMOUS_BOT_ID);
                    else
                        adminIds.remove(GROUP_ANONYMOUS_BOT_ID);

                    newChat->setAdminIds(adminIds);
                }

                if (!ownerIds.isEmpty())
                    newChat->setOwnerIds(ownerIds);

                if (!adminNames.isEmpty())
                    newChat->setAdminNames(adminNames);

                newChat->antiRaidTurnOn = antiRaidTurnOn;

                if (botInfo)
                    newChat->setBotInfo(botInfo);
            }
        }
    }

    if (chats.sortState() != lst::SortState::Up)
        chats.sort();

//    GroupChat::List retChats;
//    for (GroupChat* c : chats)
//    {
//        c->add_ref();
//        retChats.add(c);
//    }
//    retChats.sort();
//    return retChats;

    return chats;
}

static QMutex timelimitInactiveChatsMutex;
static QSet<qint64> timelimitInactiveChatsSet;

QSet<qint64> timelimitInactiveChats()
{
    QMutexLocker locker {&timelimitInactiveChatsMutex}; (void) locker;
    return timelimitInactiveChatsSet;
}

void setTimelimitInactiveChats(const QSet<qint64>& chats)
{
    QMutexLocker locker {&timelimitInactiveChatsMutex}; (void) locker;
    timelimitInactiveChatsSet = chats;
}

void timelimitInactiveChatsAdd(qint64 chatId)
{
    QMutexLocker locker {&timelimitInactiveChatsMutex}; (void) locker;
    timelimitInactiveChatsSet.insert(chatId);
}

void timelimitInactiveChatsRemove(qint64 chatId)
{
    QMutexLocker locker {&timelimitInactiveChatsMutex}; (void) locker;
    timelimitInactiveChatsSet.remove(chatId);
}

} // namespace tbot
