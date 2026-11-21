# "package" is a CMake-reserved target name (CPack), so the library
# target is "vpackage" (-> libvpackage.so) even though the kit's
# headers live under package/ and its directory is src/kits/package/.
# ported subset: the plain build container has no libapt-pkg-dev (the bake
# chroot does). Without it libvpackage is left out, with a warning.
find_path(VOS_APT_PKG_INCLUDE_DIR apt-pkg/cachefile.h)
if(VOS_APT_PKG_INCLUDE_DIR)

AddOn(
	vpackage
	SHARED
	SOURCES
	AptCacheAdapter.cpp
	DependencyExpression.cpp
	PackageInfo.cpp
	PackageRoster.cpp
	PackageVersion.cpp

	LIBS be root apt-pkg
)

# libapt-pkg-dev has no pkg-config file (verified: pkg-config --list-all
# has no apt entry); link the SONAME's library name directly. ABI is
# exact-version pinned (libapt-pkg-dev Depends: libapt-pkg (= 3.0.3));
# accepted per PACKAGE-KIT-STUDY.md D1, a rebase cost not a runtime one.
target_compile_options(vpackage PRIVATE -std=c++17)

# VitruvianOS's headers live in headers/os/experimental/package; air/OS's
# BPackageKit headers of the same names (headers/os/package) come first on
# the global include path, so the apt kit and its users put the
# experimental directory first.
target_include_directories(vpackage BEFORE PRIVATE
	${CMAKE_SOURCE_DIR}/headers/os/experimental)

else()
	message(WARNING "package kit: libapt-pkg not found (libapt-pkg-dev); "
		"libvpackage is left out")
endif()


# air/OS's package kit (BPackageKit: hpkg reader/writer, repositories,
# package manager, solver interface) as libpackage.so. "package" is CPack's
# target name, hence "bpackage" with the output name "package". The
# package daemon and packagefs do not exist on Linux: DaemonClient requests
# fail, reading and writing .hpkg files and repositories works.
set(BPACKAGE_HPKG_SOURCES
	hpkg/AttributeDataReader.cpp
	hpkg/BlockBufferPool.cpp
	hpkg/BlockBufferPoolImpl.cpp
	hpkg/BlockBufferPoolNoLock.cpp
	hpkg/BufferPool.cpp
	hpkg/DataReader.cpp
	hpkg/ErrorOutput.cpp
	hpkg/FDDataReader.cpp
	hpkg/HPKGDefs.cpp
	hpkg/NoErrorOutput.cpp
	hpkg/PackageContentHandler.cpp
	hpkg/PackageData.cpp
	hpkg/PackageDataReader.cpp
	hpkg/PackageEntry.cpp
	hpkg/PackageEntryAttribute.cpp
	hpkg/PackageFileHeapAccessorBase.cpp
	hpkg/PackageFileHeapReader.cpp
	hpkg/PackageFileHeapWriter.cpp
	hpkg/PackageReader.cpp
	hpkg/PackageReaderImpl.cpp
	hpkg/PackageWriter.cpp
	hpkg/PackageWriterImpl.cpp
	hpkg/PoolBuffer.cpp
	hpkg/ReaderImplBase.cpp
	hpkg/RepositoryContentHandler.cpp
	hpkg/RepositoryReader.cpp
	hpkg/RepositoryReaderImpl.cpp
	hpkg/RepositoryWriter.cpp
	hpkg/RepositoryWriterImpl.cpp
	hpkg/StandardErrorOutput.cpp
	hpkg/Strings.cpp
	hpkg/WriterImplBase.cpp

	hpkg/v1/HPKGDefsV1.cpp
	hpkg/v1/PackageContentHandlerV1.cpp
	hpkg/v1/PackageDataReaderV1.cpp
	hpkg/v1/PackageDataV1.cpp
	hpkg/v1/PackageEntryAttributeV1.cpp
	hpkg/v1/PackageEntryV1.cpp
	hpkg/v1/PackageInfoContentHandlerV1.cpp
	hpkg/v1/PackageReaderImplV1.cpp
	hpkg/v1/PackageReaderV1.cpp
	hpkg/v1/ReaderImplBaseV1.cpp
)

