# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Icon-O-Matic builds libicon's sources itself with ICON_O_MATIC=1, which
# turns on the editing side of the icon classes, as air/OS's Jamfile does.
foreach(_lib agg tracker shared translation localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "Icon-O-Matic: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	Icon-O-Matic

	SOURCES
	${PROJECT_SOURCE_DIR}/src/libs/icon/flat_icon/FlatIconFormat.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/flat_icon/FlatIconImporter.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/flat_icon/LittleEndianBuffer.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/flat_icon/PathCommandQueue.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/message/Defines.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/message/MessageImporter.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/shape/PathSourceShape.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/shape/ReferenceImage.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/shape/Shape.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/shape/VectorPath.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/style/GradientTransformable.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/style/Style.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/transformable/Transformable.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/transformer/AffineTransformer.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/transformer/CompoundStyleTransformer.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/transformer/ContourTransformer.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/transformer/PathSource.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/transformer/PerspectiveTransformer.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/transformer/StrokeTransformer.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/transformer/StyleTransformer.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/transformer/TransformerFactory.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/Icon.cpp
	${PROJECT_SOURCE_DIR}/src/libs/icon/IconRenderer.cpp
	document/Document.cpp
	document/IconObject.cpp
	document/SetPropertiesCommand.cpp
	document/savers/AttributeSaver.cpp
	document/savers/BitmapSetSaver.cpp
	document/savers/DocumentSaver.cpp
	document/savers/FileSaver.cpp
	document/savers/MessengerSaver.cpp
	document/savers/NativeSaver.cpp
	document/savers/SimpleFileSaver.cpp
	generic/command/Command.cpp
	generic/command/CommandStack.cpp
	generic/command/CompoundCommand.cpp
	generic/gui/Group.cpp
	generic/gui/InputTextView.cpp
	generic/gui/NummericalTextView.cpp
	generic/gui/StringTextView.cpp
	generic/gui/ListViews.cpp
	generic/gui/SwatchView.cpp
	generic/gui/panel/Panel.cpp
	generic/gui/panel/color_picker/AlphaSlider.cpp
	generic/gui/panel/color_picker/ColorField.cpp
	generic/gui/panel/color_picker/ColorPickerPanel.cpp
	generic/gui/panel/color_picker/ColorPickerView.cpp
	generic/gui/panel/color_picker/ColorPreview.cpp
	generic/gui/panel/color_picker/ColorSlider.cpp
	generic/gui/scrollview/Scrollable.cpp
	generic/gui/scrollview/Scroller.cpp
	generic/gui/scrollview/ScrollView.cpp
	generic/gui/stateview/Manipulator.cpp
	generic/gui/stateview/MultipleManipulatorState.cpp
	generic/gui/stateview/StateView.cpp
	generic/gui/stateview/ViewState.cpp
	generic/listener/Observable.cpp
	generic/listener/Observer.cpp
	generic/property/CommonPropertyIDs.cpp
	generic/property/Property.cpp
	generic/property/PropertyObject.cpp
	generic/property/specific_properties/ColorProperty.cpp
	generic/property/specific_properties/IconProperty.cpp
	generic/property/specific_properties/Int64Property.cpp
	generic/property/specific_properties/OptionProperty.cpp
	generic/property/view/PropertyEditorFactory.cpp
	generic/property/view/PropertyEditorView.cpp
	generic/property/view/PropertyItemView.cpp
	generic/property/view/PropertyListView.cpp
	generic/property/view/specific_properties/BoolValueView.cpp
	generic/property/view/specific_properties/ColorValueView.cpp
	generic/property/view/specific_properties/FloatValueView.cpp
	generic/property/view/specific_properties/IconValueView.cpp
	generic/property/view/specific_properties/Int64ValueView.cpp
	generic/property/view/specific_properties/IntValueView.cpp
	generic/property/view/specific_properties/OptionValueView.cpp
	generic/property/view/specific_properties/StringValueView.cpp
	generic/property/view/specific_properties/SwatchValueView.cpp
	generic/property/view/specific_properties/TextInputValueView.cpp
	generic/selection/Selectable.cpp
	generic/selection/Selection.cpp
	generic/support/support.cpp
	generic/support/support_ui.cpp
	generic/support/support_settings.cpp
	gui/GradientControl.cpp
	gui/IconObjectListView.cpp
	gui/PathListView.cpp
	gui/SavePanel.cpp
	gui/ShapeListView.cpp
	gui/StyleListView.cpp
	gui/StyleView.cpp
	gui/SwatchGroup.cpp
	gui/TransformerListView.cpp
	import_export/Exporter.cpp
	import_export/Importer.cpp
	import_export/bitmap/BitmapExporter.cpp
	import_export/flat_icon/FlatIconExporter.cpp
	import_export/flat_icon/RDefExporter.cpp
	import_export/flat_icon/SourceExporter.cpp
	import_export/message/MessageExporter.cpp
	import_export/styled_text/StyledTextImporter.cpp
	import_export/svg/DocumentBuilder.cpp
	import_export/svg/SVGExporter.cpp
	import_export/svg/SVGImporter.cpp
	shape/PathManipulator.cpp
	shape/commands/AddPathsCommand.cpp
	shape/commands/AddPointCommand.cpp
	shape/commands/AddShapesCommand.cpp
	shape/commands/AddTransformersCommand.cpp
	shape/commands/ChangePointCommand.cpp
	shape/commands/CleanUpPathCommand.cpp
	shape/commands/FlipPointsCommand.cpp
	shape/commands/FreezeTransformationCommand.cpp
	shape/commands/InsertPointCommand.cpp
	shape/commands/MoveShapesCommand.cpp
	shape/commands/MovePathsCommand.cpp
	shape/commands/MoveTransformersCommand.cpp
	shape/commands/NudgePointsCommand.cpp
	shape/commands/PathCommand.cpp
	shape/commands/RemovePathsCommand.cpp
	shape/commands/RemovePointsCommand.cpp
	shape/commands/RemoveShapesCommand.cpp
	shape/commands/RemoveTransformersCommand.cpp
	shape/commands/ReversePathCommand.cpp
	shape/commands/RotatePathIndicesCommand.cpp
	shape/commands/SplitPointsCommand.cpp
	shape/commands/TransformPointsCommand.cpp
	shape/commands/UnassignPathCommand.cpp
	style/AddStylesCommand.cpp
	style/AssignStyleCommand.cpp
	style/CurrentColor.cpp
	style/MoveStylesCommand.cpp
	style/RemoveStylesCommand.cpp
	style/SetColorCommand.cpp
	style/SetGradientCommand.cpp
	transformable/CanvasTransformBox.cpp
	transformable/ChannelTransform.cpp
	transformable/PerspectiveBox.cpp
	transformable/PerspectiveBoxStates.cpp
	transformable/PerspectiveCommand.cpp
	transformable/ResetTransformationCommand.cpp
	transformable/TransformBox.cpp
	transformable/TransformBoxStates.cpp
	transformable/TransformCommand.cpp
	transformable/TransformGradientBox.cpp
	transformable/TransformGradientCommand.cpp
	transformable/TransformObjectsCommand.cpp
	transformable/TransformPointsBox.cpp
	transformable/TransformShapesBox.cpp
	CanvasView.cpp
	IconEditorApp.cpp
	gui/IconView.cpp
	main.cpp
	MainWindow.cpp
	Util.cpp

	LIBS tracker translation shared agg localestub
	RDEF Icon-O-Matic.rdef
)

