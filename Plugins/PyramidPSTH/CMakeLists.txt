cmake_minimum_required(VERSION 3.15)

include(../PluginRules.cmake)

add_sources(
  ${PLUGIN_NAME}
  PyramidPSTH.cpp
  PyramidPSTH.h
  PyramidPSTHEditor.cpp
  PyramidPSTHEditor.h
  PyramidRuleEngine.cpp
  PyramidRuleEngine.h)

if(APPLE)
  set_target_properties(
    ${PLUGIN_NAME} PROPERTIES XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
                              "org.open-ephys.plugin.PyramidPSTH")
endif()
