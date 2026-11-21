# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# MAPM arbitrary precision math, the static libmapm.a of air/OS's Jamfile.
# DeskCalc and DiskProbe link it through the shared kit's ExpressionParser.
add_library(mapm STATIC
	mapmhasn.c
	mapmhsin.c
	mapm_pow.c
	mapm_log.c
	mapm_lg2.c
	mapm_lg4.c
	mapm_exp.c
	mapm_lg3.c
	mapmasin.c
	mapmasn0.c
	mapm_sin.c
	mapm5sin.c
	mapmrsin.c
	mapm_cpi.c
	mapmsqrt.c
	mapmcbrt.c
	mapmgues.c
	mapmfact.c
	mapm_gcd.c
	mapmipwr.c
	mapmpwr2.c
	mapm_rnd.c
	mapm_flr.c
	mapm_fpf.c
	mapm_rcp.c
	mapmstck.c
	mapm_div.c
	mapm_mul.c
	mapmfmul.c
	mapm_fft.c
	mapm_add.c
	mapmistr.c
	mapm_set.c
	mapm_fam.c
	mapmutil.c
	mapmutl2.c
	mapmutl1.c
	mapmcnst.c
)
set_target_properties(mapm PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(mapm PUBLIC ${PROJECT_SOURCE_DIR}/headers/libs/mapm)
target_link_libraries(mapm PUBLIC m)
