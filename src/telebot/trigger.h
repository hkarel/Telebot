#pragma once

#include "commands/tele_data.h"

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
        Content = 0,
        UserId,
        IsPremium,
        FrwdUserId, // Forward UserId
        FrwdChatId, // Forward ChatId
        PersChatId, // Personal ChatId
        UserName,
        FileMime,
        UrlLinks,
    };
    typedef QMap<TextType, QVariant> Text;

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

    // Использовать триггер для проверки BIO пользователя
    bool checkBio = {false};

    // Использовать триггер для проверки только BIO пользователя, основное
    // сообщение триггером проверяться не будет
    bool onlyBio = {false};

    // Отправлять отчет о спаме в систему учета штрафов
    bool reportSpam = {true};

    // Если триггер сработал при вступлении нового пользователя в группу,
    // то пользователь будет заблокирован
    bool newUserBan = {false};

    // Если триггер однозначно идентифицирует спам-сообщение, и пользователь
    // имеет премиум аккаунт, то пользователя можно заблокировать сразу.
    // Так же смотри описание аналогичного свойства у группы
    bool premiumBan = {false};

    // Если триггер однозначно идентифицирует спам-сообщение, то пользователя
    // можно заблокировать сразу
    bool immediatelyBan = {false};

    // Содержит текстовую информацию о причине активации триггера, используется
    // для объяснения причины удаления телеграм-сообщения
    static thread_local QString activationReasonMessage;

    // Проверяет сообщение на соответствие критериям фильтрации.
    // Параметр Text содержит текстовое сообщение с удаленными линками
    virtual bool isActive(const tbot::Update&, GroupChat*, const Text&) const = 0;

    struct Find
    {
        int operator() (const QString* name, const Trigger* item2) const
            {return name->compare(item2->name);}
    };
    typedef lst::List<Trigger, Find, clife_alloc_ref<Trigger>> List;

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
    //   filemime - наименование и mimetype вложенного документа;
    //   urllinks - список URL ссылок доступных в сообщении.
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

struct TriggerBlackUser : public Trigger
{
    typedef clife_ptr<TriggerBlackUser> Ptr;

    TriggerBlackUser() = default;
    DISABLE_DEFAULT_COPY(TriggerBlackUser)

    struct Group
    {
        QString description;  // Причина почему пользователь оказался в черном списке
        QSet<qint64> userIds; // Список идентификаторов пользователей
        QSet<qint64> chatIds; // Список идентификаторов каналов/чатов

        typedef QList<Group> List;
    };

    // Список групп запрещенных пользователей/каналов/чатов
    Group::List groups;

    bool isActive(const tbot::Update&, GroupChat*, const Text&) const override;

    void assign(const TriggerBlackUser&);
};

struct TriggerEmptyText : public Trigger
{
    typedef clife_ptr<TriggerEmptyText> Ptr;

    TriggerEmptyText() = default;
    DISABLE_DEFAULT_COPY(TriggerEmptyText)

    struct UserLimit
    {
        // Запретить новому пользователю публиковать сообщения без текста на за-
        // данное количество часов. Ограничение не накладывается  если  значение
        // параметра меньше или равно 0
        int time = {-1};

        // Запретить пользователю публиковать сообщения  без  текста  если  его
        // идентификатор  больше  или  равен  значению  thresh_id.  Ограничение
        // не накладывается если значение параметра меньше или равно 0
        qint64 threshId = {-1};

        // Запретить пользователю с преимум-аккаунтом публиковать сообщения без
        // текста. Значение параметра по умолчанию равно FALSE
        bool premium = {false};
    };
    UserLimit userLimit;

    bool isActive(const tbot::Update&, GroupChat*, const Text&) const override;

    void assign(const TriggerEmptyText&);
};

struct TriggerBigId : public Trigger
{
    typedef clife_ptr<TriggerBigId> Ptr;

    TriggerBigId() = default;
    DISABLE_DEFAULT_COPY(TriggerBigId)

    struct UserLimit
    {
        // Запретить новому пользователю публиковать сообщения без текста на за-
        // данное количество часов. Ограничение не накладывается  если  значение
        // параметра меньше или равно 0
        int time = {-1};

        // Запретить пользователю публиковать сообщения  без  текста  если  его
        // идентификатор  больше  или  равен  значению  thresh_id.  Ограничение
        // не накладывается если значение параметра меньше или равно 0
        qint64 threshId = {-1};

        // // Запретить пользователю с преимум-аккаунтом публиковать сообщения без
        // // текста. Значение параметра по умолчанию равно FALSE
        // bool premium = {false};
    };
    UserLimit userLimit;

    bool isActive(const tbot::Update&, GroupChat*, const Text&) const override;

    void assign(const TriggerBigId&);
};

const char* yamlTypeName(YAML::NodeType::value type);

bool loadTriggers(Trigger::List&, const YamlConfig&);
void printTriggers(Trigger::List&);

Trigger::List triggers(Trigger::List* = nullptr);

// Проверяет нахождение времени time в диапазоне [begin, end]
bool timeInRange(const QTime& begin, const QTime& time, const QTime& end);

} // namespace tbot
