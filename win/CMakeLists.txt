# We need to keep this code into separate directory so CMake will execute it after all other subdirectories install code
if(WIN32)
	set(deployTargets)
	if(TARGET vcmilauncher)
		list(APPEND deployTargets vcmilauncher)
	endif()
	if(TARGET vcmieditor)
		list(APPEND deployTargets vcmieditor)
	endif()
	foreach(deployTarget ${deployTargets})
		set(targetBinary "\"\${CMAKE_INSTALL_PREFIX}/${BIN_DIR}/$<TARGET_FILE_NAME:${deployTarget}>\"")
		vcmi_deploy_qt(windeployqt "--no-compiler-runtime ${targetBinary}")
	endforeach()

	# DMB: the GPL text travels with the programs, not only on the installer's license page. It is the
	# license of VCMI, of DMB's changes and of OmniAI; Node's and the generator's are in mapgen\.
	install(FILES "${CMAKE_SOURCE_DIR}/license.txt" DESTINATION "${BIN_DIR}")

	# DMB: the map generator ships beside the programs in mapgen\ (MapGen's layout, 2026-09-26): its
	# src\ from the mapgen submodule, the official node.exe with Node's license in mapgen\node\, and
	# generate.cmd, the MapGen tab's default command. CI downloads Node and passes DMB_NODE_DIR; a
	# build without either leaves the folder out, and the tab then says no generator is configured.
	set(DMB_MAPGEN_DIR "${CMAKE_SOURCE_DIR}/mapgen" CACHE PATH "The map generator's repository (the mapgen submodule)")
	set(DMB_NODE_DIR "" CACHE PATH "An unpacked official Node.js build for Windows, holding node.exe and LICENSE")
	if(EXISTS "${DMB_MAPGEN_DIR}/src/main/generate-cli.js" AND EXISTS "${DMB_NODE_DIR}/node.exe")
		install(DIRECTORY "${DMB_MAPGEN_DIR}/src" DESTINATION "${BIN_DIR}/mapgen")
		install(PROGRAMS "${DMB_NODE_DIR}/node.exe" DESTINATION "${BIN_DIR}/mapgen/node")
		install(FILES "${DMB_NODE_DIR}/LICENSE" DESTINATION "${BIN_DIR}/mapgen/node")
		install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/mapgen/generate.cmd" DESTINATION "${BIN_DIR}/mapgen")
		if(EXISTS "${DMB_MAPGEN_DIR}/LICENSE")
			install(FILES "${DMB_MAPGEN_DIR}/LICENSE" DESTINATION "${BIN_DIR}/mapgen")
		endif()
		message(STATUS "DMB: the package carries the map generator (${DMB_MAPGEN_DIR}, Node from ${DMB_NODE_DIR})")
	else()
		message(STATUS "DMB: no map generator in the package (it needs ${DMB_MAPGEN_DIR}/src and DMB_NODE_DIR)")
	endif()
endif()
