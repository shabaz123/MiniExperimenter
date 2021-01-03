# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_definitions(USE_LWIP_SOCKET_FOR_AZURE_IOT HSM_TYPE_SYMM_KEY USE_PROV_MODULE)
project(mini_explorer)