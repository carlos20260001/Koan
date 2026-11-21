# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# Debugger: the graphical, command-line and report front ends of the
# debugger kit (src/kits/debugger, libdebugger.so).
#
# Built only with the debugger kit (VOS_WITH_DEBUGGER_KIT, which needs
# VOS_WITH_DEBUG_KIT), both OFF until the kits are ported to ptrace(2); the
# app itself has no kernel code: it drives everything through the kit's
# TargetHostInterface. The command-line interface needs libedit
# (pkg-config libedit, Debian libedit-dev). The table widgets come from
# DebugAnalyzer (DebugAnalyzer_gui_table), BColumnListView from libbe.

if(NOT TARGET debugger)
	message(STATUS "Debugger: skipped, needs debugger (VOS_WITH_DEBUGGER_KIT)")
	return()
endif()
foreach(_lib debug bpackage tracker shared localestub
		DebugAnalyzer_gui_table)
	if(NOT TARGET ${_lib})
		message(STATUS "Debugger: skipped, needs ${_lib}")
		return()
	endif()
endforeach()
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(VOS_LIBEDIT QUIET libedit)
endif()
if(NOT VOS_LIBEDIT_FOUND)
	message(STATUS "Debugger: skipped, needs libedit (libedit-dev)")
	return()
endif()

Application(
	Debugger

	SOURCES
	Debugger.cpp
	settings/DebuggerSettingsManager.cpp
	user_interface/cli/CliContext.cpp
	user_interface/cli/CommandLineUserInterface.cpp
	user_interface/cli/commands/CliCommand.cpp
	user_interface/cli/commands/CliContinueCommand.cpp
	user_interface/cli/commands/CliDebugReportCommand.cpp
	user_interface/cli/commands/CliDumpMemoryCommand.cpp
	user_interface/cli/commands/CliDumpStringCommand.cpp
	user_interface/cli/commands/CliPrintVariableCommand.cpp
	user_interface/cli/commands/CliQuitCommand.cpp
	user_interface/cli/commands/CliStackFrameCommand.cpp
	user_interface/cli/commands/CliStackTraceCommand.cpp
	user_interface/cli/commands/CliStopCommand.cpp
	user_interface/cli/commands/CliThreadCommand.cpp
	user_interface/cli/commands/CliThreadsCommand.cpp
	user_interface/cli/commands/CliVariablesCommand.cpp
	user_interface/cli/commands/CliWriteCoreFileCommand.cpp
	user_interface/gui/GraphicalUserInterface.cpp
	user_interface/gui/connection_config/ConnectionConfigHandler.cpp
	user_interface/gui/connection_config/ConnectionConfigHandlerRoster.cpp
	user_interface/gui/connection_config/ConnectionConfigView.cpp
	user_interface/gui/connection_config/config_handlers/NetworkConnectionConfigHandler.cpp
	user_interface/gui/connection_config/config_handlers/NetworkConnectionConfigView.cpp
	user_interface/gui/model/VariablesViewState.cpp
	user_interface/gui/model/VariablesViewStateHistory.cpp
	user_interface/gui/expression_eval_window/ExpressionEvaluationWindow.cpp
	user_interface/gui/inspector_window/InspectorWindow.cpp
	user_interface/gui/inspector_window/MemoryView.cpp
	user_interface/gui/settings/DebuggerUiSettingsFactory.cpp
	user_interface/gui/settings/GuiTeamUiSettings.cpp
	user_interface/gui/teams_window/TeamsWindow.cpp
	user_interface/gui/teams_window/TeamsListView.cpp
	user_interface/gui/team_window/BreakpointListView.cpp
	user_interface/gui/team_window/BreakpointsView.cpp
	user_interface/gui/team_window/ConsoleOutputView.cpp
	user_interface/gui/team_window/ImageFunctionsView.cpp
	user_interface/gui/team_window/ImageListView.cpp
	user_interface/gui/team_window/RegistersView.cpp
	user_interface/gui/team_window/SourceView.cpp
	user_interface/gui/team_window/StackTraceView.cpp
	user_interface/gui/team_window/TeamWindow.cpp
	user_interface/gui/team_window/ThreadListView.cpp
	user_interface/gui/team_window/TypeHandlerMenuItem.cpp
	user_interface/gui/team_window/VariablesView.cpp
	user_interface/gui/team_settings_window/ExceptionStopConfigView.cpp
	user_interface/gui/team_settings_window/ImageStopConfigView.cpp
	user_interface/gui/team_settings_window/SignalsConfigView.cpp
	user_interface/gui/team_settings_window/TeamSettingsWindow.cpp
	user_interface/gui/util/ActionMenuItem.cpp
	user_interface/gui/util/AlertWithCheckbox.cpp
	user_interface/gui/util/GuiSettingsUtils.cpp
	user_interface/gui/util/SettingsMenu.cpp
	user_interface/gui/util/SignalDispositionMenu.cpp
	user_interface/gui/util/TargetAddressTableColumn.cpp
	user_interface/gui/utility_windows/BreakpointEditWindow.cpp
	user_interface/gui/utility_windows/ConnectionConfigWindow.cpp
	user_interface/gui/utility_windows/ExpressionPromptWindow.cpp
	user_interface/gui/utility_windows/SignalDispositionEditWindow.cpp
	user_interface/gui/utility_windows/StartTeamWindow.cpp
	user_interface/gui/utility_windows/VariableEditWindow.cpp
	user_interface/gui/utility_windows/WatchPromptWindow.cpp
	user_interface/gui/value/TableCellBoolEditor.cpp
	user_interface/gui/value/TableCellEnumerationEditor.cpp
	user_interface/gui/value/TableCellFloatEditor.cpp
	user_interface/gui/value/TableCellFormattedValueEditor.cpp
	user_interface/gui/value/TableCellFormattedValueRenderer.cpp
	user_interface/gui/value/TableCellIntegerEditor.cpp
	user_interface/gui/value/TableCellOptionPopUpEditor.cpp
	user_interface/gui/value/TableCellTextControlEditor.cpp
	user_interface/gui/value/TableCellValueEditor.cpp
	user_interface/gui/value/TableCellValueRenderer.cpp
	user_interface/gui/value/TableCellValueRendererUtils.cpp
	user_interface/gui/value/ValueHandler.cpp
	user_interface/gui/value/ValueHandlerRoster.cpp
	user_interface/gui/value/value_handlers/AddressValueHandler.cpp
	user_interface/gui/value/value_handlers/BoolValueHandler.cpp
	user_interface/gui/value/value_handlers/EnumerationValueHandler.cpp
	user_interface/gui/value/value_handlers/FloatValueHandler.cpp
	user_interface/gui/value/value_handlers/IntegerValueHandler.cpp
	user_interface/gui/value/value_handlers/StringValueHandler.cpp
	user_interface/report/ReportUserInterface.cpp

	$<TARGET_OBJECTS:DebugAnalyzer_gui_table>

	LIBS debugger debug bpackage tracker shared localestub
		${VOS_LIBEDIT_LIBRARIES}
	RDEF Debugger.rdef
)

