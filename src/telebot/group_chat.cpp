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

QSet<qint64> GroupChat::adminIds() const
{
    QMutexLocker locker(&_adminIdsLock); (void) locker;
    return _adminIds;
}

void GroupChat::setAdminIds(const QSet<qint64>& val)
{
    QMutexLocker locker(&_adminIdsLock); (void) locker;
    _adminIds = val;
}

QSet<qint64> GroupChat::ownerIds() const
{
    QMutexLocker locker(&_ownerIdsLock); (void) locker;
    return _ownerIds;
}

void GroupChat::setOwnerIds(const QSet<qint64>& val)
{
    QMutexLocker locker(&_ownerIdsLock); (void) locker;
    _ownerIds = val;
}

GroupChat::Ptr createGroupChat(const YAML::Node& ychat)
{
    auto checkFiedType = [&ychat](const string& field, YAML::NodeType::value type)
    {
        if (ychat[field].IsNull())
            throw std::logic_error(
                "For 'group_chats' node a field '" + field + "' can not be null");

        if (ychat[field].Type() != type)
            throw std::logic_error(
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
        throw std::logic_error("In a 'group_chats' node a field 'id' can not be empty");

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

    GroupChat::Ptr chat {new GroupChat};
    chat->id = id;
    chat->name = name;
    chat->skipAdmins = skipAdmins;
    chat->whiteUsers = whiteUsers;
    chat->userSpamLimit = userSpamLimit;

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
        }
    }
    return chat;
}

bool loadGroupChats(GroupChat::List& chats)
{
    auto locker {config::base().locker()}; (void) locker;

    bool result = false;
    try
    {
        YAML::Node ychats = config::base().nodeGet("group_chats");
        if (ychats.IsDefined())
        {
            if (!ychats.IsSequence())
                throw std::logic_error("'group_chats' node must have sequence type");

            for (const YAML::Node& ychat : ychats)
                if (GroupChat::Ptr c = createGroupChat(ychat))
                    chats.add(c.detach());
        }
        chats.sort();
        result = true;
    }
    catch (YAML::ParserException& e)
    {
        log_error_m << "YAML error. Detail: " << e.what()
                    << ". Config file: " << config::base().filePath();
    }
    catch (std::exception& e)
    {
        log_error_m << "Configuration error. Detail: " << e.what()
                    << ". Config file: " << config::base().filePath();
    }
    catch (...)
    {
        log_error_m << "Unknown error"
                    << ". Config file: " << config::base().filePath();
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
        logLine << "; name: " << chat->name;

        nextCommaVal = false;
        logLine << "; triggers: [";
        for (Trigger* trigger : chat->triggers)
            logLine << nextComma() << trigger->name;
        logLine << "]";

        logLine << "; skip_admins: " << chat->skipAdmins;

        nextCommaVal = false;
        logLine << "; white_users: [";
        for (qint64 item : chat->whiteUsers)
            logLine << nextComma() << item;
        logLine << "]";

        logLine << "; user_spam_limit: " << chat->userSpamLimit;
    }
    log_info_m << "---";
}

GroupChat::List groupChats(GroupChat::List* list)
{
    static QMutex mutex;
    static QMutexLocker locker {&mutex}; (void) locker;
    static GroupChat::List chats;

    if (list)
        chats.swap(*list);

    GroupChat::List retChats;
    for (GroupChat* c : chats)
    {
        c->add_ref();
        retChats.add(c);
    }
    retChats.sort();
    return retChats;
}

} // namespace tbot
