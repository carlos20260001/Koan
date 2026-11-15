# ported subset: the be_bootstrap sources that root (libroot2) needs,
# BList, BLocker and BString (with BStringList, which BString::Split
# returns, and its base BFlattenable), plus the user_sem BLocker is built on.
# Upstream also adds the rest of the support kit to be_bootstrap and to "be".
target_sources(be_bootstrap PRIVATE
	List.cpp
	Locker.cpp
	UserSemaphore.cpp
	PointerList.cpp
	String.cpp
	StringList.cpp
	Flattenable.cpp
)

UsePrivateHeaders(be_bootstrap support)
