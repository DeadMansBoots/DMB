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
	# license of VCMI and of DMB's changes.
	install(FILES "${CMAKE_SOURCE_DIR}/license.txt" DESTINATION "${BIN_DIR}")
endif()
