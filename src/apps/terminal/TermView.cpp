/*
 * Copyright 2001-2026, Haiku, Inc. All rights reserved.
 * Copyright 2003-2004 Kian Duffy, myob@users.sourceforge.net
 * Parts Copyright 1998-1999 Kazuho Okui and Takashi Murai.
 * All rights reserved. Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Stefano Ceccherini, stefano.ceccherini@gmail.com
 *		Kian Duffy, myob@users.sourceforge.net
 *		Y.Hayakawa, hida@sawada.riec.tohoku.ac.jp
 *		Jonathan Schleifer, js@webkeks.org
 *		Simon South, simon@simonsouth.net
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 *		Clemens Zeidler, haiku@Clemens-Zeidler.de
 *		Siarzhuk Zharski, zharik@gmx.li
 */


#include "TermView.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <termios.h>

#include <algorithm>
#include <new>

#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <Beep.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <ControlLook.h>
#include <Cursor.h>
#include <DataIO.h>
#include <Debug.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <IconUtils.h>
#include <Input.h>
#include <Locale.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Mime.h>
#include <Node.h>
#include <Notification.h>
#include <Notifications.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <PropertyInfo.h>
#include <Region.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <UTF8.h>
#include <Window.h>
#include <image.h>

#include "ActiveProcessInfo.h"
#include "Base64.h"
#include "TermDragAndDrop.h"
#include "TermFileTransfer.h"
#include "Colors.h"
#include "InlineInput.h"
#include "PrefHandler.h"
#include "Shell.h"
#include "ShellParameters.h"
#include "SoftwareRenderer.h"
#include "TermApp.h"
#include "TermConst.h"
#include "TerminalCharClassifier.h"
#include "TerminalGraphics.h"
#include "TermViewStates.h"


#define ROWS_DEFAULT 25
#define COLUMNS_DEFAULT 80


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Terminal TermView"

static property_info sPropList[] = {
	{ "encoding",
	{B_GET_PROPERTY, 0},
	{B_DIRECT_SPECIFIER, 0},
	"get terminal encoding"},
	{ "encoding",
	{B_SET_PROPERTY, 0},
	{B_DIRECT_SPECIFIER, 0},
	"set terminal encoding"},
	{ "tty",
	{B_GET_PROPERTY, 0},
	{B_DIRECT_SPECIFIER, 0},
	"get tty name."},
	{ "command",
	{B_EXECUTE_PROPERTY, 0},
	{B_DIRECT_SPECIFIER, 0},
	"execute command"},
	{ "renderer",
	{B_GET_PROPERTY, 0},
	{B_DIRECT_SPECIFIER, 0},
	"get how the terminal is drawn"},
	{ "contents",
	{B_GET_PROPERTY, 0},
	{B_DIRECT_SPECIFIER, 0},
	"get the text on the screen"},
	{ "snapshot",
	{B_EXECUTE_PROPERTY, 0},
	{B_DIRECT_SPECIFIER, 0},
	"save what the view shows, in frame buffer pixels, as a PNG (path)"},
	{ 0  },
};


static const uint32 kUpdateSigWinch = 'Rwin';
static const uint32 kBlinkCursor = 'BlCr';
static const uint32 kFrameNeeded = 'TFnd';
static const uint32 kRenderFrame = 'TRnd';
static const uint32 kEventsPending = 'TEvt';
static const uint32 kAnimate = 'TAni';
static const uint32 kClipboardReadAnswer = 'TCra';

// at most this many frames a second
static const bigtime_t kFrameInterval = 1000000 / 120;
static const bigtime_t kSynchronizedRetry = 20000;

// choosing between the GPU and the software renderer: large frames timed
// in a series, after some that are not
static const int32 kRendererWarmUpFrames = 4;
static const int32 kRendererSeriesFrames = 8;
static const bigtime_t kRendererRetryInterval = 60000000;

static const int32 kCursorBlinkIntervals = 3;
static const int32 kCursorVisibleIntervals = 2;
static const bigtime_t kCursorBlinkInterval = 500000;

static const rgb_color kBlackColor = { 0, 0, 0, 255 };
static const rgb_color kWhiteColor = { 255, 255, 255, 255 };

// secondary mouse button drop
const int32 kSecondaryMouseDropAction = 'SMDA';

enum {
	kInsert,
	kChangeDirectory,
	kLinkFiles,
	kMoveFiles,
	kCopyFiles
};


// what the emulator reports, handed to the window thread
enum {
	EVENT_TITLE,
	EVENT_BELL,
	EVENT_SET_CLIPBOARD,
	EVENT_REQUEST_CLIPBOARD,
	EVENT_CLIPBOARD_WRITE,
	EVENT_CLIPBOARD_READ,
	EVENT_NOTIFY,
	EVENT_CLOSE_NOTIFICATION,
	EVENT_POINTER_SHAPE,
	EVENT_COLORS,
	EVENT_WORKING_DIRECTORY,
	EVENT_MODES,
	EVENT_PROGRESS,
	EVENT_REMOTE_CONTROL,
	EVENT_DRAG_AND_DROP,
	EVENT_FILE_TRANSFER
};


struct TermView::Event {
	int32				type;
	std::string			text;
	std::string			data;
	std::vector<std::string> types;
	ClipboardData		clipboard;
	TermNotification	notification;
	bool				primary;
	int32				value1;
	int32				value2;

	Event(int32 type)
		:
		type(type),
		primary(false),
		value1(0),
		value2(0)
	{
	}
};


static inline uint32
rgb_value(rgb_color color)
{
	return ((uint32)color.red << 16) | ((uint32)color.green << 8)
		| color.blue;
}


static inline rgb_color
color_from_value(uint32 value)
{
	return make_color((value >> 16) & 0xff, (value >> 8) & 0xff,
		value & 0xff);
}


static void
append_utf8(BString& text, uint32 c)
{
	char buffer[5];
	int32 length = 0;
	if (c < 0x80) {
		buffer[length++] = c;
	} else if (c < 0x800) {
		buffer[length++] = 0xc0 | (c >> 6);
		buffer[length++] = 0x80 | (c & 0x3f);
	} else if (c < 0x10000) {
		buffer[length++] = 0xe0 | (c >> 12);
		buffer[length++] = 0x80 | ((c >> 6) & 0x3f);
		buffer[length++] = 0x80 | (c & 0x3f);
	} else {
		buffer[length++] = 0xf0 | (c >> 18);
		buffer[length++] = 0x80 | ((c >> 12) & 0x3f);
		buffer[length++] = 0x80 | ((c >> 6) & 0x3f);
		buffer[length++] = 0x80 | (c & 0x3f);
	}
	text.Append(buffer, length);
}


static uint32
first_code_point(const char* text, int32 length)
{
	if (length <= 0)
		return 0;
	const uint8* bytes = (const uint8*)text;
	uint32 c = bytes[0];
	int32 extra = 0;
	if (c >= 0xf0) {
		c &= 0x07;
		extra = 3;
	} else if (c >= 0xe0) {
		c &= 0x0f;
		extra = 2;
	} else if (c >= 0xc0) {
		c &= 0x1f;
		extra = 1;
	}
	for (int32 i = 1; i <= extra && i < length; i++)
		c = (c << 6) | (bytes[i] & 0x3f);
	return c;
}


// #pragma mark - keyboard tables


//! kitty's numbers for the keys Haiku has key codes for.
static uint32
functional_key_for_key_code(int32 key)
{
	switch (key) {
		case 0x01: return KEY_ESCAPE;
		case 0x0e: return KEY_PRINT_SCREEN;
		case 0x0f: return KEY_SCROLL_LOCK;
		case 0x10: return KEY_PAUSE;
		case 0x1e: return KEY_BACKSPACE;
		case 0x1f: return KEY_INSERT;
		case 0x20: return KEY_HOME;
		case 0x21: return KEY_PAGE_UP;
		case 0x22: return KEY_NUM_LOCK;
		case 0x26: return KEY_TAB;
		case 0x34: return KEY_DELETE;
		case 0x35: return KEY_END;
		case 0x36: return KEY_PAGE_DOWN;
		case 0x3b: return KEY_CAPS_LOCK;
		case 0x47: return KEY_ENTER;
		case 0x4b: return KEY_LEFT_SHIFT;
		case 0x56: return KEY_RIGHT_SHIFT;
		case 0x57: return KEY_UP;
		case 0x5c: return KEY_LEFT_CONTROL;
		case 0x5d: return KEY_LEFT_SUPER;
		case 0x5f: return KEY_RIGHT_SUPER;
		case 0x60: return KEY_RIGHT_CONTROL;
		case 0x61: return KEY_LEFT;
		case 0x62: return KEY_DOWN;
		case 0x63: return KEY_RIGHT;
		case 0x66: return KEY_LEFT_ALT;
		case 0x67: return KEY_RIGHT_ALT;
		case 0x68: return KEY_MENU;
	}
	if (key >= 0x02 && key <= 0x0d)
		return KEY_F1 + key - 0x02;
	return 0;
}


//! The keypad's keys, as kitty numbers them.
static uint32
keypad_key_for_key_code(int32 key)
{
	switch (key) {
		case 0x23: return KEY_KP_DIVIDE;
		case 0x24: return KEY_KP_MULTIPLY;
		case 0x25: return KEY_KP_SUBTRACT;
		case 0x37: return KEY_KP_0 + 7;
		case 0x38: return KEY_KP_0 + 8;
		case 0x39: return KEY_KP_0 + 9;
		case 0x3a: return KEY_KP_ADD;
		case 0x48: return KEY_KP_0 + 4;
		case 0x49: return KEY_KP_0 + 5;
		case 0x4a: return KEY_KP_0 + 6;
		case 0x58: return KEY_KP_0 + 1;
		case 0x59: return KEY_KP_0 + 2;
		case 0x5a: return KEY_KP_0 + 3;
		case 0x5b: return KEY_KP_ENTER;
		case 0x64: return KEY_KP_0;
		case 0x65: return KEY_KP_DECIMAL;
	}
	return 0;
}


//! What a keypad key does with Num Lock off.
static uint32
keypad_navigation_key(uint32 key)
{
	switch (key) {
		case KEY_KP_0 + 7: return KEY_KP_HOME;
		case KEY_KP_0 + 8: return KEY_KP_UP;
		case KEY_KP_0 + 9: return KEY_KP_PAGE_UP;
		case KEY_KP_0 + 4: return KEY_KP_LEFT;
		case KEY_KP_0 + 5: return KEY_KP_BEGIN;
		case KEY_KP_0 + 6: return KEY_KP_RIGHT;
		case KEY_KP_0 + 1: return KEY_KP_END;
		case KEY_KP_0 + 2: return KEY_KP_DOWN;
		case KEY_KP_0 + 3: return KEY_KP_PAGE_DOWN;
		case KEY_KP_0: return KEY_KP_INSERT;
		case KEY_KP_DECIMAL: return KEY_KP_DELETE;
	}
	return key;
}


//! The character of a key on a US (PC-101) keyboard.
static uint32
us_layout_key(int32 key)
{
	static const char kRow1[] = "`1234567890-=";		// 0x11 - 0x1d
	static const char kRow2[] = "qwertyuiop[]\\";		// 0x27 - 0x33
	static const char kRow3[] = "asdfghjkl;'";			// 0x3c - 0x46
	static const char kRow4[] = "zxcvbnm,./";			// 0x4c - 0x55
	if (key >= 0x11 && key <= 0x1d)
		return kRow1[key - 0x11];
	if (key >= 0x27 && key <= 0x33)
		return kRow2[key - 0x27];
	if (key >= 0x3c && key <= 0x46)
		return kRow3[key - 0x3c];
	if (key >= 0x4c && key <= 0x55)
		return kRow4[key - 0x4c];
	if (key == 0x5e)
		return ' ';
	return 0;
}


// #pragma mark - TermView


TermView::TermView(BRect frame, const ShellParameters& shellParameters,
	int32 historySize)
	:
	BView(frame, "termview", B_FOLLOW_ALL,
		B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fListener(NULL),
	fBuilder(&fGlyphs),
	fColumns(COLUMNS_DEFAULT),
	fRows(ROWS_DEFAULT),
	fEncoding(M_UTF8),
	fActive(false),
	fScrBufSize(historySize)
{
	status_t status = _InitObject(shellParameters);
	if (status != B_OK)
		throw status;
	SetTermSize(frame);
}


TermView::TermView(int rows, int columns,
	const ShellParameters& shellParameters, int32 historySize)
	:
	BView(BRect(0, 0, 0, 0), "termview", B_FOLLOW_ALL,
		B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fListener(NULL),
	fBuilder(&fGlyphs),
	fColumns(columns),
	fRows(rows),
	fEncoding(M_UTF8),
	fActive(false),
	fScrBufSize(historySize)
{
	status_t status = _InitObject(shellParameters);
	if (status != B_OK)
		throw status;

	ResizeToPreferred();
}


TermView::TermView(BMessage* archive)
	:
	BView(archive),
	fListener(NULL),
	fBuilder(&fGlyphs),
	fColumns(COLUMNS_DEFAULT),
	fRows(ROWS_DEFAULT),
	fEncoding(M_UTF8),
	fActive(false),
	fScrBufSize(1000)
{
	BRect frame = Bounds();

	if (archive->FindInt32("encoding", (int32*)&fEncoding) < B_OK)
		fEncoding = M_UTF8;
	if (archive->FindInt32("columns", (int32*)&fColumns) < B_OK)
		fColumns = COLUMNS_DEFAULT;
	if (archive->FindInt32("rows", (int32*)&fRows) < B_OK)
		fRows = ROWS_DEFAULT;

	int32 argc = 0;
	const char** argv = NULL;
	_GetArgumentsFromMessage(archive, argv, argc);

	status_t status = _InitObject(ShellParameters(argc, argv));
	delete[] argv;

	if (status != B_OK)
		throw status;

	bool useRect = false;
	if ((archive->FindBool("use_rect", &useRect) == B_OK) && useRect)
		SetTermSize(frame);
}


/*!	Initializes the object for further use.
	The members fRows, fColumns, fEncoding, and fScrBufSize must
	already be initialized; they are not touched by this method.
*/
status_t
TermView::_InitObject(const ShellParameters& shellParameters)
{
	SetFlags(Flags() | B_WILL_DRAW | B_FRAME_EVENTS
		| B_FULL_UPDATE_ON_RESIZE/* | B_INPUT_METHOD_AWARE*/);

	fShell = NULL;
	fTerminal = NULL;
	fReaderThread = -1;
	fReaderQuit = false;
	fWriterThread = -1;
	fWriterQuit = false;
	fWriteSemaphore = -1;
	fWriteQueueSize = 0;
	fFramePending = false;
	fConverterState = 0;
	fEventsPosted = false;
	fWinchRunner = NULL;
	fCursorBlinkRunner = NULL;
	fAutoScrollRunner = NULL;
	fResizeRunner = NULL;
	fFrameRunner = NULL;
	fAnimationRunner = NULL;
	fResizeView = NULL;
	fCharClassifier = NULL;
	fEmulateBold = false;
	fAllowBold = true;
	fFontWidth = 8;
	fFontHeight = 16;
	fScale = 1;
	fRenderer = NULL;
	fSoftwareRenderer = NULL;
	fGPURenderer = NULL;
	fChooseRenderer = true;
	fGPUStale = false;
	fWarmUpFrames = kRendererWarmUpFrames;
	fMeasuredFrames = 0;
	fCost[0] = fCost[1] = -1;
	fMeasured[0] = fMeasured[1] = 0;
	fBitmap = NULL;
	fLastFrameTime = 0;
	fFrameNeeded = false;
	fFrameResized = false;
	fResizeViewDisableCount = 0;
	static int32 sNextRemoteControlID = 1;
	fRemoteControlID = atomic_add(&sNextRemoteControlID, 1);
	fDragAndDrop = new(std::nothrow) TermDragAndDrop(this,
		[this](const std::string& data) { _Send(data); });
	fFileTransfer = new(std::nothrow) TermFileTransfer(this,
		[this](const std::string& data) { _Send(data); },
		[this]() { return _WriteBacklog(); });
	fDragGesture = false;
	fLastActivityTime = 0;
	fCursorState = 0;
	fCursorBlinking = false;
	fScrollBar = NULL;
	fInline = NULL;
	fTextForeColor = kBlackColor;
	fTextBackColor = kWhiteColor;
	fSelectForeColor = kWhiteColor;
	fSelectBackColor = kBlackColor;
	fCursorForeColor = fTextBackColor;
	fCursorBackColor = fTextForeColor;
	fScrollRows = 0;
	fLinesScrolled = 0;
	fHistorySize = 0;
	fAutoScrollSpeed = 0;
	fSelection.start = fSelection.end = TermPoint(0, 0);
	fHoverLink = false;
	fHoverCell = TermPoint(-1, -1);
	fHoverGeneration = 0;
	fActivatedTime = 0;
	fPrevPos = TermPoint(-1, - 1);
	fKeymap = NULL;
	fKeymapChars = NULL;
	fUseOptionAsMetaKey = false;
	fPressedModifierKeys = 0;
	fMouseButtons = 0;
	fReportedMouseButtons = 0;
	fModifiers = 0;
	fMouseClipboard = be_clipboard;
	fNotificationCounter = 0;
	fDefaultState = new(std::nothrow) DefaultState(this);
	fSelectState = new(std::nothrow) SelectState(this);
	fHyperLinkState = new(std::nothrow) HyperLinkState(this);
	fHyperLinkMenuState = new(std::nothrow) HyperLinkMenuState(this);
	fLinkClickState = new(std::nothrow) LinkClickState(this);
	fActiveState = NULL;

	fTerminal = new(std::nothrow) Terminal;
	if (fTerminal == NULL)
		return B_NO_MEMORY;
	status_t error = fTerminal->Init(fColumns, fRows, fScrBufSize);
	if (error != B_OK)
		return error;
	fTerminal->SetListener(this);
	fTerminal->SetDefaultAlternateScroll(PrefHandler::Default() == NULL
		|| PrefHandler::Default()->getBool(PREF_ALTERNATE_SCROLL));

	// TODO: Make the special word chars user-settable!
	fCharClassifier = new(std::nothrow) DefaultCharClassifier(
		kDefaultAdditionalWordCharacters);
	if (fCharClassifier == NULL)
		return B_NO_MEMORY;

	fSoftwareRenderer = new(std::nothrow) SoftwareRenderer;
	if (fSoftwareRenderer == NULL)
		return B_NO_MEMORY;
	fRenderer = fSoftwareRenderer;

	fShell = new (std::nothrow) Shell();
	if (fShell == NULL)
		return B_NO_MEMORY;

	SetTermFont(be_fixed_font);

	// the colors the terminal starts with; the window sets the user's
	SetTextColor(fTextForeColor, fTextBackColor);
	SetCursorColor(fCursorForeColor, fCursorBackColor);
	SetSelectColor(fSelectForeColor, fSelectBackColor);
	const rgb_color* palette = TermApp::DefaultPalette();
	for (int32 i = 0; i < 16; i++)
		fTerminal->SetDefaultColor(i, rgb_value(palette[i]));

	// set the shell parameters' encoding
	ShellParameters modifiedShellParameters(shellParameters);
	modifiedShellParameters.SetEncoding(fEncoding);

	error = _AttachShell(fShell, modifiedShellParameters);
	if (error < B_OK)
		return error;

	if (fDefaultState == NULL || fSelectState == NULL || fHyperLinkState == NULL
		|| fHyperLinkMenuState == NULL || fLinkClickState == NULL) {
		return B_NO_MEMORY;
	}

	SetLowColor(fTextBackColor);
	SetViewColor(B_TRANSPARENT_32_BIT);

	_NextState(fDefaultState);

	return B_OK;
}


TermView::~TermView()
{
	Shell* shell = _DetachShell();
		// _DetachShell sets fShell to NULL

	for (std::map<std::string, BBitmap*>::iterator it
			= fNotificationIcons.begin(); it != fNotificationIcons.end(); it++) {
		delete it->second;
	}
	delete fDragAndDrop;
	delete fFileTransfer;

	delete fDefaultState;
	delete fSelectState;
	delete fHyperLinkState;
	delete fHyperLinkMenuState;
	delete fLinkClickState;
	delete fFrameRunner;
	delete fAnimationRunner;
	delete fAutoScrollRunner;
	delete fCharClassifier;
	delete shell;

	for (size_t i = 0; i < fEvents.size(); i++)
		delete fEvents[i];

	// a broken renderer is left alone, its context may not survive being
	// destroyed
	if (fGPURenderer != NULL && !fGPURenderer->IsBroken())
		delete fGPURenderer;
	delete fSoftwareRenderer;
	delete fBitmap;
	delete fTerminal;
}


bool
TermView::IsShellBusy() const
{
	return fShell != NULL && fShell->HasActiveProcesses();
}


bool
TermView::GetActiveProcessInfo(ActiveProcessInfo& _info) const
{
	if (fShell == NULL) {
		_info.Unset();
		return false;
	}

	return fShell->GetActiveProcessInfo(_info);
}


bool
TermView::GetShellInfo(ShellInfo& _info) const
{
	if (fShell == NULL) {
		_info = ShellInfo();
		return false;
	}

	_info = fShell->Info();
	return true;
}


/* static */
BArchivable *
TermView::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "TermView")) {
		TermView *view = new (std::nothrow) TermView(data);
		return view;
	}

	return NULL;
}


