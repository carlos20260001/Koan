# Accessibility interface and screen reader

air/OS issue haiku#54. Status: first working version. The interface is in
private headers (`headers/private/interface/Accessible.h`,
`AccessibilityProtocol.h`) so that roles, states and messages can still
change before the ABI is frozen.

Parts:

| Part | Where | What it does |
|------|-------|--------------|
| `BAccessible` and the bridge | libbe, `src/kits/interface/Accessibility.cpp` | default nodes for Interface Kit views, custom trees, events, answers queries |
| Interface Kit hooks | `BWindow`, `BView`, `BControl`, `BTextView`, `BListView`, `BTabView`, `BMenu`, `BColumnListView`, `BApplication` | focus, activation, value, text, caret, selection and menu events |
| `accessibility_server` | `src/servers/accessibility`, on-demand launch_daemon service | connects applications and assistive technologies |
| `ScreenReader` | `src/apps/screenreader` | speaks focus, values, windows; captions |
| `accessibility_keys` | input_server filter | Option+Command+S/F/W/R/X |
| tests | `src/tests/kits/interface/accessibility` | `AccessibilityTestApp`, `accessibility_tree_test` |

## Design

### Nothing runs while no assistive technology does

Applications never look for the accessibility server. The server is started
on demand by launch_daemon when an assistive technology (AT) subscribes to it,
and only then sends `B_ACCESSIBILITY_ENABLE` to every running application and
to each one launched later (it watches the roster). An application reacts by
starting its *bridge* (a `BLooper` named "accessibility bridge") and setting
`BPrivate::gAccessibilityEnabled`. When the last AT unsubscribes, the server
sends `B_ACCESSIBILITY_DISABLE`, the bridge quits, and the server itself quits
30 seconds later.

Every hook in the Interface Kit is `if (ACCESSIBILITY_ENABLED()) ...`, a
single predictable branch on a global bool. With no AT running, no thread is
started, no message is sent and no state is kept. `accessibility_tree_test`
checks that the test application has no bridge thread before it subscribes
and that the thread is gone after it unsubscribes.

One more global, `gAccessibilityViewData`, is set once a view has been given
an ID, an accessible name, or a custom tree; while it is set, `~BView` removes
the view from the registry (one locked map lookup).
`BColumnListView` attaches its own node, so applications with one pay that
lookup when they delete views.

### Nodes

A node is described as a `BMessage` (fields in `AccessibilityProtocol.h`):

| Field | Type | |
|-------|------|-|
| `id` | int64 | stable while the object exists |
| `parent` | int64 | 0 for the application root |
| `role`, `role_name` | int32, string | `accessible_role`, English name |
| `name` | string | label, title, accessible name |
| `description` | string | tool tip, alert text, app-set description |
| `value` | string | text, slider value, selected item, chosen menu entry |
| `states` | int64 | `B_ACCESSIBLE_STATE_*` bits |
| `frame` | BRect | screen coordinates |
| `actions` | int32 | `B_ACCESSIBLE_ACTION_*` bits |
| `child_count`, `child_ids` | int32, int64[] | children |
| `child` | message[] | nested children when a depth was asked for |
| `current`, `minimum`, `maximum` | double | ranges (sliders, spinners, progress) |
| `sel_start`, `sel_end`, `length` | int32 | text selection, text length |
| `index`, `count` | int32 | position among siblings ("2 of 5") |
| `level` | int32 | tree items, headings |
| `shortcut` | string | menu item shortcut |

Custom trees add their own fields (`AddProperties()`), for example `url`.

IDs are 64 bit: the upper half names a registered object (window, view or
`BAccessible`), the lower half a virtual item of it (0 = the object, n = list
item, menu item or tab n - 1). Item IDs are index based, so they change when
items are inserted before them.

### Default nodes for Interface Kit views

Nothing has to be done for windows built from standard controls. Roles,
names, values and states come from the views themselves:

