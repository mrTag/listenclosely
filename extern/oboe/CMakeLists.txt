add_library(oboe-resampler)

target_compile_features( oboe-resampler
        PRIVATE
        cxx_std_17
)

target_sources( oboe-resampler
        PUBLIC
        "resampler/IntegerRatio.cpp"
        "resampler/LinearResampler.cpp"
        "resampler/MultiChannelResampler.cpp"
        "resampler/PolyphaseResampler.cpp"
        "resampler/PolyphaseResamplerMono.cpp"
        "resampler/PolyphaseResamplerStereo.cpp"
        "resampler/SincResampler.cpp"
        "resampler/SincResamplerStereo.cpp"
)

target_include_directories( oboe-resampler
        PUBLIC
        "./"
)