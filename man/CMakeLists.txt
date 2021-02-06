find_program(RONN ronn)

if(RONN)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/timg.1
    COMMAND ${RONN} ARGS "--roff" "${CMAKE_CURRENT_SOURCE_DIR}/timg.1.md" "-o" "."
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/timg.1.md
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Building manpage timg.1"
  )

  add_custom_target(man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/timg.1)

  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/timg.1
    DESTINATION ${CMAKE_INSTALL_FULL_MANDIR}/man1
  )
else()
  message(WARNING "NOT re-generating the manpage. Possibly ronn is not installed.")
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/timg.1.gz
    DESTINATION ${CMAKE_INSTALL_FULL_MANDIR}/man1
  )
endif()
