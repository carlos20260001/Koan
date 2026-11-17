# Auckland Layout Model: a BLayout whose areas are placed by linear
# constraints solved with liblinprog.
AddOn(
	alm
	SHARED

	SOURCES
	ALMGroup.cpp
	ALMLayout.cpp
	ALMLayoutBuilder.cpp
	Area.cpp
	Column.cpp
	Row.cpp
	RowColumnManager.cpp
	SharedSolver.cpp
	Tab.cpp

	LIBS
	linprog
	be
	root

	INCLUDES
	"${PROJECT_SOURCE_DIR}/headers/libs/alm"
	"${PROJECT_SOURCE_DIR}/headers/libs/linprog"
	"${PROJECT_SOURCE_DIR}/src/kits/interface"
)

UsePrivateHeaders(alm shared)
