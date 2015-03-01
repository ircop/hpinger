# Confirm that oping library is installed

# This module defines
# LIBOPING_FOUND, If false, don't try to use liboping.

FIND_PATH(LIBOPING_INCLUDE_DIR oping.h
    /usr/include
    /usr/local/include
)

FIND_LIBRARY(LIBOPING_LIBRARY oping
	/usr/lib64
	/usr/lib
)

set(LIBOPING_FOUND FALSE)

if(LIBOPING_INCLUDE_DIR)
    IF(LIBOPING_LIBRARY)
	SET(LIBOPING_FOUND TRUE)
    ENDIF(LIBOPING_LIBRARY)
ENDIF(LIBOPING_INCLUDE_DIR)

IF(LIBOPING_FOUND)
	MESSAGE( STATUS "Found Liboping: ${LIBOPING_LIBRARY}")
ENDIF(LIBOPING_FOUND)