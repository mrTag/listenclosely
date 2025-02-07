# SPDX-License-Identifier: Unlicense

target_sources( ${PROJECT_NAME}
    PRIVATE
        AudioStreamPlayerVoipExtension.h
        AudioStreamPlayerVoipExtension.cpp
        RegisterExtension.cpp
        DebugInfoWindow.cpp
        DebugInfoWindow.h
)

target_include_directories( ${PROJECT_NAME}
    PRIVATE
        "src"
)
