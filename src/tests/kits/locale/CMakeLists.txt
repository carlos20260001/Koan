# ported subset: the cppunit suite (liblocaletest: Collator, DateFormat,
# DurationFormat, Language, NumberFormat, ... tests) needs cppunit, which
# this tree does not have yet. catalogSpeed, catalogTest, collatorSpeed and
# collatorTest use the pre-R1 locale API (be_locale, BCollatorAddOn,
# EditableCatalog) and do not compile against the current kit. ICUTest
# needs nexus.ko to run, so it is only built.

find_package(ICU REQUIRED COMPONENTS i18n uc)

UsePrivateHeaders(GLOBAL locale shared)

Test(ICUTest SOURCES ICUTest.cpp LIBS ICU::i18n ICU::uc)
# The system ICU headers need C++17, as for "be"; ICUTest names ICU classes
# without the icu:: prefix.
target_compile_options(ICUTest PRIVATE -std=c++17)
target_compile_definitions(ICUTest PRIVATE U_USING_ICU_NAMESPACE=1)