status_t
TermView::Archive(BMessage* data, bool deep) const
{
	status_t status = BView::Archive(data, deep);
	if (status == B_OK)
		status = data->AddString("add_on", TERM_SIGNATURE);
	if (status == B_OK)
		status = data->AddInt32("encoding", (int32)fEncoding);
	if (status == B_OK)
		status = data->AddInt32("columns", (int32)fColumns);
	if (status == B_OK)
		status = data->AddInt32("rows", (int32)fRows);

	if (data->ReplaceString("class", "TermView") != B_OK)
		data->AddString("class", "TermView");

	return status;
}


const char*
TermView::RendererName() const
{
	return fRenderer != NULL ? fRenderer->Name() : "none";
}


void
TermView::NotificationActivated(const char* identifier, int32 button)
{
	std::string output;
	fTerminal->Lock();
	fTerminal->ReportNotificationActivated(identifier, button);
	fTerminal->TakeOutput(output);
	fTerminal->Unlock();
	_Send(output);
}


void
TermView::NotificationClosed(const char* identifier)
{
	std::string output;
	fTerminal->Lock();
	fTerminal->ReportNotificationClosed(identifier);
	fTerminal->TakeOutput(output);
	fTerminal->Unlock();
	_Send(output);
}


// #pragma mark - remote control


void
TermView::RemoteControlReply(const BString& json)
{
	std::string reply = "\x1bP@kitty-cmd";
	reply += json.String();
	reply += "\x1b\\";
	_Send(reply);
}


/*!	The rows of a command's output by the shell's marks (OSC 133): the last
	one, the last one with text, or the first on the screen. The terminal
	must be locked.
*/
static bool
command_output_rows(Terminal* terminal, const char* extent, int32& _first,
	int32& _last)
{
	int32 top = -terminal->HistorySize();
	int32 rows = terminal->Rows();
	bool firstOnScreen = strcmp(extent, "first_cmd_output_on_screen") == 0;
	bool nonEmpty = strcmp(extent, "last_non_empty_output") == 0;

	if (firstOnScreen) {
		for (int32 row = 0; row < rows; row++) {
			if ((terminal->LineFlags(row) & LINE_OUTPUT) == 0)
				continue;
			int32 end = row + 1;
			while (end < rows && (terminal->LineFlags(end) & LINE_PROMPT) == 0)
				end++;
			_first = row;
			_last = end - 1;
			return true;
		}
		return false;
	}

	// backwards from the last prompt
	int32 end = rows;
	for (int32 row = rows - 1; row >= top; row--) {
		uint16 flags = terminal->LineFlags(row);
		if ((flags & LINE_PROMPT) != 0 && (flags & LINE_OUTPUT) == 0)
			end = row;
		if ((flags & LINE_OUTPUT) == 0 || row >= end)
			continue;
		bool empty = true;
		for (int32 line = row; line < end && empty; line++)
			empty = terminal->LineLength(line) == 0;
		if (!nonEmpty || !empty) {
			_first = row;
			_last = end - 1;
			return true;
		}
		end = row;
	}
	return false;
}


static int
signal_for_name(const char* name)
{
	static const struct {
		const char*	name;
		int			number;
	} kSignals[] = {
		{ "SIGHUP", SIGHUP }, { "SIGINT", SIGINT }, { "SIGQUIT", SIGQUIT },
		{ "SIGKILL", SIGKILL }, { "SIGTERM", SIGTERM }, { "SIGUSR1", SIGUSR1 },
		{ "SIGUSR2", SIGUSR2 }, { "SIGSTOP", SIGSTOP }, { "SIGCONT", SIGCONT },
		{ "SIGTSTP", SIGTSTP }, { "SIGWINCH", SIGWINCH },
		{ "SIGALRM", SIGALRM }, { "SIGPIPE", SIGPIPE }
	};
	for (size_t i = 0; i < B_COUNT_OF(kSignals); i++) {
		if (strcasecmp(name, kSignals[i].name) == 0
			|| strcasecmp(name, kSignals[i].name + 3) == 0) {
			return kSignals[i].number;
		}
	}
	return atoi(name);
}


/*!	The commands of kitty's remote control that concern one terminal:
	send-text, get-text, scroll-window, set-colors, get-colors,
	set-window-title and signal-child. Returns false with \a _error set
	when the command cannot be carried out.
*/
bool
TermView::RemoteCommand(const char* command, const BMessage& payload,
	BString& _data, BString& _error)
{
	if (strcmp(command, "send-text") == 0) {
		BString data = payload.GetString("data", "");
		std::string bytes;
		if (data.StartsWith("text:"))
			bytes = data.String() + 5;
		else if (data.StartsWith("base64:")) {
			if (!base64_decode(data.String() + 7, data.Length() - 7, bytes)) {
				_error = "The data is not valid base64";
				return false;
			}
		} else {
			_error = "The data must start with text: or base64:";
			return false;
		}
		BString paste = payload.GetString("bracketed_paste", "disable");
		if (paste == "auto") {
			// as a paste would be: bracketed if the program asked for it
			std::string encoded;
			fTerminal->Lock();
			fTerminal->EncodePaste(bytes.data(), bytes.size(), encoded);
			fTerminal->Unlock();
			bytes = encoded;
		} else if (paste == "enable")
			bytes = "\x1b[200~" + bytes + "\x1b[201~";
		_Send(bytes);
		return true;
	}

	if (strcmp(command, "get-text") == 0) {
		BString extent = payload.GetString("extent", "screen");
		std::string text;
		fTerminal->Lock();
		int32 rows = fTerminal->Rows();
		int32 columns = fTerminal->Columns();
		TermPoint start(0, 0);
		TermPoint end(columns, rows - 1);
		bool found = true;
		if (extent == "all")
			start = TermPoint(0, -fTerminal->HistorySize());
		else if (extent == "selection") {
			found = !fSelection.IsEmpty();
			start = fSelection.start;
			end = fSelection.end;
		} else if (extent.EndsWith("_output")) {
			int32 first, last;
			found = command_output_rows(fTerminal, extent.String(), first,
				last);
			if (found) {
				start = TermPoint(0, first);
				end = TermPoint(columns, last);
			}
		}
		if (found)
			fTerminal->GetText(start, end, text);
		fTerminal->Unlock();
		_data = text.c_str();
		return true;
	}

	if (strcmp(command, "scroll-window") == 0) {
		BMessage amount;
		if (payload.FindMessage("amount", &amount) != B_OK) {
			_error = "No amount to scroll by";
			return false;
		}
		const char* keyword;
		if (amount.FindString("0", &keyword) == B_OK) {
			if (strcmp(keyword, "start") == 0) {
				fTerminal->Lock();
				int32 history = fTerminal->HistorySize();
				fTerminal->Unlock();
				_ScrollToRows(history);
			} else
				_ScrollToBottom();
			return true;
		}
		int32 count = (int32)amount.GetDouble("0", 0);
		BString unit = amount.GetString("1", "l");
		if (unit == "p")
			_ScrollToRows(fScrollRows - count * fRows);
		else if (unit == "r") {
			for (int32 i = 0; i < abs(count); i++)
				_ScrollToPrompt(count > 0);
		} else
			_ScrollToRows(fScrollRows - count);
		return true;
	}

	if (strcmp(command, "set-colors") == 0) {
		fTerminal->Lock();
		if (payload.GetBool("reset", false))
			fTerminal->RemoteResetColors();
		BMessage colors;
		if (payload.FindMessage("colors", &colors) == B_OK) {
			BString spec;
			char* name;
			type_code type;
			for (int32 i = 0; colors.GetInfo(B_ANY_TYPE, i, &name, &type)
					== B_OK; i++) {
				double value;
				const char* string;
				if (colors.FindDouble(name, &value) == B_OK) {
					spec << (spec.IsEmpty() ? "" : ";") << name << "=";
					spec << BString().SetToFormat("#%06x",
						(unsigned)value & 0xffffff);
				} else if (colors.FindString(name, &string) == B_OK)
					spec << (spec.IsEmpty() ? "" : ";") << name << "=" << string;
			}
			if (!spec.IsEmpty())
				fTerminal->RemoteSetColors(spec.String());
		}
		fTerminal->Unlock();
		return true;
	}

	if (strcmp(command, "get-colors") == 0) {
		static const struct {
			const char*	name;
			int32		index;
		} kColors[] = {
			{ "foreground", COLOR_FOREGROUND },
			{ "background", COLOR_BACKGROUND },
			{ "cursor", COLOR_CURSOR },
			{ "cursor_text", COLOR_CURSOR_TEXT },
			{ "selection_foreground", COLOR_SELECTION_FOREGROUND },
			{ "selection_background", COLOR_SELECTION_BACKGROUND }
		};
		fTerminal->Lock();
		for (size_t i = 0; i < B_COUNT_OF(kColors); i++) {
			uint32 color = fTerminal->Color(kColors[i].index);
			if (color == kColorUnset)
				continue;
			_data << BString().SetToFormat("%-24s #%06x\n", kColors[i].name,
				(unsigned)color);
		}
		for (int32 i = 0; i < 256; i++) {
			_data << BString().SetToFormat("color%-19d #%06x\n", (int)i,
				(unsigned)fTerminal->ResolveColor(palette_color(i), true));
		}
		fTerminal->Unlock();
		return true;
	}

	if (strcmp(command, "set-window-title") == 0) {
		fTerminal->Lock();
		fTerminal->RemoteSetTitle(payload.GetString("title", ""));
		fTerminal->Unlock();
		return true;
	}

	if (strcmp(command, "signal-child") == 0) {
		ActiveProcessInfo info;
		if (!GetActiveProcessInfo(info)) {
			_error = "No process to signal";
			return false;
		}
		pid_t group = getpgid(info.ID());
		BMessage signals;
		payload.FindMessage("signals", &signals);
		const char* name;
		for (int32 i = 0; signals.FindString(BString() << i, &name) == B_OK;
				i++) {
			int number = signal_for_name(name);
			if (number <= 0) {
				_error.SetToFormat("Unknown signal: %s", name);
				return false;
			}
			kill(group > 0 ? -group : info.ID(), number);
		}
		return true;
	}

	_error.SetToFormat("Not supported by Terminal: %s", command);
	return false;
}


// #pragma mark - notifications


//!	A picture made the size of an icon: averaged over the pixels it covers.
static BBitmap*
scale_icon(const BBitmap* source, BRect bounds)
{
	BBitmap rgba(source->Bounds(), 0, B_RGBA32);
	if (rgba.InitCheck() != B_OK || rgba.ImportBits(source) != B_OK)
		return NULL;
	BBitmap* icon = new(std::nothrow) BBitmap(bounds, 0, B_RGBA32);
	if (icon == NULL || icon->InitCheck() != B_OK) {
		delete icon;
		return NULL;
	}

	int32 sourceWidth = rgba.Bounds().IntegerWidth() + 1;
	int32 sourceHeight = rgba.Bounds().IntegerHeight() + 1;
	int32 width = bounds.IntegerWidth() + 1;
	int32 height = bounds.IntegerHeight() + 1;
	// keep the proportions, centered
	float scale = std::min((float)width / sourceWidth,
		(float)height / sourceHeight);
	int32 targetWidth = std::max((int32)1, (int32)(sourceWidth * scale));
	int32 targetHeight = std::max((int32)1, (int32)(sourceHeight * scale));
	int32 left = (width - targetWidth) / 2;
	int32 top = (height - targetHeight) / 2;

	memset(icon->Bits(), 0, icon->BitsLength());
	const uint8* bits = (const uint8*)rgba.Bits();
	int32 sourceBytesPerRow = rgba.BytesPerRow();
	for (int32 y = 0; y < targetHeight; y++) {
		int32 sourceTop = y * sourceHeight / targetHeight;
		int32 sourceBottom = std::max(sourceTop + 1,
			(y + 1) * sourceHeight / targetHeight);
		uint8* row = (uint8*)icon->Bits() + (top + y) * icon->BytesPerRow()
			+ left * 4;
		for (int32 x = 0; x < targetWidth; x++) {
			int32 sourceLeft = x * sourceWidth / targetWidth;
			int32 sourceRight = std::max(sourceLeft + 1,
				(x + 1) * sourceWidth / targetWidth);
			// premultiplied while summed, so that transparent pixels add
			// no color
			uint32 sum[4] = { 0, 0, 0, 0 };
			for (int32 sy = sourceTop; sy < sourceBottom; sy++) {
				const uint8* pixel = bits + sy * sourceBytesPerRow
					+ sourceLeft * 4;
				for (int32 sx = sourceLeft; sx < sourceRight; sx++) {
					for (int32 c = 0; c < 3; c++)
						sum[c] += pixel[c] * pixel[3];
					sum[3] += pixel[3];
					pixel += 4;
				}
			}
			uint32 count = (sourceBottom - sourceTop)
				* (sourceRight - sourceLeft);
			uint8* target = row + x * 4;
			for (int32 c = 0; c < 3; c++)
				target[c] = sum[3] > 0 ? sum[c] / sum[3] : 0;
			target[3] = sum[3] / count;
		}
	}
	return icon;
}


/*!	The icon of a notification: the first of its names that is one of
	kitty's (error, warning, info, question, help, the file manager, the
	system monitor, the text editor) or an application signature, or else
	the picture the program sent, kept by its key for later notifications.
*/
BBitmap*
TermView::_NotificationIcon(const TermNotification& notification)
{
	static const struct {
		const char*	name;
		const char*	systemIcon;
		const char*	signature;
	} kIcons[] = {
		{ "error", "dialog-error", NULL },
		{ "warn", "dialog-warning", NULL },
		{ "warning", "dialog-warning", NULL },
		{ "info", "dialog-information", NULL },
		{ "question", "dialog-idea", NULL },
		{ "help", "dialog-idea", NULL },
		{ "file-manager", NULL, "application/x-vnd.Be-TRAK" },
		{ "system-monitor", NULL, "application/x-vnd.vos-ActivityMonitor" },
		{ "text-editor", NULL, "application/x-vnd.vos-StyledEdit" }
	};

	BRect bounds(BPoint(0, 0), be_control_look->ComposeIconSize(32));
	for (size_t i = 0; i < notification.iconNames.size(); i++) {
		const char* name = notification.iconNames[i].c_str();
		const char* systemIcon = NULL;
		const char* signature = strchr(name, '/') != NULL ? name : NULL;
		for (size_t j = 0; j < B_COUNT_OF(kIcons); j++) {
			if (strcmp(name, kIcons[j].name) == 0) {
				systemIcon = kIcons[j].systemIcon;
				signature = kIcons[j].signature;
				break;
			}
		}

		BBitmap* icon = new(std::nothrow) BBitmap(bounds, 0, B_RGBA32);
		if (icon == NULL || icon->InitCheck() != B_OK) {
			delete icon;
			return NULL;
		}
		if (systemIcon != NULL
			&& BIconUtils::GetSystemIcon(systemIcon, icon) == B_OK) {
			return icon;
		}
		uint8* data;
		size_t size;
		if (signature != NULL
			&& BMimeType(signature).GetIcon(&data, &size) == B_OK) {
			status_t status = BIconUtils::GetVectorIcon(data, size, icon);
			delete[] data;
			if (status == B_OK)
				return icon;
		}
		delete icon;
	}

	if (!notification.iconData.empty()) {
		BMemoryIO stream(notification.iconData.data(),
			notification.iconData.size());
		BBitmap* picture = BTranslationUtils::GetBitmap(&stream);
		if (picture == NULL)
			return NULL;
		BBitmap* icon = scale_icon(picture, bounds);
		delete picture;
		if (icon != NULL && !notification.iconKey.empty()) {
			// a few of them, for the notifications that only name them
			if (fNotificationIcons.size() >= 16) {
				delete fNotificationIcons.begin()->second;
				fNotificationIcons.erase(fNotificationIcons.begin());
			}
			BBitmap*& cached = fNotificationIcons[notification.iconKey];
			delete cached;
			cached = new(std::nothrow) BBitmap(icon);
		}
		return icon;
	}

	if (!notification.iconKey.empty()) {
		std::map<std::string, BBitmap*>::iterator found
			= fNotificationIcons.find(notification.iconKey);
		if (found != fNotificationIcons.end() && found->second != NULL)
			return new(std::nothrow) BBitmap(found->second);
	}
	return NULL;
}


