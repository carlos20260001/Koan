# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# air/OS's HaikuDepot on its hpkg package kit (libpackage, target
# "bpackage"); the network kit (libnetapi, libnetservices) and
# BColumnListView are in libbe.
# TODO(linux backend): installing and removing packages goes through
# BPackageManager, which needs the libsolv solver add-on (not built: Debian's
# libsolv has no repo_haiku) and package_daemon (built, not started; the
# system's packages are Debian's, apt's), so those actions end in the kit's
# error message on Linux. Browsing HaikuDepotServer's catalogue works over
# the network kit.
foreach(_lib bpackage shared translation localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "HaikuDepot: skipped, needs ${_lib}")
		return()
	endif()
endforeach()
find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
	message(STATUS "HaikuDepot: skipped, needs python3 (generated sources)")
	return()
endif()

# The data transfer objects of HaikuDepotServer and their JSON parsers are
# generated from its JSON schemas (build/jam/JsonSchemaGenRules).
set(_hd_scripts ${CMAKE_CURRENT_SOURCE_DIR}/build/scripts)
set(_hd_generated)
set(_hd_generated_dirs)
foreach(_schema dumpexportpkg:DumpExportPkg dumpexportrepository:DumpExportRepository
		dumpexportreference:DumpExportReference)
	string(REPLACE ":" ";" _parts "${_schema}")
	list(GET _parts 0 _file)
	list(GET _parts 1 _class)
	set(_json ${CMAKE_CURRENT_SOURCE_DIR}/server/schema/${_file}.json)
	foreach(_kind model:Model:jsonschema2cppmodel.py:modelheader.mustache:modelimplementation.mustache
			parser:JsonListener:jsonschema2cppparser.py:parserheader.mustache:parserimplementation.mustache)
		string(REPLACE ":" ";" _k "${_kind}")
		list(GET _k 0 _dirname)
		list(GET _k 1 _suffix)
		list(GET _k 2 _script)
		list(GET _k 3 _header)
		list(GET _k 4 _impl)
		set(_out ${CMAKE_CURRENT_BINARY_DIR}/generated/${_file}${_dirname})
		add_custom_command(
			OUTPUT ${_out}/${_class}${_suffix}.cpp ${_out}/${_class}${_suffix}.h
			COMMAND ${CMAKE_COMMAND} -E make_directory ${_out}
			COMMAND ${Python3_EXECUTABLE} ${_hd_scripts}/${_script}
				--jsonschemafile ${_json} --outputdirectory ${_out}
			DEPENDS ${_json} ${_hd_scripts}/${_script}
				${_hd_scripts}/jsonschemacommon.py ${_hd_scripts}/ustache.py
				${_hd_scripts}/template/${_header}
				${_hd_scripts}/template/${_impl}
			COMMENT "Generating HaikuDepot ${_class}${_suffix}"
			VERBATIM
		)
		list(APPEND _hd_generated ${_out}/${_class}${_suffix}.cpp
			${_out}/${_class}${_suffix}.h)
		list(APPEND _hd_generated_dirs ${_out})
	endforeach()
endforeach()

