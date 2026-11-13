/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef GLOBAL_MENU_BAR_H
#define GLOBAL_MENU_BAR_H


// Application area of the global menu bar, between the Deskbar menu
// and the replicant tray: the active application's name, opening that
// application's window menu (its windows, Hide all, Show all, Close all).


#include <List.h>
#include <MenuBar.h>
#include <OS.h>


class BarTeamInfo;
class TGlobalAppMenuItem;


class TGlobalAppMenuBar : public BMenuBar {
public:
							TGlobalAppMenuBar(BRect frame);
	virtual					~TGlobalAppMenuBar();

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
			TGlobalAppMenuItem* fAppItem;
			bool			fUpdatePending;
};


#endif	// GLOBAL_MENU_BAR_H
