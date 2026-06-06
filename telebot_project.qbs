import qbs
import "telebot_base.qbs" as TelebotBase

TelebotBase {
    name: "TeleBot (Project)"

    readonly property bool slaveMode: false

    references: [
        "src/commands/commands.qbs",
        "src/telebot/telebot.qbs",
        "src/pproto/pproto.qbs",
        "src/rapidfuzz/rapidfuzz.qbs",
        "src/rapidjson/rapidjson.qbs",
        "src/shared/shared.qbs",
        "src/yaml/yaml.qbs",
        //"setup/package_build.qbs",
    ]
}
