
idf_component_register(SRCS "spotify_client.c"
    "src/handler_callbacks.c"
    "src/parse_objects.c"
    "src/spotify_utils.c"
    "lib/jsmn.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src/include" "lib/include"
    REQUIRES esp_http_client
    EMBED_TXTFILES certs.pem)
