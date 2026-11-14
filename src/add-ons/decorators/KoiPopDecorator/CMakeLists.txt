# The KoiPop decorator subclasses the tab strip decorator, which lives in
# another add-on: its source is built in here too, without that add-on's
# entry point. Add-ons are loaded with local symbol scope, so the two copies
# do not interfere.
AddOn(KoiPopDecorator SHARED
	SOURCES
		KoiPopDecorator.cpp
		../ModernTabDecorator/ModernTabDecorator.cpp
	LIBS be app_server
	RDEF resources.rdef
)
DecoratorAddOnSettings(KoiPopDecorator)
target_compile_definitions(KoiPopDecorator PRIVATE
	MODERN_TAB_DECORATOR_NO_ADDON_ENTRY)
target_include_directories(KoiPopDecorator PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../ModernTabDecorator)
