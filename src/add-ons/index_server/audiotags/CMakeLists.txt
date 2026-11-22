# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Artist, title and album of audio files (TagLib) as Media:* attributes.
find_package(PkgConfig REQUIRED)
pkg_check_modules(TAGLIB REQUIRED taglib)

IndexServerAddOn(AudioTagAnalyser
	SOURCES
	AudioTagAnalyser.cpp
)
# The chroot's /usr/include stays behind the C++ headers (-isystem).
list(FILTER TAGLIB_INCLUDE_DIRS EXCLUDE REGEX "/usr/include/?$")
target_link_libraries(AudioTagAnalyser PRIVATE media ${TAGLIB_LINK_LIBRARIES})
target_include_directories(AudioTagAnalyser PRIVATE ${TAGLIB_INCLUDE_DIRS})
UseMediaKit(AudioTagAnalyser)
