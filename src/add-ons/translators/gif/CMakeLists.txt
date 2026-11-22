
TranslatorAddOn(
	GIFTranslator SHARED

	SOURCES
	GIFLoad.cpp	
	GIFSave.cpp
	GIFTranslator.cpp
	GIFView.cpp
	LoadPalette.cpp
	SavePalette.cpp
	SFHash.cpp

	LIBS be translation translatorutils localestub

	INCLUDES
	"../shared"

	RDEF GIFTranslator.rdef
)
DoCatalogs("x-vnd.vos-GIFTranslator" add-ons/translators/gif)
