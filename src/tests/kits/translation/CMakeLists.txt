# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# ported subset: the cppunit suite (libtranslationtest: TranslatorRoster,
# BitmapStream, TranslationUtils, Translator tests) needs cppunit, which this
# tree does not have yet.

# Decodes the PNG, JPEG and GIF test images through BTranslatorRoster and the
# translators built in this tree. libroot needs nexus.ko, so CTest skips it
# when the Nexus devices are missing.
Test(TranslatorDecodeTest
	SOURCES TranslatorDecodeTest.cpp
	LIBS translation)

if(TARGET PNGTranslator AND TARGET JPEGTranslator AND TARGET GIFTranslator)
	add_test(NAME translation_decode_png_jpeg_gif
		COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run_translator_decode_test.sh
			$<TARGET_FILE:TranslatorDecodeTest>
			${CMAKE_CURRENT_SOURCE_DIR}/data
			$<TARGET_FILE:PNGTranslator>
			$<TARGET_FILE:JPEGTranslator>
			$<TARGET_FILE:GIFTranslator>)
	set_tests_properties(translation_decode_png_jpeg_gif PROPERTIES
		SKIP_RETURN_CODE 77)
endif()
