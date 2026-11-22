
# Decodes through Debian's LibRaw, as on air/OS when its libraw build
# feature is enabled; the bundled dcraw backend (RAW.cpp) is the fallback
# there and is not built.
TranslatorAddOn(
	RAWTranslator SHARED

	SOURCES
	main.cpp
	RAWTranslator.cpp
	ConfigView.cpp
	LibRAW.cpp

	LIBS be translation translatorutils localestub ${RAW_LIBRARIES}

	INCLUDES
	"../shared"

	RDEF RAWTranslator.rdef
)
target_compile_definitions(RAWTranslator PRIVATE USES_LIBRAW)
DoCatalogs("x-vnd.vos-RAWTranslator" add-ons/translators/raw)
