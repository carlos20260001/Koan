# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Full-text index of every file a translator turns into text (CLucene).
find_package(PkgConfig REQUIRED)
pkg_check_modules(CLUCENE REQUIRED libclucene-core)

IndexServerAddOn(FullTextAnalyser
	SOURCES
	CLuceneDataBase.cpp
	FullTextAnalyser.cpp
)
# clucene-config.h is in the library directory; the chroot's /usr/include
# stays behind the C++ headers (-isystem).
list(FILTER CLUCENE_INCLUDE_DIRS EXCLUDE REGEX "/usr/include/?$")
target_link_libraries(FullTextAnalyser PRIVATE translation
	${CLUCENE_LINK_LIBRARIES})
target_include_directories(FullTextAnalyser PRIVATE ${CLUCENE_INCLUDE_DIRS})
