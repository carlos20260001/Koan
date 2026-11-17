# air/OS's mail kit (libmail.so): BEmailMessage, the mail daemon's filter
# and protocol add-on interfaces and their settings views. VitruvianOS has
# no mail kit (its WebMail uses libetpan); this one serves air/OS's
# mail_daemon, its add-ons and Mail.
AddOn(
	mail
	SHARED

	SOURCES
	b_mail_message.cpp
	c_mail_api.cpp
	crypt.cpp
	FileConfigView.cpp
	HaikuMailFormatFilter.cpp
	mail_encoding.cpp
	mail_util.cpp
	MailAttachment.cpp
	MailComponent.cpp
	MailContainer.cpp
	MailDaemon.cpp
	MailFilter.cpp
	MailMessage.cpp
	MailProtocol.cpp
	MailSettings.cpp
	MailSettingsView.cpp
	NodeMessage.cpp
	numailkit.cpp
	ProtocolConfigView.cpp

	LIBS be root textencoding tracker shared localestub

	RDEF
	libmail.rdef
)

target_compile_definitions(mail PRIVATE _BUILDING_mail=1)
target_include_directories(mail PRIVATE
	${CMAKE_SOURCE_DIR}/headers/private/mail
	${CMAKE_SOURCE_DIR}/headers/os/add-ons/mail_daemon
)
UsePrivateHeaders(mail shared textencoding)
install(TARGETS mail DESTINATION /system/lib/)
