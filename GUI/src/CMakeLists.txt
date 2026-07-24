cmake_minimum_required(VERSION 3.16)

project(SteamDeck_rEFInd VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network LinguistTools)

# SteamDeck_rEFInd_en_US.ts is the source-language reference; the others carry
# the actual translations. Refresh all of them from the sources with the
# update_translations target (Qt 6) or lupdate directly — see I18N_AUDIT.md.
set(TS_FILES
    SteamDeck_rEFInd_en_US.ts
    SteamDeck_rEFInd_ar.ts
    SteamDeck_rEFInd_bn.ts
    SteamDeck_rEFInd_de.ts
    SteamDeck_rEFInd_es.ts
    SteamDeck_rEFInd_fa.ts
    SteamDeck_rEFInd_fr.ts
    SteamDeck_rEFInd_hi.ts
    SteamDeck_rEFInd_id.ts
    SteamDeck_rEFInd_it.ts
    SteamDeck_rEFInd_ja.ts
    SteamDeck_rEFInd_ko.ts
    SteamDeck_rEFInd_nl.ts
    SteamDeck_rEFInd_pt.ts
    SteamDeck_rEFInd_ru.ts
    SteamDeck_rEFInd_scn.ts
    SteamDeck_rEFInd_tr.ts
    SteamDeck_rEFInd_uk.ts
    SteamDeck_rEFInd_ur.ts
    SteamDeck_rEFInd_vi.ts
    SteamDeck_rEFInd_zh_CN.ts
)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        osdetect.h
        osdetect_common.cpp
        platform.cpp
        platform.h
        previewdialog.cpp
        previewdialog.h
        uitranslation.cpp
        uitranslation.h
        resources.qrc
        ${TS_FILES}
)

if(WIN32)
    enable_language(RC)
    list(APPEND PROJECT_SOURCES osdetect_win.cpp rEFInd_GUI.rc)
else()
    list(APPEND PROJECT_SOURCES osdetect_linux.cpp)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(SteamDeck_rEFInd
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET SteamDeck_rEFInd APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    # Compile the .ts files at build time and embed the .qm output under the
    # ":/i18n" resource prefix (the default) that main.cpp loads from. The old
    # qt_create_translation() call left QM_FILES dangling with no dependent
    # target, so no .qm was ever built and the app always ran untranslated.
    qt_add_translations(SteamDeck_rEFInd TS_FILES ${TS_FILES})

    # Embed Qt's own qtbase catalogs for the shipped languages so standard
    # dialog buttons are translated even where no qt6-translations package
    # exists (SteamOS); UiTranslation loads ":/i18n_qtbase" as the last
    # fallback after the Qt install and windeployqt's translations/ dir.
    set(_qtbase_dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
    set(_qtbase_entries "")
    foreach(_lang ar bn de es fa fr hi id it ja ko pt ru tr uk ur vi zh_CN)
        if(EXISTS "${_qtbase_dir}/qtbase_${_lang}.qm")
            string(APPEND _qtbase_entries "  <file alias=\"qtbase_${_lang}.qm\">${_qtbase_dir}/qtbase_${_lang}.qm</file>\n")
        endif()
    endforeach()
    if(_qtbase_entries)
        set(_qtbase_qrc "${CMAKE_CURRENT_BINARY_DIR}/qtbase_translations.qrc")
        file(WRITE "${_qtbase_qrc}"
             "<RCC>\n <qresource prefix=\"/i18n_qtbase\">\n${_qtbase_entries} </qresource>\n</RCC>\n")
        target_sources(SteamDeck_rEFInd PRIVATE "${_qtbase_qrc}")
    else()
        message(WARNING "No qtbase_*.qm found under ${_qtbase_dir} - standard dialog buttons will rely on system Qt translations")
    endif()
else()
    if(ANDROID)
        add_library(SteamDeck_rEFInd SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(SteamDeck_rEFInd
            ${PROJECT_SOURCES}
        )
    endif()

    # Qt 5 has no qt_add_translations: compile the .qm files (lrelease only —
    # qt5_create_translation would also rewrite the .ts files in the source
    # tree on every build) into translations/ next to the binary, where
    # main.cpp's filesystem fallback picks them up.
    set_source_files_properties(${TS_FILES} PROPERTIES
        OUTPUT_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/translations)
    qt5_add_translation(QM_FILES ${TS_FILES})
    add_custom_target(SteamDeck_rEFInd_translations ALL DEPENDS ${QM_FILES})
endif()

target_link_libraries(SteamDeck_rEFInd PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)

if(WIN32)
    # shell32: SHGetKnownFolderPath; ole32: CoTaskMemFree; uuid: the
    # FOLDERID_* GUID definitions (MinGW resolves them from libuuid).
    target_link_libraries(SteamDeck_rEFInd PRIVATE shell32 ole32 uuid)
endif()

if(MSVC)
    # MinGW gets the manifest via rEFInd_GUI.rc; MSVC generates its own.
    target_link_options(SteamDeck_rEFInd PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

set_target_properties(SteamDeck_rEFInd PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

install(TARGETS SteamDeck_rEFInd
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Optional developer unit tests (Linux only; OFF so packaging builds are
# unaffected): cmake -DBUILD_GUI_TESTS=ON, then ctest --test-dir <builddir>.
option(BUILD_GUI_TESTS "Build the developer unit tests" OFF)
if(BUILD_GUI_TESTS AND NOT WIN32)
    enable_testing()
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(tst_osdetect tests/tst_osdetect.cpp osdetect_common.cpp osdetect_linux.cpp platform.cpp)
    target_include_directories(tst_osdetect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(tst_osdetect PRIVATE
        Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets)
    add_test(NAME osdetect COMMAND tst_osdetect)
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(SteamDeck_rEFInd)
endif()