// #pragma mark - geometry


float
TermView::_DeviceScale() const
{
	BScreen screen(Window());
	float scale = screen.DeviceScale();
	return scale >= 1 ? scale : 1;
}


int32
TermView::_FirstRow() const
{
	return -fScrollRows;
}


// convert view coordinates to terminal text buffer position
TermPoint
TermView::_ConvertToTerminal(const BPoint &p) const
{
	int32 column = p.x >= 0 ? (int32)(p.x / fFontWidth) : -1;
	int32 row = (int32)floorf(p.y / fFontHeight);
	return TermPoint(column, row + _FirstRow());
}


// convert terminal text buffer position to view coordinates
BPoint
TermView::_ConvertFromTerminal(const TermPoint &pos) const
{
	return BPoint(fFontWidth * pos.x, (pos.y - _FirstRow()) * fFontHeight);
}


void
TermView::_PixelPosition(const BPoint& point, int32& x, int32& y) const
{
	x = (int32)(point.x * fScale);
	y = (int32)(point.y * fScale);
}


void
TermView::GetPreferredSize(float *width, float *height)
{
	if (width)
		*width = fColumns * fFontWidth - 1;
	if (height)
		*height = fRows * fFontHeight - 1;
}


const char *
TermView::TerminalName() const
{
	if (fShell == NULL)
		return NULL;

	return fShell->TTYName();
}


//! Get width and height for terminal font
void
TermView::GetFontSize(float* _width, float* _height) const
{
	*_width = fFontWidth;
	*_height = fFontHeight;
}


int
TermView::Rows() const
{
	return fRows;
}


int
TermView::Columns() const
{
	return fColumns;
}


//! Set number of rows and columns in terminal
BRect
TermView::SetTermSize(int rows, int columns, bool notifyShell)
{
	// if nothing changed, don't do anything
	if (rows == fRows && columns == fColumns)
		return BRect(0, 0, fColumns * fFontWidth, fRows * fFontHeight);

	if (rows > 0)
		fRows = rows;
	if (columns > 0)
		fColumns = columns;

	// To keep things simple, get rid of the selection first.
	_Deselect();

	fTerminal->Lock();
	fTerminal->Resize(fColumns, fRows);
	std::string output;
	fTerminal->TakeOutput(output);
	fScrollRows = std::min(fScrollRows, fTerminal->HistorySize());
	fLinesScrolled = fTerminal->LinesScrolled();
	fTerminal->Unlock();
	_Send(output);

	_UpdateScrollBarRange();
	if (fScrollBar != NULL)
		fScrollBar->SetSteps(fFontHeight, fFontHeight * fRows);

	if (notifyShell)
		fFrameResized = true;

	_InvalidateAll();

	return BRect(0, 0, fColumns * fFontWidth, fRows * fFontHeight);
}


void
TermView::SetTermSize(BRect rect, bool notifyShell)
{
	int rows;
	int columns;

	GetTermSizeFromRect(rect, &rows, &columns);
	SetTermSize(rows, columns, notifyShell);
}


void
TermView::GetTermSizeFromRect(const BRect &rect, int *_rows,
	int *_columns) const
{
	int columns = int((rect.IntegerWidth() + 1) / fFontWidth);
	int rows = int((rect.IntegerHeight() + 1) / fFontHeight);

	if (_rows)
		*_rows = rows;
	if (_columns)
		*_columns = columns;
}


void
TermView::GetFrameSize(float *width, float *height) const
{
	int32 historySize = fTerminal->HistorySize();

	if (width != NULL)
		*width = fColumns * fFontWidth;

	if (height != NULL)
		*height = (fRows + historySize) * fFontHeight;
}


// #pragma mark - colors


void
TermView::SetTextColor(rgb_color fore, rgb_color back)
{
	fTextBackColor = back;
	fTextForeColor = fore;

	SetLowColor(fTextBackColor);

	fTerminal->Lock();
	fTerminal->SetDefaultColor(COLOR_FOREGROUND, rgb_value(fore));
	fTerminal->SetDefaultColor(COLOR_BACKGROUND, rgb_value(back));
	fTerminal->SetColorScheme(back.Brightness() < 128);
	std::string output;
	fTerminal->TakeOutput(output);
	fTerminal->Unlock();
	_Send(output);
	_InvalidateAll();
}


void
TermView::SetCursorColor(rgb_color fore, rgb_color back)
{
	fCursorForeColor = fore;
	fCursorBackColor = back;

	fTerminal->Lock();
	fTerminal->SetDefaultColor(COLOR_CURSOR, rgb_value(back));
	fTerminal->SetDefaultColor(COLOR_CURSOR_TEXT, rgb_value(fore));
	fTerminal->Unlock();
	_InvalidateAll();
}


void
TermView::SetSelectColor(rgb_color fore, rgb_color back)
{
	fSelectForeColor = fore;
	fSelectBackColor = back;

	fTerminal->Lock();
	fTerminal->SetDefaultColor(COLOR_SELECTION_FOREGROUND, rgb_value(fore));
	fTerminal->SetDefaultColor(COLOR_SELECTION_BACKGROUND, rgb_value(back));
	fTerminal->Unlock();
	_InvalidateAll();
}


void
TermView::SetTermColor(uint index, rgb_color color, bool dynamic)
{
	fTerminal->Lock();
	if (!dynamic) {
		if (index < kTermColorCount)
			fTerminal->SetDefaultColor(index, rgb_value(color));
	} else {
		switch (index) {
			case 10:
				fTerminal->SetDefaultColor(COLOR_FOREGROUND,
					rgb_value(color));
				break;
			case 11:
				fTerminal->SetDefaultColor(COLOR_BACKGROUND,
					rgb_value(color));
				break;
			case 12:
				fTerminal->SetDefaultColor(COLOR_CURSOR, rgb_value(color));
				break;
		}
	}
	fTerminal->Unlock();
	_InvalidateAll();
}


status_t
TermView::GetTermColor(uint index, rgb_color* color) const
{
	if (color == NULL)
		return B_BAD_VALUE;

	int32 which;
	switch (index) {
		case 10:
			which = COLOR_FOREGROUND;
			break;
		case 11:
			which = COLOR_BACKGROUND;
			break;
		case 12:
			which = COLOR_CURSOR;
			break;
		default:
			if (index >= 256)
				return B_BAD_VALUE;
			which = index;
			break;
	}

	fTerminal->Lock();
	uint32 value = fTerminal->Color(which);
	fTerminal->Unlock();
	if (value == kColorUnset)
		return B_BAD_VALUE;
	*color = color_from_value(value);
	return B_OK;
}


int
TermView::Encoding() const
{
	return fEncoding;
}


void
TermView::SetEncoding(int encoding)
{
	fEncoding = encoding;
	fConverterState = 0;

	if (fShell != NULL)
		fShell->SetEncoding(fEncoding);
}


void
TermView::SetKeymap(const key_map* keymap, const char* chars)
{
	fKeymap = keymap;
	fKeymapChars = chars;

	fKeymapTableForModifiers.Put(B_SHIFT_KEY,
		&fKeymap->shift_map);
	fKeymapTableForModifiers.Put(B_CAPS_LOCK,
		&fKeymap->caps_map);
	fKeymapTableForModifiers.Put(B_CAPS_LOCK | B_SHIFT_KEY,
		&fKeymap->caps_shift_map);
	fKeymapTableForModifiers.Put(B_CONTROL_KEY,
		&fKeymap->control_map);
}


void
TermView::SetUseOptionAsMetaKey(bool enable)
{
	fUseOptionAsMetaKey = enable && fKeymap != NULL && fKeymapChars != NULL;
}


void
TermView::SetMouseClipboard(BClipboard *clipboard)
{
	fMouseClipboard = clipboard;
}


void
TermView::GetTermFont(BFont *font) const
{
	if (font != NULL)
		*font = fFont;
}


//! Sets font for terminal
void
TermView::SetTermFont(const BFont *font)
{
	fFont = *font;
	fFont.SetSpacing(B_FIXED_SPACING);

	fEmulateBold = PrefHandler::Default() == NULL ? false
		: PrefHandler::Default()->getBool(PREF_EMULATE_BOLD);
	fAllowBold = PrefHandler::Default() == NULL ? true
		: PrefHandler::Default()->getBool(PREF_ALLOW_BOLD);

	// kitty's disable_ligatures: never (the default), cursor or always
	const char* ligatures = PrefHandler::Default() == NULL ? NULL
		: PrefHandler::Default()->getString(PREF_DISABLE_LIGATURES);
	fGlyphs.SetLigatureMode(ligatures != NULL
		&& strcmp(ligatures, "cursor") == 0 ? LIGATURES_NOT_AT_CURSOR
		: ligatures != NULL && strcmp(ligatures, "always") == 0
			? LIGATURES_NEVER : LIGATURES_ALWAYS);

	_UpdateMetrics();

	int cursorStyle = PrefHandler::Default() == NULL ? BLOCK_CURSOR
		: PrefHandler::Default()->getCursor(PREF_CURSOR_STYLE);
	bool blinking = PrefHandler::Default() == NULL ? true
		: PrefHandler::Default()->getBool(PREF_BLINK_CURSOR);
	int32 shape = cursorStyle == UNDERLINE_CURSOR ? CURSOR_SHAPE_UNDERLINE
		: cursorStyle == IBEAM_CURSOR ? CURSOR_SHAPE_BEAM : CURSOR_SHAPE_BLOCK;
	fTerminal->Lock();
	fTerminal->SetDefaultCursorShape(shape, blinking);
	fTerminal->Unlock();
	SwitchCursorBlinking(blinking);

	if (fScrollBar != NULL)
		fScrollBar->SetSteps(fFontHeight, fFontHeight * fRows);
}


/*!	The cell size follows the font and the frame buffer's density: glyphs
	are drawn in frame buffer pixels.
*/
void
TermView::_UpdateMetrics()
{
	fScale = Window() != NULL ? _DeviceScale() : fScale;
	fGlyphs.SetFont(fFont, fScale, fAllowBold, fEmulateBold);
	fFontWidth = fGlyphs.CellWidth();
	fFontHeight = fGlyphs.CellHeight();

	const RenderMetrics& metrics = fGlyphs.Metrics();
	fTerminal->Lock();
	fTerminal->SetCellSize(metrics.cellWidth, metrics.cellHeight);
	fTerminal->Unlock();

	fBuilder.Invalidate();
	_ResizeBitmap();
}


void
TermView::_ResizeBitmap()
{
	if (Window() == NULL)
		return;

	BRect bounds = Bounds();
	int32 width = std::max((int32)1,
		(int32)ceilf((bounds.Width() + 1) * fScale));
	int32 height = std::max((int32)1,
		(int32)ceilf((bounds.Height() + 1) * fScale));

	if (fBitmap != NULL && fBitmap->Bounds().IntegerWidth() + 1 == width
		&& fBitmap->Bounds().IntegerHeight() + 1 == height) {
		return;
	}

	delete fBitmap;
	fBitmap = new(std::nothrow) BBitmap(BRect(0, 0, width - 1, height - 1),
		B_RGB32);
	if (fBitmap != NULL && fBitmap->InitCheck() != B_OK) {
		delete fBitmap;
		fBitmap = NULL;
	}
	fBuilder.Invalidate();
	_ScheduleFrame();
}


// #pragma mark - shell and reader


void
TermView::_GetArgumentsFromMessage(const BMessage* message, const char**& argv, int32& argc)
{
	type_code type;
	if (message->GetInfo("argv", &type, &argc) == B_OK) {
		argv = new const char*[argc + 1];
		int32 i = 0;
		while (message->FindString("argv", i, &argv[i]) == B_OK)
			i++;
		argv[i] = NULL;
	}
}


status_t
TermView::_AttachShell(Shell *shell, const ShellParameters& shellParameters)
{
	if (shell == NULL)
		return B_BAD_VALUE;

	status_t status = shell->Open(fRows, fColumns, shellParameters);
	if (status != B_OK)
		return status;

	fShell = shell;
	return _StartReader();
}


Shell*
TermView::_DetachShell()
{
	Shell* shell = fShell;
	_StopReader();
	fShell = NULL;
	return shell;
}


status_t
TermView::_StartReader()
{
	fWriterQuit = false;
	fWriteSemaphore = create_sem(0, "pty writer");
	if (fWriteSemaphore < 0)
		return fWriteSemaphore;
	fWriterThread = spawn_thread(&_WriterThread, "pty writer",
		B_NORMAL_PRIORITY, this);
	if (fWriterThread < 0)
		return fWriterThread;
	resume_thread(fWriterThread);

	fReaderQuit = false;
	fReaderThread = spawn_thread(&_ReaderThread, "pty reader",
		B_NORMAL_PRIORITY, this);
	if (fReaderThread < 0)
		return fReaderThread;
	return resume_thread(fReaderThread);
}


void
TermView::_StopReader()
{
	status_t result;
	if (fWriterThread >= 0) {
		fWriterQuit = true;
		delete_sem(fWriteSemaphore);
		fWriteSemaphore = -1;
		// interrupt a write
		suspend_thread(fWriterThread);
		wait_for_thread(fWriterThread, &result);
		fWriterThread = -1;
		fWriteLock.Lock();
		fWriteQueue.clear();
		fWriteQueueSize = 0;
		fWriteLock.Unlock();
	}

	if (fReaderThread < 0)
		return;

	fReaderQuit = true;
	// interrupt the read
	suspend_thread(fReaderThread);
	wait_for_thread(fReaderThread, &result);
	fReaderThread = -1;
}


/*static*/ status_t
TermView::_ReaderThread(void* data)
{
	((TermView*)data)->_ReadLoop();
	return B_OK;
}


/*!	Reads what the shell writes, and has the terminal interpret it. Answers
	to queries go back to the shell at once, so that a program waiting for
	one (and blocking on it) is not held up by drawing.
*/
void
TermView::_ReadLoop()
{
	static const size_t kBufferSize = 64 * 1024;
	char* buffer = new(std::nothrow) char[kBufferSize];
	char* converted = new(std::nothrow) char[kBufferSize * 4];
	if (buffer == NULL || converted == NULL) {
		delete[] buffer;
		delete[] converted;
		return;
	}

	int32 reason = B_OK;
	std::string output;
	while (!fReaderQuit) {
		ssize_t bytesRead = fShell->Read(buffer, kBufferSize);
		if (fReaderQuit)
			break;
		if (bytesRead <= 0) {
			if (bytesRead < 0 && (errno == B_INTERRUPTED || errno == EINTR))
				continue;
			reason = bytesRead < 0 ? errno : B_OK;
			break;
		}

		const char* data = buffer;
		size_t length = bytesRead;
		if (fEncoding != M_UTF8) {
			int32 sourceLength = bytesRead;
			int32 destinationLength = kBufferSize * 4;
			if (convert_to_utf8(fEncoding, buffer, &sourceLength, converted,
					&destinationLength, &fConverterState) == B_OK) {
				data = converted;
				length = destinationLength;
			}
		}

		fTerminal->Lock();
		fTerminal->Feed((const uint8*)data, length);
		output.clear();
		fTerminal->TakeOutput(output);
		fTerminal->Unlock();

		if (!output.empty())
			_Send(output);
	}

	delete[] buffer;
	delete[] converted;

	if (!fReaderQuit) {
		BMessage message(MSG_QUIT_TERMNAL);
		message.AddInt32("reason", reason);
		BMessenger(this).SendMessage(&message);
	}
}


// #pragma mark - Terminal::Listener (reader thread, terminal locked)


void
TermView::_PostEvent(Event* event)
{
	fEvents.push_back(event);
	if (!fEventsPosted) {
		fEventsPosted = true;
		BMessage message(kEventsPending);
		if (BMessenger(this).SendMessage(&message, (BHandler*)NULL, 0)
				!= B_OK) {
			fEventsPosted = false;
		}
	}
}


void
TermView::TerminalContentsChanged()
{
	if (!fFramePending.exchange(true)) {
		BMessage message(kFrameNeeded);
		if (BMessenger(this).SendMessage(&message, (BHandler*)NULL, 0)
				!= B_OK) {
			fFramePending = false;
		}
	}
}


void
TermView::TerminalTitleChanged(const char* title)
{
	Event* event = new(std::nothrow) Event(EVENT_TITLE);
	if (event != NULL) {
		event->text = title;
		_PostEvent(event);
	}
}


void
TermView::TerminalIconTitleChanged(const char* title)
{
}


void
TermView::TerminalBell()
{
	Event* event = new(std::nothrow) Event(EVENT_BELL);
	if (event != NULL)
		_PostEvent(event);
}


void
TermView::TerminalSetClipboard(const char* selections, const void* data,
	size_t length)
{
	Event* event = new(std::nothrow) Event(EVENT_SET_CLIPBOARD);
	if (event != NULL) {
		event->text = selections;
		event->data.assign((const char*)data, length);
		_PostEvent(event);
	}
}


void
TermView::TerminalRequestClipboard(const char* selections)
{
	Event* event = new(std::nothrow) Event(EVENT_REQUEST_CLIPBOARD);
	if (event != NULL) {
		event->text = selections;
		_PostEvent(event);
	}
}


