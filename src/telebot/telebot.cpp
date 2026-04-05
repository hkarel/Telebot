#include "telebot_appl.h"

#include "commands/tele_data.h"
#include "trigger.h"

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

#include "pproto/commands/base.h"
#include "pproto/commands/pool.h"

#include <QtCore>
#include <QUrl>
#include <QNetworkProxy>

#ifdef MINGW
#include <windows.h>
#else
#include <csignal>
#endif
#include <unistd.h>
#include <fstream>
#include <list>

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

struct TestData
{
    int field1 = 10;
    int field2 = 15;
};

int main(int argc, char *argv[])
{
    // Устанавливаем в качестве разделителя целой и дробной части символ '.',
    // если этого не сделать - функции преобразования строк в числа (std::atof)
    // буду неправильно работать.
    qputenv("LC_NUMERIC", "C");

    std::list<TestData> dataList;

//    YamlConfig yconfig;
//    for (int i = 0; i < 1500; ++i)
//    {
//        dataList.append(TestData{});

//        YamlConfig::Func saveFunc = [&dataList](YamlConfig* conf, YAML::Node& node, bool)
//        {
//            YAML::Node node1;
//            for (const TestData& td  : dataList)
//            {
//                YAML::Node n;
//                conf->setValue(n, "field1", td.field1);
//                conf->setValue(n, "field2", td.field2);
//                node1.push_back(n);
//            }
//            node = node1;
//            return true;
//        };

//        //yconfig.remove("test.items");
//        yconfig.setValue("test.items", saveFunc);
//    }

//    yconfig.setNodeStyle("test.items", YAML::EmitterStyle::Flow);
//    yconfig.saveFile("/tmp/mem-leak-test.yaml");

//----------------------------------------------------------------

//    std::ofstream file {"/tmp/mem-leak-test-2.yaml", ios_base::out};
//    if (!file.is_open())
//        return 1;

//    YAML::Node rootNode;

//    YAML::Node testNode = rootNode["test"];

//    YAML::Node itemsNode;
//    for (int i = 0; i < 1500; ++i)
//    {
//        dataList.push_back(TestData{});
//        itemsNode = YAML::Node();

//        for (const TestData& td  : dataList)
//        {
//            YAML::Node n;
//            n["field1"] = td.field1;
//            n["field2"] = td.field2;
//            itemsNode.push_back(n);
//        }
//        testNode.remove("items");
//        testNode["items"] = itemsNode;
//    }
//    //testNode["items"] = itemsNode;
//    //testNode["items"].SetStyle(YAML::EmitterStyle::Flow);

//    file << rootNode;
//    file.close();

    YAML::Node a;
    for (int i = 0; i < 100000; ++i)
    {
        a = YAML::Load("{1B: Prince Fielder, 2B: Rickie Weeks, LF: Ryan Braun}");
    }

    return 0;

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

#ifdef SLAVE_MODE
        configFile += ".slave";
#endif
        if (!QFile::exists(configFile))
        {
            log_error << "Config file " << configFile << " not exists";
            alog::stop();
            return 1;
        }

////----------------------------
//        QFile fl {configFile};
//        fl.open(QIODevice::ReadOnly);
//        QByteArray bb = fl.readAll();
//        fl.close();
//        string sss = bb.toStdString();
//        if (!config::base().readString(sss, true))
//        {
//            alog::stop();
//            return 1;
//        }
////---------------------------

        config::base().setReadOnly(true);
        config::base().setSaveDisabled(true);
        if (!config::base().readFile(configFile.toStdString(), true))
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

        if (!pproto::command::checkUnique())
        {
            stopProgram();
            return 1;
        }

        if (!pproto::error::checkUnique())
        {
            stopProgram();
            return 1;
        }

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

        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

        if (!appl.init())
        {
            stopProgram();
            return 1;
        }

        config::observerBase().start();
        config::observer().start();

        ret = appl.exec();
        appl.deinit();

        config::observerBase().stop();
        config::observer().stop();

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
