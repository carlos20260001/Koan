/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2011, Oliver Tappe <zooey@hirschkaefer.de>
 * Distributed under the terms of the MIT License.
 */
#ifndef _SYSTEM_DIRECTORIES_H
#define _SYSTEM_DIRECTORIES_H


#define kGlobalBinDirectory 			"/bin"
#define kGlobalEtcDirectory 			"/etc"
#define kGlobalPackageLinksDirectory	"/packages"
#define kGlobalSystemDirectory 			"/system"
#define kGlobalTempDirectory 			"/tmp"
#define kGlobalVarDirectory 			"/var"

#ifndef __VOS__
#define kAppsDirectory 					"/boot/system/apps"
#define kPreferencesDirectory 			"/boot/system/preferences"
#else /* __VOS__ */
#define kAppsDirectory 					"/system/apps"
#define kPreferencesDirectory 			"/system/preferences"
#endif /* __VOS__ */

#define kAppLocalAddonsDirectory 		"%A/add-ons"
#define kAppLocalLibDirectory 			"%A/lib"

#define kVolumeLocalSystemKernelAddonsDirectory	"system/add-ons/kernel"
#define kVolumeLocalCommonNonpackagedKernelAddonsDirectory	\
	"system/non-packaged/add-ons/kernel"
#define kVolumeLocalCommonKernelAddonsDirectory	"system/add-ons/kernel"
#define kVolumeLocalUserNonpackagedKernelAddonsDirectory	\
	"home/config/non-packaged/add-ons/kernel"
#define kVolumeLocalUserKernelAddonsDirectory	"home/config/add-ons/kernel"

#ifndef __VOS__
#define kSystemDirectory 				"/boot/system"
#define kSystemAddonsDirectory 			"/boot/system/add-ons"
#define kSystemAppsDirectory 			"/boot/system/apps"
#define kSystemBinDirectory 			"/boot/system/bin"
#define kSystemDataDirectory 			"/boot/system/data"
#define kSystemDevelopDirectory 		"/boot/system/develop"
#define kSystemLibDirectory 			"/boot/system/lib"
#define kSystemPackagesDirectory 		"/boot/system/packages"
#define kSystemPackageLinksDirectory 		"/boot/system/package-links"
#define kSystemPreferencesDirectory 	"/boot/system/preferences"
#define kSystemServersDirectory 		"/boot/system/servers"
#define kSystemSettingsDirectory 		"/boot/system/settings"

#define kSystemEtcDirectory 			"/boot/system/settings/etc"
#define kSystemTempDirectory 			"/boot/system/cache/tmp"
#define kSystemVarDirectory 			"/boot/system/var"
#define kSystemLogDirectory 			"/boot/system/var/log"
#define kSystemNonpackagedDirectory		"/boot/system/non-packaged"
#define kSystemNonpackagedAddonsDirectory	"/boot/system/non-packaged/add-ons"
#define kSystemNonpackagedBinDirectory 	"/boot/system/non-packaged/bin"
#define kSystemNonpackagedLibDirectory 	"/boot/system/non-packaged/lib"

#define kUserDirectory 					"/boot/home"
#define kUserConfigDirectory 			"/boot/home/config"
#define kUserAddonsDirectory 			"/boot/home/config/add-ons"
#define kUserBinDirectory 				"/boot/home/config/bin"
#define kUserLibDirectory 				"/boot/home/config/lib"
#define kUserPackagesDirectory	 		"/boot/home/config/packages"
#define kUserSettingsDirectory 			"/boot/home/config/settings"
#define kUserSettingsGlobalDirectory 	"/boot/home/config/settings/global"
#define kUserNonpackagedDirectory		"/boot/home/config/non-packaged"
#define kUserNonpackagedAddonsDirectory "/boot/home/config/non-packaged/add-ons"
#define kUserNonpackagedBinDirectory 	"/boot/home/config/non-packaged/bin"
#define kUserNonpackagedLibDirectory 	"/boot/home/config/non-packaged/lib"
#else /* __VOS__ */
#define kSystemDirectory 				"/system"
#define kSystemAddonsDirectory 			"/system/add-ons"
#define kSystemAppsDirectory 			"/system/apps"
#define kSystemBinDirectory 			"/system/bin"
#define kSystemDataDirectory 			"/system/data"
#define kSystemDevelopDirectory 		"/system/develop"
#define kSystemLibDirectory 			"/system/lib"
#define kSystemPackagesDirectory 		"/system/packages"
#define kSystemPackageLinksDirectory 	"/system/package-links"
#define kSystemPreferencesDirectory 	"/system/preferences"
#define kSystemServersDirectory 		"/system/servers"
#define kSystemSettingsDirectory 		"/system/settings"

#define kSystemEtcDirectory 			"/system/settings/etc"
#define kSystemTempDirectory 			"/system/cache/tmp"
#define kSystemVarDirectory 			"/system/var"
#define kSystemLogDirectory 			"/system/var/log"
#define kSystemNonpackagedDirectory		"/system/non-packaged"
#define kSystemNonpackagedAddonsDirectory	"/system/non-packaged/add-ons"
#define kSystemNonpackagedBinDirectory 	"/system/non-packaged/bin"
#define kSystemNonpackagedLibDirectory 	"/system/non-packaged/lib"

#define kUserDirectory 					"/home"
#define kUserConfigDirectory 			"/home/config"
#define kUserAddonsDirectory 			"/home/config/add-ons"
#define kUserBinDirectory 				"/home/config/bin"
#define kUserLibDirectory 				"/home/config/lib"
#define kUserPackagesDirectory	 		"/home/config/packages"
#define kUserSettingsDirectory 			"/home/config/settings"
#define kUserSettingsGlobalDirectory 	"/home/config/settings/global"
#define kUserNonpackagedDirectory		"/home/config/non-packaged"
#define kUserNonpackagedAddonsDirectory "/home/config/non-packaged/add-ons"
#define kUserNonpackagedBinDirectory 	"/home/config/non-packaged/bin"
#define kUserNonpackagedLibDirectory 	"/home/config/non-packaged/lib"
#endif /* __VOS__ */


#endif	// _SYSTEM_DIRECTORIES_H