void
TermView::TerminalClipboardWrite(bool primary, const ClipboardData& data,
	const char* identifier)
{
	Event* event = new(std::nothrow) Event(EVENT_CLIPBOARD_WRITE);
	if (event != NULL) {
		event->primary = primary;
		event->clipboard = data;
		event->text = identifier;
		_PostEvent(event);
	}
}


void
TermView::TerminalClipboardRead(bool primary,
	const std::vector<std::string>& types, const char* identifier,
	const char* programName)
{
	Event* event = new(std::nothrow) Event(EVENT_CLIPBOARD_READ);
	if (event != NULL) {
		event->primary = primary;
		event->types = types;
		event->text = identifier;
		event->data = programName;
		_PostEvent(event);
	}
}


void
TermView::TerminalNotify(const TermNotification& notification)
{
	Event* event = new(std::nothrow) Event(EVENT_NOTIFY);
	if (event != NULL) {
		event->notification = notification;
		_PostEvent(event);
	}
}


void
TermView::TerminalDragAndDrop(const char* data)
{
	Event* event = new(std::nothrow) Event(EVENT_DRAG_AND_DROP);
	if (event != NULL) {
		event->text = data;
		_PostEvent(event);
	}
}


void
TermView::TerminalFileTransfer(const char* data)
{
	Event* event = new(std::nothrow) Event(EVENT_FILE_TRANSFER);
	if (event != NULL) {
		event->text = data;
		_PostEvent(event);
	}
}


void
TermView::TerminalRemoteControl(const char* json)
{
	Event* event = new(std::nothrow) Event(EVENT_REMOTE_CONTROL);
	if (event != NULL) {
		event->text = json;
		_PostEvent(event);
	}
}


void
TermView::TerminalCloseNotification(const char* identifier)
{
	Event* event = new(std::nothrow) Event(EVENT_CLOSE_NOTIFICATION);
	if (event != NULL) {
		event->text = identifier;
		_PostEvent(event);
	}
}


void
TermView::TerminalPointerShapeChanged(const char* shape)
{
	Event* event = new(std::nothrow) Event(EVENT_POINTER_SHAPE);
	if (event != NULL) {
		event->text = shape;
		_PostEvent(event);
	}
}


void
TermView::TerminalColorsChanged()
{
	TerminalContentsChanged();
}


void
TermView::TerminalWorkingDirectoryChanged(const char* url)
{
}


void
TermView::TerminalModesChanged()
{
	Event* event = new(std::nothrow) Event(EVENT_MODES);
	if (event != NULL)
		_PostEvent(event);
}


void
TermView::TerminalProgressChanged(int32 state, int32 percent)
{
	Event* event = new(std::nothrow) Event(EVENT_PROGRESS);
	if (event != NULL) {
		event->value1 = state;
		event->value2 = percent;
		_PostEvent(event);
	}
}


// #pragma mark - events from the terminal (window thread)


static void
put_clipboard_data(BClipboard* clipboard, const ClipboardData& data)
{
	if (!clipboard->Lock())
		return;
	clipboard->Clear();
	if (BMessage* message = clipboard->Data()) {
		for (size_t i = 0; i < data.size(); i++) {
			message->AddData(data[i].first.c_str(), B_MIME_TYPE,
				data[i].second.data(), data[i].second.size());
		}
		clipboard->Commit();
	}
	clipboard->Unlock();
}


