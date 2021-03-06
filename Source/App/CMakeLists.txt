# 
# Copyright(c) 2018 Intel Corporation
# SPDX - License - Identifier: BSD - 2 - Clause - Patent
# 

# APP Directory CMakeLists.txt

# Include Subdirectories
include_directories (${PROJECT_SOURCE_DIR}/API/)
include_directories (${PROJECT_SOURCE_DIR}/API/OpenMAX/IL/)
include_directories (${PROJECT_SOURCE_DIR}/../Bin/${CMAKE_BUILD_TYPE}/)
if(${COMPILE_AS_CPP} EQUAL 1) 

	file(GLOB SourceFiles
	    "*.c"
	)

	set_source_files_properties( 
		${SourceFiles} 
		PROPERTIES LANGUAGE CXX )
endif(${COMPILE_AS_CPP} EQUAL 1)


# App Source Files
add_executable (HevcEncoderApp 
    EbAppConfig.c
    EbAppContext.c
    EbAppFifo.c
    EbAppMain.c
    EbAppProcessCmd.c
    EbAppSei.c
    EbTime.c
)

#********** SET COMPILE FLAGS************

if (UNIX)

	# Configure the RPATH to allow relative runtime linking	
	INSTALL(TARGETS HevcEncoderApp RUNTIME DESTINATION ".:\$ORIGIN/../lib:\$ORIGIN/")



	# Link the Encoder App
	target_link_libraries (HevcEncoderApp
		${PROJECT_SOURCE_DIR}/../Bin/${CMAKE_BUILD_TYPE}/libHevcEncoder.so
		pthread rt
		m)


else()

	# Link the Encoder App
	target_link_libraries (HevcEncoderApp
		${PROJECT_SOURCE_DIR}/../Bin/${CMAKE_BUILD_TYPE}/libHevcEncoder.dll)

endif()