target_compile_definitions(Icon-O-Matic PRIVATE ICON_O_MATIC=1)
UsePrivateHeaders(Icon-O-Matic shared)
# The editor's and libicon's directories are quote-include directories, as
# Jam's SEARCH_SOURCE made them: "List.h", "Shape.h" and "ScrollView.h"
# are the editor's own, <List.h> and <Shape.h> the kits'. libicon's come
# first, in the order of air/OS's Jamfile.
foreach(_dir
		${PROJECT_SOURCE_DIR}/src/libs/icon
		${PROJECT_SOURCE_DIR}/src/libs/icon/flat_icon
		${PROJECT_SOURCE_DIR}/src/libs/icon/generic
		${PROJECT_SOURCE_DIR}/src/libs/icon/message
		${PROJECT_SOURCE_DIR}/src/libs/icon/shape
		${PROJECT_SOURCE_DIR}/src/libs/icon/style
		${PROJECT_SOURCE_DIR}/src/libs/icon/transformable
		${PROJECT_SOURCE_DIR}/src/libs/icon/transformer
		${CMAKE_CURRENT_SOURCE_DIR}/document
		${CMAKE_CURRENT_SOURCE_DIR}/document/savers
		${CMAKE_CURRENT_SOURCE_DIR}/generic
		${CMAKE_CURRENT_SOURCE_DIR}/generic/command
		${CMAKE_CURRENT_SOURCE_DIR}/generic/gui
		${CMAKE_CURRENT_SOURCE_DIR}/generic/gui/panel
		${CMAKE_CURRENT_SOURCE_DIR}/generic/gui/panel/color_picker
		${CMAKE_CURRENT_SOURCE_DIR}/generic/gui/popup_control
		${CMAKE_CURRENT_SOURCE_DIR}/generic/gui/scrollview
		${CMAKE_CURRENT_SOURCE_DIR}/generic/gui/stateview
		${CMAKE_CURRENT_SOURCE_DIR}/generic/listener
		${CMAKE_CURRENT_SOURCE_DIR}/generic/property
		${CMAKE_CURRENT_SOURCE_DIR}/generic/property/specific_properties
		${CMAKE_CURRENT_SOURCE_DIR}/generic/property/view
		${CMAKE_CURRENT_SOURCE_DIR}/generic/property/view/specific_properties
		${CMAKE_CURRENT_SOURCE_DIR}/generic/selection
		${CMAKE_CURRENT_SOURCE_DIR}/generic/support
		${CMAKE_CURRENT_SOURCE_DIR}/gui
		${CMAKE_CURRENT_SOURCE_DIR}/import_export
		${CMAKE_CURRENT_SOURCE_DIR}/import_export/bitmap
		${CMAKE_CURRENT_SOURCE_DIR}/import_export/flat_icon
		${CMAKE_CURRENT_SOURCE_DIR}/import_export/message
		${CMAKE_CURRENT_SOURCE_DIR}/import_export/svg
		${CMAKE_CURRENT_SOURCE_DIR}/import_export/styled_text
		${CMAKE_CURRENT_SOURCE_DIR}/shape
		${CMAKE_CURRENT_SOURCE_DIR}/shape/commands
		${CMAKE_CURRENT_SOURCE_DIR}/style
		${CMAKE_CURRENT_SOURCE_DIR}/transformable
		${CMAKE_CURRENT_SOURCE_DIR}/transformer)
	target_compile_options(Icon-O-Matic PRIVATE "-iquote${_dir}")
endforeach()
target_include_directories(Icon-O-Matic PRIVATE
	${PROJECT_SOURCE_DIR}/headers/libs/agg
	${PROJECT_SOURCE_DIR}/headers/libs/icon
)

DoCatalogs("x-vnd.vos-icon_o_matic" apps/icon-o-matic)
