# Runs before any component's CMakeLists.txt, so esp_ipa finds the file when it reads the property.
idf_build_set_property(ESP_IPA_JSON_CONFIG_FILE_PATH "${CMAKE_CURRENT_LIST_DIR}/ov02c10.json" APPEND)