| View | Role | Name / value / states |
|------|------|-----------------------|
| `BWindow` | window, dialog (modal), alert (`BAlert`, text as description) | title; active |
| `BButton` | button, toggle button | label; default, pressed, has popup |
| `BCheckBox`, `BRadioButton` | check box, radio button | label; checked, mixed |
| `BTextControl` | text field | label (without ":"); text; editable, protected (hidden typing); its `BTextView` is folded in |
| `BTextView` | text area, or label when neither editable nor selectable | text (first 4 KB), selection; multi-line |
| `BSpinner`, `BDecimalSpinner` | spin button | label; text and range |
| `BSlider`, `BChannelSlider` | slider | label; `UpdateText()` or value; range |
| `BScrollBar` | scroll bar | value, range, orientation |
| `BMenuField` | pop-up button | label; chosen item |
| `BMenuBar`, `BMenu` | menu bar, menu | items as children (label, shortcut, checked, submenu, disabled) |
| `BListView`, `BOutlineListView` | list, tree | selected item; items as children (text, selected, level, expanded) |
| `BColumnListView` | table | focused row's string fields (rows are not children yet) |
| `BTabView` | tab list | selected tab; tabs as children |
| `BStatusBar` | progress bar | label; text and percent |
| `BStringView` | label | text |
| `BBox`, `BScrollView`, plain views | group, scroll pane | box label |

Unlabeled controls fall back to their tool tip, then to a `BStringView` right
in front of them. Disabled controls are "disabled"; navigable views are
"focusable".

### Custom names and trees

```cpp
#include <Accessible.h>

// an icon-only button
BAccessible::SetName(backButton, "Back");
BAccessible::SetDescription(backButton, "Go to the previous page");

// a view that publishes its own content tree
class DocumentNode : public BAccessible {
	virtual accessible_role Role() const;
	virtual BString Name() const;
	virtual BAccessible* Parent() const;		// NULL for the root
	virtual int32 CountChildren() const;
	virtual BAccessible* ChildAt(int32 index) const;
	virtual BRect Frame() const;				// screen coordinates
	virtual uint64 States() const;
	virtual uint32 Actions() const;
	virtual status_t PerformAction(accessible_action action);
	...
};

BAccessible::Attach(webView, rootNode);	// the view's node becomes the tree
rootNode->NotifyEvent(B_ACCESSIBLE_EVENT_FOCUS_CHANGED);	// on any node
BAccessible::Announce("Download complete");
```

All `BAccessible` methods are called with the host view's window locked: from
the window thread for events and from the bridge thread for queries. Nodes
may be created lazily; their ID is assigned on first use and retired by the
destructor (delete nodes with the window locked). An attached tree covers the
host view and all its child views: focus on any of them maps to the root.

### Events

Applications send `B_ACCESSIBILITY_EVENT` to the server without waiting
(timeout 0; events are dropped rather than ever blocking a window thread).
Each carries `event` (`accessible_event`), `when`, the `node`, and:

| Event | Sent by | Extra fields |
|-------|---------|--------------|
| focus changed | `BWindow::_SetFocus()` (every `MakeFocus()`), menu item highlight in `BMenu::_SelectItem()` | `window`, `window_active`; `container`, `menu` for menu items |
| window activated / deactivated | `BWindow` on `B_WINDOW_ACTIVATED` | `window`; the focus as `node` |
| value changed | `BControl::SetValue()`, `SetValueNoUpdate()`, `BColumnListView` focus row | |
| name changed | `BControl::SetLabel()` | |
| state changed | `BControl::SetEnabled()` | `changed`: the state bits that changed (`int64`); missing means `B_ACCESSIBLE_STATE_DISABLED` |
| selection changed | `BListView::Select()`, `BTabView::Select()` | `container` |
| text inserted / deleted | `BTextView::InsertText()`, `DeleteText()` | `text`, `offset`, `length`, `hidden` |
| caret moved | `BTextView::Select()` (arrow keys, clicks) | `caret`, `sel_start`, `sel_end`, `char`, `line`, `line_text` or the selected `text` |
| announcement | `BAccessible::Announce()` | `text`, `interrupt` |

