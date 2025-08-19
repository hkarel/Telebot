#pragma once

#include "commands/tele_data.h"

#include "shared/list.h"
#include "shared/defmac.h"
#include "shared/container_ptr.h"
#include "shared/steady_timer.h"
#include "shared/qt/qthreadex.h"

#include <QtCore>
#include <atomic>

namespace tbot {

/**
  Вспомогательная структура для обработки BIO сообщений
*/
struct Bio
{
    qint64  userId = {0};
    qint64  chatId = {0};
    qint32  updateId = {0};
    qint32  messageId = {0}; // Идентификатор оригинального сообщения
    QString mediaGroupId;    // Идентификатор медиагруппы оригинального сообщения
    QString messageOrigin;   // Текст оригинального сообщения
};

/**
  Параметры для функции sendTgCommand()
*/
struct TgParams
{
    typedef container_ptr<TgParams> Ptr;

    // Наименование вызываемой TG-функции
    QString funcName;

    // Список параметров для Телеграм API функций
    QMap<QString, QVariant> api;

    // Задержка отправки команды, задается в миллисекундах
    int delay = {0};

    // Номер попытки выполняемого запроса
    int attempt = {1};

    // Определяет тайм-аут для удаления сервисных сообщений бота, задается
    // в секундах. Если значение тайм-аута равно 0 сообщение будет удалено
    // немедленно, таким образом оно останется только в истории сообщений.
    // При значении параметра меньше 0 сообщение не удаляется
    int messageDel = {0};

    // Вспомогательное поле для обработки BIO сообщений
    Bio bio;

    // Пользователь только что вступил в группу
    bool isNewUser = {false};

    // Команда для режима Anti-Raid
    bool isAntiRaid = {false};

    // Признак бот-команды
    bool isBotCommand = {false};

    // Идентификатор спам-сообщения, используется как признак информационного
    // сообщения о спаме
    qint32 spamMessageId = {0};
};

inline TgParams::Ptr tgfunction(const char* funcName)
{
    TgParams::Ptr p = TgParams::Ptr::create_join();
    p->funcName = funcName;
    return p;
}

inline TgParams::Ptr tgfunction(const QString& funcName)
{
    TgParams::Ptr p = TgParams::Ptr::create_join();
    p->funcName = funcName;
    return p;
}

struct MessageData
{
    typedef container_ptr<MessageData> Ptr;

    // Вспомогательное поле для обработки BIO сообщений
    Bio bio;

    // Пользователь только что вступил в группу
    bool isNewUser = {false};

    // Администратор группы отметивший сообщение как спам
    User::Ptr adminMarkSpam;

    // Десериализованное Телеграм-сообщение
    Update update;
};

class Processing : public QThreadEx
{
public:
    typedef lst::List<Processing, lst::CompareItemDummy> List;

    Processing();

    bool init(qint64 botUserId);
    static void addUpdate(const MessageData::Ptr&);
    static void addVerifyAdmin(qint64 chatId, qint64 userId, qint32 messageId);

signals:
    void sendTgCommand(const tbot::TgParams::Ptr&);

    // Обработка штрафов за спам-сообщения
    void reportSpam(qint64 chatId, const tbot::User::Ptr&);

    // Аннулирование штрафов за спам-сообщения
    // void resetSpam(qint64 chatId, qint64 userId);

    // Обновление информации о группе и её администраторах
    void reloadGroup(qint64 chatId, bool);

    // Учет пользователей и сообщений в системе Anti-Raid
    void antiRaidUser(qint64 chatId, const tbot::User::Ptr&);
    void antiRaidMessage(qint64 chatId, qint64 userId, qint32 messageId);

    void restrictNewUser(qint64 chatId, qint64 userId, qint32 newUserMute);
    void adjacentMessageDel(qint64 chatId, qint32 messageId);

public slots:
    void reloadConfig();

private:
    Q_OBJECT
    DISABLE_DEFAULT_COPY(Processing)

    void run() override;

private:
    static QMutex _threadLock;
    static QWaitCondition _threadCond;
    static QList<MessageData::Ptr> _updates;

    typedef QPair<qint64 /*chat_id*/, qint64 /*user_id*/> TemporaryKey;
    static QMap<TemporaryKey, steady_timer> _temporaryNewUsers;

    volatile bool _configChanged = {true};

    struct MediaGroup
    {
        // Одно из сообщений медиа-группы плохое
        bool isBad = {false};

        // Идентификатор чата для сообщений медиа-группы
        qint64 chatId = {0};
        QMap<qint64 /*message_id*/, bool /*empty text trait*/> messageIds;

        // Таймер для очистки _mediaGroups
        steady_timer timer;

    };
    static QMap<QString /*media_group_id*/, MediaGroup> _mediaGroups;

    struct VerifyAdmin
    {
        qint64 chatId = {0};
        qint64 userId = {0};

        // Идентификатор сообщения для обработки
        qint32 messageId = {0};

        // Идентификаторы сообщений для пропуска обработки
        QSet<qint32> messageIds;

        // Таймер для очистки списка VerifyAdmin
        steady_timer timer;

        struct Compare
        {
            int operator() (const VerifyAdmin* item1, const VerifyAdmin* item2) const
            {
                LIST_COMPARE_MULTI_ITEM(item1->chatId,    item2->chatId)
                LIST_COMPARE_MULTI_ITEM(item1->userId,    item2->userId)
                return 0;
            }
            int operator() (const QPair<qint64 /*chat id*/, qint64 /*user id*/>* pair,
                            const VerifyAdmin* item2) const
            {
                LIST_COMPARE_MULTI_ITEM(pair->first  /*chat id*/, item2->chatId)
                LIST_COMPARE_MULTI_ITEM(pair->second /*user id*/, item2->userId);
                return 0;
            }
        };
        typedef lst::List<VerifyAdmin, Compare> List;
    };
    static VerifyAdmin::List _verifyAdmins;

    qint64 _botUserId = {0};
};

} // namespace tbot
