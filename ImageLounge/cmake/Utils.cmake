# Searches for Qt with the required components
macro(NMC_FINDQT)
	set(CMAKE_AUTOMOC ON)
	set(CMAKE_AUTORCC OFF)

	if (MSVC)
		if(NOT QT_QMAKE_EXECUTABLE)
		find_program(QT_QMAKE_EXECUTABLE NAMES "qmake" "qmake.exe")
		endif()
		if(NOT QT_QMAKE_EXECUTABLE)
		message(FATAL_ERROR "you have to set the path to the qmake executable")
		endif()

		message(STATUS "QMake found: ${QT_QMAKE_EXECUTABLE}")
		get_filename_component(QT_QMAKE_PATH ${QT_QMAKE_EXECUTABLE} PATH)
	 endif()

	if (NOT DEFINED QT_VERSION_MAJOR)
	   find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core)
	endif()

	find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network LinguistTools PrintSupport Concurrent Gui Svg)

	if (NOT Qt${QT_VERSION_MAJOR}_FOUND)
		message(FATAL_ERROR "Qt Libraries not found!")
	endif()


endmacro(NMC_FINDQT)

macro(NMC_INSTALL)
	set(NOMACS_INSTALL_DIRECTORY ${CMAKE_SOURCE_DIR}/../installer/ CACHE PATH "Path to the installer directory")

	if (MSVC)
		set(PACKAGE_DIR ${NOMACS_INSTALL_DIRECTORY}/${PROJECT_NAME}.${NMC_ARCHITECTURE})
		install(TARGETS ${PROJECT_NAME} ${DLL_CORE_NAME} RUNTIME DESTINATION ${PACKAGE_DIR} CONFIGURATIONS Release)
		install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Release/ DESTINATION ${PACKAGE_DIR})

		# dependencies
		set(CMAKE_INSTALL_MFC_LIBRARIES 0)
		set(CMAKE_INSTALL_DEBUG_LIBRARIES 0)
		set(CMAKE_INSTALL_OPENMP_LIBRARIES TRUE)
		set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${PACKAGE_DIR})
		include(${CMAKE_ROOT}/Modules/InstallRequiredSystemLibraries.cmake)
	endif (MSVC)

endmacro(NMC_INSTALL)

macro(NMC_COPY_FILES)

# copy themes so we can run from the build directory
add_custom_target(
    copy_css_files ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/themes/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NOMACS_THEMES} "${CMAKE_BINARY_DIR}/themes/"
    SOURCES ${NOMACS_THEMES}
)

endmacro(NMC_COPY_FILES)

# find all targets in dir, copying into global property COLLECTED_TARGETS
# note: only finds targets after the previous add_subdirectory()'s
function(collect_dir_targets dir)
	get_directory_property(DIR_TARGETS DIRECTORY ${dir} BUILDSYSTEM_TARGETS)

	if (DIR_TARGETS)
		set_property(GLOBAL APPEND PROPERTY COLLECTED_TARGETS ${DIR_TARGETS})
	endif()

	get_directory_property(SUBDIRS DIRECTORY ${dir} SUBDIRECTORIES)

	foreach(subdir ${SUBDIRS})
		collect_dir_targets(${subdir})
	endforeach()
endfunction()
