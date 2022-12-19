import qbs
import "qbs/imports/QbsUtl/qbsutl.js" as QbsUtl

Project {
    minimumQbsVersion: "1.23.0"
    qbsSearchPaths: ["qbs"]

    readonly property var projectVersion: projectProbe.projectVersion
    readonly property string projectGitRevision: projectProbe.projectGitRevision

    Probe {
        id: projectProbe
        property var projectVersion;
        property string projectGitRevision;

        readonly property string projectBuildDirectory:  project.buildDirectory
        readonly property string projectSourceDirectory: project.sourceDirectory

        configure: {
            projectVersion = QbsUtl.getVersions(projectSourceDirectory + "/VERSION");
            projectGitRevision = QbsUtl.gitRevision(projectSourceDirectory);
        }
    }

    property var cppDefines: {
        var def = [
            "VERSION_PROJECT=\"" + projectVersion[0] + "\"",
            "VERSION_PROJECT_MAJOR=" + projectVersion[1],
            "VERSION_PROJECT_MINOR=" + projectVersion[2],
            "VERSION_PROJECT_PATCH=" + projectVersion[3],
            "GIT_REVISION=\"" + projectGitRevision + "\"",
            "QDATASTREAM_VERSION=QDataStream::Qt_5_12",
            "PPROTO_VERSION_LOW=0",
            "PPROTO_VERSION_HIGH=0",
            "PPROTO_JSON_SERIALIZE",
            "CONFIG_DIR=\"/etc/telebot\"",
            "VAROPT_DIR=\"/var/opt/telebot\"",
        ];

        return def;
    }

    property var cxxFlags: [
        "-ggdb3",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
        "-Wno-variadic-macros",
        "-Wno-register",
    ]
    property string cxxLanguageVersion: "c++17"
}
