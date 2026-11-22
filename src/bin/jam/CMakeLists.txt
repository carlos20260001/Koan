# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# jam, the build tool of the old Jamfile build. Plain C on the Unix back
# end; jamgram.c and jambase.c are the generated files kept in the tree.
# The three extensions are the ones the Jamfile build relies on (header
# and Jamfile caches, the JAM_TARGETS variable).
add_executable(jam
	builtins.c command.c compile.c execunix.c expand.c fileunix.c glob.c
	hash.c hcache.c headers.c jam.c jambase.c jamgram.c jcache.c lists.c
	make.c make1.c newstr.c option.c parse.c pathunix.c regexp.c rules.c
	scan.c search.c timestamp.c variable.c
)
target_compile_definitions(jam PRIVATE
	OPT_HEADER_CACHE_EXT OPT_JAMFILE_CACHE_EXT OPT_JAM_TARGETS_VARIABLE_EXT)
target_compile_options(jam PRIVATE -w)