void
TermView::_HandleEvents()
{
	std::vector<Event*> events;
	fTerminal->Lock();
	events.swap(fEvents);
	fEventsPosted = false;
	fTerminal->Unlock();

	for (size_t i = 0; i < events.size(); i++) {
		Event* event = events[i];
		switch (event->type) {
			case EVENT_TITLE:
				if (fListener != NULL)
					fListener->SetTermViewTitle(this, event->text.c_str());
				break;

			case EVENT_BELL:
				beep();
				break;

			case EVENT_SET_CLIPBOARD:
			{
				// OSC 52: "p" and "s" are the selection, the rest the
				// clipboard
				bool primary = event->text.find_first_of("ps")
						!= std::string::npos
					&& event->text.find('c') == std::string::npos;
				ClipboardData data;
				data.push_back(std::make_pair(std::string("text/plain"),
					event->data));
				put_clipboard_data(primary ? fMouseClipboard : be_clipboard,
					data);
				if (!primary && be_clipboard != fMouseClipboard)
					put_clipboard_data(fMouseClipboard, data);
				break;
			}

			case EVENT_CLIPBOARD_WRITE:
				put_clipboard_data(event->primary ? fMouseClipboard
					: be_clipboard, event->clipboard);
				break;

			case EVENT_REQUEST_CLIPBOARD:
			case EVENT_CLIPBOARD_READ:
			{
				bool listOnly = event->type == EVENT_CLIPBOARD_READ
					&& event->types.size() == 1 && event->types[0] == ".";
				BMessage* answer = new BMessage(kClipboardReadAnswer);
				answer->AddInt32("type", event->type);
				answer->AddString("selections", event->text.c_str());
				answer->AddString("id", event->text.c_str());
				answer->AddBool("primary", event->primary);
				for (size_t j = 0; j < event->types.size(); j++)
					answer->AddString("mime", event->types[j].c_str());
				if (listOnly) {
					answer->AddInt32("which", 0);
					MessageReceived(answer);
					delete answer;
					break;
				}
				// reading the clipboard needs the user's permission
				BString text;
				if (!event->data.empty()) {
					text.SetToFormat(B_TRANSLATE("\"%s\" running in Terminal "
						"wants to read the clipboard."), event->data.c_str());
				} else {
					text = B_TRANSLATE("A program running in Terminal wants "
						"to read the clipboard.");
				}
				BAlert* alert = new BAlert(B_TRANSLATE("Clipboard"), text,
					B_TRANSLATE("Allow"), B_TRANSLATE("Deny"), NULL,
					B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				alert->SetShortcut(1, B_ESCAPE);
				alert->Go(new BInvoker(answer, this));
				break;
			}

			case EVENT_NOTIFY:
			{
				const TermNotification& notification = event->notification;
				notification_type type = notification.urgency >= 2
					? B_IMPORTANT_NOTIFICATION : B_INFORMATION_NOTIFICATION;
				BNotification message(type);
				message.SetGroup(B_TRANSLATE_SYSTEM_NAME("Terminal"));
				message.SetTitle(notification.title.c_str());
				message.SetContent(notification.body.c_str());
				BString identifier;
				if (!notification.identifier.empty()) {
					identifier.SetToFormat("terminal-%p-%s", this,
						notification.identifier.c_str());
					message.SetMessageID(identifier);
				}

				BBitmap* icon = _NotificationIcon(notification);
				if (icon != NULL) {
					message.SetIcon(icon);
					delete icon;
				}

				// A click starts Terminal, which passes it on to this view
				// (see TermApp::ArgvReceived()).
				if (notification.focusOnActivation
					|| notification.reportActivation) {
					message.SetOnClickApp(TERM_SIGNATURE);
					message.AddOnClickArg(NOTIFICATION_ACTIVATED_ARGUMENT);
					BString argument;
					argument.SetToFormat("%" B_PRId32, be_app->Team());
					message.AddOnClickArg(argument);
					argument.SetToFormat("%p", this);
					message.AddOnClickArg(argument);
					message.AddOnClickArg(notification.identifier.empty()
						? "0" : notification.identifier.c_str());
					argument.SetToFormat("%d",
						(notification.focusOnActivation ? 1 : 0)
							| (notification.reportActivation ? 2 : 0));
					message.AddOnClickArg(argument);
				}

				// w: milliseconds; 0 is "never", a day here
				bigtime_t timeout = -1;
				if (notification.timeout > 0)
					timeout = (bigtime_t)notification.timeout * 1000;
				else if (notification.timeout == 0)
					timeout = 24LL * 3600 * 1000000;

				// Sent as BNotification::Send() does, with the fields air/OS's
				// notification_server answers: clicks and buttons, and the
				// notification going away (see its NotificationView).
				BMessage request(kNotificationMessage);
				if (message.Archive(&request) != B_OK)
					break;
				if (timeout > 0)
					request.AddInt64("timeout", timeout);
				BString view;
				view.SetToFormat("%p", this);
				const char* id = notification.identifier.empty() ? "0"
					: notification.identifier.c_str();
				request.AddMessenger("_reply:target", BMessenger(Window()));
				if (notification.focusOnActivation
					|| notification.reportActivation) {
					BMessage activated(MSG_NOTIFICATION_ACTIVATED);
					activated.AddString("view", view);
					activated.AddString("id", id);
					activated.AddInt32("actions",
						(notification.focusOnActivation ? 1 : 0)
							| (notification.reportActivation ? 2 : 0));
					request.AddMessage("_reply:activated", &activated);
				}
				if (notification.reportClose) {
					BMessage closed(MSG_NOTIFICATION_CLOSED);
					closed.AddString("view", view);
					closed.AddString("id", id);
					request.AddMessage("_reply:closed", &closed);
				}
				for (size_t i = 0; i < notification.buttons.size(); i++)
					request.AddString("_buttons", notification.buttons[i].c_str());
				BMessenger(kNotificationServerSignature).SendMessage(&request);
				break;
			}

			case EVENT_DRAG_AND_DROP:
				if (fDragAndDrop != NULL)
					fDragAndDrop->Command(event->text);
				break;

			case EVENT_FILE_TRANSFER:
				if (fFileTransfer != NULL)
					fFileTransfer->Command(event->text);
				break;

			case EVENT_REMOTE_CONTROL:
			{
				// the window has all the tabs a command may be about
				BMessage message(MSG_REMOTE_CONTROL);
				message.AddString("json", event->text.c_str());
				message.AddPointer("view", this);
				if (Window() != NULL)
					Window()->PostMessage(&message);
				break;
			}

			case EVENT_CLOSE_NOTIFICATION:
			{
				// Notifications cannot be taken back; one with the same id
				// that goes at once takes its place.
				BString identifier;
				identifier.SetToFormat("terminal-%p-%s", this,
					event->text.c_str());
				BNotification message(B_INFORMATION_NOTIFICATION);
				message.SetGroup(B_TRANSLATE_SYSTEM_NAME("Terminal"));
				message.SetMessageID(identifier);
				message.Send(1);
				break;
			}

			case EVENT_POINTER_SHAPE:
				_SetPointerShape(event->text.c_str());
				break;

			case EVENT_MODES:
				// the pointer may have to change for mouse reporting
				if (fPointerShape.IsEmpty())
					_SetPointerShape("");
				break;

			case EVENT_PROGRESS:
			{
				// ConEmu's progress (OSC 9;4): 0 removes it
				BString identifier;
				identifier.SetToFormat("terminal-progress-%p", this);
				BNotification message(B_PROGRESS_NOTIFICATION);
				message.SetGroup(B_TRANSLATE_SYSTEM_NAME("Terminal"));
				// the program's title, or the window's
				const char* title = fTerminal->Title();
				if ((title == NULL || title[0] == '\0') && Window() != NULL)
					title = Window()->Title();
				message.SetTitle(title);
				message.SetMessageID(identifier);
				message.SetProgress(event->value1 == 0 ? 1.0f
					: std::min(100, std::max(0, (int)event->value2)) / 100.0f);
				// to remove it: a replacement that goes at once (no
				// timeout means the default one)
				message.Send(event->value1 == 0 ? 1 : -1);
				break;
			}
		}
		delete event;
	}
}


/*!	OSC 22: the pointer shape a program asked for. Over text Terminal shows
	the I-beam; mouse reporting programs get the arrow.
*/
void
TermView::_SetPointerShape(const char* shape)
{
	fPointerShape = shape;

	struct {
		const char*	name;
		BCursorID	id;
	} kShapes[] = {
		{ "default", B_CURSOR_ID_SYSTEM_DEFAULT },
		{ "text", B_CURSOR_ID_I_BEAM },
		{ "vertical-text", B_CURSOR_ID_I_BEAM_HORIZONTAL },
		{ "pointer", B_CURSOR_ID_FOLLOW_LINK },
		{ "help", B_CURSOR_ID_HELP },
		{ "wait", B_CURSOR_ID_PROGRESS },
		{ "progress", B_CURSOR_ID_PROGRESS },
		{ "crosshair", B_CURSOR_ID_CROSS_HAIR },
		{ "cell", B_CURSOR_ID_CROSS_HAIR },
		{ "move", B_CURSOR_ID_MOVE },
		{ "copy", B_CURSOR_ID_COPY },
		{ "alias", B_CURSOR_ID_CREATE_LINK },
		{ "not-allowed", B_CURSOR_ID_NOT_ALLOWED },
		{ "no-drop", B_CURSOR_ID_NOT_ALLOWED },
		{ "grab", B_CURSOR_ID_GRAB },
		{ "grabbing", B_CURSOR_ID_GRABBING },
		{ "zoom-in", B_CURSOR_ID_ZOOM_IN },
		{ "zoom-out", B_CURSOR_ID_ZOOM_OUT },
		{ "e-resize", B_CURSOR_ID_RESIZE_EAST },
		{ "w-resize", B_CURSOR_ID_RESIZE_WEST },
		{ "n-resize", B_CURSOR_ID_RESIZE_NORTH },
		{ "s-resize", B_CURSOR_ID_RESIZE_SOUTH },
		{ "ne-resize", B_CURSOR_ID_RESIZE_NORTH_EAST },
		{ "nw-resize", B_CURSOR_ID_RESIZE_NORTH_WEST },
		{ "se-resize", B_CURSOR_ID_RESIZE_SOUTH_EAST },
		{ "sw-resize", B_CURSOR_ID_RESIZE_SOUTH_WEST },
		{ "ew-resize", B_CURSOR_ID_RESIZE_EAST_WEST },
		{ "ns-resize", B_CURSOR_ID_RESIZE_NORTH_SOUTH },
		{ "nesw-resize", B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST },
		{ "nwse-resize", B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST },
		{ NULL, B_CURSOR_ID_SYSTEM_DEFAULT }
	};

	BCursorID id = _ReportsMouse() ? B_CURSOR_ID_SYSTEM_DEFAULT
		: B_CURSOR_ID_I_BEAM;
	if (shape != NULL && shape[0] != '\0') {
		for (int32 i = 0; kShapes[i].name != NULL; i++) {
			if (strcmp(kShapes[i].name, shape) == 0) {
				id = kShapes[i].id;
				break;
			}
		}
	}

	if (fActiveState == fHyperLinkState || fActiveState == fLinkClickState
		|| fHoverLink)
		return;
	BCursor cursor(id);
	SetViewCursor(&cursor);
}


// #pragma mark - drawing


void
TermView::_InvalidateAll()
{
	fBuilder.Invalidate();
	_ScheduleFrame();
}


void
TermView::_ScheduleFrame()
{
	if (Window() == NULL)
		return;

	bigtime_t now = system_time();
	bigtime_t elapsed = now - fLastFrameTime;
	if (elapsed >= kFrameInterval) {
		_RenderFrame();
		return;
	}

	if (fFrameRunner == NULL) {
		BMessage message(kRenderFrame);
		fFrameRunner = new(std::nothrow) BMessageRunner(BMessenger(this),
			&message, kFrameInterval - elapsed, 1);
	}
}


void
TermView::_UpdateScrollBarRange()
{
	if (fScrollBar == NULL)
		return;

	int32 historySize = fTerminal->HistorySize();
	fHistorySize = historySize;

	float viewHeight = fRows * fFontHeight;
	float historyHeight = (float)historySize * fFontHeight;

	fScrollBar->SetRange(-historyHeight, 0);
	if (historySize > 0)
		fScrollBar->SetProportion(viewHeight / (viewHeight + historyHeight));
	else
		fScrollBar->SetProportion(1);
	fScrollBar->SetValue(-fScrollRows * fFontHeight);
}


void
TermView::_RenderFrame()
{
	delete fFrameRunner;
	fFrameRunner = NULL;

	if (Window() == NULL || fBitmap == NULL || fRenderer == NULL)
		return;

	if (!fEvents.empty())
		_HandleEvents();

	bigtime_t frameStart = system_time();
	for (int32 attempt = 0; attempt < 2; attempt++) {
		fTerminal->Lock();

		if (fTerminal->IsSynchronizedOutput()) {
			// the program is in the middle of drawing (DECSET 2026)
			fTerminal->Unlock();
			BMessage message(kRenderFrame);
			fFrameRunner = new(std::nothrow) BMessageRunner(BMessenger(this),
				&message, kSynchronizedRetry, 1);
			return;
		}

		// the history grew: the selection moves up with the text, and a
		// view scrolled back stays on the text it shows
		uint64 linesScrolled = fTerminal->LinesScrolled();
		int32 historySize = fTerminal->HistorySize();
		if (linesScrolled != fLinesScrolled) {
			int32 delta = (int32)std::min((uint64)INT32_MAX,
				linesScrolled > fLinesScrolled
					? linesScrolled - fLinesScrolled : 0);
			if (delta > 0) {
				if (fScrollRows > 0)
					fScrollRows = std::min(fScrollRows + delta, historySize);
				if (!fSelection.IsEmpty()) {
					fSelection.start.y -= delta;
					fSelection.end.y -= delta;
					fInitialSelectionStart.y -= delta;
					fInitialSelectionEnd.y -= delta;
					if (fSelection.start.y < -historySize)
						fSelection.start = fSelection.end = TermPoint(0, 0);
				}
			}
			fLinesScrolled = linesScrolled;
		}
		fScrollRows = std::min(fScrollRows, historySize);
		bool historyChanged = historySize != fHistorySize;

		FrameState state;
		state.firstRow = _FirstRow();
		state.focused = fActive;
		state.cursorVisible = _IsCursorVisible() && fScrollRows >= 0
			&& !(fInline != NULL && fInline->IsActive());
		uint32 cursorColor = fTerminal->Color(COLOR_CURSOR);
		uint32 cursorText = fTerminal->Color(COLOR_CURSOR_TEXT);
		state.cursorColor = cursorColor != kColorUnset ? cursorColor
			: rgb_value(fCursorBackColor);
		state.cursorTextColor = cursorText != kColorUnset ? cursorText
			: rgb_value(fCursorForeColor);
		if (!fSelection.IsEmpty()) {
			FrameHighlight highlight;
			highlight.start = fSelection.start;
			highlight.end = fSelection.end;
			uint32 foreground = fTerminal->Color(COLOR_SELECTION_FOREGROUND);
			uint32 background = fTerminal->Color(COLOR_SELECTION_BACKGROUND);
			highlight.useColors = true;
			highlight.foreground = foreground != kColorUnset ? foreground
				: rgb_value(fSelectForeColor);
			highlight.background = background != kColorUnset ? background
				: rgb_value(fSelectBackColor);
			highlight.underline = false;
			state.highlights.push_back(highlight);
		}
		bool hoverLinkGone = false;
		if (fHoverLink && fActiveState == fDefaultState
			&& fTerminal->ContentGeneration() != fHoverGeneration) {
			// the text under the pointer changed: the link there now
			fHoverGeneration = fTerminal->ContentGeneration();
			TermLinkRange link;
			fLinkHighlights.clear();
			if (FindTerminalLink(*fTerminal, fHoverCell, link)) {
				for (size_t i = 0; i < link.segments.size(); i++) {
					Highlight segment;
					segment.start = link.segments[i].start;
					segment.end = link.segments[i].end;
					fLinkHighlights.push_back(segment);
				}
			} else
				hoverLinkGone = true;
		}
		for (size_t i = 0; i < fLinkHighlights.size(); i++) {
			if (fLinkHighlights[i].IsEmpty())
				continue;
			FrameHighlight highlight;
			highlight.start = fLinkHighlights[i].start;
			highlight.end = fLinkHighlights[i].end;
			highlight.useColors = false;
			highlight.foreground = highlight.background = 0;
			highlight.underline = true;
			state.highlights.push_back(highlight);
		}

		uint32 epoch = fGlyphs.Epoch();
		BRect bitmapBounds = fBitmap->Bounds();
		fBuilder.Build(fTerminal, state, bitmapBounds.IntegerWidth() + 1,
			bitmapBounds.IntegerHeight() + 1, fFrame);

		TerminalGraphics* graphics = fTerminal->Graphics();
		bool animating = graphics != NULL && graphics->HasAnimations();
		fTerminal->Unlock();

		if (hoverLinkGone) {
			// no link under the pointer any more: its pointer goes, too
			_ClearHoverLink();
		}

		if (historyChanged)
			_UpdateScrollBarRange();
		if (animating && fAnimationRunner == NULL) {
			BMessage message(kAnimate);
			fAnimationRunner = new(std::nothrow) BMessageRunner(
				BMessenger(this), &message, 10000, 1);
		}

		fGlyphs.DrawPending();
		if (fGlyphs.Epoch() != epoch) {
			// the atlas was full and started over: build again
			fBuilder.Invalidate();
			continue;
		}
		break;
	}

	bigtime_t renderStart = system_time();
	int32 firstRow;
	int32 lastRow;
	bool rendered = _Render(firstRow, lastRow);

	static bool sStatistics = getenv("TERMINAL_STATS") != NULL;
	if (sStatistics && rendered) {
		// how long frames take to build and draw, every 2 seconds
		static bigtime_t sBuild = 0, sRender = 0, sLast = 0;
		static int32 sFrames = 0, sRows = 0;
		bigtime_t now = system_time();
		sBuild += renderStart - frameStart;
		sRender += now - renderStart;
		sRows += lastRow - firstRow + 1;
		sFrames++;
		if (now - sLast > 2000000 && sFrames > 0) {
			fprintf(stderr, "Terminal %s: %" B_PRId32 " frames, build %.2f ms,"
				" render %.2f ms, %.1f rows a frame, %dx%d\n",
				fRenderer->Name(), sFrames, sBuild / 1000.0 / sFrames,
				sRender / 1000.0 / sFrames, (float)sRows / sFrames,
				fFrame.width, fFrame.height);
			sBuild = sRender = 0;
			sFrames = sRows = 0;
			sLast = now;
		}
	}

	if (rendered) {
		if (fFrame.allDirty)
			Invalidate();
		else {
			BRect bounds = Bounds();
			Invalidate(BRect(bounds.left, firstRow * fFontHeight,
				bounds.right, (lastRow + 1) * fFontHeight));
		}
	}
	fLastFrameTime = system_time();
}


/*!	Draws the frame with the processor or the GPU, whichever is faster.
	Reading back what the GPU drew can cost more than drawing with the
	processor: a 3802x2150 frame takes 23.5 ms on the GTX 1070 with NVK
	against 13 ms in software, a 2223x1495 frame 2.8 ms on the Radeon 760M
	against 1.8 ms. When the choice is Terminal's, it times the large frames
	(scrolling), and has each renderer draw a series of them now and then.
	The first frames of a series are not counted: a GPU that was idle takes
	a few frames to speed up.
*/
bool
TermView::_Render(int32& _firstRow, int32& _lastRow)
{
	const GlyphAtlasInfo& atlas = fGlyphs.Atlas();
	uint8* bits = (uint8*)fBitmap->Bits();
	int32 bytesPerRow = fBitmap->BytesPerRow();

	if (fGPURenderer == NULL || fGPURenderer->IsBroken()) {
		// no GPU, or it is gone: the processor draws from now on
		if (fRenderer != fSoftwareRenderer) {
			fRenderer = fSoftwareRenderer;
			fFrame.allDirty = true;
		}
		return fSoftwareRenderer->Render(fFrame, atlas, bits, bytesPerRow,
			_firstRow, _lastRow);
	}

	bool gpu = fRenderer == fGPURenderer;
	if (gpu && fGPUStale) {
		// its copy of the frame misses what the processor drew
		fFrame.allDirty = true;
	}

	int32 dirtyRows = 0;
	for (int32 row = 0; row < fFrame.rows; row++) {
		if (fFrame.allDirty || fFrame.dirtyRows[row])
			dirtyRows++;
	}
	float frameSize = (float)fFrame.width * fFrame.height;
	float dirtySize = fFrame.allDirty ? frameSize
		: std::min(frameSize,
			(float)fFrame.width * dirtyRows * fFrame.metrics.cellHeight);

	bigtime_t start = system_time();
	if (!fRenderer->Render(fFrame, atlas, bits, bytesPerRow, _firstRow,
			_lastRow)) {
		if (gpu && fGPURenderer->IsBroken())
			return _Render(_firstRow, _lastRow);
		return false;
	}
	bigtime_t now = system_time();
	fGPUStale = !gpu;

	if (!fChooseRenderer || dirtySize < frameSize / 2)
		return true;
	if (fWarmUpFrames > 0) {
		fWarmUpFrames--;
		return true;
	}

	int32 index = gpu ? 1 : 0;
	float cost = (now - start) / (dirtySize / 1000000);
	fCost[index] = fCost[index] < 0 ? cost : fCost[index] * 0.75f + cost / 4;
	fMeasured[index] = now;
	if (++fMeasuredFrames < kRendererSeriesFrames)
		return true;

	// The other one draws a series when it has not for a while, and from
	// then on when it is clearly faster.
	int32 other = 1 - index;
	bool change = fCost[other] < 0
		|| now - fMeasured[other] > kRendererRetryInterval
		|| fCost[other] < fCost[index] * 0.8f;

	static bool sStatistics = getenv("TERMINAL_STATS") != NULL;
	if (sStatistics && (change || fMeasuredFrames == kRendererSeriesFrames)) {
		fprintf(stderr, "Terminal: software %.0f us per megapixel, %s %.0f:"
			" %s\n", fCost[0], fGPURenderer->Name(), fCost[1],
			change ? "changing" : "staying");
	}
	if (change) {
		fRenderer = other == 1 ? fGPURenderer : fSoftwareRenderer;
		fWarmUpFrames = kRendererWarmUpFrames;
		fMeasuredFrames = 0;
	}
	return true;
}


void
TermView::Draw(BRect updateRect)
{
	if (fBitmap != NULL) {
		BRect source = fBitmap->Bounds();
		BRect destination(0, 0, (source.Width() + 1) / fScale - 1,
			(source.Height() + 1) / fScale - 1);
		DrawBitmap(fBitmap, source, destination,
			fScale != 1 ? B_DEVICE_PIXEL_BITMAP : 0);
	} else {
		SetHighColor(fTextBackColor);
		FillRect(updateRect);
	}

	if (fInline && fInline->IsActive())
		_DrawInlineMethodString();
}


// #pragma mark - cursor


void
TermView::SwitchCursorBlinking()
{
	if (fCursorBlinking) {
		if (fCursorBlinkRunner == NULL) {
			BMessage blinkMessage(kBlinkCursor);
			fCursorBlinkRunner = new (std::nothrow) BMessageRunner(
				BMessenger(this), &blinkMessage, kCursorBlinkInterval);
		}
	} else {
		// make sure the cursor becomes visible
		fCursorState = 0;
		delete fCursorBlinkRunner;
		fCursorBlinkRunner = NULL;
	}
	_ScheduleFrame();
}


void
TermView::SwitchCursorBlinking(bool blinkingOn)
{
	fCursorBlinking = blinkingOn;
	SwitchCursorBlinking();
}


bool
TermView::_IsCursorVisible() const
{
	return fCursorState < kCursorVisibleIntervals;
}


void
TermView::_BlinkCursor()
{
	bool wasVisible = _IsCursorVisible();

	if (!wasVisible && fInline && fInline->IsActive())
		return;

	// the program may switch blinking off (DECSET 12, DECSCUSR)
	fTerminal->Lock();
	bool blinking = fTerminal->Cursor().blinking;
	fTerminal->Unlock();

	bigtime_t now = system_time();
	if (blinking && Window()->IsActive()
		&& now - fLastActivityTime >= kCursorBlinkInterval) {
		fCursorState = (fCursorState + 1) % kCursorBlinkIntervals;
	} else
		fCursorState = 0;

	if (wasVisible != _IsCursorVisible())
		_ScheduleFrame();
}


void
TermView::_ActivateCursor(bool invalidate)
{
	fLastActivityTime = system_time();
	if (invalidate && fCursorState != 0)
		_BlinkCursor();
	else
		fCursorState = 0;
}


void
TermView::_Activate()
{
	fActive = true;
	bool blink = PrefHandler::Default()->getBool(PREF_BLINK_CURSOR);
	SwitchCursorBlinking(blink);

	fTerminal->Lock();
	fTerminal->SetFocused(true);
	std::string output;
	fTerminal->TakeOutput(output);
	fTerminal->Unlock();
	_Send(output);
	_ScheduleFrame();
}


void
TermView::_Deactivate()
{
	// make sure the cursor becomes visible
	fCursorState = 0;

	SwitchCursorBlinking(false);

	fActive = false;

	fTerminal->Lock();
	fTerminal->SetFocused(false);
	std::string output;
	fTerminal->TakeOutput(output);
	fTerminal->Unlock();
	_Send(output);
	_ScheduleFrame();
}


// #pragma mark - writing to the shell


/*!	Write strings to PTY device. If encoding system isn't UTF8, change
	encoding to UTF8 before writing PTY.
*/
void
TermView::_WritePTY(const char* text, int32 numBytes)
{
	if (fEncoding != M_UTF8) {
		std::string converted;
		while (numBytes > 0) {
			char buffer[1024];
			int32 bufferSize = sizeof(buffer);
			int32 sourceSize = numBytes;
			int32 state = 0;
			if (convert_from_utf8(fEncoding, text, &sourceSize, buffer,
					&bufferSize, &state, '?') != B_OK || bufferSize == 0) {
				break;
			}

			converted.append(buffer, bufferSize);
			text += sourceSize;
			numBytes -= sourceSize;
		}
		_Send(converted);
	} else
		_Send(std::string(text, numBytes));
}


/*!	Queues data for the program. A thread of its own writes it, so that
	neither the window (a large paste) nor the reader (answers to queries)
	waits for a program that does not read its input.
*/
void
TermView::_Send(const std::string& data)
{
	if (data.empty())
		return;

	fWriteLock.Lock();
	fWriteQueue.push_back(data);
	fWriteQueueSize += data.size();
	fWriteLock.Unlock();
	if (fWriteSemaphore >= 0)
		release_sem(fWriteSemaphore);
}


//!	What is waiting to be written to the shell, in bytes.
size_t
TermView::_WriteBacklog()
{
	BAutolock _(fWriteLock);
	return fWriteQueueSize;
}


/*static*/ status_t
TermView::_WriterThread(void* data)
{
	((TermView*)data)->_WriteLoop();
	return B_OK;
}


void
TermView::_WriteLoop()
{
	while (!fWriterQuit) {
		status_t status = acquire_sem(fWriteSemaphore);
		if (status == B_INTERRUPTED)
			continue;
		if (status != B_OK || fWriterQuit)
			break;

		while (!fWriterQuit) {
			fWriteLock.Lock();
			if (fWriteQueue.empty()) {
				fWriteLock.Unlock();
				break;
			}
			std::string data;
			data.swap(fWriteQueue.front());
			fWriteQueue.pop_front();
			fWriteQueueSize -= data.size();
			fWriteLock.Unlock();

			size_t written = 0;
			while (written < data.size() && !fWriterQuit) {
				ssize_t result = fShell->Write(data.data() + written,
					data.size() - written);
				if (result < 0) {
					if (errno == B_INTERRUPTED || errno == EINTR
						|| errno == EAGAIN) {
						continue;
					}
					break;
				}
				written += result;
			}
		}
	}
}


//!	Handler for SIGWINCH
void
TermView::_UpdateSIGWINCH()
{
	if (fFrameResized) {
		const RenderMetrics& metrics = fGlyphs.Metrics();
		fShell->UpdateWindowSize(fRows, fColumns,
			fColumns * metrics.cellWidth, fRows * metrics.cellHeight);
		fFrameResized = false;
	}
}


// #pragma mark - keyboard


/*!	Turns a key event into what the terminal encodes: the key (without
	shift), the character with shift, the key of a US keyboard and the
	text it types.
*/
bool
TermView::_TranslateKey(const BMessage* message, bool keyUp,
	TermKeyEvent& event, BString& text)
{
	int32 key = 0;
	int32 modifiers = 0;
	int32 rawChar = 0;
	message->FindInt32("key", &key);
	message->FindInt32("modifiers", &modifiers);
	message->FindInt32("raw_char", &rawChar);
	const char* bytes = NULL;
	if (message->FindString("bytes", &bytes) != B_OK)
		bytes = "";

	event.modifiers = 0;
	if ((modifiers & B_SHIFT_KEY) != 0)
		event.modifiers |= KEY_MODIFIER_SHIFT;
	if ((modifiers & B_CONTROL_KEY) != 0)
		event.modifiers |= KEY_MODIFIER_CONTROL;
	if ((modifiers & B_COMMAND_KEY) != 0)
		event.modifiers |= KEY_MODIFIER_SUPER;
	if ((modifiers & B_CAPS_LOCK) != 0)
		event.modifiers |= KEY_MODIFIER_CAPS_LOCK;
	if ((modifiers & B_NUM_LOCK) != 0)
		event.modifiers |= KEY_MODIFIER_NUM_LOCK;

	// the Option key as Meta: the character without it, with alt
	bool optionAsMeta = (modifiers & B_LEFT_OPTION_KEY) != 0
		&& fUseOptionAsMetaKey;

	int32 repeat = 0;
	message->FindInt32("be:key_repeat", &repeat);
	event.action = keyUp ? KEY_ACTION_RELEASE
		: repeat > 0 ? KEY_ACTION_REPEAT : KEY_ACTION_PRESS;

	text = bytes;
	event.shiftedKey = 0;
	event.baseKey = us_layout_key(key);

	uint32 keypad = keypad_key_for_key_code(key);
	uint32 functional = functional_key_for_key_code(key);
	if (keypad != 0) {
		if ((modifiers & B_NUM_LOCK) == 0)
			keypad = keypad_navigation_key(keypad);
		event.key = keypad;
		uint32 c = first_code_point(text.String(), text.Length());
		if (c < 0x20 || c == 0x7f)
			text = "";
	} else if (functional != 0) {
		event.key = functional;
		text = "";
		if (functional == KEY_ENTER)
			text = "\r";
		else if (functional == KEY_TAB)
			text = "\t";
	} else {
		// a key with a character: as the keymap has it without modifiers
		uint32 c = 0;
		if (fKeymap != NULL && fKeymapChars != NULL && key >= 0
			&& key < 128) {
			const char* character = &fKeymapChars[fKeymap->normal_map[key]];
			int32 length = *character++;
			c = first_code_point(character, length);
			const char* shifted = &fKeymapChars[fKeymap->shift_map[key]];
			int32 shiftedLength = *shifted++;
			event.shiftedKey = first_code_point(shifted, shiftedLength);
		}
		if (c == 0)
			c = rawChar;
		if (c == 0)
			c = first_code_point(text.String(), text.Length());
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		event.key = c;

		if (optionAsMeta) {
			// the character the key makes without Option
			int32 remaining = modifiers
				& (B_SHIFT_KEY | B_CAPS_LOCK | B_CONTROL_KEY);
			const int32 (*table)[128] = remaining == 0 ? NULL
				: fKeymapTableForModifiers.Get(remaining);
			text = "";
			if (table == NULL) {
				append_utf8(text, c);
			} else if (key >= 0 && key < 128) {
				const char* character = &fKeymapChars[(*table)[key]];
				int32 length = *character++;
				text.SetTo(character, length);
			}
			event.modifiers |= KEY_MODIFIER_ALT;
		}

		if (c == 0 && text.IsEmpty())
			return false;
	}

	if ((event.modifiers & KEY_MODIFIER_CONTROL) != 0) {
		// the keymap's control characters are what ctrl makes of the key,
		// not text
		uint32 c = first_code_point(text.String(), text.Length());
		if (c < 0x20 || c == 0x7f)
			text = "";
	}
	return true;
}


void
TermView::_SendKey(const TermKeyEvent& event)
{
	std::string output;
	fTerminal->Lock();
	TermKeyEvent copy = event;
	if (fTerminal->KeyboardFlags() == 0)
		copy.modifiers &= ~KEY_MODIFIER_SUPER;
	bool encoded = fTerminal->EncodeKey(copy, output);
	fTerminal->Unlock();
	if (!encoded || output.empty())
		return;

	if (fEncoding != M_UTF8)
		_WritePTY(output.data(), output.size());
	else
		_Send(output);
}


void
TermView::KeyDown(const char *bytes, int32 numBytes)
{
	_UpdateModifiers();

	fActiveState->KeyDown(bytes, numBytes);
}


void
TermView::KeyUp(const char *bytes, int32 numBytes)
{
	_UpdateModifiers();

	// key releases matter only to programs that asked for them
	fTerminal->Lock();
	bool wantsReleases
		= (fTerminal->KeyboardFlags() & KEYBOARD_REPORT_EVENTS) != 0;
	fTerminal->Unlock();
	if (!wantsReleases)
		return;

	BMessage* message = Window()->CurrentMessage();
	if (message == NULL)
		return;
	TermKeyEvent event;
	BString text;
	if (!_TranslateKey(message, true, event, text))
		return;
	event.text = text.String();
	_SendKey(event);
}


void
TermView::_UpdateModifiers()
{
	// TODO: This method is a general work-around for missing or out-of-order
	// B_MODIFIERS_CHANGED messages. This should really be fixed where it is
	// broken (app server?).
	int32 oldModifiers = fModifiers;
	fModifiers = modifiers();
	if (fModifiers != oldModifiers && fActiveState != NULL)
		fActiveState->ModifiersChanged(oldModifiers, fModifiers);

	if (fModifiers == oldModifiers || !fActive)
		return;

	// programs that want all keys get the modifier keys too
	fTerminal->Lock();
	bool reportAll
		= (fTerminal->KeyboardFlags() & KEYBOARD_REPORT_ALL_KEYS) != 0;
	fTerminal->Unlock();
	if (!reportAll)
		return;

	static const struct {
		int32	modifier;
		uint32	key;
		uint32	bit;
	} kModifierKeys[] = {
		{ B_LEFT_SHIFT_KEY, KEY_LEFT_SHIFT, KEY_MODIFIER_SHIFT },
		{ B_RIGHT_SHIFT_KEY, KEY_RIGHT_SHIFT, KEY_MODIFIER_SHIFT },
		{ B_LEFT_CONTROL_KEY, KEY_LEFT_CONTROL, KEY_MODIFIER_CONTROL },
		{ B_RIGHT_CONTROL_KEY, KEY_RIGHT_CONTROL, KEY_MODIFIER_CONTROL },
		{ B_LEFT_OPTION_KEY, KEY_LEFT_ALT, KEY_MODIFIER_ALT },
		{ B_RIGHT_OPTION_KEY, KEY_RIGHT_ALT, KEY_MODIFIER_ALT },
		{ B_LEFT_COMMAND_KEY, KEY_LEFT_SUPER, KEY_MODIFIER_SUPER },
		{ B_RIGHT_COMMAND_KEY, KEY_RIGHT_SUPER, KEY_MODIFIER_SUPER },
	};
	for (size_t i = 0; i < sizeof(kModifierKeys) / sizeof(kModifierKeys[0]);
			i++) {
		bool was = (oldModifiers & kModifierKeys[i].modifier) != 0;
		bool is = (fModifiers & kModifierKeys[i].modifier) != 0;
		if (was == is)
			continue;
		TermKeyEvent event;
		event.key = kModifierKeys[i].key;
		event.shiftedKey = 0;
		event.baseKey = 0;
		event.text = "";
		event.action = is ? KEY_ACTION_PRESS : KEY_ACTION_RELEASE;
		event.modifiers = 0;
		if ((fModifiers & B_SHIFT_KEY) != 0)
			event.modifiers |= KEY_MODIFIER_SHIFT;
		if ((fModifiers & B_CONTROL_KEY) != 0)
			event.modifiers |= KEY_MODIFIER_CONTROL;
		if ((fModifiers & B_OPTION_KEY) != 0)
			event.modifiers |= KEY_MODIFIER_ALT;
		if ((fModifiers & B_COMMAND_KEY) != 0)
			event.modifiers |= KEY_MODIFIER_SUPER;
		if ((fModifiers & B_CAPS_LOCK) != 0)
			event.modifiers |= KEY_MODIFIER_CAPS_LOCK;
		if ((fModifiers & B_NUM_LOCK) != 0)
			event.modifiers |= KEY_MODIFIER_NUM_LOCK;
		_SendKey(event);
	}
}


// #pragma mark - mouse


bool
TermView::_ReportsMouse() const
{
	fTerminal->Lock();
	bool reports = fTerminal->MouseTracking() != MOUSE_TRACKING_NONE;
	fTerminal->Unlock();
	return reports;
}


void
TermView::_SendMouseEvent(int32 type, int32 button, BPoint where,
	int32 modifiers)
{
	TermPoint position = _ConvertToTerminal(where);
	int32 row = position.y - _FirstRow();
	int32 column = std::max((int32)0, std::min(position.x, fColumns - 1));
	row = std::max((int32)0, std::min(row, fRows - 1));
	int32 pixelX, pixelY;
	_PixelPosition(where, pixelX, pixelY);

	uint32 keyModifiers = 0;
	if ((modifiers & B_SHIFT_KEY) != 0)
		keyModifiers |= KEY_MODIFIER_SHIFT;
	if ((modifiers & B_OPTION_KEY) != 0)
		keyModifiers |= KEY_MODIFIER_ALT;
	if ((modifiers & B_CONTROL_KEY) != 0)
		keyModifiers |= KEY_MODIFIER_CONTROL;

	std::string output;
	fTerminal->Lock();
	fTerminal->EncodeMouse(type, button, keyModifiers, column, row,
		pixelX, pixelY, output);
	fTerminal->Unlock();
	_Send(output);
}


void
TermView::MouseDown(BPoint where)
{
	if (!IsFocus())
		MakeFocus();

	_UpdateModifiers();

	BMessage* currentMessage = Window()->CurrentMessage();
	int32 buttons = currentMessage->GetInt32("buttons", 0);

	fActiveState->MouseDown(where, buttons, fModifiers);

	fMouseButtons = buttons;
	fLastClickPoint = where;
}


void
TermView::MouseMoved(BPoint where, uint32 transit, const BMessage *message)
{
	_UpdateModifiers();

	fActiveState->MouseMoved(where, transit, message, fModifiers);
}


void
TermView::MouseUp(BPoint where)
{
	_UpdateModifiers();

	int32 buttons = Window()->CurrentMessage()->GetInt32("buttons", 0);

	fActiveState->MouseUp(where, buttons);

	fMouseButtons = buttons;
}


// #pragma mark - window and focus


void
TermView::AttachedToWindow()
{
	fMouseButtons = 0;
	fReportedMouseButtons = 0;

	_UpdateModifiers();

	// the frame buffer's density is known now
	_UpdateMetrics();

	// update the terminal size because it may have changed while the TermView
	// was detached from the window. On such conditions FrameResized was not
	// called when the resize occured
	SetTermSize(Bounds(), true);
	MakeFocus(true);
	if (fScrollBar) {
		fScrollBar->SetSteps(fFontHeight, fFontHeight * fRows);
		_UpdateScrollBarRange();
	}

	BMessenger thisMessenger(this);

	BMessage message(kUpdateSigWinch);
	fWinchRunner = new (std::nothrow) BMessageRunner(thisMessenger,
		&message, 500000);

	be_clipboard->StartWatching(thisMessenger);

	_LoadRenderer();
	_SetPointerShape(fPointerShape.String());

	// A frame request the reader thread queued while the view was being
	// detached (switching tabs) was dropped: BLooper drops messages for
	// handlers it no longer has. Left set, fFramePending would keep the
	// reader from asking for frames again, and the tab would only redraw
	// when its cursor blinks.
	fFramePending = false;
	_InvalidateAll();
}


void
TermView::DetachedFromWindow()
{
	be_clipboard->StopWatching(BMessenger(this));

	 _NextState(fDefaultState);

	delete fWinchRunner;
	fWinchRunner = NULL;

	delete fCursorBlinkRunner;
	fCursorBlinkRunner = NULL;

	delete fResizeRunner;
	fResizeRunner = NULL;

	delete fFrameRunner;
	fFrameRunner = NULL;

	delete fAnimationRunner;
	fAnimationRunner = NULL;
}


void
TermView::WindowActivated(bool active)
{
	BView::WindowActivated(active);
	if (active)
		fActivatedTime = system_time();
	else
		_ClearHoverLink();
	if (active && IsFocus()) {
		if (!fActive)
			_Activate();
	} else {
		if (fActive)
			_Deactivate();
	}

	_UpdateModifiers();

	fActiveState->WindowActivated(active);
}


void
TermView::MakeFocus(bool focusState)
{
	BView::MakeFocus(focusState);

	if (focusState && Window() && Window()->IsActive()) {
		if (!fActive)
			_Activate();
	} else {
		if (fActive)
			_Deactivate();
	}
}


void
TermView::FrameResized(float width, float height)
{
	_ResizeBitmap();

	int32 columns = (int32)((width + 1) / fFontWidth);
	int32 rows = (int32)((height + 1) / fFontHeight);

	if (columns == fColumns && rows == fRows) {
		_InvalidateAll();
		return;
	}

	bool hasResizeView = fResizeRunner != NULL;
	if (!hasResizeView) {
		// show the current size in a view
		fResizeView = new BStringView(BRect(100, 100, 300, 140), "size", "");
		fResizeView->SetAlignment(B_ALIGN_CENTER);
		fResizeView->SetFont(be_bold_font);
		fResizeView->SetViewColor(fTextBackColor);
		fResizeView->SetLowColor(fTextBackColor);
		fResizeView->SetHighColor(fTextForeColor);

		BMessage message(MSG_REMOVE_RESIZE_VIEW_IF_NEEDED);
		fResizeRunner = new(std::nothrow) BMessageRunner(BMessenger(this),
			&message, 25000LL);
	}

	BString text;
	text.SetToFormat("%" B_PRId32 " × %" B_PRId32, columns, rows);
	fResizeView->SetText(text.String());
	fResizeView->GetPreferredSize(&width, &height);
	fResizeView->ResizeTo(width * 1.5, height * 1.5);
	fResizeView->MoveTo((Bounds().Width() - fResizeView->Bounds().Width()) / 2,
		(Bounds().Height()- fResizeView->Bounds().Height()) / 2);
	if (!hasResizeView && fResizeViewDisableCount < 1)
		AddChild(fResizeView);

	if (fResizeViewDisableCount > 0)
		fResizeViewDisableCount--;

	SetTermSize(rows, columns, true);
}


void
TermView::DisableResizeView(int32 disableCount)
{
	fResizeViewDisableCount += disableCount;
}


// #pragma mark - the renderer


/*!	Loads the OpenGL renderer add-on, unless OpenGL is not there or runs in
	software (Terminal's own renderer is faster than Mesa's). _Render() then
	uses whichever of it and the software renderer is faster. The
	TERMINAL_RENDERER environment variable or the "Renderer" setting can
	choose one: "software" or "opengl" (Mesa's software renderers included).
*/
void
TermView::_LoadRenderer()
{
	if (fGPURenderer != NULL)
		return;

	BString choice = getenv("TERMINAL_RENDERER");
	if (choice.IsEmpty() && PrefHandler::Default() != NULL) {
		const char* setting = PrefHandler::Default()->getString("Renderer");
		if (setting != NULL)
			choice = setting;
	}
	if (choice == "software")
		return;

	static image_id sAddOn = -1;
	static create_terminal_renderer_func sCreate = NULL;
	if (sAddOn < 0) {
		// next to the application, or in the add-on directories
		const char* const kNames[] = {
			"Terminal-OpenGL",
			NULL
		};
		BPath path;
		app_info info;
		if (be_app->GetAppInfo(&info) == B_OK) {
			BEntry entry(&info.ref);
			BPath appPath;
			if (entry.GetPath(&appPath) == B_OK
				&& appPath.GetParent(&path) == B_OK) {
				path.Append(kNames[0]);
				sAddOn = load_add_on(path.Path());
			}
		}
		const directory_which kDirectories[] = {
			B_USER_NONPACKAGED_ADDONS_DIRECTORY,
			B_USER_ADDONS_DIRECTORY,
			B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
			B_SYSTEM_ADDONS_DIRECTORY
		};
		for (size_t i = 0; sAddOn < 0
				&& i < sizeof(kDirectories) / sizeof(kDirectories[0]); i++) {
			if (find_directory(kDirectories[i], &path) != B_OK)
				continue;
			path.Append("Terminal");
			path.Append(kNames[0]);
			sAddOn = load_add_on(path.Path());
		}
		if (sAddOn < 0 || get_image_symbol(sAddOn, TERMINAL_RENDERER_ENTRY,
				B_SYMBOL_TYPE_TEXT, (void**)&sCreate) != B_OK) {
			sCreate = NULL;
		}
	}
	if (sCreate == NULL)
		return;

	const char* error = NULL;
	TermRenderer* renderer = sCreate(&error);
	if (renderer == NULL) {
		if (error != NULL)
			fprintf(stderr, "Terminal: no OpenGL renderer: %s\n", error);
		return;
	}

	// Mesa's software renderers are slower than drawing ourselves
	if (choice != "opengl" && (strstr(renderer->Name(), "llvmpipe") != NULL
			|| strstr(renderer->Name(), "softpipe") != NULL
			|| strstr(renderer->Name(), "Software") != NULL)) {
		delete renderer;
		return;
	}

	fGPURenderer = renderer;
	fRenderer = renderer;
	fChooseRenderer = choice != "opengl";
	fBuilder.Invalidate();
}


// #pragma mark - scrolling


void
TermView::ScrollTo(BPoint where)
{
	// the view itself does not move: the rows drawn do
	int32 rows = (int32)roundf(-where.y / fFontHeight);
	_ScrollToRows(rows);
}


void
TermView::_ScrollToRows(int32 rows)
{
	fTerminal->Lock();
	int32 historySize = fTerminal->HistorySize();
	fTerminal->Unlock();

	rows = std::max((int32)0, std::min(rows, historySize));
	if (rows == fScrollRows)
		return;
	fScrollRows = rows;
	if (fScrollBar != NULL && fScrollBar->Value() != -rows * fFontHeight)
		fScrollBar->SetValue(-rows * fFontHeight);
	_ScheduleFrame();
}


void
TermView::_ScrollTo(float y, bool scrollGfx)
{
	_ScrollToRows((int32)roundf(-y / fFontHeight));
}


void
TermView::_ScrollToBottom()
{
	_ScrollToRows(0);
}


void
TermView::_ScrollToRange(TermPoint start, TermPoint end)
{
	if (start > end)
		std::swap(start, end);

	int32 first = _FirstRow();
	int32 last = first + fRows - 1;
	if (start.y >= first && end.y <= last)
		return;

	int32 top;
	if (end.y - start.y + 1 > fRows || start.y < first)
		top = start.y;
	else
		top = end.y - fRows + 1;
	_ScrollToRows(-std::min(top, (int32)0));
}


//! kitty's shell integration: go to the previous or next prompt.
void
TermView::_ScrollToPrompt(bool forward)
{
	fTerminal->Lock();
	int32 row;
	bool found = fTerminal->FindPrompt(_FirstRow(), forward, row);
	fTerminal->Unlock();
	if (found)
		_ScrollToRows(-std::min(row, (int32)0));
	else if (forward)
		_ScrollToBottom();
}


void
TermView::TargetedByScrollView(BScrollView *scrollView)
{
	BView::TargetedByScrollView(scrollView);

	SetScrollBar(scrollView ? scrollView->ScrollBar(B_VERTICAL) : NULL);
}


void
TermView::SetScrollBar(BScrollBar *scrollBar)
{
	fScrollBar = scrollBar;
	if (fScrollBar != NULL) {
		fScrollBar->SetSteps(fFontHeight, fFontHeight * fRows);
		_UpdateScrollBarRange();
	}
}


// #pragma mark - selection and clipboard


//! Returns the square of the actual pixel distance between both points
float
TermView::_MouseDistanceSinceLastClick(BPoint where)
{
	return (fLastClickPoint.x - where.x) * (fLastClickPoint.x - where.x)
		+ (fLastClickPoint.y - where.y) * (fLastClickPoint.y - where.y);
}


//! Select a range of text.
void
TermView::_Select(TermPoint start, TermPoint end, bool inclusive,
	bool setInitialSelection)
{
	if (end < start)
		std::swap(start, end);

	if (inclusive)
		end.x++;

	if (start.x < 0)
		start.x = 0;
	if (end.x >= fColumns)
		end.x = fColumns;

	fTerminal->Lock();
	int32 historySize = fTerminal->HistorySize();
	TermPoint minPos(0, -historySize);
	TermPoint maxPos(0, fTerminal->Rows());
	start = std::max(minPos, std::min(start, maxPos));
	end = std::max(minPos, std::min(end, maxPos));

	// if the end is past the end of the line, select the line break, too
	if (fTerminal->LineLength(end.y) < end.x && end.y < fTerminal->Rows()) {
		end.y++;
		end.x = 0;
	}

	// whole double width characters
	TerminalCell cell;
	if (fTerminal->GetCell(start.y, start.x, cell) && cell.IsWideTail()
		&& start.x > 0) {
		start.x--;
	}
	if (fTerminal->GetCell(end.y, end.x, cell) && cell.IsWideTail()) {
		end.x++;
		if (end.x >= fColumns)
			end.x = fColumns;
	}
	fTerminal->Unlock();

	fSelection.start = start;
	fSelection.end = end;

	if (setInitialSelection) {
		fInitialSelectionStart = fSelection.start;
		fInitialSelectionEnd = fSelection.end;
	}

	_ScheduleFrame();
}


//! Extend selection (shift + mouse click).
void
TermView::_ExtendSelection(TermPoint pos, bool inclusive,
	bool useInitialSelection)
{
	if (!useInitialSelection && !_HasSelection())
		return;

	TermPoint start = fSelection.start;
	TermPoint end = fSelection.end;

	if (useInitialSelection) {
		start = fInitialSelectionStart;
		end = fInitialSelectionEnd;
	}

	if (inclusive) {
		if (pos >= start && pos >= end)
			pos.x++;
	}

	if (pos < start)
		_Select(pos, end, false, !useInitialSelection);
	else if (pos > end)
		_Select(start, pos, false, !useInitialSelection);
	else if (useInitialSelection)
		_Select(start, end, false, false);
}


// clear the selection.
void
TermView::_Deselect()
{
	if (fSelection.IsEmpty())
		return;
	fSelection.start = fSelection.end = TermPoint(0, 0);
	fInitialSelectionStart = fInitialSelectionEnd = TermPoint(0, 0);
	_ScheduleFrame();
}


bool
TermView::_HasSelection() const
{
	return !fSelection.IsEmpty();
}


struct ClassifierCookie {
	DefaultCharClassifier*	classifier;
};


static bool
is_word_char(uint32 c, void* cookie)
{
	DefaultCharClassifier* classifier = (DefaultCharClassifier*)cookie;
	return classifier->Classify(c) == CHAR_TYPE_WORD_CHAR;
}


bool
TermView::_FindWord(TermPoint position, DefaultCharClassifier* classifier,
	bool findNonWords, TermPoint& start, TermPoint& end) const
{
	fTerminal->Lock();
	bool found = fTerminal->FindWord(position, &is_word_char, classifier,
		findNonWords, start, end);
	fTerminal->Unlock();
	return found;
}


void
TermView::_SelectWord(BPoint where, bool extend, bool useInitialSelection)
{
	TermPoint pos = _ConvertToTerminal(where);
	TermPoint start, end;
	if (!_FindWord(pos, fCharClassifier, true, start, end))
		return;

	if (extend) {
		if (start
				< (useInitialSelection
					? fInitialSelectionStart : fSelection.start)) {
			_ExtendSelection(start, false, useInitialSelection);
		} else if (end
				> (useInitialSelection
					? fInitialSelectionEnd : fSelection.end)) {
			_ExtendSelection(end, false, useInitialSelection);
		} else if (useInitialSelection)
			_Select(start, end, false, false);
	} else
		_Select(start, end, false, !useInitialSelection);
}


void
TermView::_SelectLine(BPoint where, bool extend, bool useInitialSelection)
{
	TermPoint start = TermPoint(0, _ConvertToTerminal(where).y);
	TermPoint end = TermPoint(0, start.y + 1);

	if (extend) {
		if (start
				< (useInitialSelection
					? fInitialSelectionStart : fSelection.start)) {
			_ExtendSelection(start, false, useInitialSelection);
		} else if (end
				> (useInitialSelection
					? fInitialSelectionEnd : fSelection.end)) {
			_ExtendSelection(end, false, useInitialSelection);
		} else if (useInitialSelection)
			_Select(start, end, false, false);
	} else
		_Select(start, end, false, !useInitialSelection);
}


void
TermView::_GetText(TermPoint start, TermPoint end, BString& text) const
{
	std::string string;
	fTerminal->Lock();
	fTerminal->GetText(start, end, string);
	fTerminal->Unlock();
	text.SetTo(string.data(), string.size());
}


void
TermView::_SetLinkHighlight(TermPoint start, TermPoint end)
{
	if (start == end) {
		if (fLinkHighlights.empty())
			return;
		fLinkHighlights.clear();
	} else {
		if (fLinkHighlights.size() == 1 && fLinkHighlights[0].start == start
			&& fLinkHighlights[0].end == end)
			return;
		Highlight highlight;
		highlight.start = start;
		highlight.end = end;
		fLinkHighlights.assign(1, highlight);
	}
	_ScheduleFrame();
}


void
TermView::_SetLinkHighlight(const TermLinkRange& link)
{
	std::vector<Highlight> highlights;
	for (size_t i = 0; i < link.segments.size(); i++) {
		Highlight highlight;
		highlight.start = link.segments[i].start;
		highlight.end = link.segments[i].end;
		highlights.push_back(highlight);
	}
	bool same = highlights.size() == fLinkHighlights.size();
	for (size_t i = 0; same && i < highlights.size(); i++) {
		same = highlights[i].start == fLinkHighlights[i].start
			&& highlights[i].end == fLinkHighlights[i].end;
	}
	if (same)
		return;
	fLinkHighlights.swap(highlights);
	_ScheduleFrame();
}


void
TermView::_ClearLinkHighlight()
{
	_SetLinkHighlight(TermPoint(0, 0), TermPoint(0, 0));
}


/*!	The link at \a where: an OSC 8 hyperlink, or an address in the text
	(see TerminalLinks.cpp).
*/
bool
TermView::_LinkAt(BPoint where, TermLinkRange& _link)
{
	TermPoint position = _ConvertToTerminal(where);
	fTerminal->Lock();
	bool found = FindTerminalLink(*fTerminal, position, _link);
	fTerminal->Unlock();
	return found;
}


bool
TermView::_ClicksOpenLinks() const
{
	return PrefHandler::Default()->getBool(PREF_OPEN_LINKS_ON_CLICK);
}


/*!	Whether the click at \a when is the one that brought the window to the
	front, which is not to open anything. (The window may hear that it is
	active just before or just after the click.)
*/
bool
TermView::_IsActivatingClick(bigtime_t when) const
{
	bigtime_t difference = when - fActivatedTime;
	return difference > -300000 && difference < 300000;
}


/*!	Underlines the link under the pointer and shows the link pointer, as a
	click opens it.
*/
void
TermView::_UpdateHoverLink(BPoint where)
{
	if (fMouseButtons != 0 || Window() == NULL || !Window()->IsActive()
		|| !_ClicksOpenLinks()) {
		_ClearHoverLink();
		return;
	}

	TermPoint cell = _ConvertToTerminal(where);
	if (cell == fHoverCell)
		return;

	TermLinkRange link;
	if (!_LinkAt(where, link)) {
		_ClearHoverLink();
		fHoverCell = cell;
		return;
	}

	fHoverCell = cell;
	fTerminal->Lock();
	fHoverGeneration = fTerminal->ContentGeneration();
	fTerminal->Unlock();
	_SetLinkHighlight(link);
	if (!fHoverLink) {
		fHoverLink = true;
		BCursor cursor(B_CURSOR_ID_FOLLOW_LINK);
		SetViewCursor(&cursor);
	}
}


void
TermView::_ClearHoverLink()
{
	fHoverCell = TermPoint(-1, -1);
	if (!fHoverLink)
		return;
	fHoverLink = false;
	if (fActiveState != fHyperLinkState && fActiveState != fLinkClickState)
		_ClearLinkHighlight();
	_SetPointerShape(fPointerShape.String());
}


void
TermView::_PutOnClipboard(BClipboard* clipboard, const char* text,
	size_t length)
{
	if (!clipboard->Lock())
		return;
	clipboard->Clear();
	if (BMessage* message = clipboard->Data()) {
		message->AddData("text/plain", B_MIME_TYPE, text, length);
		clipboard->Commit();
	}
	clipboard->Unlock();
}


void
TermView::Copy(BClipboard *clipboard)
{
	if (!_HasSelection())
		return;

	BString copyStr;
	_GetText(fSelection.start, fSelection.end, copyStr);
	_PutOnClipboard(clipboard, copyStr.String(), copyStr.Length());
}


void
TermView::_PasteText(const char* text, size_t length)
{
	std::string output;
	fTerminal->Lock();
	fTerminal->EncodePaste(text, length, output);
	fTerminal->Unlock();
	_WritePTY(output.data(), output.size());
}


void
TermView::Paste(BClipboard *clipboard)
{
	if (clipboard->Lock()) {
		BMessage *clipMsg = clipboard->Data();
		const char* text;
		ssize_t numBytes;
		if (clipMsg->FindData("text/plain", B_MIME_TYPE,
				(const void**)&text, &numBytes) == B_OK ) {
			BString copy(text, numBytes);
			clipboard->Unlock();
			_PasteText(copy.String(), copy.Length());
		} else
			clipboard->Unlock();

		_ScrollToBottom();
	}
}


void
TermView::SyncClipboard()
{
	if (be_clipboard != fMouseClipboard && be_clipboard->Lock()) {
		if (fMouseClipboard->Lock()) {
			BMessage* clipMsgA = be_clipboard->Data();
			const char* text;
			ssize_t numBytes;
			if (clipMsgA->FindData("text/plain", B_MIME_TYPE,
					(const void**)&text, &numBytes) == B_OK ) {
				fMouseClipboard->Clear();
				BMessage* clipMsgB = fMouseClipboard->Data();
				clipMsgB->AddData("text/plain", B_MIME_TYPE,
					text, numBytes);
				fMouseClipboard->Commit();
			}
			fMouseClipboard->Unlock();
		}
		be_clipboard->Unlock();
	}
}


void
TermView::SelectAll()
{
	fTerminal->Lock();
	int32 historySize = fTerminal->HistorySize();
	int32 rows = fTerminal->Rows();
	fTerminal->Unlock();

	_Select(TermPoint(0, -historySize), TermPoint(0, rows), false, true);
}


void
TermView::Clear()
{
	_Deselect();

	fTerminal->Lock();
	fTerminal->ClearAll();
	fLinesScrolled = fTerminal->LinesScrolled();
	fTerminal->Unlock();

	fScrollRows = 0;
	_UpdateScrollBarRange();
	_InvalidateAll();
}


// Find a string, and select it if found
bool
TermView::Find(const BString &str, bool forwardSearch, bool matchCase,
	bool matchWord)
{
	TermPoint start;
	fTerminal->Lock();
	if (_HasSelection()) {
		if (forwardSearch)
			start = fSelection.end;
		else
			start = fSelection.start;
	} else {
		// search from the very beginning/end
		if (forwardSearch)
			start = TermPoint(0, -fTerminal->HistorySize());
		else
			start = TermPoint(0, fTerminal->Rows());
	}

	TermPoint matchStart, matchEnd;
	bool found = fTerminal->Find(str.String(), start, forwardSearch,
		matchCase, matchWord, matchStart, matchEnd);
	fTerminal->Unlock();
	if (!found)
		return false;

	_Select(matchStart, matchEnd, false, true);
	_ScrollToRange(fSelection.start, fSelection.end);

	return true;
}


//! Get the selected text and copy to str
void
TermView::GetSelection(BString &str) const
{
	str.SetTo("");
	_GetText(fSelection.start, fSelection.end, str);
}


bool
TermView::CheckShellGone() const
{
	if (!fShell)
		return false;

	// check, if the shell does still live
	pid_t pid = fShell->ProcessID();
	team_info info;
	return get_team_info(pid, &info) == B_BAD_TEAM_ID;
}


void
TermView::InitiateDrag()
{
	BString copyStr("");
	_GetText(fSelection.start, fSelection.end, copyStr);

	BMessage message(B_MIME_DATA);
	message.AddData("text/plain", B_MIME_TYPE, copyStr.String(),
		copyStr.Length());

	BPoint start = _ConvertFromTerminal(fSelection.start);
	BPoint end = _ConvertFromTerminal(fSelection.end);

	BRect rect;
	if (fSelection.start.y == fSelection.end.y)
		rect.Set(start.x, start.y, end.x + fFontWidth, end.y + fFontHeight);
	else
		rect.Set(0, start.y, fColumns * fFontWidth, end.y + fFontHeight);

	rect = rect & Bounds();

	DragMessage(&message, rect);
}


// #pragma mark - messages


void
TermView::MessageReceived(BMessage *message)
{
	if (fActiveState->MessageReceived(message))
		return;

	entry_ref ref;
	const char *ctrl_l = "\x0c";

	if (message->what == kFileTransferAnswer && fFileTransfer != NULL) {
		fFileTransfer->Answer(message->GetString("id", ""),
			message->GetInt32("which", 0) == 1);
		return;
	}
	if (message->what == kFileTransferPump && fFileTransfer != NULL) {
		fFileTransfer->Pump();
		return;
	}

	// a program that takes drops (kitty's drag and drop) gets them
	if (message->WasDropped() && fDragAndDrop != NULL
		&& fDragAndDrop->IsAcceptingDrops()) {
		BPoint where = message->DropPoint();
		ConvertFromScreen(&where);
		if (fDragAndDrop->Dropped(message, (int32)(where.x / fFontWidth),
				(int32)(where.y / fFontHeight), (int32)(where.x * fScale),
				(int32)(where.y * fScale))) {
			return;
		}
	}

	// first check for any dropped message
	if (message->WasDropped() && (message->what == B_SIMPLE_DATA
			|| message->what == B_MIME_DATA)) {
		char *text;
		ssize_t numBytes;
		//rgb_color *color;

		int32 i = 0;

		if (message->FindRef("refs", i++, &ref) == B_OK) {
			// first check if secondary mouse button is pressed
			int32 buttons = 0;
			message->FindInt32("buttons", &buttons);

			if (buttons == B_SECONDARY_MOUSE_BUTTON) {
				// start popup menu
				_SecondaryMouseButtonDropped(message);
				return;
			}

			_DoFileDrop(ref);

			while (message->FindRef("refs", i++, &ref) == B_OK) {
				_WritePTY(" ", 1);
				_DoFileDrop(ref);
			}
			return;
		} else if (message->FindData("text/plain", B_MIME_TYPE,
				(const void **)&text, &numBytes) == B_OK) {
			_PasteText(text, numBytes);
			return;
		}
	}

	switch (message->what) {
		case B_SIMPLE_DATA:
		case B_REFS_RECEIVED:
		{
			// handle refs if they weren't dropped
			int32 i = 0;
			if (message->FindRef("refs", i++, &ref) == B_OK) {
				_DoFileDrop(ref);

				while (message->FindRef("refs", i++, &ref) == B_OK) {
					_WritePTY(" ", 1);
					_DoFileDrop(ref);
				}
			} else
				BView::MessageReceived(message);
			break;
		}

		case B_COPY:
			Copy(be_clipboard);
			break;

		case B_PASTE:
		{
			int32 code;
			if (message->FindInt32("index", &code) == B_OK)
				Paste(be_clipboard);
			break;
		}

		case B_CLIPBOARD_CHANGED:
			// This message originates from the system clipboard. Overwrite
			// the contents of the mouse clipboard with the ones from the
			// system clipboard, in case it contains text data.
			SyncClipboard();
			break;

		case B_SELECT_ALL:
			SelectAll();
			break;

		case B_SET_PROPERTY:
		{
			int32 i;
			int32 encodingID;
			BMessage specifier;
			if (message->GetCurrentSpecifier(&i, &specifier) == B_OK
				&& strcmp("encoding",
					specifier.FindString("property", i)) == 0) {
				message->FindInt32 ("data", &encodingID);
				SetEncoding(encodingID);
				message->SendReply(B_REPLY);
			} else {
				BView::MessageReceived(message);
			}
			break;
		}

		case B_GET_PROPERTY:
		{
			int32 i;
			BMessage specifier;
			if (message->GetCurrentSpecifier(&i, &specifier) == B_OK) {
				const char* property = specifier.FindString("property", i);
				if (strcmp("encoding", property) == 0) {
					BMessage reply(B_REPLY);
					reply.AddInt32("result", Encoding());
					message->SendReply(&reply);
				} else if (strcmp("tty", property) == 0) {
					BMessage reply(B_REPLY);
					reply.AddString("result", TerminalName());
					message->SendReply(&reply);
				} else if (strcmp("renderer", property) == 0) {
					BMessage reply(B_REPLY);
					reply.AddString("result", RendererName());
					message->SendReply(&reply);
				} else if (strcmp("contents", property) == 0) {
					BString text;
					_GetText(TermPoint(0, 0), TermPoint(0, fRows), text);
					BMessage reply(B_REPLY);
					reply.AddString("result", text);
					message->SendReply(&reply);
				} else
					BView::MessageReceived(message);
			} else
				BView::MessageReceived(message);
			break;
		}

		case B_EXECUTE_PROPERTY:
		{
			int32 i;
			BMessage specifier;
			if (message->GetCurrentSpecifier(&i, &specifier) == B_OK
				&& strcmp("snapshot",
					specifier.FindString("property", i)) == 0) {
				// for tests: the frame as drawn, at full density
				_RenderFrame();
				status_t status = B_NO_INIT;
				const char* path = message->GetString("path",
					"/boot/home/terminal-snapshot.png");
				if (fBitmap != NULL) {
					BFile file(path, B_WRITE_ONLY | B_CREATE_FILE
						| B_ERASE_FILE);
					BBitmapStream stream(new BBitmap(fBitmap));
					status = BTranslatorRoster::Default()->Translate(&stream,
						NULL, NULL, &file, B_PNG_FORMAT);
					BBitmap* bitmap = NULL;
					stream.DetachBitmap(&bitmap);
					delete bitmap;
				}
				BMessage reply(B_REPLY);
				reply.AddInt32("error", status);
				message->SendReply(&reply);
				break;
			}
			if (message->GetCurrentSpecifier(&i, &specifier) == B_OK
				&& strcmp("command",
					specifier.FindString("property", i)) == 0) {

				Shell* shell = _DetachShell();
				shell->Close();

				int32 argc = 0;
				const char** argv = NULL;
				_GetArgumentsFromMessage(message, argv, argc);

				if (message->GetBool("clear", false))
					Clear();

				ShellParameters shellParameters(argc, argv);
				shellParameters.SetEncoding(fEncoding);
				_AttachShell(shell, shellParameters);

				delete[] argv;

				message->SendReply(B_REPLY);
			} else {
				BView::MessageReceived(message);
			}
			break;
		}

		case B_MODIFIERS_CHANGED:
		{
			_UpdateModifiers();
			break;
		}

		case B_INPUT_METHOD_EVENT:
		{
			int32 opcode;
			if (message->FindInt32("be:opcode", &opcode) == B_OK) {
				switch (opcode) {
					case B_INPUT_METHOD_STARTED:
					{
						BMessenger messenger;
						if (message->FindMessenger("be:reply_to",
								&messenger) == B_OK) {
							fInline = new (std::nothrow)
								InlineInput(messenger);
						}
						break;
					}

					case B_INPUT_METHOD_STOPPED:
						delete fInline;
						fInline = NULL;
						break;

					case B_INPUT_METHOD_CHANGED:
						if (fInline != NULL)
							_HandleInputMethodChanged(message);
						break;

					case B_INPUT_METHOD_LOCATION_REQUEST:
						if (fInline != NULL)
							_HandleInputMethodLocationRequest();
						break;

					default:
						break;
				}
			}
			break;
		}

		case B_MOUSE_WHEEL_CHANGED:
		{
			float deltaX = 0;
			float deltaY = 0;
			message->FindFloat("be:wheel_delta_x", &deltaX);
			message->FindFloat("be:wheel_delta_y", &deltaY);
			BPoint where;
			uint32 buttons;
			GetMouse(&where, &buttons, false);

			fTerminal->Lock();
			bool reports = fTerminal->MouseTracking() != MOUSE_TRACKING_NONE;
			bool alternate = fTerminal->IsAlternateScreen();
			bool alternateScroll = fTerminal->IsAlternateScroll();
			bool cursorKeys = fTerminal->IsApplicationCursorKeys();
			fTerminal->Unlock();

			if (reports && (modifiers() & B_SHIFT_KEY) == 0) {
				// the program gets the wheel as buttons 4 to 7
				int32 steps = (int32)fabsf(deltaY);
				for (int32 i = 0; i < steps; i++) {
					_SendMouseEvent(MOUSE_PRESS, deltaY > 0
						? MOUSE_BUTTON_WHEEL_DOWN : MOUSE_BUTTON_WHEEL_UP,
						where, modifiers());
				}
				steps = (int32)fabsf(deltaX);
				for (int32 i = 0; i < steps; i++) {
					_SendMouseEvent(MOUSE_PRESS, deltaX > 0
						? MOUSE_BUTTON_WHEEL_RIGHT : MOUSE_BUTTON_WHEEL_LEFT,
						where, modifiers());
				}
				break;
			}

			if (alternate && alternateScroll && deltaY != 0) {
				// On the alternate screen there is no history: emulate
				// scrolling with the cursor keys (DECSET 1007, on unless
				// the program or the "Alternate screen scroll" setting
				// turned it off).
				// scroll pagewise, if one of Option, Command, or Control is
				// pressed
				int32 steps;
				const char* stepString;
				if ((modifiers() & B_SHIFT_KEY) != 0) {
					// pagewise
					stepString = deltaY > 0 ? "\x1b[6~" : "\x1b[5~";
					steps = abs((int)deltaY);
				} else {
					// three lines per step
					stepString = deltaY > 0
						? (cursorKeys ? "\x1bOB" : "\x1b[B")
						: (cursorKeys ? "\x1bOA" : "\x1b[A");
					steps = 3 * abs((int)deltaY);
				}

				// We want to do only a single write(), so compose a string
				// repeating the sequence as often as required by the delta.
				BString toWrite;
				for (int32 i = 0; i <steps; i++)
					toWrite << stepString;

				_WritePTY(toWrite.String(), toWrite.Length());
			} else {
				// let the BView's implementation handle the standard
				// scrolling
				BView::MessageReceived(message);
			}

			break;
		}

		case MENU_CLEAR_ALL:
			Clear();
			_Send(ctrl_l);
			break;
		case kBlinkCursor:
			_BlinkCursor();
			break;
		case kUpdateSigWinch:
			_UpdateSIGWINCH();
			break;
		case kSecondaryMouseDropAction:
			_DoSecondaryMouseDropAction(message);
			break;
		case kFrameNeeded:
			fFramePending = false;
			_ScheduleFrame();
			break;
		case kRenderFrame:
			_RenderFrame();
			break;
		case kEventsPending:
			_HandleEvents();
			break;
		case kAnimate:
		{
			delete fAnimationRunner;
			fAnimationRunner = NULL;
			fTerminal->Lock();
			bool changed = false;
			bigtime_t next = B_INFINITE_TIMEOUT;
			TerminalGraphics* graphics = fTerminal->Graphics();
			if (graphics != NULL)
				next = graphics->Animate(system_time(), changed);
			fTerminal->Unlock();
			if (changed)
				_ScheduleFrame();
			if (next != B_INFINITE_TIMEOUT) {
				BMessage animate(kAnimate);
				fAnimationRunner = new(std::nothrow) BMessageRunner(
					BMessenger(this), &animate,
					std::max((bigtime_t)5000, next - system_time()), 1);
			}
			break;
		}
		case kClipboardReadAnswer:
		{
			// the user's answer to reading the clipboard (0 = allow)
			int32 which = message->GetInt32("which", 1);
			int32 type = message->GetInt32("type", EVENT_REQUEST_CLIPBOARD);
			bool primary = message->GetBool("primary", false);
			BClipboard* clipboard = primary ? fMouseClipboard : be_clipboard;

			std::string output;
			if (type == EVENT_REQUEST_CLIPBOARD) {
				// OSC 52: text only, nothing if denied
				if (which != 0)
					break;
				BString text;
				if (clipboard->Lock()) {
					const char* data;
					ssize_t length;
					if (clipboard->Data()->FindData("text/plain",
							B_MIME_TYPE, (const void**)&data, &length)
							== B_OK) {
						text.SetTo(data, length);
					}
					clipboard->Unlock();
				}
				fTerminal->Lock();
				fTerminal->ReplyClipboard(
					message->GetString("selections", "c"), text.String(),
					text.Length());
				fTerminal->TakeOutput(output);
				fTerminal->Unlock();
			} else {
				const char* identifier = message->GetString("id", "");
				ClipboardData data;
				bool listOnly = false;
				if (which == 0 && clipboard->Lock()) {
					BMessage* contents = clipboard->Data();
					const char* mime;
					for (int32 i = 0; message->FindString("mime", i, &mime)
							== B_OK; i++) {
						if (strcmp(mime, ".") == 0) {
							listOnly = true;
							// the types on the clipboard
							std::string list;
							char* name;
							type_code typeFound;
							for (int32 j = 0; contents->GetInfo(B_MIME_TYPE,
									j, &name, &typeFound) == B_OK; j++) {
								if (!list.empty())
									list += " ";
								list += name;
							}
							list += "\n";
							data.push_back(std::make_pair(std::string("."),
								list));
							continue;
						}
						const void* bytes;
						ssize_t length;
						if (contents->FindData(mime, B_MIME_TYPE, &bytes,
								&length) == B_OK) {
							data.push_back(std::make_pair(std::string(mime),
								std::string((const char*)bytes, length)));
						}
					}
					clipboard->Unlock();
				}
				(void)listOnly;
				fTerminal->Lock();
				fTerminal->ReplyClipboardRead(identifier, primary,
					which == 0 ? "OK" : "EPERM", which == 0 ? &data : NULL);
				fTerminal->TakeOutput(output);
				fTerminal->Unlock();
			}
			_Send(output);
			break;
		}
		case MSG_SET_CURSOR_STYLE:
		{
			int cursorStyle = PrefHandler::Default()->getCursor(
				PREF_CURSOR_STYLE);
			int32 shape = cursorStyle == UNDERLINE_CURSOR
				? CURSOR_SHAPE_UNDERLINE : cursorStyle == IBEAM_CURSOR
					? CURSOR_SHAPE_BEAM : CURSOR_SHAPE_BLOCK;
			fTerminal->Lock();
			fTerminal->SetDefaultCursorShape(shape, fCursorBlinking);
			fTerminal->Unlock();
			SwitchCursorBlinking();
			break;
		}
		case MSG_REMOVE_RESIZE_VIEW_IF_NEEDED:
		{
			BPoint point;
			uint32 buttons;
			GetMouse(&point, &buttons, false);
			if (buttons != 0)
				break;

			if (fResizeView != NULL) {
				fResizeView->RemoveSelf();
				delete fResizeView;
				fResizeView = NULL;
			}
			delete fResizeRunner;
			fResizeRunner = NULL;
			break;
		}

		case MSG_QUIT_TERMNAL:
		{
			int32 reason;
			if (message->FindInt32("reason", &reason) != B_OK)
				reason = 0;
			if (fListener != NULL)
				fListener->NotifyTermViewQuit(this, reason);
			break;
		}
		default:
			BView::MessageReceived(message);
			break;
	}
}


status_t
TermView::GetSupportedSuites(BMessage* message)
{
	BPropertyInfo propInfo(sPropList);
	message->AddString("suites", "suite/vnd.naan-termview");
	message->AddFlat("messages", &propInfo);
	return BView::GetSupportedSuites(message);
}


BHandler*
TermView::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 what, const char* property)
{
	BHandler* target = this;
	BPropertyInfo propInfo(sPropList);
	if (propInfo.FindMatch(message, index, specifier, what, property) < B_OK) {
		target = BView::ResolveSpecifier(message, index, specifier, what,
			property);
	}

	return target;
}


