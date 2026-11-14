/*
 * Copyright 2010, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Clemens Zeidler <haiku@clemens-zeidler.de>
 */
#ifndef STACKING_H
#define STACKING_H

#include "ObjectList.h"
#include "StackAndTile.h"


class SATWindow;


class StackingEventHandler
{
public:
	static bool				HandleMessage(SATWindow* sender,
								BPrivate::LinkReceiver& link,
								BPrivate::LinkSender& reply);
};


class SATStacking : public SATSnappingBehaviour {
public:
							SATStacking(SATWindow* window);
							~SATStacking();

		bool				FindSnappingCandidates(SATGroup* group);
		bool				JoinCandidates();
		void				DoWindowLayout();

		void				RemovedFromArea(WindowArea* area);
		void				WindowLookChanged(window_look look);

		//! Drops a found stacking parent and its highlight.
		void				ClearCandidates();
		//! Forgets \a window if it is the found stacking parent; called
		//! before \a window is deleted.
		void				ForgetWindow(SATWindow* window);
private:
		bool				_IsStackableWindow(Window* window);
		void				_ClearSearchResult();
		void				_HighlightWindows(bool highlight = true);

		SATWindow*			fSATWindow;

		SATWindow*			fStackingParent;
};

#endif
