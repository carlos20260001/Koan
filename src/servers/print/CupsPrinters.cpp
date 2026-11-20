/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	print_server on CUPS.

	On air/OS a printer is a spool folder in ~/config/settings/printers whose
	attributes name its driver add-on and transport; the print_server keeps
	the list, the default printer, and has the driver add-ons render the
	spooled jobs. On VitruvianOS the printers are CUPS queues, and BPrintJob
	talks to CUPS itself through libprintcups. So that the print_server's
	API (the R5 messages, scripting, the printers folder that preferences
	and Tracker show) still describes the system's printers, every CUPS
	queue gets a spool folder with the driver "CUPS": the print_server
	syncs them with cupsd, keeps the CUPS default as its default, and hands
	jobs spooled to such a printer to CUPS (printcups_submit_spool renders
	the spool file to PDF).
*/


#include "PrintServerApp.h"

#include <stdio.h>
#include <string.h>

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <NodeInfo.h>
#include <Path.h>

#include <printcups.h>

#include "Printer.h"
#include "pr_server.h"


static const int kMaxQueues = 64;


/*!	Brings the printers folder in line with the CUPS queues: a folder for each
	queue, and none for queues that are gone. Printers of other drivers are
	left alone. */
void
PrintServerApp::SyncCupsPrinters()
{
	printcups_queue_info* queues = new(std::nothrow)
		printcups_queue_info[kMaxQueues];
	if (queues == NULL)
		return;
	int count = printcups_list_queues(queues, kMaxQueues);
	if (count < 0) {
		// cupsd is not there (yet): leave the folders as they are
		delete[] queues;
		return;
	}

	BPath path;
	if (find_directory(B_USER_PRINTERS_DIRECTORY, &path, true) != B_OK) {
		delete[] queues;
		return;
	}
	BDirectory printers(path.Path());

	// remove the folders of queues that are gone
	BEntry entry;
	while (printers.GetNextEntry(&entry) == B_OK) {
		BDirectory folder(&entry);
		BString driver;
		if (folder.InitCheck() != B_OK
			|| folder.ReadAttrString(PSRV_PRINTER_ATTR_DRV_NAME, &driver)
				!= B_OK
			|| driver != kCupsDriverName)
			continue;

		char name[B_FILE_NAME_LENGTH];
		entry.GetName(name);
		bool found = false;
		for (int i = 0; i < count && !found; i++)
			found = strcmp(queues[i].name, name) == 0;
		if (!found) {
			printf("print_server: CUPS queue \"%s\" is gone\n", name);
			// (only empty folders can be removed: spooled jobs keep it)
			entry.Remove();
		}
	}

	// add the new ones
	for (int i = 0; i < count; i++) {
		const printcups_queue_info& queue = queues[i];
		BDirectory folder;
		status_t status = printers.CreateDirectory(queue.name, &folder);
		if (status == B_FILE_EXISTS)
			continue;
		if (status != B_OK)
			continue;

		BNodeInfo(&folder).SetType(PSRV_PRINTER_FILETYPE);
		const char* connection = strncmp(queue.uri, "usb:", 4) == 0
			|| strncmp(queue.uri, "parallel:", 9) == 0 ? "Local" : "Network";
		BString value(queue.name);
		folder.WriteAttrString(PSRV_PRINTER_ATTR_PRT_NAME, &value);
		value = kCupsDriverName;
		folder.WriteAttrString(PSRV_PRINTER_ATTR_DRV_NAME, &value);
		folder.WriteAttrString(PSRV_PRINTER_ATTR_TRANSPORT, &value);
		value = queue.uri;
		folder.WriteAttrString(PSRV_PRINTER_ATTR_TRANSPORT_ADDR, &value);
		value = connection;
		folder.WriteAttrString(PSRV_PRINTER_ATTR_CNX, &value);
		value = queue.make_model;
		folder.WriteAttrString(PSRV_PRINTER_ATTR_COMMENTS, &value);
		value = "free";
		folder.WriteAttrString(PSRV_PRINTER_ATTR_STATE, &value);

		RegisterPrinter(&folder);
		printf("print_server: CUPS queue \"%s\" (%s)\n", queue.name,
			queue.uri);
	}

	delete[] queues;

	// CUPS's default printer, while the user chose none here
	if (fDefaultPrinter == NULL) {
		char name[256];
		if (printcups_default_queue(name, sizeof(name)) == B_OK
			&& name[0] != '\0' && Printer::Find(name) != NULL)
			SelectPrinter(name);
	}
}


// #pragma mark - Printer


/*!	Hands a job spooled to a CUPS printer to CUPS, with the settings the
	spool file carries. */
status_t
Printer::_PrintCupsJob(const char* spoolFile)
{
	BFile file(spoolFile, B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	print_file_header header;
	if (file.Read(&header, sizeof(header)) != (ssize_t)sizeof(header))
		return B_IO_ERROR;
	BMessage settings;
	status = settings.Unflatten(&file);
	if (status != B_OK)
		return status;

	settings.RemoveName(PSRV_FIELD_CURRENT_PRINTER);
	settings.AddString(PSRV_FIELD_CURRENT_PRINTER, Name());

	BString jobName;
	if (file.ReadAttrString(PSRV_SPOOL_ATTR_DESCRIPTION, &jobName) != B_OK)
		jobName = BPath(spoolFile).Leaf();

	int32 jobID;
	status = printcups_submit_spool(spoolFile, &settings, jobName.String(),
		&jobID);
	printf("print_server: \"%s\" to CUPS queue %s: %s (job %" B_PRId32 ")\n",
		jobName.String(), Name(), strerror(status), jobID);
	return status;
}


status_t
Printer::_ConfigureCups(BMessage& settings, bool page)
{
	settings.RemoveName(PSRV_FIELD_CURRENT_PRINTER);
	settings.AddString(PSRV_FIELD_CURRENT_PRINTER, Name());

	bool canceled = false;
	status_t status = page ? printcups_page_setup(&settings, &canceled)
		: printcups_job_setup(&settings, &canceled);
	if (status == B_OK && canceled)
		status = B_CANCELED;
	return status;
}
