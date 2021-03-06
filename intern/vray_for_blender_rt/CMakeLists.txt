# V-Ray For Blender Scene Exporter
#

add_definitions(
	${BOOST_DEFINITIONS}
)

set(INC_SYS
	${JPEG_INCLUDE_DIR}
	${PYTHON_INCLUDE_DIRS}
	${ZLIB_INCLUDE_DIRS}
	${BOOST_INCLUDE_DIR}
)

set(INC
	${CMAKE_SOURCE_DIR}/extern/glew/include
	${CMAKE_SOURCE_DIR}/intern/guardedalloc
	${CMAKE_SOURCE_DIR}/intern/smoke/extern
	${CMAKE_SOURCE_DIR}/source/blender/blenkernel
	${CMAKE_SOURCE_DIR}/source/blender/blenlib
	${CMAKE_SOURCE_DIR}/source/blender/blenloader
	${CMAKE_SOURCE_DIR}/source/blender/editors/include
	${CMAKE_SOURCE_DIR}/source/blender/imbuf
	${CMAKE_SOURCE_DIR}/source/blender/makesdna
	${CMAKE_SOURCE_DIR}/source/blender/makesrna
	${CMAKE_SOURCE_DIR}/source/blender/makesrna/intern
	${CMAKE_SOURCE_DIR}/source/blender/python
	${CMAKE_SOURCE_DIR}/source/blender/render/extern/include
	${CMAKE_SOURCE_DIR}/source/blender/render/intern/include
	${CMAKE_SOURCE_DIR}/source/blender/windowmanager
	${CMAKE_BINARY_DIR}/source/blender/makesrna/intern

	../vray_for_blender

	.
	src
	src/params
	src/plugin_exporter
	src/scene_exporter
	src/scene_exporter/utils

	extern/vray-zmq-wrapper/include
	${OPENIMAGEIO_INCLUDE_DIRS}
	${OPENIMAGEIO_INCLUDE_DIRS}/OpenImageIO
	${OPENEXR_INCLUDE_DIR}
	${OPENEXR_INCLUDE_DIRS}
)

if (WIN32)
	list(APPEND INC ${LIBDIR}/osl/include)
else()
	list(APPEND INC ${OSL_INCLUDE_DIR})
endif()

# generate good folder structure for Visual Studio
if (WIN32)
	macro(GroupSources curdir ignore_prefix)
		file(GLOB children RELATIVE ${PROJECT_SOURCE_DIR}/${curdir} ${PROJECT_SOURCE_DIR}/${curdir}/*)
		foreach(child ${children})
			if(IS_DIRECTORY ${PROJECT_SOURCE_DIR}/${curdir}/${child})
				if (${child} STREQUAL ".git")
					# pass
				else()
					GroupSources(${curdir}/${child} ${ignore_prefix})
				endif()
			else()
				string(REPLACE "${ignore_prefix}/" "" rel_path ${curdir})
				string(REPLACE "/" "\\" groupname ${rel_path})
				source_group(${groupname} FILES ${PROJECT_SOURCE_DIR}/${curdir}/${child})
			endif()
		endforeach()
	endmacro()
	GroupSources(intern/vray_for_blender_rt intern/vray_for_blender_rt)
endif()

if(USE_BLENDER_VRAY_APPSDK)
	list(APPEND INC ${APPSDK_ROOT}/cpp/include)
endif()


list(APPEND INC ${JPEG_INCLUDE_DIR})
list(APPEND INC ${LIBS_ROOT}/${CMAKE_SYSTEM_NAME}/zmq/include)
list(APPEND INC extern/vray-zmq-wrapper/extern/cppzmq)

file(GLOB_RECURSE HEADERS *.h)
file(GLOB_RECURSE SOURCES *.cpp *.hpp)

if (WITH_CYCLES_OSL)
	add_definitions(-DWITH_OSL)
	add_definitions(-DOSL_STATIC_BUILD)
	add_definitions(-DOSL_STATIC_LIBRARY)
endif()


if (UNIX)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch -std=c++11")
endif()
list(REMOVE_DUPLICATES INC)
blender_add_lib(vray_for_blender_rt "${HEADERS};${SOURCES}" "${INC}" "${INC_SYS}")

add_dependencies(vray_for_blender_rt bf_rna bf_blenlib)