To keep traffic low, value, state, text and selection events are only sent
for the control that has the keyboard focus (and its parts). The server adds
`team`, `signature` and `bridge` and sends the events on to its clients,
together with its own `app registered` and `app quit` events.

### Protocol

```
AT  -> server   B_ACCESSIBILITY_SUBSCRIBE "client"     reply: "app"[] (team, signature, bridge)
AT  -> server   B_ACCESSIBILITY_UNSUBSCRIBE "client"
AT  -> server   B_ACCESSIBILITY_GET_APPS               reply: "app"[]
server -> app   B_ACCESSIBILITY_ENABLE "server" / B_ACCESSIBILITY_DISABLE
app -> server   B_ACCESSIBILITY_REGISTER "bridge" "team" "signature"
app -> server   B_ACCESSIBILITY_EVENT ...
server -> AT    B_ACCESSIBILITY_EVENT ... + "team" "signature" "bridge"

AT  -> bridge   (synchronous; reply B_ACCESSIBILITY_REPLY with "status")
  B_ACCESSIBILITY_GET_ROOT      "depth"                 -> "node" (application, windows as children)
  B_ACCESSIBILITY_GET_NODE      "id" "depth"            -> "node"
  B_ACCESSIBILITY_GET_FOCUS                             -> "window", "node"
  B_ACCESSIBILITY_PERFORM_ACTION "id" "action"
  B_ACCESSIBILITY_GET_TEXT      "id" "start" "end"      -> "text"
```

`depth` 0 returns a node with child IDs only, -1 the whole subtree. The bridge
locks the window of a node with a 250 ms timeout and answers `B_BUSY` when the
window is stuck. Password fields never give out their text.

Actions: press (click a button, toggle a check box, invoke a list or menu
item), focus (also activates the window), increment/decrement (sliders,
spinners, scroll bars), select (list items, tabs), expand/collapse (outline
list items), others for custom trees.

## Screen reader

`ScreenReader` (in `/system/apps`) subscribes to the server and says:

- on window activation: the title and kind ("Network, window"; alerts with
  their text), then the focused control;
