/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef CUPERTINO_DOCK_H
#define CUPERTINO_DOCK_H


// The Dock of the Cupertino layout: a window at the bottom centre of the
// screen with an icon for every running application, from the same team
// list as the application bar (TBarApp::Subscribe()).
//
// A primary click brings the application's windows to the front, a
// secondary or Control click opens its window menu (TWindowMenu).


#include <List.h>
#include <MenuBar.h>
#include <OS.h>
#include <Window.h>


const uint32 kUpdateDockPlacement = 'CdPl';


class TCupertinoDock;


class TCupertinoDockWindow : public BWindow {
public:
							TCupertinoDockWindow();

	virtual	void			MenusBeginning();
	virtual	void			MenusEnded();
	virtual	void			ScreenChanged(BRect frame, color_space mode);
	virtual	void			MessageReceived(BMessage* message);

			bool			IsShowingMenu() const
								{ return fMenusShown > 0; }

			void			UpdatePlacement();

private:
			BView*			fBackground;
			TCupertinoDock*	fDock;
			int32			fMenusShown;
};


class TCupertinoDock : public BMenuBar {
public:
							TCupertinoDock();
	virtual					~TCupertinoDock();

	virtual	void			AttachedToWindow();
	virtual	void			DetachedFromWindow();
	virtual	void			MessageReceived(BMessage* message);
	virtual	void			Draw(BRect updateRect);
	virtual	void			DrawBackground(BRect updateRect);
	virtual	void			MouseDown(BPoint where);
	virtual	void			MouseMoved(BPoint where, uint32 transit,
								const BMessage* dragMessage);

			void			GetDockSize(float* width, float* height);
			team_id			ActiveTeam() const { return fActiveTeam; }
			void			SetActiveTeam(team_id team);

private:
			void			_Rebuild();
			void			_ClearTeamList();
			int32			_IndexAt(BPoint where);

private:
			BList			fTeamList;
				// BarTeamInfo copies from TBarApp::Subscribe()
			team_id			fActiveTeam;
			bool			fRebuildPending;
			int32			fLastToolTipIndex;
};


#endif	// CUPERTINO_DOCK_H
