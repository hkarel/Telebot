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

using namespace std;

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
    auto checkFiedType = [&ychat](const string& field, YAML::NodeType::value type)
    {
        if (ychat[field].IsNull())
            throw group_logic_error(
                "For 'group_chats' node a field '" + field + "' can not be null");

        if (ychat[field].Type() != type)
            throw group_logic_error(
                "For 'group_chats' node a field '" + field + "' "
                "must have type '" + yamlTypeName(type) + "'");
    };

    qint64 id = {0};
    if (ychat["id"].IsDefined())
    {
        checkFiedType("id", YAML::NodeType::Scalar);
        id = ychat["id"].as<int64_t>();
    }
    if (id == 0)
        throw group_logic_error("In a 'group_chats' node a field 'id' can not be empty");

    QString name;
    if (ychat["name"].IsDefined())
    {
        checkFiedType("name", YAML::NodeType::Scalar);
        name = QString::fromStdString(ychat["name"].as<string>());
    }

    QStringList triggerNames;
    if (ychat["triggers"].IsDefined())
    {
        checkFiedType("triggers", YAML::NodeType::Sequence);
        const YAML::Node& ytriggers = ychat["triggers"];
        for (const YAML::Node& ytrigger : ytriggers)
            triggerNames.append(QString::fromStdString(ytrigger.as<string>()));
    }

    bool skipAdmins = true;
    if (ychat["skip_admins"].IsDefined())
    {
        checkFiedType("skip_admins", YAML::NodeType::Scalar);
        skipAdmins = ychat["skip_admins"].as<bool>();
    }

    bool premiumBan = false;
    if (ychat["premium_ban"].IsDefined())
    {
        checkFiedType("premium_ban", YAML::NodeType::Scalar);
        premiumBan = ychat["premium_ban"].as<bool>();
    }

    QSet<qint64> whiteUsers;
    if (ychat["white_users"].IsDefined())
    {
        checkFiedType("white_users", YAML::NodeType::Sequence);
        const YAML::Node& ywhite_users = ychat["white_users"];
        for (const YAML::Node& ywhite : ywhite_users)
            whiteUsers.insert(ywhite.as<int64_t>());
    }

    qint32 userSpamLimit = 5;
    if (ychat["user_spam_limit"].IsDefined())
    {
        checkFiedType("user_spam_limit", YAML::NodeType::Scalar);
        userSpamLimit = ychat["user_spam_limit"].as<int>();
    }

    QVector<qint32> userRestricts;
    if (ychat["user_restricts"].IsDefined())
    {
        checkFiedType("user_restricts", YAML::NodeType::Sequence);
        const YAML::Node& yuser_restricts = ychat["user_restricts"];
        for (const YAML::Node& yrestrict : yuser_restricts)
            userRestricts.append(yrestrict.as<int32_t>());
    }

    GroupChat::Ptr chat {new GroupChat};
    chat->id = id;
    chat->setName(name);
    chat->skipAdmins = skipAdmins;
    chat->premiumBan = premiumBan;
    chat->whiteUsers = whiteUsers;
    chat->userSpamLimit = userSpamLimit;
    chat->userRestricts = userRestricts;

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

bool loadGroupChats(GroupChat::List& chats)
{
    auto locker {config::work().locker()}; (void) locker;

    bool result = false;
    try
    {
        YAML::Node ychats = config::work().nodeGet("group_chats");
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
                            << ". Config file: " << config::work().filePath();
                ++globalConfigParceErrors;
            }

        chats.sort();
        result = true;
    }
    catch (YAML::ParserException& e)
    {
        log_error_m << "YAML error. Detail: " << e.what()
                    << ". Config file: " << config::work().filePath();
        ++globalConfigParceErrors;
    }
    catch (std::exception& e)
    {
        log_error_m << "Configuration error. Detail: " << e.what()
                    << ". Config file: " << config::work().filePath();
        ++globalConfigParceErrors;
    }
    catch (...)
    {
        log_error_m << "Unknown error"
                    << ". Config file: " << config::work().filePath();
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

        nextCommaVal = false;
        logLine << "; white_users: [";
        for (qint64 item : chat->whiteUsers)
            logLine << nextComma() << item;
        logLine << "]";

        logLine << "; user_spam_limit: " << chat->userSpamLimit;

        nextCommaVal = false;
        logLine << "; user_restricts: [";
        for (qint32 item : chat->userRestricts)
            logLine << nextComma() << item;
        logLine << "]";
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
        // обновлен через 1-2 секунды.
        for (GroupChat* chat : *list)
        {
            QSet<qint64> adminIds = chat->adminIds();
            QSet<qint64> ownerIds = chat->ownerIds();
            ChatMemberAdministrator::Ptr botInfo = chat->botInfo();
            if (lst::FindResult fr = chats.findRef(chat->id))
            {
                if (!adminIds.isEmpty())
                    chats[fr.index()].setAdminIds(adminIds);

                if (!ownerIds.isEmpty())
                    chats[fr.index()].setOwnerIds(ownerIds);

                if (botInfo)
                    chats[fr.index()].setBotInfo(botInfo);
            }
        }
    }

    GroupChat::List retChats;
    for (GroupChat* c : chats)
    {
        c->add_ref();
        retChats.add(c);
    }
    retChats.sort();
    return retChats;
}

static QMutex timelimitInactiveChatsMutex;
static QSet<qint64> timelimitInactiveChatsSet;

QSet<qint64> timelimitInactiveChats()
{
    QMutexLocker locker {&timelimitInactiveChatsMutex}; (void) locker;
    return timelimitInactiveChatsSet;
}

void setTimelimitInactiveChats(const QList<qint64>& chats)
{
    QMutexLocker locker {&timelimitInactiveChatsMutex}; (void) locker;
    timelimitInactiveChatsSet = QSet<qint64>::fromList(chats);
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
