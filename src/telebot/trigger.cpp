#include "trigger.h"
#include "group_chat.h"

#include "shared/break_point.h"
#include "shared/logger/logger.h"
#include "shared/logger/format.h"

#include <string>
#include <stdexcept>

#define log_error_m   alog::logger().error  (alog_line_location, "Trigger")
#define log_warn_m    alog::logger().warn   (alog_line_location, "Trigger")
#define log_info_m    alog::logger().info   (alog_line_location, "Trigger")
#define log_verbose_m alog::logger().verbose(alog_line_location, "Trigger")
#define log_debug_m   alog::logger().debug  (alog_line_location, "Trigger")
#define log_debug2_m  alog::logger().debug2 (alog_line_location, "Trigger")

namespace tbot {

using namespace std;

bool TriggerLink::isActive(const Update& update, GroupChat* chat,
                           const QString& /*clearText*/) const
{
    Message::Ptr message = update.message;
    if (message.empty())
        message = update.edited_message;

    if (message.empty())
        return false;

    bool entityUrl = false;
    for (const MessageEntity& entity : message->entities)
    {
        if (entity.type == "url")
        {
            entityUrl = true;
            QString urlStr = message->text.mid(entity.offset, entity.length);

            log_debug_m << log_format(
                R"("update_id":%?. Chat: %?. Trigger %?. Input url: %?)",
                update.update_id, chat->name, name, urlStr);

            QUrl url = QUrl::fromEncoded(urlStr.toUtf8());
            QString host = url.host();
            QString path = url.path();
            for (const ItemLink& item : whiteList)
                if (host.endsWith(item.host, Qt::CaseInsensitive))
                {
                    if (item.paths.isEmpty())
                    {
                        log_verbose_m << log_format(
                            R"("update_id":%?. Chat: %?. Trigger %? is skipped)"
                            ". Link belong to whitelist [host: %?; path: empty]",
                                             update.update_id, chat->name, name,
                                             item.host);
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
                                R"("update_id":%?. Chat: %?. Trigger %? is skipped)"
                                ". Link belong to whitelist [host: %?; path: %?]",
                                                 update.update_id, chat->name, name,
                                                 item.host, ipath);
                            return false;
                        }
                    }
                }
        }
    }
    if (entityUrl)
    {
        log_verbose_m << log_format(
            R"("update_id":%?. Chat: %?. Trigger %? activated)",
            update.update_id, chat->name, name);
        return true;
    }
    return false;
}

bool TriggerWord::isActive(const Update& update, GroupChat* chat,
                           const QString& clearText) const
{
    if (clearText.isEmpty())
        return false;

    Qt::CaseSensitivity caseSens = (caseInsensitive)
                                   ? Qt::CaseInsensitive
                                   : Qt::CaseSensitive;
    for (const QString& word : wordList)
        if (clearText.contains(word, caseSens))
        {
            log_verbose_m << log_format(
                R"("update_id":%?. Chat: %?. Trigger %? activated)"
                ". The word '%?' was found",
                update.update_id, chat->name, name, word);
            return true;
        }

    return false;
}