// #pragma mark - drag and drop


void
TermView::_SecondaryMouseButtonDropped(BMessage* message)
{
	// Launch menu to choose what is to do with the message data
	BPoint point;
	if (message->FindPoint("_drop_point_", &point) != B_OK)
		return;

	BMessage* insertMessage = new BMessage(*message);
	insertMessage->what = kSecondaryMouseDropAction;
	insertMessage->AddInt8("action", kInsert);

	BMessage* cdMessage = new BMessage(*message);
	cdMessage->what = kSecondaryMouseDropAction;
	cdMessage->AddInt8("action", kChangeDirectory);

	BMessage* lnMessage = new BMessage(*message);
	lnMessage->what = kSecondaryMouseDropAction;
	lnMessage->AddInt8("action", kLinkFiles);

	BMessage* mvMessage = new BMessage(*message);
	mvMessage->what = kSecondaryMouseDropAction;
	mvMessage->AddInt8("action", kMoveFiles);

	BMessage* cpMessage = new BMessage(*message);
	cpMessage->what = kSecondaryMouseDropAction;
	cpMessage->AddInt8("action", kCopyFiles);

	BMenuItem* insertItem = new BMenuItem(
		B_TRANSLATE("Insert path"), insertMessage);
	BMenuItem* cdItem = new BMenuItem(
		B_TRANSLATE("Change directory"), cdMessage);
	BMenuItem* lnItem = new BMenuItem(
		B_TRANSLATE("Create link here"), lnMessage);
	BMenuItem* mvItem = new BMenuItem(B_TRANSLATE("Move here"), mvMessage);
	BMenuItem* cpItem = new BMenuItem(B_TRANSLATE("Copy here"), cpMessage);
	BMenuItem* chItem = new BMenuItem(B_TRANSLATE("Cancel"), NULL);

	// if the refs point to different directorys disable the cd menu item
	bool differentDirs = false;
	BDirectory firstDir;
	entry_ref ref;
	int i = 0;
	while (message->FindRef("refs", i++, &ref) == B_OK) {
		BNode node(&ref);
		BEntry entry(&ref);
		BDirectory dir;
		if (node.IsDirectory())
			dir.SetTo(&ref);
		else
			entry.GetParent(&dir);

		if (i == 1) {
			node_ref nodeRef;
			dir.GetNodeRef(&nodeRef);
			firstDir.SetTo(&nodeRef);
		} else if (firstDir != dir) {
			differentDirs = true;
			break;
		}
	}
	if (differentDirs)
		cdItem->SetEnabled(false);

	BPopUpMenu *menu = new BPopUpMenu(
		"Secondary mouse button drop menu");
	menu->SetAsyncAutoDestruct(true);
	menu->AddItem(insertItem);
	menu->AddSeparatorItem();
	menu->AddItem(cdItem);
	menu->AddItem(lnItem);
	menu->AddItem(mvItem);
	menu->AddItem(cpItem);
	menu->AddSeparatorItem();
	menu->AddItem(chItem);
	menu->SetTargetForItems(this);
	menu->Go(point, true, true, true);
}


