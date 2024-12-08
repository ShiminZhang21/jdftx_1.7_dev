find_path(GSL_INCLUDE_DIR gsl/gsl_cblas.h ${GSL_PATH} ${GSL_PATH}/include NO_DEFAULT_PATH)
find_path(GSL_INCLUDE_DIR gsl/gsl_cblas.h)
find_library(GSL_LIBRARY NAMES gsl PATHS ${GSL_PATH} ${GSL_PATH}/lib ${GSL_PATH}/lib64 NO_DEFAULT_PATH)
find_library(GSL_LIBRARY NAMES gsl)

#Check version:
if(GSL_INCLUDE_DIR AND GSL_LIBRARY)
	include(CheckSymbolExists)
	find_library(GSL_BLAS_LIBRARY NAMES gslcblas PATHS ${GSL_PATH} ${GSL_PATH}/lib ${GSL_PATH}/lib64 NO_DEFAULT_PATH)
	find_library(GSL_BLAS_LIBRARY NAMES gslcblas)
	if(GSL_BLAS_LIBRARY)
		set(CMAKE_REQUIRED_FLAGS "-I${GSL_INCLUDE_DIR} ${CMAKE_REQUIRED_FLAGS}")
		set(CMAKE_REQUIRED_LIBRARIES "${GSL_LIBRARY};${GSL_BLAS_LIBRARY};${CMAKE_REQUIRED_LIBRARIES};m")
		if (GSL_SKIP_VERSION_CHECK)
			set(GSL_VERSION_ADEQUATE TRUE)
		else()
			check_symbol_exists("gsl_integration_glfixed_point" "${GSL_INCLUDE_DIR}/gsl/gsl_integration.h" GSL_VERSION_ADEQUATE)
		endif()
	endif()
endif()

if(GSL_INCLUDE_DIR AND GSL_LIBRARY AND (GSL_VERSION_ADEQUATE OR GSL_SKIP_VERSION_CHECK))
	set(GSL_FOUND TRUE)
endif()


if(GSL_FOUND)
	if(NOT GSL_FIND_QUIETLY)
		message(STATUS "Found GSL: ${GSL_LIBRARY}")
	endif()
else()
	if(GSL_FIND_REQUIRED)
		if(NOT GSL_INCLUDE_DIR)
			message(FATAL_ERROR "Could not find the GNU Scientific Library headers (Add -D GSL_PATH=<path> to the cmake commandline for a non-standard installation)")
		endif()
		if(NOT GSL_LIBRARY)
			message(FATAL_ERROR "Could not find the GNU Scientific Library shared libraries (Add -D GSL_PATH=<path> to the cmake commandline for a non-standard installation)")
		endif()
		if(NOT GSL_VERSION_ADEQUATE)
			message(FATAL_ERROR "Unsupported GNU Scientific Library version; need version >= 1.15")
		endif()
	endif()
endif()
