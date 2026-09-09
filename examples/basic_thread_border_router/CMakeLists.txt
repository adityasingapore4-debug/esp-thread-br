# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS
    "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/node_identity"
    "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/utils"
    "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/addressing"
    "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/transport"
    "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/coap_transport"
    "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/control_frame"
    "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/security_root"
    "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/mesh_session"
    "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/mesh_auth"
    "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/provisioning_v2")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_ot_br)
