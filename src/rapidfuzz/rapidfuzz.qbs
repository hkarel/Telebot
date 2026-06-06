import qbs
import qbs.FileInfo

Product {
    name: "RapidFuzz"
    targetName: "rapidfuzz"

    type: "staticlibrary"
    Depends { name: "cpp" }

    cpp.cxxFlags: [
        "-ggdb3",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
    ]
    cpp.includePaths: ["rapidfuzz"]
    cpp.cxxLanguageVersion: "c++17"

    files: [
        "rapidfuzz/rapidfuzz/details/*.hpp",
        "rapidfuzz/rapidfuzz/distance/*.hpp",
        "rapidfuzz/rapidfuzz/*.hpp",
    ]

    Export {
        Depends { name: "cpp" }
        cpp.includePaths: [
            FileInfo.joinPaths(exportingProduct.sourceDirectory, "rapidfuzz")
        ]
    }
}
