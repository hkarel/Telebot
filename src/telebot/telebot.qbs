import qbs
import QbsUtl
import ProbExt

Product {
    name: "TeleBot"
    targetName: "telebot"
    condition: true

    type: "application"
    destinationDirectory: "./bin"

    Depends { name: "cpp" }
    Depends { name: "PProto" }
    Depends { name: "RapidJson" }
    Depends { name: "SharedLib" }
    Depends { name: "Yaml" }
    Depends { name: "Qt"; submodules: ["core", "network", "sql"] }

    cpp.defines: project.cppDefines
    cpp.cxxFlags: project.cxxFlags
    cpp.cxxLanguageVersion: project.cxxLanguageVersion

    cpp.includePaths: [
        "./",
        "../",
    ]

    cpp.systemIncludePaths: QbsUtl.concatPaths(
        Qt.core.cpp.includePaths // Декларация для подавления Qt warning-ов
    )

//    cpp.rpaths: QbsUtl.concatPaths(
//        "$ORIGIN/../lib"
//    )

//    cpp.libraryPaths: QbsUtl.concatPaths(
//        project.buildDirectory + "/lib"
//    )

    cpp.dynamicLibraries: QbsUtl.concatPaths(
        "pthread"
    )

    files: [
        "group_chat.cpp",
        "group_chat.h",
        "processing.cpp",
        "processing.h",
        "tele_data.h",
        "telebot.cpp",
        "telebot_appl.cpp",
        "telebot_appl.h",
        "trigger.cpp",
        "trigger.h",
    ]

} // Product


// Создание сертификата (https://core.telegram.org/bots/self-signed)
//   openssl req -newkey rsa:2048 -sha256 -nodes -keyout private.key -x509 -days 365 -out cert.pem -subj "/C=RU/CN=hkarel.noip.me"

// Просмотр сертификата
//   openssl x509 -text -noout -in cert.pem

// Регистрация WebHook
//   curl -F"url=https://hkarel.noip.me:8443/" -F"certificate=@cert.pem" https://api.telegram.org/botTOKENID/setWebhook
//   curl -F"url=https://185.2.184.60:8443/"   -F"certificate=@cert.pem" https://api.telegram.org/botTOKENID/setWebhook
