set(QT_TRANSLATIONS_DIR "" CACHE PATH "Directory containing Qt's standard .qm translations")

# Qt's CMake metadata already contains Unicode paths. Prefer it to asking
# qmake, whose Windows console output uses the system ANSI code page.
# Extra arguments form the qmake command (normally just Qt::qmake's location).
function(openmw_get_qt_translations_dir output)
    if (QT_TRANSLATIONS_DIR)
        get_filename_component(translations "${QT_TRANSLATIONS_DIR}" ABSOLUTE)
    elseif (QT6_INSTALL_PREFIX AND QT6_INSTALL_TRANSLATIONS)
        get_filename_component(translations "${QT6_INSTALL_TRANSLATIONS}" ABSOLUTE
            BASE_DIR "${QT6_INSTALL_PREFIX}")
    else ()
        if (NOT ARGN)
            message(FATAL_ERROR "Qt translation lookup requires a qmake command or -DQT_TRANSLATIONS_DIR=<path>.")
        endif ()
        execute_process(COMMAND ${ARGN} -query QT_INSTALL_TRANSLATIONS
            OUTPUT_VARIABLE translations OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_VARIABLE query_error RESULT_VARIABLE query_result ENCODING ANSI)
        if (NOT query_result STREQUAL "0" OR translations STREQUAL "")
            message(FATAL_ERROR "Could not query Qt translations (qmake: ${query_result}): ${query_error}\n"
                "Set -DQT_TRANSLATIONS_DIR=<path> to the Qt SDK translations directory.")
        endif ()
        get_filename_component(translations "${translations}" ABSOLUTE)
    endif ()

    if (NOT IS_DIRECTORY "${translations}")
        message(FATAL_ERROR "Qt translations directory does not exist: '${translations}'.\n"
            "Set -DQT_TRANSLATIONS_DIR=<path> to the Qt SDK translations directory.")
    endif ()
    set(${output} "${translations}" PARENT_SCOPE)
endfunction()
