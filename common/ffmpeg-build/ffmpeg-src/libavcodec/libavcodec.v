LIBAVCODEC_$MAJOR {
        global: av*;
                #deprecated, remove after next bump
                audio_resample;
                audio_em_resample_close;
        local:  *;
};