AddOn(
	bpackage
	SHARED

	SOURCES
	ActivateRepositoryCacheJob.cpp
	ActivateRepositoryConfigJob.cpp
	ActivationTransaction.cpp
	AddRepositoryRequest.cpp
	Attributes.cpp
	ChecksumAccessors.cpp
	CleanUpAdminDirectoryRequest.cpp
	CommitTransactionResult.cpp
	Context.cpp
	DaemonClient.cpp
	DownloadFileRequest.cpp
	DropRepositoryRequest.cpp
	FetchFileJob.cpp
	FetchUtils.cpp
	GlobalWritableFileInfo.cpp
	InstallationLocationInfo.cpp
	Job.cpp
	PackageInfoContentHandler.cpp
	PackageInfoParser.cpp
	PackageInfoSet.cpp
	PackageResolvable.cpp
	PackageResolvableExpression.cpp
	PathFinder.cpp
	RefreshRepositoryRequest.cpp
	RemoveRepositoryJob.cpp
	RepositoryCache.cpp
	RepositoryConfig.cpp
	RepositoryInfo.cpp
	Request.cpp
	TempfileManager.cpp
	User.cpp
	UserSettingsFileInfo.cpp
	ValidateChecksumJob.cpp
	bpackage/PackageInfo.cpp
	bpackage/PackageRoster.cpp
	bpackage/PackageVersion.cpp
	${BPACKAGE_HPKG_SOURCES}
	manager/Exceptions.cpp
	manager/PackageManager.cpp
	manager/RepositoryBuilder.cpp
	solver/Solver.cpp
	solver/SolverPackage.cpp
	solver/SolverPackageSpecifier.cpp
	solver/SolverPackageSpecifierList.cpp
	solver/SolverProblem.cpp
	solver/SolverProblemSolution.cpp
	solver/SolverRepository.cpp
	solver/SolverResult.cpp

	LIBS be root shared localestub ${ZLIB_LIBRARIES} zstd

	RDEF
	libpackage.rdef
)
set_target_properties(bpackage PROPERTIES OUTPUT_NAME package)
# The kit's sources tell the Haiku-API target (netservices downloads,
# find_directory(), the solver add-on by soname) from the host build tools
# with this macro; this tree's target is the Haiku API on Linux.
target_compile_definitions(bpackage PRIVATE HAIKU_TARGET_PLATFORM_HAIKU)
target_compile_options(bpackage PRIVATE -std=c++17)
target_include_directories(bpackage PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/hpkg
	${CMAKE_CURRENT_SOURCE_DIR}/manager
	${CMAKE_CURRENT_SOURCE_DIR}/solver
	${CMAKE_SOURCE_DIR}/headers/private/netservices
	${CMAKE_SOURCE_DIR}/headers/private/package
)
UsePrivateHeaders(bpackage app kernel shared libroot storage support system)
install(TARGETS bpackage DESTINATION /lib/)

# The libsolv based BSolver add-on, against Debian's libsolv (air/OS uses
# the copy in src/libs/libsolv).
# Debian's libsolv lacks HaikuPorts' repo_haiku (reading installed hpkg
# packages), which the add-on needs: it is built only against a libsolv
# that has it. TODO(linux backend): build src/libs/libsolv with
# repo_haiku, or feed the solver through repo_write instead.
find_path(VOS_LIBSOLV_INCLUDE_DIR solv/repo_haiku.h)
find_library(VOS_LIBSOLV_LIBRARY solv)
find_library(VOS_LIBSOLVEXT_LIBRARY solvext)
if(VOS_LIBSOLV_INCLUDE_DIR AND VOS_LIBSOLV_LIBRARY AND VOS_LIBSOLVEXT_LIBRARY)
	AddOn(
		package-add-on-libsolv
		SHARED

		SOURCES
		solver/libsolv/LibsolvSolver.cpp

		LIBS bpackage be root ${VOS_LIBSOLVEXT_LIBRARY} ${VOS_LIBSOLV_LIBRARY}

		RDEF
		solver/libsolv/LibsolvSolver.rdef
	)
	target_compile_options(package-add-on-libsolv PRIVATE -std=c++17)
	target_include_directories(package-add-on-libsolv PRIVATE
		${VOS_LIBSOLV_INCLUDE_DIR}/solv
		${CMAKE_SOURCE_DIR}/headers/private/package
	)
	UsePrivateHeaders(package-add-on-libsolv shared)
	install(TARGETS package-add-on-libsolv DESTINATION /lib/)
else()
	message(STATUS "package kit: no libsolv with repo_haiku; the libsolv "
		"solver add-on is left out")
endif()
