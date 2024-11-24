#pragma once

#include "commands/commands.h"

namespace tbot {

using namespace std;
using namespace pproto;

/**
  Группа функций по работе с датой вступления пользователей в группу
*/
data::UserJoinTime::List userJoinTimes();
void setUserJoinTimes(data::UserJoinTime::List&);

void userJoinTimesAdd(qint64 chatId, qint64 userId);
void userJoinTimesRemoveByTime();

data::UserJoinTime::Ptr userJoinTimesFind(qint64 chatId, qint64 userId);

bool userJoinTimesChanged();
void userJoinTimesResetChangeFlag();


} //namespace tbot
