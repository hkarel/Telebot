#include "trigger.h"
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
    reportSpam     = trigger.reportSpam;
    immediatelyBan = trigger.immediatelyBan;
}

void TriggerLinkBase::assign(const TriggerLinkBase& trigger)
{
    Trigger::assign(trigger);

    whiteList = trigger.whiteList;
    blackList = trigger.blackList;
}

bool TriggerLinkDisable::isActive(const Update& update, GroupChat* chat,
                                  const Text& text_) const
{
    (void) text_;
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

        activationReasonMessage = u8"ссылка: " + urlStr;

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
                                 const Text& text_) const
{
    (void) text_;
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

        activationReasonMessage = u8"ссылка: " + urlStr;

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
    QString text = text_[TextType::Content];

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

            activationReasonMessage = u8"слово: " + word;
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

    if      (analyze == "content" ) text = text_[TextType::Content];
    else if (analyze == "username") text = text_[TextType::UserName];
    else if (analyze == "filemime") text = text_[TextType::FileMime];

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

void TriggerRegexp::assign(const TriggerRegexp& trigger)
{
    Trigger::assign(trigger);

    caseInsensitive = trigger.caseInsensitive;
    multiline       = trigger.multiline;
    analyze         = trigger.analyze;
    regexpRemove    = trigger.regexpRemove;
    regexpList      = trigger.regexpList;
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
        && type != "regexp")
    {
        throw trigger_logic_error(
            "In a 'trigger' node a field 'type' can take one of the following "
            "values: link_enable, link_disable/link, word, regexp. "
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

    optional<bool> reportSpam0;
    if (ytrigger["report_spam"].IsDefined())
    {
        checkFiedType(ytrigger, "report_spam", YAML::NodeType::Scalar);
        reportSpam0 = ytrigger["report_spam"].as<bool>();
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

        // Параметр analyze может принимать значения: content, username, filemime
        // Значение параметра по умолчанию равно content
        if ((analyzeO != "username") && (analyzeO != "filemime"))
            analyzeO = "content";
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
    if (trigger)
    {
        trigger->name = name;
        trigger->type = type;
        trigger->active = active;
        trigger->description = description;

        assignValue(trigger->skipAdmins, skipAdminsO);
        assignValue(trigger->whiteUsers, whiteUsersO);
        assignValue(trigger->inverse, inverseO);
        assignValue(trigger->reportSpam, reportSpam0);
        assignValue(trigger->immediatelyBan, immediatelyBanO);
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
                if (Trigger::Ptr t = createTrigger(ytrigger, triggers))
                    triggers.add(t.detach());
            }
            catch (trigger_logic_error& e)
            {
                log_error_m << "Trigger configure error. Detail: " << e.what()
                            << ". Config file: " << config::work().filePath();
                ++globalConfigParceErrors;
            }

        result = true;
    }
    catch (YAML::ParserException& e)
    {
        triggers.clear();
        log_error_m << "YAML error. Detail: " << e.what()
                    << ". Config file: " << config::work().filePath();
        ++globalConfigParceErrors;
    }
    catch (std::exception& e)
    {
        triggers.clear();
        log_error_m << "Configuration error. Detail: " << e.what()
                    << ". Config file: " << config::work().filePath();
        ++globalConfigParceErrors;
    }
    catch (...)
    {
        triggers.clear();
        log_error_m << "Unknown error"
                    << ". Config file: " << config::work().filePath();
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

        logLine << "; skip_admins: " << trigger->skipAdmins;

        nextCommaVal = false;
        logLine << "; white_users: [";
        for (qint64 item : trigger->whiteUsers)
            logLine << nextComma() << item;
        logLine << "]";

        logLine << "; inverse: " << trigger->inverse
                << "; report_spam: " << trigger->reportSpam
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
