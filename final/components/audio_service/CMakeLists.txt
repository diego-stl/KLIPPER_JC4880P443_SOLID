idf_component_register(SRCS "audio_service.c" "audio_assets.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES board_jc4880 esp_codec_dev esp_driver_i2s esp_driver_gpio
                  freertos esp_timer nvs_flash log
    EMBED_FILES "assets/wpn_portalgun_activation_01.wav" "assets/laser_node_power_off.wav"
                "assets/hackbuzzer.wav" "assets/turret_disabled_2.wav"
                "assets/aperture_logo_bells_01_01.wav" "assets/wpn_portal_fizzler_shimmy_01.wav"
                "assets/portal_enter_01.wav" "assets/portal_fizzle2.wav"
                "assets/interior_arm_platform_close_01.wav" "assets/retract.wav"
                "assets/button_synth_positive_01.wav" "assets/button_synth_negative_02.wav")
