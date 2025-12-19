file(GLOB_RECURSE SOURCES
     "*.cpp"
)
file(GLOB_RECURSE HEADERS
    "*.h"
    "*.hpp"
)
source_group("Sources" FILES ${SOURCES})
source_group("Headers" FILES ${HEADERS})
set(SOURCES GLOB ${SOURCES})
set(HEADERS GLOB ${HEADERS})