string(REPLACE "\"" "" HTML_INDEX "${HTML_INDEX}")
string(REPLACE "\"" "" XML_INDEX "${XML_INDEX}")

if(NOT DEFINED HTML_INDEX OR NOT EXISTS "${HTML_INDEX}")
  message(FATAL_ERROR "Generated HTML documentation is missing: ${HTML_INDEX}")
endif()

if(NOT DEFINED XML_INDEX OR NOT EXISTS "${XML_INDEX}")
  message(FATAL_ERROR "Generated XML documentation is missing: ${XML_INDEX}")
endif()

message(STATUS "Documentation outputs are present")
