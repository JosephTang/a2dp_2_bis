idf_component_register(SRCS "main.c" "pool_reg.c" "a2dp_bis_bridge.c" "cmd_reg.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt nvs_flash console)
