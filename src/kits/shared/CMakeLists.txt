AddOn(
	shared
	STATIC
	SOURCES
	AboutMenuItem.cpp
	ArgumentVector.cpp
	AttributeUtilities.cpp
	# TODO: this should be a separate lib
	#ExpressionParser.cpp
	BarberPole.cpp
	BitmapButton.cpp
	CalendarView.cpp
	ColorItem.cpp
	ColorListView.cpp
	ColorPreview.cpp
	ColorQuantizer.cpp
	CommandPipe.cpp
	DateTimeEdit.cpp
	DragTrackingFilter.cpp
	DriverSettingsMessageAdapter.cpp
	EmojiTables.cpp
	#Geolocation.cpp
	HashString.cpp
	HSL.cpp
	IconButton.cpp
	IconView.cpp
	JsonWriter.cpp
	JsonEventListener.cpp
	JsonMessageWriter.cpp
	JsonTextWriter.cpp
	JsonEvent.cpp
	Json.cpp
	Keymap.cpp
	KeymapXkb.cpp
	LongAndDragTrackingFilter.cpp
	MemoryRingIO.cpp
	MessageBuilder.cpp
	NaturalCompare.cpp
	PromptWindow.cpp
	QueryFile.cpp
	RegExp.cpp
	BRWLocker.cpp
	# air/OS keeps RWLocker next to VitruvianOS's BRWLocker
	RWLocker.cpp
	RWLockManager.cpp
	SettingsHandler.cpp
	SettingsMessage.cpp
	StatusView.cpp
	ShakeTrackingFilter.cpp
	StringForRate.cpp
	StringForSize.cpp
	StripeView.cpp
	TemperatureSensors.cpp
	TextTable.cpp
	Thread.cpp
	Variant.cpp
	WeakReferenceable.cpp
	md5.cpp

	INCLUDES
	"${PROJECT_SOURCE_DIR}/src/add-ons/input_server/devices/keyboard"
	# QueryFile.cpp includes <tracker/MimeTypes.h>
	"${PROJECT_SOURCE_DIR}/src/kits"
)

UsePrivateHeaders(shared shared locale interface kernel libroot system app)
target_link_libraries(shared PRIVATE xkbcommon)

# ExpressionParser on MAPM, the separate static libexpression_parser.a of
# air/OS's Jamfile (DeskCalc, DiskProbe).
add_library(expression_parser STATIC ExpressionParser.cpp)
set_target_properties(expression_parser PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	COMPILE_FLAGS "-include LinuxBuildCompatibility.h")
target_include_directories(expression_parser
	PUBLIC ${PROJECT_SOURCE_DIR}/headers/private/shared
	PRIVATE ${PROJECT_SOURCE_DIR}/headers/libs/mapm)
target_link_libraries(expression_parser PUBLIC be mapm)
