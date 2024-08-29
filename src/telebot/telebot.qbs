import qbs
import QbsUtl
import ProbExt

Product {
    name: "TeleBot"
    condition: true

    targetName: "telebot";
    Properties {
         condition: project.slaveMode
         targetName: "telebot-slave"
    }

    type: "application"
    destinationDirectory: "./bin"

    Depends { name: "cpp" }
    Depends { name: "lib.sodium" }
    Depends { name: "Commands" }
    Depends { name: "PProto" }
    Depends { name: "RapidJson" }
    Depends { name: "SharedLib" }
    Depends { name: "Yaml" }
    Depends { name: "Qt"; submodules: ["core", "network"] }

    lib.sodium.enabled: project.useSodium
    lib.sodium.version: project.sodiumVersion

    cpp.defines: project.cppDefines
    cpp.cxxFlags: project.cxxFlags
    cpp.cxxLanguageVersion: project.cxxLanguageVersion

    cpp.includePaths: [
        "./",
        "../",
    ]

    cpp.systemIncludePaths: QbsUtl.concatPaths(
        Qt.core.cpp.includePaths // Декларация для подавления Qt warning-ов
       ,lib.sodium.includePath
    )

    cpp.dynamicLibraries: QbsUtl.concatPaths(
        "pthread"
    )

    cpp.staticLibraries: {
        return lib.sodium.staticLibrariesPaths(product);
    }

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
}
