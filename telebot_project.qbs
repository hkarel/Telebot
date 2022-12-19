import qbs
import "telebot_base.qbs" as TelebotBase

TelebotBase {
    name: "TeleBot (Project)"

    references: [
        "src/telebot/telebot.qbs",
        //"src/demo/serialize/serialize.qbs",
        //"src/demo/transport/transport.qbs",
        //"src/demo/web/web_clients.qbs",
        //"src/demo/web/web_servers.qbs",
        "src/pproto/pproto.qbs",
        "src/rapidjson/rapidjson.qbs",
        "src/shared/shared.qbs",
        "src/yaml/yaml.qbs",
    ]
}
