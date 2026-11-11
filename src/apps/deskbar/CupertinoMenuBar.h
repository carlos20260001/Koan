/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef CUPERTINO_MENU_BAR_H
#define CUPERTINO_MENU_BAR_H


// Application area of the Cupertino menu bar, between the Deskbar menu
// and the replicant tray: the active application's name, opening that
// application's window menu (its windows, Hide all, Show all, Close all).


#include <List.h>
#include <MenuBar.h>
#include <OS.h>


class BarTeamInfo;
class TCupertinoAppMenuItem;


class TCupertinoAppMenuBar : public BMenuBar {
public:
							TCupertinoAppMenuBar(BRect frame);
	virtual					~TCupertinoAppMenuBar();

	virtual	void			AttachedToWindow();
	virtual	void			DetachedFromWindow();
	virtual	void			MessageReceived(BMessage* message);
	virtual	void			Draw(BRect updateRect);

			void			SetActiveTeam(team_id team);
			team_id			ShownTeam() const;

private:
			void			_Resubscribe();
			void			_ClearTeamList();
			BarTeamInfo*	_InfoForTeam(team_id team) const;
			void			_Update();

private:
			BList			fTeamList;
				// BarTeamInfo copies from TBarApp::Subscribe()
			team_id			fActiveTeam;
			TCupertinoAppMenuItem* fAppItem;
			bool			fUpdatePending;
};


#endif	// CUPERTINO_MENU_BAR_H
