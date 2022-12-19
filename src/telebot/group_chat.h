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
    QString name;

    // Список триггеров
    Trigger::List triggers;

    // Действия триггеров не распространяются на администраторов группы если
    // параметр установлен в TRUE
    bool skipAdmins = {true};

    // Список идентификаторов пользователей, на которых не распространяется
    // действие триггеров
    QSet<qint64> whiteUsers;

    // Список идентификаторов администраторов группы
    QSet<qint64> adminIds() const;
    void setAdminIds(const QSet<qint64>&);

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

    QSet<qint64> _adminIds;
    mutable QMutex _adminIdsLock;
};

bool loadGroupChats(GroupChat::List&);
void printGroupChats(GroupChat::List&);

GroupChat::List groupChats(GroupChat::List* = nullptr);

} // namespace tbot
