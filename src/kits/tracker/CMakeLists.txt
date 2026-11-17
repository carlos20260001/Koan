AddOn(
	tracker
	SHARED

	SOURCES
	AttributeStream.cpp
	AutoMounterSettings.cpp
	BackgroundImage.cpp
	BatchRenameWindow.cpp
	Bitmaps.cpp
	BreadcrumbView.cpp
	ContainerWindow.cpp
	CopyConflicts.cpp
	CountView.cpp
	DesktopPoseView.cpp
	DetailsPane.cpp
	DeskWindow.cpp
	DialogPane.cpp
	DirMenu.cpp
	DraggableContainerIcon.cpp
	EntryIterator.cpp
	FavoritesMenu.cpp
	FBCPadding.cpp
	FilePanel.cpp
	FilePanelPriv.cpp
	#FilePermissionsView.cpp
	FindPanel.cpp
	FolderSizes.cpp
	FolderStateStore.cpp
	FSClipboard.cpp
	FSUndoRedo.cpp
	FSUtils.cpp
	GroupedMenu.cpp
	IconCache.cpp
	IconMenuItem.cpp
	#InfoWindow.cpp
	LiveMenu.cpp
	MimeTypeList.cpp
	MiniMenuField.cpp
	Model.cpp
	MountMenu.cpp
	Navigator.cpp
	NavMenu.cpp
	NetworkSharesSettings.cpp
	NodePreloader.cpp
	NodeWalker.cpp
	OpenWithWindow.cpp
	OverrideAlert.cpp
	PaneSplitView.cpp
	PendingNodeMonitorCache.cpp
	Pose.cpp
	PoseList.cpp
	PoseView.cpp
	PoseViewScripting.cpp
	QuickLook.cpp
	QueryContainerWindow.cpp
	QueryPoseView.cpp
	RecentItems.cpp
	RegExp.cpp
	SelectionWindow.cpp
	Settings.cpp
	#SettingsHandler.cpp
	SettingsViews.cpp
	Shortcuts.cpp
	SidebarView.cpp
	#SlowContextPopup.cpp
	SlowMenu.cpp
	StatusWindow.cpp
	TabBarView.cpp
	Tags.cpp
	TaskLoop.cpp
	TemplatesMenu.cpp
	Tests.cpp
	TextWidget.cpp
	Thumbnails.cpp
	TitleView.cpp
	Tracker.cpp
	TrackerInitialState.cpp
	TrackerScripting.cpp
	TrackerSettings.cpp
	TrackerSettingsWindow.cpp
	TrackerString.cpp
	TrashInfo.cpp
	TrashWatcher.cpp
	Utilities.cpp
	ViewState.cpp
	DisksWindow.cpp
	VirtualDirectoryEntryList.cpp
	VirtualDirectoryManager.cpp
	VirtualDirectoryPoseView.cpp
	VirtualDirectoryWindow.cpp
	WidgetAttributeText.cpp

	infowindow/AttributesView.cpp
	infowindow/FilePermissionsView.cpp
	infowindow/GeneralInfoView.cpp
	infowindow/HeaderView.cpp
	infowindow/InfoWindow.cpp

	# TODO maybe this should be a static library
	../../servers/input/InputServerFilter.cpp
	../../add-ons/input_server/filters/shortcut_catcher/BitFieldTesters.cpp
	../../add-ons/input_server/filters/shortcut_catcher/CommandActuators.cpp
	../../add-ons/input_server/filters/shortcut_catcher/CommandExecutor.cpp
	../../add-ons/input_server/filters/shortcut_catcher/KeyCommandMap.cpp
	../../add-ons/input_server/filters/shortcut_catcher/KeyInfos.cpp
	../../add-ons/input_server/filters/shortcut_catcher/ParseCommandLine.cpp
	../../add-ons/input_server/filters/shortcut_catcher/ShortcutsServerFilter.cpp

	INCLUDES
	"infowindow"
	"../../../headers/os/add-ons/input_server/"
	"../../add-ons/input_server/filters/shortcut_catcher/"

	LIBS be localestub root shared game translation

	RDEF TrackerIcons.rdef libtracker.rdef
)

UsePrivateHeaders(tracker app interface shared tracker mount storage system support print)
# air/OS's Quick Look integration; UsePrivateHeaders has no "quicklook" entry.
target_include_directories(tracker PRIVATE
	"${PROJECT_SOURCE_DIR}/headers/private/quicklook")

target_compile_definitions(tracker PRIVATE
	_BUILDING_tracker=1
	_SUPPORTS_RESOURCES=1
)
DoCatalogs("x-vnd.vos-libtracker" kits/tracker)
