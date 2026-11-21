# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# DebugAnalyzer: reads a scheduling recording (scheduling_recorder's
# system-profiler event stream, read through libdebug's BDebugEventInputStream)
# and shows teams, threads, wait objects and a scheduling chart.
#
# Built only with the debug kit (VOS_WITH_DEBUG_KIT, src/kits/debug), which
# is OFF until it is ported to ptrace(2). The recordings themselves come
# from Haiku's kernel system profiler (start_system_profiler()).
# TODO(linux backend): a recorder that writes the same event stream from
# perf_event_open(2) sched tracepoints (sched_switch, sched_wakeup,
# sched_process_fork/exit) or from "perf sched record" output, so that
# Model/ModelLoader can stay as they are.
#
# The table widgets (gui/table) are also used by Debugger, which links the
# DebugAnalyzer_gui_table object library defined here.

if(NOT TARGET debug)
	message(STATUS "DebugAnalyzer: skipped, needs debug (VOS_WITH_DEBUG_KIT)")
	return()
endif()
foreach(_lib shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "DebugAnalyzer: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

set(_debug_analyzer_headers
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/gui
	${CMAKE_CURRENT_SOURCE_DIR}/model
	${CMAKE_CURRENT_SOURCE_DIR}/model_loader
	${CMAKE_CURRENT_SOURCE_DIR}/util
	${PROJECT_SOURCE_DIR}/headers/private/debug
)

# DebugAnalyzer_gui_table.o: shared with Debugger.
add_library(DebugAnalyzer_gui_table OBJECT
	gui/table/AbstractTable.cpp
	gui/table/Table.cpp
	gui/table/TableColumn.cpp
	gui/table/TableColumns.cpp
	gui/table/TreeTable.cpp
)
set_target_properties(DebugAnalyzer_gui_table PROPERTIES
	COMPILE_FLAGS "-include LinuxBuildCompatibility.h"
	POSITION_INDEPENDENT_CODE ON)
target_include_directories(DebugAnalyzer_gui_table PRIVATE
	${_debug_analyzer_headers}
)
UsePrivateHeaders(DebugAnalyzer_gui_table interface shared system)

Application(
	DebugAnalyzer

	SOURCES
	DebugAnalyzer.cpp

	# gui
	gui/AbstractGeneralPage.cpp
	gui/ColorCheckBox.cpp
	gui/HeaderView.cpp
	gui/ListSelectionModel.cpp
	gui/SubWindow.cpp
	gui/SubWindowManager.cpp

	# gui/chart
	gui/chart/BigtimeChartAxisLegendSource.cpp
	gui/chart/Chart.cpp
	gui/chart/ChartAxis.cpp
	gui/chart/ChartAxisLegendSource.cpp
	gui/chart/ChartDataSource.cpp
	gui/chart/ChartLegend.cpp
	gui/chart/ChartRenderer.cpp
	gui/chart/DefaultChartAxisLegendSource.cpp
	gui/chart/LegendChartAxis.cpp
	gui/chart/LineChartRenderer.cpp
	gui/chart/NanotimeChartAxisLegendSource.cpp
	gui/chart/StringChartLegend.cpp

	# gui/main_window
	gui/main_window/GeneralPage.cpp
	gui/main_window/MainWindow.cpp
	gui/main_window/SchedulingPage.cpp
	gui/main_window/TeamsPage.cpp
	gui/main_window/ThreadsPage.cpp
	gui/main_window/WaitObjectsPage.cpp

	# gui/thread_window
	gui/thread_window/ActivityPage.cpp
	gui/thread_window/GeneralPage.cpp
	gui/thread_window/ThreadWindow.cpp
	gui/thread_window/WaitObjectsPage.cpp

	# model
	model/Model.cpp
	model/ThreadModel.cpp

	# model_loader
	model_loader/AbstractModelLoader.cpp
	model_loader/ModelLoader.cpp
	model_loader/ThreadModelLoader.cpp

	# util
	util/DataSource.cpp

	$<TARGET_OBJECTS:DebugAnalyzer_gui_table>

	LIBS shared debug localestub
	RDEF DebugAnalyzer.rdef
)

target_include_directories(DebugAnalyzer PRIVATE
	${_debug_analyzer_headers}
	${PROJECT_SOURCE_DIR}/src/apps/debugger/util
)
UsePrivateHeaders(DebugAnalyzer interface shared system)
