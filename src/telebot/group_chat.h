#pragma once

#include "commands/compare.h"
#include "trigger.h"
#include <atomic>

namespace tbot {

using namespace std;

struct GroupChat : public clife_base
{
public:
    typedef clife_ptr<GroupChat> Ptr;

    GroupChat() = default;
    virtual ~GroupChat() = default;

    // Идентификатор группового чата
    qint64 id = {0};

    // Информационная подпись для чата, используется при выводе в лог
    QString name() const;
    void setName(const QString&);

    // Список триггеров
    Trigger::List triggers;

    // Действия триггеров не распространяются на администраторов группы если
    // параметр установлен в TRUE
    bool skipAdmins = {true};

    // Если триггер однозначно идентифицирует спам-сообщение, и пользователь
    // имеет премиум аккаунт, то пользователя можно заблокировать сразу, при
    // условии что у триггера аналогичное свойство будет равно TRUE
    bool premiumBan = {false};

    // Проверять у членов группы BIO пользователя на спам
    bool checkBio = {false};

    struct WhiteUser
    {
        qint64  userId = {0};
        QString info;
        typedef lst::List<WhiteUser, CompareUser<WhiteUser>> List;
    };

    // Список идентификаторов пользователей, на которых не распространяется
    // действие триггеров
    WhiteUser::List whiteUsers;

    // Количество спам-сообщений по достижении которого пользователь блокируется.
    // Если параметр равен 0 пользователь блокироваться не будет
    qint32 userSpamLimit = {5};

    // Запретить новому пользователю писать сообщения на заданное количество
    // минут. Ограничение не накладывается если значение параметра меньше или
    // равно -1
    qint32 newUserMute = {-1};

    // Интерпретировать GroupAnonymousBot как администратора
    bool anonymousAsAdmin = {true};

    struct JoinViaChatFolder
    {
        // Запрещает присоединение к группе
        bool restrict_ = {false};

        //Разрешает присоединение к группе, но запрещает публиковать сообщения
        bool mute = {false};

        // Отправляет отчет о спаме в систему учета штрафов при попытке опубли-
        // ковать сообщение
        bool reportSpam = {true};
    };
    JoinViaChatFolder joinViaChatFolder;

    // Ограничение пользователя за публикацию спам-сообщений
    QVector<qint32> userRestricts;

    // Механизм противодействия Anti-Raid атаке
    struct AntiRaid
    {
        // Признак активации Anti-Raid механизма
        bool active = {false};

        // Временной фрейм задает интервал в течении которого происходит учет
        // вступивших в группу новых участников. Параметр задается в секундах
        int timeFrame = {60};

        // Предельное количество  новых  участников,  которое  может  вступить
        // в группу за время time_frame.  При превышении  указанного  значения
        // активируется anti-raid режим
        int usersLimit = {50};

        // Временной интервал в течении которого anti-raid  механизм  находится
        // в активной фазе. Все новые участники вступившие в группу в это время
        // будут заблокированы. Параметр задается в минутах
        int duration = {60};
    };
    AntiRaid antiRaid;

    // Признак включенного режима Anti-Raid
    atomic_bool antiRaidTurnOn = {false};

    // Список идентификаторов администраторов группы
    QSet<qint64> adminIds() const;
    void setAdminIds(const QSet<qint64>&);

    // Список идентификаторов владельцев группы
    QSet<qint64> ownerIds() const;
    void setOwnerIds(const QSet<qint64>&);

    // Список username администраторов группы
    QStringList adminNames() const;
    void setAdminNames(const QStringList&);

    // Информация о правах Tele-бота в группе
    ChatMemberAdministrator::Ptr botInfo() const;
    void setBotInfo(const ChatMemberAdministrator::Ptr&);

    typedef lst::List<GroupChat, CompareId<GroupChat>, clife_alloc_ref<GroupChat>> List;

private:
    DISABLE_DEFAULT_COPY(GroupChat)

    QString _name;
    QSet<qint64> _adminIds;
    QSet<qint64> _ownerIds;
    QStringList  _adminNames;
    ChatMemberAdministrator::Ptr _botInfo;

    mutable QMutex _lock {QMutex::Recursive};
};

bool loadGroupChats(GroupChat::List&, const YamlConfig&);
void printGroupChats(GroupChat::List&);

GroupChat::List groupChats(GroupChat::List* = nullptr);

QSet<qint64> timelimitInactiveChats();
void setTimelimitInactiveChats(const QSet<qint64>& chats);

void timelimitInactiveChatsAdd(qint64 chatId);
void timelimitInactiveChatsRemove(qint64 chatId);

} // namespace tbot
