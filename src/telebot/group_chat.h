#pragma once

#include "trigger.h"

namespace tbot {

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

    // Список идентификаторов пользователей, на которых не распространяется
    // действие триггеров
    QSet<qint64> whiteUsers;

    // Количество спам-сообщений по достижении которого пользователь блокируется.
    // Если параметр равен 0 пользователь блокироваться не будет
    qint32 userSpamLimit = {5};

    // Ограничение пользователя за публикацию спам-сообщений
    QVector<qint32> userRestricts;

    // Список идентификаторов администраторов группы
    QSet<qint64> adminIds() const;
    void setAdminIds(const QSet<qint64>&);

    // Список владельцев группы
    QSet<qint64> ownerIds() const;
    void setOwnerIds(const QSet<qint64>&);

    // Информация о правах Tele-бота в группе
    ChatMemberAdministrator::Ptr botInfo() const;
    void setBotInfo(const ChatMemberAdministrator::Ptr&);

    struct Compare
    {
        int operator() (const GroupChat* item1, const GroupChat* item2) const
            {return LIST_COMPARE_ITEM(item1->id, item2->id);}

        int operator() (const qint64* id, const GroupChat* item2) const
            {return LIST_COMPARE_ITEM(*id, item2->id);}
    };
    typedef lst::List<GroupChat, Compare, clife_alloc<GroupChat>> List;

private:
    DISABLE_DEFAULT_COPY(GroupChat)

    QString _name;
    QSet<qint64> _adminIds;
    QSet<qint64> _ownerIds;
    ChatMemberAdministrator::Ptr _botInfo;

    mutable QMutex _lock {QMutex::Recursive};
};

bool loadGroupChats(GroupChat::List&);
void printGroupChats(GroupChat::List&);

GroupChat::List groupChats(GroupChat::List* = nullptr);

QSet<qint64> timelimitInactiveChats();
void setTimelimitInactiveChats(const QList<qint64>& chats);

void timelimitInactiveChatsAdd(qint64 chatId);
void timelimitInactiveChatsRemove(qint64 chatId);

} // namespace tbot
