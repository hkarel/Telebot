#pragma once

#include "tele_data.h"

#include "shared/list.h"
#include "shared/defmac.h"
#include "shared/clife_base.h"
#include "shared/clife_alloc.h"
#include "shared/clife_ptr.h"
#include "shared/config/appl_conf.h"

#include <QtCore>
#include <QRegularExpression>

namespace tbot {

struct Trigger : public clife_base
{
public:
    typedef clife_ptr<Trigger> Ptr;

    Trigger() = default;
    virtual ~Trigger() = default;

    // Имя триггера
    QString name;

    // Действия триггера не распространяются на администраторов группы если
    // параметр установлен в TRUE
    bool skipAdmins = {true};

    // Список идентификаторов пользователей, на которых не распространяется
    // действие триггера
    QSet<qint64> whiteUsers;

    // Проверяет сообщение на соответствие критериям фильтрации.
    // Параметр clearText содержит текстовое сообщение с удаленными линками
    virtual bool isActive(const tbot::Update&, const QString& clearText) const = 0;

    struct Find
    {
        int operator() (const QString* name, const Trigger* item2) const
            {return name->compare(item2->name);}
    };
    typedef lst::List<Trigger, Find, clife_alloc<Trigger>> List;

private:
    DISABLE_DEFAULT_COPY(Trigger)
};

struct TriggerLink : public Trigger
{
public:
    typedef clife_ptr<TriggerLink> Ptr;

    TriggerLink() = default;

    // Список исключений
    QStringList whiteList;

    bool isActive(const tbot::Update&, const QString& clearText) const override;

private:
    DISABLE_DEFAULT_COPY(TriggerLink)
};

struct TriggerWord : public Trigger
{
public:
    typedef clife_ptr<TriggerWord> Ptr;

    TriggerWord() = default;

    // Признак сравнения без учета регистра
    bool caseInsensitive = {true};

    // Список слов
    QStringList wordList;

    bool isActive(const tbot::Update&, const QString& clearText) const override;

private:
    DISABLE_DEFAULT_COPY(TriggerWord)
};

struct TriggerRegexp : public Trigger
{
public:
    typedef clife_ptr<TriggerRegexp> Ptr;

    TriggerRegexp() = default;

    // Признак сравнения без учета регистра
    bool caseInsensitive = {true};

    // См. описание опции QRegularExpression::MultilineOption
    // https://doc.qt.io/qt-6/qregularexpression.html#PatternOption-enum
    bool multiline = {false};

    // Список регулярных выражений
    QList<QRegularExpression> regexpList;

    bool isActive(const tbot::Update&, const QString& clearText) const override;

private:
    DISABLE_DEFAULT_COPY(TriggerRegexp)
};

const char* yamlTypeName(YAML::NodeType::value type);

bool loadTriggers(Trigger::List&);
void printTriggers(Trigger::List&);

Trigger::List triggers(Trigger::List* = nullptr);

} // namespace tbot