bool TriggerRegexp::isActive(const Update& update, GroupChat* chat,
                             const QString& clearText) const
{
    if (clearText.isEmpty())
        return false;

    QString text = clearText;
    for (const QRegularExpression& re : regexpRemove)
        text.remove(re);

    if (text.length() != clearText.length())
        log_verbose_m << log_format(
            R"("update_id":%?. Chat: %?. Trigger %?)"
            ". Clear text after regexp remove: %?",
            update.update_id, chat->name, name, text);

    for (const QRegularExpression& re : regexpList)
    {
        const QRegularExpressionMatch match = re.match(text);
        if (match.hasMatch())
        {
            alog::Line logLine = log_verbose_m << log_format(
                R"("update_id":%?. Chat: %?. Trigger %? activated)"
                ". Regular expression '%?' matched. Captured text: ",
                update.update_id, chat->name, name, re.pattern());
            for (const QString& cap : match.capturedTexts())
                logLine << cap << "; ";

            return true;
        }
        else
        {
            log_debug_m << log_format(
                R"("update_id":%?. Chat: %?. Trigger %?)"
                ". Regular expression pattern '%?' not match",
                update.update_id, chat->name, name, re.pattern());
        }
    }
    return false;
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

Trigger::Ptr createTrigger(const YAML::Node& ytrigger)
{
    auto checkFiedType = [](const YAML::Node& ynode, const string& field,
                            YAML::NodeType::value type)
    {
        if (ynode[field].IsNull())
            throw std::logic_error(
                "For 'trigger' node a field '" + field + "' can not be null");

        if (ynode[field].Type() != type)
            throw std::logic_error(
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
        throw std::logic_error("In a filter-node a field 'name' can not be empty");

    string type;
    if (ytrigger["type"].IsDefined())
    {
        checkFiedType(ytrigger, "type", YAML::NodeType::Scalar);
        type = ytrigger["type"].as<string>();
    }
    if (type != "link"
        && type != "word"
        && type != "regexp")
    {
        throw std::logic_error(
            "In a filter-node a field 'type' can take one of the following "
            "values: link, word, regexp. "
            "Current value: " + type);
    }

    TriggerLink::WhiteList linkWhiteList;
    if (ytrigger["white_list"].IsDefined())
    {
        checkFiedType(ytrigger, "white_list", YAML::NodeType::Sequence);
        const YAML::Node& ywhite_list = ytrigger["white_list"];
        for (const YAML::Node& ywhite_item : ywhite_list)
        {
            checkFiedType(ywhite_item, "host", YAML::NodeType::Scalar);
            TriggerLink::ItemLink itemLink;

            const YAML::Node yhost = ywhite_item["host"];
            itemLink.host = QString::fromStdString(yhost.as<string>()).trimmed();

            if (itemLink.host.isEmpty())
                continue;

            const YAML::Node ypath_list = ywhite_item["paths"];
            if (ypath_list.IsDefined())
            {
                checkFiedType(ywhite_item, "paths", YAML::NodeType::Sequence);
                for (const YAML::Node& ypath : ypath_list)
                {
                    QString path = QString::fromStdString(ypath.as<string>()).trimmed();
                    if (path.isEmpty())
                        continue;
                    itemLink.paths.append(path);
                }
            }
            linkWhiteList.append(itemLink);
        }
    }

    QStringList wordList;
    if (ytrigger["word_list"].IsDefined())
    {
        checkFiedType(ytrigger, "word_list", YAML::NodeType::Sequence);
        const YAML::Node& yword_list = ytrigger["word_list"];
        for (const YAML::Node& yword : yword_list)
            wordList.append(QString::fromStdString(yword.as<string>()));
    }

    QStringList regexpRemove;
    if (ytrigger["regexp_remove"].IsDefined())
    {
        checkFiedType(ytrigger, "regexp_remove", YAML::NodeType::Sequence);
        const YAML::Node& yregexp_remove = ytrigger["regexp_remove"];
        for (const YAML::Node& yregexp : yregexp_remove)
            regexpRemove.append(QString::fromStdString(yregexp.as<string>()));
    }

    QStringList regexpList;
    if (ytrigger["regexp_list"].IsDefined())
    {
        checkFiedType(ytrigger, "regexp_list", YAML::NodeType::Sequence);
        const YAML::Node& yregexp_list = ytrigger["regexp_list"];
        for (const YAML::Node& yregexp : yregexp_list)
            regexpList.append(QString::fromStdString(yregexp.as<string>()));
    }

    bool caseInsensitive = true;
    if (ytrigger["case_insensitive"].IsDefined())
    {
        checkFiedType(ytrigger, "case_insensitive", YAML::NodeType::Scalar);
        caseInsensitive = ytrigger["case_insensitive"].as<bool>();
    }

    bool skipAdmins = false;
    if (ytrigger["skip_admins"].IsDefined())
    {
        checkFiedType(ytrigger, "skip_admins", YAML::NodeType::Scalar);
        skipAdmins = ytrigger["skip_admins"].as<bool>();
    }

    QSet<qint64> whiteUsers;
    if (ytrigger["white_users"].IsDefined())
    {
        checkFiedType(ytrigger, "white_users", YAML::NodeType::Sequence);
        const YAML::Node& ywhite_users = ytrigger["white_users"];
        for (const YAML::Node& ywhite : ywhite_users)
            whiteUsers.insert(ywhite.as<int64_t>());
    }

    bool multiline = false;
    if (ytrigger["multiline"].IsDefined())
    {
        checkFiedType(ytrigger, "multiline", YAML::NodeType::Scalar);
        multiline = ytrigger["multiline"].as<bool>();
    }

    Trigger::Ptr trigger;

    if (type == "link")
    {
        TriggerLink::Ptr triggerLink {new TriggerLink};
        triggerLink->whiteList = linkWhiteList;
        trigger = triggerLink;
    }
    else if (type == "word")
    {
        TriggerWord::Ptr triggerWord {new TriggerWord};
        triggerWord->caseInsensitive = caseInsensitive;
        triggerWord->wordList = wordList;
        trigger = triggerWord;
    }
    else if (type == "regexp")
    {
        TriggerRegexp::Ptr triggerRegexp {new TriggerRegexp};
        triggerRegexp->caseInsensitive = caseInsensitive;
        triggerRegexp->multiline = multiline;

        QRegularExpression::PatternOptions pattern =
            {QRegularExpression::UseUnicodePropertiesOption};

        if (caseInsensitive)
            pattern |= QRegularExpression::CaseInsensitiveOption;

        if (multiline)
            pattern |= QRegularExpression::MultilineOption;

        for (const QString& s : regexpRemove)
        {
            QRegularExpression re {s, pattern};
            if (!re.isValid())
            {
                log_error_m << "Trigger '" << name << "'"
                            << ". Failed regular expression in regexp_remove"
                            << ". Error: " << re.errorString()
                            << ". Offset: " << re.patternErrorOffset();
                continue;
            }
            triggerRegexp->regexpRemove.append(std::move(re));
        }

        for (const QString& s : regexpList)
        {
            QRegularExpression re {s, pattern};
            if (!re.isValid())
            {
                log_error_m << "Trigger '" << name << "'"
                            << ". Failed regular expression in regexp_list"
                            << ". Error: " << re.errorString()
                            << ". Offset: " << re.patternErrorOffset();
                continue;
            }
            triggerRegexp->regexpList.append(std::move(re));
        }
        trigger = triggerRegexp;
    }
    if (trigger)
    {
        trigger->name = name;
        trigger->skipAdmins = skipAdmins;
        trigger->whiteUsers = whiteUsers;
    }
    return trigger;
}

bool loadTriggers(Trigger::List& triggers)
{
    auto locker {config::base().locker()}; (void) locker;

    bool result = false;
    try
    {
        YAML::Node ytriggers = config::base().nodeGet("triggers");
        if (!ytriggers.IsDefined() || ytriggers.IsNull())
            return false;

        if (!ytriggers.IsSequence())
            throw std::logic_error("'triggers' node must have sequence type");

        for (const YAML::Node& ytrigger : ytriggers)
            if (Trigger::Ptr t = createTrigger(ytrigger))
                triggers.add(t.detach());

        result = true;
    }
    catch (YAML::ParserException& e)
    {
        triggers.clear();
        log_error_m << "YAML error. Detail: " << e.what()
                    << ". Config file: " << config::base().filePath();
    }
    catch (std::exception& e)
    {
        triggers.clear();
        log_error_m << "Configuration error. Detail: " << e.what()
                    << ". Config file: " << config::base().filePath();
    }
    catch (...)
    {
        triggers.clear();
        log_error_m << "Unknown error"
                    << ". Config file: " << config::base().filePath();
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

        if (TriggerLink* triggerLink = dynamic_cast<TriggerLink*>(trigger))
        {
            logLine << "; type: link";

            nextCommaVal = false;
            logLine << "; white_list: [";
            for (const TriggerLink::ItemLink& item : triggerLink->whiteList)
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
            logLine << "]";
        }
        else if (TriggerWord* triggerWord = dynamic_cast<TriggerWord*>(trigger))
        {
            logLine << "; type: word"
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
                    << "; case_insensitive: " << triggerRegexp->caseInsensitive
                    << "; multiline: " << triggerRegexp->multiline;

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

        logLine << "; skip_admins: " << trigger->skipAdmins;

        nextCommaVal = false;
        logLine << "; white_users: [";
        for (qint64 item : trigger->whiteUsers)
            logLine << nextComma() << item;
        logLine << "]";
    }
    log_info_m << "---";
}

Trigger::List triggers(Trigger::List* list)
{
    static QMutex mutex;
    static QMutexLocker locker {&mutex}; (void) locker;
    static Trigger::List triggers;

    if (list)
        triggers.swap(*list);

    Trigger::List retTriggers;
    for (Trigger* t : triggers)
    {
        t->add_ref();
        retTriggers.add(t);
    }
    return retTriggers;
}

} // namespace tbot
