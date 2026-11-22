# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The target mail is the mail kit (libmail.so).
Application(mail_cli  SOURCES mail.cpp      LIBS mail     RDEF mail.rdef)
set_target_properties(mail_cli PROPERTIES OUTPUT_NAME mail)
Application(mail2mbox SOURCES mail2mbox.cpp               RDEF mail2mbox.rdef)
Application(mbox2mail SOURCES mbox2mail.cpp LIBS mail     RDEF mbox2mail.rdef)
Application(spamdbm   SOURCES spamdbm.cpp   LIBS tracker mail RDEF spamdbm.rdef)
foreach(_tool mail_cli mail2mbox mbox2mail spamdbm)
	target_include_directories(${_tool} PRIVATE
		${PROJECT_SOURCE_DIR}/headers/private/mail)
endforeach()