- on focus change: name, role, value and state ("Enable feature, check box,
  not checked", "Name, text field, Ada", "Volume, slider, 40", "Banana, list
  item, 2 of 3", "Open…, menu item, Alt+O");
- value changes of the focused control ("checked", "45"), list and tab
  selection, typed and deleted text (password fields say "star"), caret moves
  (the character, or the line when the line changes), announcements.

Keys (input_server filter `accessibility_keys`; Option is the Windows key and
Command the Alt key in the default keymap):

| Keys | |
|------|-|
| Option+Command+S | turn the screen reader on or off |
| Option+Command+F | read the focused control again |
| Option+Command+W | read the window title and the focused control |
| Option+Command+R | read the whole window (every control in order; on a web page, its text as sentences, links inline, headings and lists with their items, landmarks by kind) |
| Option+Command+X | stop speaking |
| Option+Command+Down / Up | browse mode: next / previous item (what read all says, item by item) |
| Option+Command+H, K, J, L, D | browse mode: next heading, link, form field, list, landmark; with Shift the previous one |
| Option+Command+Return | browse mode: activate the item (press it; a field takes the focus) |

Browse mode is a virtual cursor for web pages, where Tab only reaches what
takes the focus. It works in any window; the tree is fetched again when it
is more than a second old, and the cursor keeps its place by the node.
Links inside running text are read with the sentence and are still found by
"next link". `hey application/x-vnd.vos-ScreenReader srBR with
'kind=int32(1)'` drives it from a script (`srAC` activates).

The review keys are only taken while the screen reader runs.

Settings, `~/config/settings/ScreenReader` (driver settings):

```
autostart true     # launch_daemon starts the screen reader at login
captions false     # no caption window (shown by default, always without speech)
engine espeak-ng   # auto (default), speech_server, speak, espeak-ng, espeak, flite, a path, none
rate 175           # words per minute, passed to the engine
```

### Speech

Text to speech is haiku#52. The screen reader uses, in this order:

1. a `speech_server` (`application/x-vnd.vos-speech_server`, ideally an
   on-demand launch_daemon service) with the proposed protocol
   `B_SPEECH_SPEAK` ('SPsp': "text", "interrupt", "rate", "client") and
   `B_SPEECH_STOP` ('SPst');
2. an engine command found in `PATH`: `speak`, `espeak-ng`, `espeak` or
   `flite` (`-s RATE TEXT`, flite `-t TEXT`), one utterance at a time,
   killed when interrupted;
3. none: captions only.

Everything spoken is also written to the syslog as `ScreenReader: say: ...`
and shown in a caption window at the bottom of the screen (it never takes the
focus).

## How Summit should map WebKit's accessibility tree

Summit (airos-works/summit#118) builds WebKit's `AXObjectCache`. The mapping
follows what the WebKit ports for ATK and macOS do:

- One `BAccessible` wrapper per `AXCoreObject`, created lazily and owned by
  the wrapper map of the page (like `WebAccessibilityObjectWrapperAtk`).
  Delete wrappers on `AXObjectCache::detachWrapper()` with the window locked.
- `BAccessible::Attach(webView, rootWrapper)` once the main frame's root
  (`AXObjectCache::rootObject()`, role web area) exists. The root's role is
  `B_ACCESSIBLE_ROLE_DOCUMENT`; `Parent()` returns NULL for it.
- `Role()` from `AccessibilityRole`: Button, ToggleButton → button, toggle
  button; CheckBox, Switch → check box; RadioButton → radio button; TextField,
  SearchField → text field; TextArea → text area; Slider → slider;
  SpinButton → spin button; PopUpButton, ComboBox → pop-up button, combo box;
  Link → link; Heading → heading (level as `level`); StaticText, Paragraph →
  label, paragraph; Image → image; List, ListItem → list, list item;
  Table/Grid, Row, Cell, ColumnHeader → table, row, cell, column header;
  Tree, TreeItem → tree, tree item; TabList, Tab, TabPanel → tab list, tab,
  tab panel; Menu, MenuBar, MenuItem → menu, menu bar, menu item; Dialog,
  AlertDialog → dialog, alert; Group, Section, Generic → group; landmarks
  (Banner, Navigation, Main, ContentInfo, Region...) → landmark with the
  landmark kind in `AddProperties()`; Form → form; Math → math; ProgressBar,
  Meter → progress bar; WebArea/Document → document.
- `Name()` from `AXCoreObject::computedLabel()` (or `title()`),
  `Description()` from `descriptionAttributeValue()`/`helpText()`, `Value()`
  from `stringValue()` (text controls: the text; links: nothing), ranges from
  `valueForRange()`, `minValueForRange()`, `maxValueForRange()`.
- `States()`: focused/focusable, `isEnabled()` → disabled, `isChecked()` /
  `checkboxOrRadioValue()` → checked/mixed, `isPressed()`, `isSelected()`,
  `isExpanded()` + `supportsExpanded()`, `canSetValueAttribute()` → editable
  else read only, `isPasswordField()` → protected, `isRequired()`,
  `isInvalid()`, `isMultiSelectable()`, `isOffScreen()`/`isIgnored()` →
  invisible, `isVisited()`, `hasPopup()`.
- Children: `AXCoreObject::unignoredChildren()` (or `children()` filtered by
  `accessibilityIsIgnored()`), the same tree that ATK exposes.
- `Frame()`: `relativeFrame()` converted from page to view coordinates
  (scroll offset and zoom), then `ConvertToScreen()` on the web view.
- `PerformAction()`: press → `performDefaultAction()`/`press()`, focus →
  `setFocused(true)`, increment/decrement → `increment()`/`decrement()`,
  scroll to → `scrollToMakeVisible()`, expand/collapse → `setIsExpanded()`,
  select → `setSelected(true)`.
- Text: `GetTextSelection()` and `Text()` from the AX text marker APIs
  (`selectedTextRange()`, `stringForRange()`), offsets in UTF-8 bytes of the
  object's text.
- Events from `AXObjectCache::postPlatformNotification()`:
  AXFocusedUIElementChanged → focus changed on the new focused object;
  AXValueChanged, AXCheckedStateChanged → value changed; AXSelectedTextChanged
  → caret moved (with `caret`, `char`, `line_text` as the Interface Kit sends
  them); AXSelectedChildrenChanged, AXSelectedCellsChanged → selection
  changed with `container`; AXChildrenChanged, AXLayoutComplete → children
  changed; AXLoadComplete → document loaded; AXLiveRegionChanged and ARIA
  live regions → `BAccessible::Announce()` (interrupt for assertive);
  AXExpandedChanged, AXDisabledStateChanged → state changed with `changed`
  naming the bits (expanded, selected, pressed, disabled...), which
  ScreenReader says as "expanded", "not selected" and so on; AXTextChanged →
  text inserted/deleted.
- Only build the wrappers and send events while `BAccessible::IsEnabled()` is
  true (enable WebKit's accessibility with `AXObjectCache::enableAccessibility()`
  when it becomes true), so pages cost nothing extra without a screen reader.

## Testing

```
# in a QEMU image built with the test programs (UserBuildConfig:
# AddFilesToHaikuImage home tests : AccessibilityTestApp accessibility_tree_test ;)
~/tests/accessibility_tree_test                    # checks, prints PASS/FAIL
~/tests/accessibility_tree_test --dump application/x-vnd.vos-Network
```

Start the screen reader with Option+Command+S, move with Tab in
`AccessibilityTestApp` or a preferences window, and compare the captions or
`grep 'ScreenReader: say' /var/log/syslog` with what has the focus.

## Traps

- Never look up the screen reader or the speech server with
  `BMessenger(signature)` from a thread that must not block: it asks the
  launch_daemon first, and the launch_daemon holds the request forever while
  a *job* of that name waits for an unmet condition. That is why the
  autostart job is called `screen-reader-autostart`, why `accessibility_keys`
  asks only the registrar (`GetAppInfo()` + `BMessenger(NULL, team)`), and
  why the speech_server is only looked up that way when launch_daemon
  reports an enabled *service* for it. (Found when the input_server's event
  thread hung on the first review key.)
- `BSlider::MaxUpdateTextWidth()` sets the value to the maximum to measure
  the text; it mutes events with `BPrivate::AccessibilityMuter`. Other code
  that changes a value only temporarily needs the same.

## Missing in this version

- Braille output, a full review cursor (moving through the tree or the text
  by character/word/line without moving the focus), a mouse-over reader,
  key echo for non-text keys, verbosity and punctuation settings, localized
  speech (role names are English), a preferences panel (settings file and
  keys only).
- Web content until Summit implements its side (above).
- List items that are not `BStringItem`s have no text (Network's interface
  list reads "tree, 1 of 9", its items "Item 2, tree item, 2 of 9"); such
  lists need a `BAccessible` or a text-providing item class. Unnamed views
  (Network's list, a bare menu bar) have no name.
- `BColumnListView` rows, `BOutlineListView` subtrees of collapsed items,
  closed submenus, Tracker's file views and Deskbar replicants are not
  described; custom-drawn views without `BAccessible` are "unknown".
- Item IDs are index based; no "children changed" events from the Interface
  Kit yet; events only for the focused control (plus announcements).
- Speech depends on haiku#52; the speech_server protocol above is a proposal.
- The interface is private; freeze it (public headers, reserved slots) once
  Summit and a second AT use it.
