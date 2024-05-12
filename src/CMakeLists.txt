# SPDX-License-Identifier: Unlicense

target_sources( ${PROJECT_NAME}
    PRIVATE
        Example.h
        Example.cpp
        AudioStreamPlayerVoipExtension.h
        AudioStreamPlayerVoipExtension.cpp
        GDExtensionTemplate.h
        GDExtensionTemplate.cpp
        RegisterExtension.cpp
        DebugInfoWindow.cpp
        DebugInfoWindow.h
)

target_include_directories( ${PROJECT_NAME}
    PRIVATE
        "src"
)