void
TermView::_DoSecondaryMouseDropAction(BMessage* message)
{
	int8 action = -1;
	message->FindInt8("action", &action);

	BString outString = "";
	BString itemString = "";

	switch (action) {
		case kInsert:
			break;
		case kChangeDirectory:
			outString = "cd ";
			break;
		case kLinkFiles:
			outString = "ln -s ";
			break;
		case kMoveFiles:
			outString = "mv ";
			break;
		case kCopyFiles:
			outString = "cp ";
			break;

		default:
			return;
	}

	bool listContainsDirectory = false;
	entry_ref ref;
	int32 i = 0;
	while (message->FindRef("refs", i++, &ref) == B_OK) {
		BEntry ent(&ref);
		BNode node(&ref);
		BPath path(&ent);
		BString string(path.Path());

		if (node.IsDirectory())
			listContainsDirectory = true;

		if (i > 1)
			itemString += " ";

		if (action == kChangeDirectory) {
			if (!node.IsDirectory()) {
				int32 slash = string.FindLast("/");
				string.Truncate(slash);
			}
			string.CharacterEscape(kShellEscapeCharacters, '\\');
			itemString += string;
			break;
		}
		string.CharacterEscape(kShellEscapeCharacters, '\\');
		itemString += string;
	}

	if (listContainsDirectory && action == kCopyFiles)
		outString += "-R ";

	outString += itemString;

	if (action == kLinkFiles || action == kMoveFiles || action == kCopyFiles)
		outString += " .";

	if (action != kInsert)
		outString += "\n";

	_WritePTY(outString.String(), outString.Length());
}