Application(
	HaikuDepot

	SOURCES
	edits_generic/CompoundEdit.cpp
	edits_generic/EditContext.cpp
	edits_generic/EditManager.cpp
	edits_generic/UndoableEdit.cpp
	textview/Bullet.cpp
	textview/BulletData.cpp
	textview/CharacterStyle.cpp
	textview/CharacterStyleData.cpp
	textview/MarkupParser.cpp
	textview/Paragraph.cpp
	textview/ParagraphLayout.cpp
	textview/ParagraphStyle.cpp
	textview/ParagraphStyleData.cpp
	textview/TextDocument.cpp
	textview/TextDocumentLayout.cpp
	textview/TextDocumentView.cpp
	textview/TextEditor.cpp
	textview/TextListener.cpp
	textview/TextSelection.cpp
	textview/TextSpan.cpp
	textview/TextView.cpp
	textview/UndoableEditListener.cpp
	ui/App.cpp
	model/AccessToken.cpp
	ui_generic/BitmapHolder.cpp
	ui_generic/BitmapView.cpp
	model/Captcha.cpp
	model/CreateUserDetail.cpp
	model/DecisionProvider.cpp
	model/DeskbarLink.cpp
	ui/FeaturedPackagesView.cpp
	ui/FilterView.cpp
	ui/GeneralContentScrollView.cpp
	HaikuDepotConstants.cpp
	util/IdentityAndAccessUtils.cpp
	server/IncrementViewCounterProcess.cpp
	model/JobStateListener.cpp
	server/JwtTokenHelper.cpp
	ui_generic/LinkView.cpp
	ui_generic/LinkedBitmapView.cpp
	model/Logger.cpp
	main.cpp
	ui/MainWindow.cpp
	ui_generic/MarkupTextView.cpp
	model/Model.cpp
	ui/NotAvailableView.cpp
	ui/PackageContentsView.cpp
	model/PackageInfoListener.cpp
	ui/PackageInfoView.cpp
	ui/PackageListView.cpp
	model/PackageScreenshotRepository.cpp
	model/PasswordRequirements.cpp
	ui/RatePackageWindow.cpp
	ui_generic/RatingView.cpp
	model/RatingStability.cpp
	util/RatingUtils.cpp
	ui_generic/support.cpp
	ui/ScreenshotWindow.cpp
	model/ScreenshotCoordinate.cpp
	ui_generic/ScrollableGroupView.cpp
	ui/SettingsWindow.cpp
	ui/SharedIcons.cpp
	model/SimpleAlert.cpp
	ui/ShuttingDownWindow.cpp
	ui/ToLatestUserUsageConditionsWindow.cpp
	model/UserCredentials.cpp
	model/UserDetail.cpp
	ui/UserLoginWindow.cpp
	model/UserUsageConditions.cpp
	ui/UserUsageConditionsWindow.cpp
	model/ValidationFailure.cpp
	util/ValidationUtils.cpp
	ui/WorkStatusView.cpp
	icon/IconTarPtr.cpp
	icon/PackageIconDefaultRepository.cpp
	icon/PackageIconTarRepository.cpp
	packagemanagement/AbstractPackageProcess.cpp
	packagemanagement/InstallPackageProcess.cpp
	packagemanagement/OpenPackageProcess.cpp
	packagemanagement/PackageAction.cpp
	packagemanagement/PackageManager.cpp
	packagemanagement/UninstallPackageProcess.cpp
	packagemodel/DepotInfo.cpp
	packagemodel/Language.cpp
	packagemodel/PackageCategory.cpp
	packagemodel/PackageClassificationInfo.cpp
	packagemodel/PackageCoreInfo.cpp
	packagemodel/PackageFilter.cpp
	packagemodel/PackageFilterSpecification.cpp
	packagemodel/PackageInfo.cpp
	packagemodel/PackageLocalInfo.cpp
	packagemodel/PackageLocalizedText.cpp
	packagemodel/PackagePublisherInfo.cpp
	packagemodel/PackageScreenshotInfo.cpp
	packagemodel/PackageUserRatingInfo.cpp
	packagemodel/PackageVersion.cpp
	packagemodel/ScreenshotInfo.cpp
	packagemodel/UserInfo.cpp
	packagemodel/UserRating.cpp
	packagemodel/UserRatingSummary.cpp
	process/AbstractProcess.cpp
	process/AbstractProcessNode.cpp
	server/AbstractServerProcess.cpp
	server/AbstractSingleFileServerProcess.cpp
	server/CacheScreenshotProcess.cpp
	server/LocalPkgDataLoadProcess.cpp
	server/LocalRepositoryUpdateProcess.cpp
	server/PopulatePkgChangelogFromServerProcess.cpp
	server/PopulatePkgSizesProcess.cpp
	server/PopulatePkgUserRatingsFromServerProcess.cpp
	process/ProcessCoordinator.cpp
	process/ProcessCoordinatorFactory.cpp
	server/ServerHelper.cpp
	server/ServerIconExportUpdateProcess.cpp
	server/ServerSettings.cpp
	server/ServerPkgDataUpdateProcess.cpp
	server/ServerReferenceDataUpdateProcess.cpp
	server/ServerRepositoryDataUpdateProcess.cpp
	server/ServerRuntimeInformationVerifierProcess.cpp
	server/StandardMetaDataJsonEventListener.cpp
	server/StandardMetaData.cpp
	process/ThreadedProcessNode.cpp
	server/UserDetailVerifierProcess.cpp
	server/WebAppInterface.cpp
	tar/TarArchiveHeader.cpp
	tar/TarArchiveService.cpp
	util/AppUtils.cpp
	util/DataIOUtils.cpp
	util/LanguageMenuUtils.cpp
	util/LocaleUtils.cpp
	util/PackageKitUtils.cpp
	util/PackageUtils.cpp
	util/StorageUtils.cpp
	util/StringUtils.cpp
	${PROJECT_SOURCE_DIR}/src/servers/package/ProblemWindow.cpp
	${PROJECT_SOURCE_DIR}/src/servers/package/ResultWindow.cpp

	${_hd_generated}

	LIBS bpackage translation shared localestub
	RDEF HaikuDepot.rdef
)

UsePrivateHeaders(HaikuDepot interface net shared storage support locale)
# The app's directories are quote-include directories, as Jam's
# SEARCH_SOURCE made them, so that its "Model.h", "PackageInfo.h", ... come
# before the global include path's Tracker and libvpackage headers of the
# same names.
target_include_directories(HaikuDepot PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/netservices
	${PROJECT_SOURCE_DIR}/headers/private/package
)
foreach(_dir
		${CMAKE_CURRENT_SOURCE_DIR}/edits_generic
		${CMAKE_CURRENT_SOURCE_DIR}/icon
		${CMAKE_CURRENT_SOURCE_DIR}/model
		${CMAKE_CURRENT_SOURCE_DIR}/packagemanagement
		${CMAKE_CURRENT_SOURCE_DIR}/packagemodel
		${CMAKE_CURRENT_SOURCE_DIR}/process
		${CMAKE_CURRENT_SOURCE_DIR}/textview
		${CMAKE_CURRENT_SOURCE_DIR}/ui
		${CMAKE_CURRENT_SOURCE_DIR}/ui_generic
		${CMAKE_CURRENT_SOURCE_DIR}/server
		${CMAKE_CURRENT_SOURCE_DIR}/tar
		${CMAKE_CURRENT_SOURCE_DIR}/util
		${PROJECT_SOURCE_DIR}/src/servers/package
		${_hd_generated_dirs})
	target_compile_options(HaikuDepot PRIVATE "-iquote${_dir}")
endforeach()
# air/OS's Jamfile builds this directory without -Werror as well
target_compile_options(HaikuDepot PRIVATE -Wno-error)

DoCatalogs("x-vnd.vos-HaikuDepot" apps/haikudepot)
