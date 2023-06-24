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

struct trigger_logic_error : public std::logic_error
{
    explicit trigger_logic_error(const string& msg) : std::logic_error(msg) {}
};

bool TriggerLinkDisable::isActive(
                           const Update& update, GroupChat* chat,
                           const QString& clearText, const QString& alterText) const
{
    (void) clearText;
    (void) alterText;
    activationReasonMessage.clear();

    Message::Ptr message = update.message;
    if (message.empty())
        message = update.edited_message;

    if (message.empty())
        return false;

    bool urlGood = true;
    for (const MessageEntity& entity : message->entities)
    {
        QString urlStr;
        bool entityUrl = false;

        if (entity.type == "url")
        {
            entityUrl = true;
            urlStr = message->text.mid(entity.offset, entity.length);
        }
        else if (entity.type == "text_link")
        {
            entityUrl = true;
            urlStr = entity.url;
        }

        if (entityUrl)
        {
            urlGood = false;
            activationReasonMessage = u8"ссылка: " + urlStr;

            log_debug_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?'. Input url: %?",
                update.update_id, chat->name(), name, urlStr);

            QUrl url = QUrl::fromEncoded(urlStr.toUtf8());
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
                        urlGood = true;
                        break;
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
                            urlGood = true;
                            break;
                        }
                    }
                }

            if (!urlGood)
                break;
        }
    }
    if (!urlGood)
    {
        log_verbose_m << log_format(
            "\"update_id\":%?. Chat: %?. Trigger '%?' activated",
            update.update_id, chat->name(), name);
        return true;
    }
    return false;
}

bool TriggerLinkEnable::isActive(
                           const Update& update, GroupChat* chat,
                           const QString& clearText, const QString& alterText) const
{
    (void) clearText;
    (void) alterText;
    activationReasonMessage.clear();

    Message::Ptr message = update.message;
    if (message.empty())
        message = update.edited_message;

    if (message.empty())
        return false;

    bool urlBad = false;
    bool urlGood = true;
    for (const MessageEntity& entity : message->entities)
    {
        QString urlStr;
        bool entityUrl = false;

        if (entity.type == "url")
        {
            entityUrl = true;
            urlStr = message->text.mid(entity.offset, entity.length);
        }
        else if (entity.type == "text_link")
        {
            entityUrl = true;
            urlStr = entity.url;
        }

        if (entityUrl)
        {
            urlGood = false;
            activationReasonMessage = u8"ссылка: " + urlStr;

            log_debug_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?'. Input url: %?",
                update.update_id, chat->name(), name, urlStr);

            QUrl url = QUrl::fromEncoded(urlStr.toUtf8());
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
                        urlGood = true;
                        break;
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
                            urlGood = true;
                            break;
                        }
                    }
                }

            if (urlGood)
                continue;

            for (const ItemLink& item : blackList)
                if (host.endsWith(item.host, Qt::CaseInsensitive))
                {
                    if (item.paths.isEmpty())
                    {
                        log_verbose_m << log_format(
                            "\"update_id\":%?. Chat: %?. Trigger '%?', link bad"
                            ". It belong to blacklist [host: %?; path: empty]",
                            update.update_id, chat->name(), name, item.host);
                        urlBad = true;
                        break;
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
                            urlBad = true;
                            break;
                        }
                    }
                }

            if (urlBad)
                break;
        }
    }
    if (urlBad)
    {
        log_verbose_m << log_format(
            "\"update_id\":%?. Chat: %?. Trigger '%?' activated",
            update.update_id, chat->name(), name);
        return true;
    }
    return false;
}

bool TriggerWord::isActive(const Update& update, GroupChat* chat,
                           const QString& clearText, const QString& alterText) const
{
    (void) alterText;
    activationReasonMessage.clear();

    if (clearText.isEmpty())
        return false;

    Qt::CaseSensitivity caseSens = (caseInsensitive)
                                   ? Qt::CaseInsensitive
                                   : Qt::CaseSensitive;
    for (const QString& word : wordList)
        if (clearText.contains(word, caseSens))
        {
            log_verbose_m << log_format(
                "\"update_id\":%?. Chat: %?. Trigger '%?' activated"
                ". The word '%?' was found",
                update.update_id, chat->name(), name, word);

            activationReasonMessage = u8"слово: " + word;
            return true;
        }

    return false;
}

