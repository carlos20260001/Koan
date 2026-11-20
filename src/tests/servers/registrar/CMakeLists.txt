Test(RegistrarTest1 SOURCES RegistrarTest1.cpp)
Test(RosterShell SOURCES RosterShell.cpp)

# ported subset: the other test programs of the Jam build. All of them talk
# to a running registrar (nexus.ko); built, not registered with CTest.
Test(message_deliverer_test SOURCES message_deliverer_test.cpp)
Test(no_shutdown_reply SOURCES no_shutdown_reply.cpp)
Test(negative_shutdown_reply SOURCES negative_shutdown_reply.cpp)
Test(user_shutdown_reply SOURCES user_shutdown_reply.cpp)
