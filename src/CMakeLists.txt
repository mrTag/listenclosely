# SPDX-License-Identifier: Unlicense

target_sources( ${PROJECT_NAME}
    PRIVATE
        AudioStreamVoip.h
        AudioStreamVoip.cpp
        VoIPConnection.h
        VoIPConnection.cpp
        RegisterExtension.cpp
)

target_include_directories( ${PROJECT_NAME}
    PRIVATE
        "src"
)