//! Gets dropped file full path and display it at cursor position.
void
TermView::_DoFileDrop(entry_ref& ref)
{
	BEntry ent(&ref);
	BPath path(&ent);
	BString string(path.Path());

	string.CharacterEscape(kShellEscapeCharacters, '\\');
	_WritePTY(string.String(), string.Length());
}


// #pragma mark - input methods


void
TermView::_DrawInlineMethodString()
{
	if (!fInline || !fInline->String())
		return;

	fTerminal->Lock();
	Terminal::CursorInfo cursor = fTerminal->Cursor();
	fTerminal->Unlock();

	const int32 numChars = BString(fInline->String()).CountChars();

	BPoint startPoint = _ConvertFromTerminal(TermPoint(cursor.x, cursor.y));
	BPoint endPoint = startPoint;
	endPoint.x += fFontWidth * numChars;
	endPoint.y += fFontHeight + 1;

	BRect eraseRect(startPoint, endPoint);

	PushState();
	SetHighColor(fTextForeColor);
	FillRect(eraseRect);
	PopState();

	BPoint loc = _ConvertFromTerminal(TermPoint(cursor.x, cursor.y));
	font_height height;
	fFont.GetHeight(&height);
	loc.y += height.ascent;
	SetFont(&fFont);
	SetHighColor(fTextBackColor);
	SetLowColor(fTextForeColor);
	DrawString(fInline->String(), loc);
}


void
TermView::_HandleInputMethodChanged(BMessage *message)
{
	const char *string = NULL;
	if (message->FindString("be:string", &string) < B_OK || string == NULL)
		return;

	_ActivateCursor(false);

	if (IsFocus())
		be_app->ObscureCursor();

	// If we find the "be:confirmed" boolean (and the boolean is true),
	// it means it's over for now, so the current InlineInput object
	// should become inactive. We will probably receive a
	// B_INPUT_METHOD_STOPPED message after this one.
	bool confirmed;
	if (message->FindBool("be:confirmed", &confirmed) != B_OK)
		confirmed = false;

	fInline->SetString("");

	Invalidate();

	fInline->SetString(string);
	fInline->ResetClauses();

	if (!confirmed && !fInline->IsActive())
		fInline->SetActive(true);

	// Get the clauses, and pass them to the InlineInput object
	// TODO: Find out if what we did it's ok, currently we don't consider
	// clauses at all, while the bebook says we should; though the visual
	// effect we obtained seems correct. Weird.
	int32 clauseCount = 0;
	int32 clauseStart;
	int32 clauseEnd;
	while (message->FindInt32("be:clause_start", clauseCount, &clauseStart)
			== B_OK
		&& message->FindInt32("be:clause_end", clauseCount, &clauseEnd)
			== B_OK) {
		if (!fInline->AddClause(clauseStart, clauseEnd))
			break;
		clauseCount++;
	}

	if (confirmed) {
		fInline->SetString("");
		_ActivateCursor(true);

		// the confirmed text is typed
		_WritePTY(string, strlen(string));
		_ScrollToBottom();
	} else {
		// temporarily show transient state of inline input
		int32 selectionStart = 0;
		int32 selectionEnd = 0;
		message->FindInt32("be:selection", 0, &selectionStart);
		message->FindInt32("be:selection", 1, &selectionEnd);

		fInline->SetSelectionOffset(selectionStart);
		fInline->SetSelectionLength(selectionEnd - selectionStart);
	}
	Invalidate();
}


void
TermView::_HandleInputMethodLocationRequest()
{
	BMessage message(B_INPUT_METHOD_EVENT);
	message.AddInt32("be:opcode", B_INPUT_METHOD_LOCATION_REQUEST);

	BString string(fInline->String());

	fTerminal->Lock();
	Terminal::CursorInfo cursor = fTerminal->Cursor();
	fTerminal->Unlock();

	const int32 &limit = string.CountChars();
	BPoint where = _ConvertFromTerminal(TermPoint(cursor.x, cursor.y));
	where.y += fFontHeight;

	for (int32 i = 0; i < limit; i++) {
		// Add the location of the UTF8 characters

		where.x += fFontWidth;
		ConvertToScreen(&where);

		message.AddPoint("be:location_reply", where);
		message.AddFloat("be:height_reply", fFontHeight);
	}

	fInline->Method()->SendMessage(&message);
}


void
TermView::_CancelInputMethod()
{
	if (!fInline)
		return;

	InlineInput *inlineInput = fInline;
	fInline = NULL;

	if (inlineInput->IsActive() && Window()) {
		Invalidate();

		BMessage message(B_INPUT_METHOD_EVENT);
		message.AddInt32("be:opcode", B_INPUT_METHOD_STOPPED);
		inlineInput->Method()->SendMessage(&message);
	}

	delete inlineInput;
}


void
TermView::_NextState(State* state)
{
	if (state != fActiveState) {
		if (fActiveState != NULL)
			fActiveState->Exited();
		fActiveState = state;
		fActiveState->Entered();
	}
}


// #pragma mark - debugging


void
TermView::MakeDebugSnapshots()
{
	// the screen's text, for bug reports
	BString text;
	_GetText(TermPoint(0, 0), TermPoint(0, fRows), text);
	BString path;
	path.SetToFormat("/tmp/%ld.TermView.dump", (long)time(NULL));
	FILE* file = fopen(path.String(), "w");
	if (file != NULL) {
		fwrite(text.String(), 1, text.Length(), file);
		fclose(file);
	}
}


void
TermView::StartStopDebugCapture()
{
}


// #pragma mark - Listener


TermView::Listener::~Listener()
{
}


void
TermView::Listener::NotifyTermViewQuit(TermView* view, int32 reason)
{
}


void
TermView::Listener::SetTermViewTitle(TermView* view, const char* title)
{
}


void
TermView::Listener::PreviousTermView(TermView* view)
{
}


void
TermView::Listener::NextTermView(TermView* view)
{
}
