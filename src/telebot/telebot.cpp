#include "telebot_appl.h"

#include "trigger.h"
#include "tele_data.h"

#include "shared/defmac.h"
#include "shared/utils.h"
#include "shared/type_name.h"
#include "shared/logger/logger.h"
#include "shared/logger/format.h"
#include "shared/logger/config.h"
#include "shared/config/appl_conf.h"
#include "shared/config/logger_conf.h"
#include "shared/qt/logger_operators.h"
#include "shared/qt/version_number.h"

#include <QtCore>
#include <QUrl>
#include <QNetworkProxy>

#ifdef MINGW
#include <windows.h>
#else
#include <csignal>
#endif
#include <unistd.h>
#include <limits>
#include <locale>
#include <codecvt>
#include <fstream>

#define APPLICATION_NAME "TeleBot"

using namespace std;

/**
  Используется для уведомления основного потока о завершении работы программы.
*/
void stopProgramHandler(int sig)
{
    if ((sig == SIGTERM) || (sig == SIGINT))
    {
        const char* sigName = (sig == SIGTERM) ? "SIGTERM" : "SIGINT";
        log_verbose << "Signal " << sigName << " is received. Program will be stopped";
        Application::stop();
    }
    else
        log_verbose << "Signal " << sig << " is received";
}

void stopProgram()
{
/*
    #define STOP_THREAD(THREAD_FUNC, NAME, TIMEOUT) \
        if (!THREAD_FUNC.stop(TIMEOUT * 1000)) { \
            log_info << "Thread '" NAME "': Timeout expired, thread will be terminated"; \
            THREAD_FUNC.terminate(); \
        }

    STOP_THREAD(udp::socket(), "TransportUDP"  , 15)
    #undef STOP_THREAD
*/
    log_info << log_format("'%?' service is stopped", APPLICATION_NAME);
    alog::stop();
}

void helpInfo()
{
    alog::logger().clearSavers();
    alog::logger().addSaverStdOut(alog::Level::Info, true);

    log_info << log_format(
        "'%?' service (version: %?; gitrev: %?)"
        ". Use and distribute under the terms GNU General Public License Version 3",
        APPLICATION_NAME, productVersion().toString(), GIT_REVISION);
    log_info << "Usage: telebot";
    log_info << "  -h this help";
    alog::logger().flush();
}

int main(int argc, char *argv[])
{
    // Устанавливаем в качестве разделителя целой и дробной части символ '.',
    // если этого не сделать - функции преобразования строк в числа (std::atof)
    // буду неправильно работать.
    qputenv("LC_NUMERIC", "C");

    int ret = 0;
    try
    {
        alog::logger().start();

#ifdef NDEBUG
        alog::logger().addSaverStdOut(alog::Level::Info, true);
#else
        alog::logger().addSaverStdOut(alog::Level::Debug2);
#endif
        signal(SIGTERM, &stopProgramHandler);
        signal(SIGINT,  &stopProgramHandler);

        int c;
        while ((c = getopt(argc, argv, "h")) != EOF)
        {
            switch (c)
            {
                case 'h':
                    helpInfo();
                    alog::stop();
                    exit(0);
                case '?':
                    log_error << "Invalid option";
                    alog::stop();
                    return 1;
            }
        }

        // Путь к основному конфиг-файлу
        QString configFile = config::qdir() + "/telebot.conf";
        if (!QFile::exists(configFile))
        {
            log_error << "Config file " << configFile << " not exists";
            alog::stop();
            return 1;
        }

        config::base().setReadOnly(true);
        config::base().setSaveDisabled(true);
        if (!config::base().readFile(configFile.toStdString()))
        {
            alog::stop();
            return 1;
        }

        // Путь к конфиг-файлу текущих настроек
        QString configFileS;
        config::base().getValue("state.file", configFileS);

        config::dirExpansion(configFileS);
        config::state().readFile(configFileS.toStdString());

        // Создаем дефолтный сэйвер для логгера
        if (!alog::configDefaultSaver())
        {
            alog::stop();
            return 1;
        }

        log_info << log_format(
            "'%?' service is running (version: %?; gitrev: %?)"
            ". Use and distribute under the terms GNU General Public License Version 3",
            APPLICATION_NAME, productVersion().toString(), GIT_REVISION);
        alog::logger().flush();

        alog::logger().removeSaverStdOut();
        alog::logger().removeSaverStdErr();

        // Создаем дополнительные сэйверы для логгера
        alog::configExtendedSavers();
        alog::printSaversInfo();

        Application appl {argc, argv};

        // Устанавливаем текущую директорию. Эта конструкция работает только
        // когда создан экземпляр QCoreApplication.
        if (QDir::setCurrent(QCoreApplication::applicationDirPath()))
        {
            log_debug << "Set work directory: " << QCoreApplication::applicationDirPath();
        }
        else
        {
            log_error << "Failed set work directory";
            stopProgram();
            return 1;
        }

        //QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

        if (!appl.init())
        {
            stopProgram();
            return 1;
        }

        config::changeChecker().start();

        alog::logger().removeSaverStdOut();
        alog::logger().removeSaverStdErr();

        ret = appl.exec();
        appl.deinit();

        config::changeChecker().stop();

        if (config::state().changed())
            config::state().saveFile();

        stopProgram();
        return ret;
    }
    catch (std::exception& e)
    {
        log_error << "Failed initialization. Detail: " << e.what();
        ret = 1;
    }
    catch (...)
    {
        log_error << "Failed initialization. Unknown error";
        ret = 1;
    }

    stopProgram();
    return ret;
}
