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
    Depends { name: "Qt"; submodules: ["core", "network"] }

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
}
