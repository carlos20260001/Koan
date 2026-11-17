target_sources(root PRIVATE
	send_signal.c
	stat.c
	user_group_common.cpp

	# Not in VitruvianOS: BPrivate::SHA256, which Haiku's libroot exported and
	# air/OS's package kit (ChecksumAccessors) and SystemUpdater use.
	crypt/SHA256.cpp
)

UsePrivateHeaders( root libroot )