# Keep the static helpers' symbols out of the executable's dynamic table,
# as the Jamfile did.
target_link_options(Debugger PRIVATE -Wl,--exclude-libs,ALL)
target_include_directories(Debugger PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/settings
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/cli
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/cli/commands
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/connection_config
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/connection_config/config_handlers
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/expression_eval_window
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/inspector_window
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/model
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/settings
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/team_window
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/team_settings_window
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/teams_window
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/utility_windows
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/util
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/value
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/gui/value/value_handlers
	${CMAKE_CURRENT_SOURCE_DIR}/user_interface/report
	${CMAKE_CURRENT_SOURCE_DIR}/util
	${PROJECT_SOURCE_DIR}/src/apps/debuganalyzer/gui
	${PROJECT_SOURCE_DIR}/headers/compatibility/bsd
	${VOS_LIBEDIT_INCLUDE_DIRS}
)
foreach(_dir
		"" arch controllers debug_info debug_managers elf files ids model
		settings settings/generic source_language source_language/c_family
		target_host_interface user_interface user_interface/util types util
		value value/value_formatters value/value_nodes value/values)
	target_include_directories(Debugger PRIVATE
		${PROJECT_SOURCE_DIR}/headers/private/debugger/${_dir})
endforeach()
UsePrivateHeaders(Debugger app interface shared system)
target_include_directories(Debugger PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/debug)
