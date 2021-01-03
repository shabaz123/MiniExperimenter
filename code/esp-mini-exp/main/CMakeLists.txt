
#target_compile_definitions(${COMPONENT_TARGET} PUBLIC "-DUSE_LWIP_SOCKET_FOR_AZURE_IOT -DHSM_TYPE_SYMM_KEY -DUSE_PROV_MODULE")
idf_component_register(SRCS "maincode.c"
                            "azure-iot-central.c"
                            "commands.c"
                            "timerfunc.c"
                            "miniexp.cpp"
                            "iotc/iotc.cpp"
                            "iotc/parson.c"
                    INCLUDE_DIRS ".")