import qbs
import "telebot_base.qbs" as TelebotBase

TelebotBase {
    name: "TeleBot (Slave)"

    readonly property bool slaveMode: true

    references: [
        "src/commands/commands.qbs",
        "src/telebot/telebot.qbs",
        "src/pproto/pproto.qbs",
        "src/rapidjson/rapidjson.qbs",
        "src/shared/shared.qbs",
        "src/yaml/yaml.qbs",
        //"setup/package_build.qbs",
    ]
}
