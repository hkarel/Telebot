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

struct GroupChat;

struct Trigger : public clife_base
{
    typedef clife_ptr<Trigger> Ptr;

    enum class TextType
    {
        Content  = 0,
        UserName = 1,
        FileMime = 2,
    };
    typedef QMap<TextType, QString> Text;

    // Имя триггера
    QString name;

    // Тип триггера
    QString type;

    // Признак активного триггера. Позволяет исключить триггер из процесса
    // обработки сообщений
    bool active = {true};

    // Информационное описание триггера
    QString description;

    // Действия триггера не распространяются на администраторов группы если
    // параметр установлен в TRUE
    bool skipAdmins = {false};

    // Список идентификаторов пользователей, на которых не распространяется
    // действие триггера
    QSet<qint64> whiteUsers;

    // Инвертирует результат работы триггера
    bool inverse = {false};

    // Отправлять отчет о спаме в систему учета штрафов
    bool reportSpam = {true};

    // Если триггер однозначно идентифицирует спам-сообщение, то пользователя
    // можно заблокировать сразу
    bool immediatelyBan = {false};

    // Содержит текстовую информацию о причине активации триггера, используется
    // для объяснения причины удаления телеграм-сообщения
    mutable QString activationReasonMessage;

    // Проверяет сообщение на соответствие критериям фильтрации.
    // Параметр Text содержит текстовое сообщение с удаленными линками
    virtual bool isActive(const tbot::Update&, GroupChat*, const Text&) const = 0;

    struct Find
    {
        int operator() (const QString* name, const Trigger* item2) const
            {return name->compare(item2->name);}
    };
    typedef lst::List<Trigger, Find, clife_alloc<Trigger>> List;

protected:
    Trigger() = default;
    DISABLE_DEFAULT_COPY(Trigger)

    void assign(const Trigger&);
};

struct TriggerLinkBase : public Trigger
{
    struct ItemLink
    {
        QString host;
        QStringList paths;
    };
    typedef QList<ItemLink> LinkList;

    // Белый список исключений
    LinkList whiteList;

    // Черный список исключений
    LinkList blackList;

    void assign(const TriggerLinkBase&);

protected:
    TriggerLinkBase() = default;
    DISABLE_DEFAULT_COPY(TriggerLinkBase)
};

struct TriggerLinkDisable : public TriggerLinkBase
{
    typedef clife_ptr<TriggerLinkDisable> Ptr;

    TriggerLinkDisable() = default;
    DISABLE_DEFAULT_COPY(TriggerLinkDisable)

    bool isActive(const tbot::Update&, GroupChat*, const Text&) const override;
};

struct TriggerLinkEnable : public TriggerLinkBase
{
    typedef clife_ptr<TriggerLinkEnable> Ptr;

    TriggerLinkEnable() = default;
    DISABLE_DEFAULT_COPY(TriggerLinkEnable)

    bool isActive(const tbot::Update&, GroupChat*, const Text&) const override;
};

struct TriggerWord : public Trigger
{
    typedef clife_ptr<TriggerWord> Ptr;

    TriggerWord() = default;
    DISABLE_DEFAULT_COPY(TriggerWord)

    // Признак сравнения без учета регистра
    bool caseInsensitive = {true};

    // Список слов
    QStringList wordList;

    bool isActive(const tbot::Update&, GroupChat*, const Text&) const override;

    void assign(const TriggerWord&);
};

struct TriggerRegexp : public Trigger
{
    typedef clife_ptr<TriggerRegexp> Ptr;

    TriggerRegexp() = default;
    DISABLE_DEFAULT_COPY(TriggerRegexp)

    // Признак сравнения без учета регистра
    bool caseInsensitive = {true};

    // Использовать многострочный режим, где ^ и $ соответствуют началу и концу
    // одной строки текста (а не началу и концу всего текста).
    // См. описание опции QRegularExpression::MultilineOption:
    //   https://doc.qt.io/qt-6/qregularexpression.html#PatternOption-enum
    bool multiline = {false};

    // Параметр определяет что будет анализироваться, может принимать следующие
    // значения:
    //   content  - контент сообщения;
    //   username - имя пользователя;
    //   filemime - наименование и mimetype вложенного документа.
    // Значение параметра по умолчанию равно content
    QString analyze = {"content"};

    // Список регулярных выражений для сокращения исходного текста
    QList<QRegularExpression> regexpRemove;

    // Список регулярных выражений
    QList<QRegularExpression> regexpList;

    bool isActive(const tbot::Update&, GroupChat*, const Text&) const override;

    void assign(const TriggerRegexp&);
};

struct TriggerTimeLimit : public Trigger
{
    typedef clife_ptr<TriggerTimeLimit> Ptr;

    TriggerTimeLimit() = default;
    DISABLE_DEFAULT_COPY(TriggerTimeLimit)

    int utc = {0}; // Часовой пояс

    struct TimeRange
    {
        QTime begin; // Начало действия ограничения
        QTime end;   // Окончание действия ограничения
        QTime hint;  // Подсказка для отображения в информ-сообщениях
    };
    typedef QList<TimeRange> Times;

    struct Day
    {
        Times times;
        QSet<int> daysOfWeek = {1,2,3,4,5,6,7}; // Дни недели, 1 - понедельник
    };
    typedef QList<Day> Week;

    Week week;

    QString messageBegin; // Сообщение отправляется в момент начала ограничения
    QString messageEnd;   // Сообщение отправляется в момент окончания ограничения
    QString messageInfo;  // Сообщение отправляется в период действия ограничения

    bool hideMessageBegin = {false}; // Скрывать сообщенеие messageBegin
    bool hideMessageEnd   = {false}; // Скрывать сообщенеие messageEnd

    mutable TimeRange activationTime;

    bool isActive(const tbot::Update&, GroupChat*, const Text&) const override;

    void assign(const TriggerTimeLimit&);
    void timesRangeOfDay(int dayOfWeek, Times& times) const;
};

const char* yamlTypeName(YAML::NodeType::value type);

bool loadTriggers(Trigger::List&);
void printTriggers(Trigger::List&);

Trigger::List triggers(Trigger::List* = nullptr);

// Проверяет нахождение времени time в диапазоне [begin, end]
bool timeInRange(const QTime& begin, const QTime& time, const QTime& end);

} // namespace tbot
