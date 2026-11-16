/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


/*!	A window with the standard controls, an unlabeled button with an
	accessible name, and a custom accessible tree (like a web view would
	publish), for testing the accessibility interface (haiku#54) with
	accessibility_tree_test and the screen reader.
*/


#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <ScrollView.h>
#include <Slider.h>
#include <StringItem.h>
#include <TextControl.h>
#include <Window.h>

#include <Accessible.h>


static const char* kSignature = "application/x-vnd.vos-AccessibilityTestApp";


/*!	A custom drawn view whose content is published as its own tree of
	BAccessible nodes: a document with a heading and a link.
*/
class DocumentView : public BView {
public:
	DocumentView();
	virtual ~DocumentView();

	virtual void Draw(BRect updateRect);
	virtual void AttachedToWindow();

private:
	class Node;
	Node* fRoot;
};


class DocumentView::Node : public BAccessible {
public:
	Node(BView* view, Node* parent, accessible_role role, const char* name,
		BRect frame)
		:
		fView(view),
		fParent(parent),
		fRole(role),
		fName(name),
		fFrame(frame)
	{
	}

	virtual ~Node()
	{
		for (int32 i = 0; i < fChildren.CountItems(); i++)
			delete (Node*)fChildren.ItemAt(i);
	}

	void AddChild(Node* child) { fChildren.AddItem(child); }

	virtual accessible_role Role() const { return fRole; }
	virtual BString Name() const { return fName; }
	virtual BAccessible* Parent() const { return fParent; }
	virtual int32 CountChildren() const { return fChildren.CountItems(); }
	virtual BAccessible* ChildAt(int32 index) const
		{ return (Node*)fChildren.ItemAt(index); }
	virtual BRect Frame() const { return fView->ConvertToScreen(fFrame); }
	virtual uint64 States() const
	{
		return fRole == B_ACCESSIBLE_ROLE_LINK
			? B_ACCESSIBLE_STATE_FOCUSABLE : B_ACCESSIBLE_STATE_READ_ONLY;
	}
	virtual uint32 Actions() const
	{
		return fRole == B_ACCESSIBLE_ROLE_LINK ? B_ACCESSIBLE_ACTION_PRESS : 0;
	}
	virtual void AddProperties(BMessage& node) const
	{
		if (fRole == B_ACCESSIBLE_ROLE_HEADING)
			node.AddInt32("level", 1);
		if (fRole == B_ACCESSIBLE_ROLE_LINK)
			node.AddString("url", "https://www.haiku-os.org/");
	}

private:
	BView*			fView;
	Node*			fParent;
	accessible_role	fRole;
	BString			fName;
	BRect			fFrame;
	BList			fChildren;
};


DocumentView::DocumentView()
	:
	BView("document", B_WILL_DRAW)
{
	SetExplicitMinSize(BSize(200, 50));
	fRoot = new Node(this, NULL, B_ACCESSIBLE_ROLE_DOCUMENT,
		"Sample document", BRect(0, 0, 200, 50));
	fRoot->AddChild(new Node(this, fRoot, B_ACCESSIBLE_ROLE_HEADING,
		"Welcome", BRect(4, 4, 196, 20)));
	fRoot->AddChild(new Node(this, fRoot, B_ACCESSIBLE_ROLE_LINK,
		"Haiku website", BRect(4, 26, 120, 42)));
}


DocumentView::~DocumentView()
{
	BAccessible::Detach(this);
	delete fRoot;
}


void
DocumentView::AttachedToWindow()
{
	BAccessible::Attach(this, fRoot);
}


void
DocumentView::Draw(BRect updateRect)
{
	SetHighColor(0, 0, 0);
	DrawString("Welcome", BPoint(4, 16));
	SetHighColor(0, 0, 200);
	DrawString("Haiku website", BPoint(4, 38));
}


class TestWindow : public BWindow {
public:
	TestWindow()
		:
		BWindow(BRect(100, 100, 500, 460), "Accessibility Test",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE | B_AUTO_UPDATE_SIZE_LIMITS)
	{
		BButton* button = new BButton("press", "Press me",
			new BMessage('push'));
		BCheckBox* checkBox = new BCheckBox("feature", "Enable feature",
			new BMessage('chck'));
		BTextControl* text = new BTextControl("name", "Name:", "Ada", NULL);
		BSlider* slider = new BSlider("volume", "Volume", NULL, 0, 100,
			B_HORIZONTAL);
		slider->SetValue(40);
		slider->SetKeyIncrementValue(5);

		BListView* list = new BListView("fruits");
		list->AddItem(new BStringItem("Apple"));
		list->AddItem(new BStringItem("Banana"));
		list->AddItem(new BStringItem("Cherry"));
		BAccessible::SetName(list, "Fruits");
		BScrollView* scrollView = new BScrollView("fruits scroll", list, 0,
			false, true);

		// an icon button: the label is only in the accessible name
		BButton* settings = new BButton("settings", "⚙", NULL);
		BAccessible::SetName(settings, "Settings");
		BAccessible::SetDescription(settings, "Opens the settings");

		BLayoutBuilder::Group<>(this, B_VERTICAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(button)
			.Add(checkBox)
			.Add(text)
			.Add(slider)
			.Add(scrollView)
			.Add(settings)
			.Add(new DocumentView());

		button->MakeFocus(true);
	}
};


class TestApp : public BApplication {
public:
	TestApp()
		:
		BApplication(kSignature)
	{
	}

	virtual void ReadyToRun()
	{
		(new TestWindow())->Show();
	}
};


int
main()
{
	TestApp app;
	app.Run();
	return 0;
}