bool TriggerRegexp::isActive(const Update& update, GroupChat* chat,
                             const QString& clearText, const QString& alterText) const
{
    activationReasonMessage.clear();

    QString text = (analyze == "username") ? alterText : clearText;
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

            activationReasonMessage = u8"фраза: " + match.capturedTexts()[0];
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

    bool active = true;
    if (ytrigger["active"].IsDefined())
    {
        checkFiedType(ytrigger, "active", YAML::NodeType::Scalar);
        active = ytrigger["active"].as<bool>();
    }

    QString description;
    if (ytrigger["description"].IsDefined())
    {
        checkFiedType(ytrigger, "description", YAML::NodeType::Scalar);
        description = QString::fromStdString(ytrigger["description"].as<string>());
    }

    string type;
    if (ytrigger["type"].IsDefined())
    {
        checkFiedType(ytrigger, "type", YAML::NodeType::Scalar);
        type = ytrigger["type"].as<string>();
    }
    if (type != "link" // Синоним link_disable
        && type != "link_enable"
        && type != "link_disable"
        && type != "word"
        && type != "regexp")
    {
        throw trigger_logic_error(
            "In a 'trigger' node a field 'type' can take one of the following "
            "values: link_enable, link_disable/link, word, regexp. "
            "Current value: " + type);
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

    TriggerLinkBase::LinkList linkWhiteList;
    if (ytrigger["white_list"].IsDefined())
    {
        checkFiedType(ytrigger, "white_list", YAML::NodeType::Sequence);
        const YAML::Node& ywhite_list = ytrigger["white_list"];
        readWhiteBlackList(ywhite_list, linkWhiteList);
    }

    TriggerLinkBase::LinkList linkBlackList;
    if (ytrigger["black_list"].IsDefined())
    {
        checkFiedType(ytrigger, "black_list", YAML::NodeType::Sequence);
        const YAML::Node& yblack_list = ytrigger["black_list"];
        readWhiteBlackList(yblack_list, linkBlackList);
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

    bool inverse = false;
    if (ytrigger["inverse"].IsDefined())
    {
        checkFiedType(ytrigger, "inverse", YAML::NodeType::Scalar);
        inverse = ytrigger["inverse"].as<bool>();
    }

    bool immediatelyBan = false;
    if (ytrigger["immediately_ban"].IsDefined())
    {
        checkFiedType(ytrigger, "immediately_ban", YAML::NodeType::Scalar);
        immediatelyBan = ytrigger["immediately_ban"].as<bool>();
    }

    bool multiline = false;
    if (ytrigger["multiline"].IsDefined())
    {
        checkFiedType(ytrigger, "multiline", YAML::NodeType::Scalar);
        multiline = ytrigger["multiline"].as<bool>();
    }

    QString analyze;
    if (ytrigger["analyze"].IsDefined())
    {
        checkFiedType(ytrigger, "analyze", YAML::NodeType::Scalar);
        analyze = QString::fromStdString(ytrigger["analyze"].as<string>());
    }

    // Параметр analyze может принимать только два значения: content и username.
    // Значение параметра по умолчанию равно content
    if (analyze != "username")
        analyze = "content";

    Trigger::Ptr trigger;

    if ((type == "link") || (type == "link_disable"))
    {
        TriggerLinkDisable::Ptr triggerLinkD {new TriggerLinkDisable};
        triggerLinkD->whiteList = linkWhiteList;
        trigger = triggerLinkD;
    }
    else if (type == "link_enable")
    {
        TriggerLinkEnable::Ptr triggerLinkE {new TriggerLinkEnable};
        triggerLinkE->whiteList = linkWhiteList;
        triggerLinkE->blackList = linkBlackList;
        trigger = triggerLinkE;
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
        triggerRegexp->analyze = analyze;

        QRegularExpression::PatternOptions patternOpt =
            {QRegularExpression::DotMatchesEverythingOption
            |QRegularExpression::UseUnicodePropertiesOption};

        if (caseInsensitive)
            patternOpt |= QRegularExpression::CaseInsensitiveOption;

        if (multiline)
            patternOpt |= QRegularExpression::MultilineOption;

        for (const QString& pattern : regexpRemove)
        {
            QRegularExpression re {pattern, patternOpt};
            if (!re.isValid())
            {
                log_error_m << "Trigger '" << name << "'"
                            << ". Failed regular expression in regexp_remove"
                            << ". Pattren '" << pattern << "'"
                            << ". Error: " << re.errorString()
                            << ". Offset: " << re.patternErrorOffset();
                continue;
            }
            triggerRegexp->regexpRemove.append(std::move(re));
        }

        for (const QString& pattern : regexpList)
        {
            QRegularExpression re {pattern, patternOpt};
            if (!re.isValid())
            {
                log_error_m << "Trigger '" << name << "'"
                            << ". Failed regular expression in regexp_list"
                            << ". Pattren '" << pattern << "'"
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
        trigger->active = active;
        trigger->description = description;
        trigger->skipAdmins = skipAdmins;
        trigger->whiteUsers = whiteUsers;
        trigger->inverse = inverse;
        trigger->immediatelyBan = immediatelyBan;
    }
    return trigger;
}

bool loadTriggers(Trigger::List& triggers)
{
    auto locker {config::work().locker()}; (void) locker;

    bool result = false;
    try
    {
        YAML::Node ytriggers = config::work().nodeGet("triggers");
        if (!ytriggers.IsDefined() || ytriggers.IsNull())
            return false;

        if (!ytriggers.IsSequence())
            throw std::logic_error("'triggers' node must have sequence type");

        for (const YAML::Node& ytrigger : ytriggers)
            try
            {
                if (Trigger::Ptr t = createTrigger(ytrigger))
                    triggers.add(t.detach());
            }
            catch (trigger_logic_error& e)
            {
                log_error_m << "Trigger configure error. Detail: " << e.what()
                            << ". Config file: " << config::work().filePath();
            }

        result = true;
    }
    catch (YAML::ParserException& e)
    {
        triggers.clear();
        log_error_m << "YAML error. Detail: " << e.what()
                    << ". Config file: " << config::work().filePath();
    }
    catch (std::exception& e)
    {
        triggers.clear();
        log_error_m << "Configuration error. Detail: " << e.what()
                    << ". Config file: " << config::work().filePath();
    }
    catch (...)
    {
        triggers.clear();
        log_error_m << "Unknown error"
                    << ". Config file: " << config::work().filePath();
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

        logLine << "; skip_admins: " << trigger->skipAdmins;

        nextCommaVal = false;
        logLine << "; white_users: [";
        for (qint64 item : trigger->whiteUsers)
            logLine << nextComma() << item;
        logLine << "]";

        logLine << "; inverse: " << trigger->inverse
                << "; immediately_ban: " << trigger->immediatelyBan;

        if (!trigger->description.isEmpty())
            logLine << "; description: " << trigger->description;
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
