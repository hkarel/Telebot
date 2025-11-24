import qbs
import qbs.FileInfo

Product {
    name: "Commands"
    targetName: "commands"

    type: "staticlibrary"

    Depends { name: "cpp" }
    Depends { name: "PProto" }
    Depends { name: "RapidJson" }
    Depends { name: "SharedLib" }
    Depends { name: "Yaml" }
    Depends { name: "Qt"; submodules: ["core", "network"] }

    cpp.defines: project.cppDefines
    cpp.cxxFlags: project.cxxFlags
    cpp.cxxLanguageVersion: project.cxxLanguageVersion

    cpp.includePaths: [".."]

    files: [
        "commands.cpp",
        "commands.h",
        "compare.h",
        "error.h",
        "tele_data.h",
    ]
    Export {
        Depends { name: "cpp" }
        cpp.includePaths: [
            FileInfo.joinPaths(exportingProduct.sourceDirectory, "..")
        ]
    }
}
