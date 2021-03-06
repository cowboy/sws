/******************************************************************************
/ BR_Misc.cpp
/
/ Copyright (c) 2013-2014 Dominik Martin Drzic
/ http://forum.cockos.com/member.php?u=27094
/ https://code.google.com/p/sws-extension
/
/ Permission is hereby granted, free of charge, to any person obtaining a copy
/ of this software and associated documentation files (the "Software"), to deal
/ in the Software without restriction, including without limitation the rights to
/ use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
/ of the Software, and to permit persons to whom the Software is furnished to
/ do so, subject to the following conditions:
/
/ The above copyright notice and this permission notice shall be included in all
/ copies or substantial portions of the Software.
/
/ THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
/ EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
/ OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
/ NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
/ HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
/ WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/ FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
/ OTHER DEALINGS IN THE SOFTWARE.
/
******************************************************************************/
#include "stdafx.h"
#include "BR_Misc.h"
#include "BR_EnvelopeUtil.h"
#include "BR_MidiUtil.h"
#include "BR_MouseUtil.h"
#include "BR_ProjState.h"
#include "BR_Util.h"
#include "../SnM/SnM.h"
#include "../SnM/SnM_Chunk.h"
#include "../SnM/SnM_Dlg.h"
#include "../SnM/SnM_Util.h"
#include "../Xenakios/XenakiosExts.h"
#include "../reaper/localize.h"

/******************************************************************************
* Constants                                                                   *
******************************************************************************/
const char* const ADJUST_PLAYRATE_KEY  = "BR - AdjustPlayrate";
const char* const ADJUST_PLAYRATE_WND  = "BR - AdjustPlayrateWnd";

/******************************************************************************
* Globals                                                                     *
******************************************************************************/
HWND g_adjustPlayrateWnd = NULL;

/******************************************************************************
* Commands: Misc                                                              *
******************************************************************************/
void SplitItemAtTempo (COMMAND_T* ct)
{
	WDL_TypedBuf<MediaItem*> items;
	SWS_GetSelectedMediaItems(&items);
	if (!items.GetSize() || !CountTempoTimeSigMarkers(NULL) || IsLocked(ITEM_FULL))
		return;

	bool update = false;
	for (int i = 0; i < items.GetSize(); ++i)
	{
		MediaItem* item = items.Get()[i];
		if (!IsItemLocked(item))
		{
			double iStart = GetMediaItemInfo_Value(item, "D_POSITION");
			double iEnd = iStart + GetMediaItemInfo_Value(item, "D_LENGTH");
			double tPos = iStart - 1;

			// Split item currently in the loop
			while (true)
			{
				item = SplitMediaItem(item, tPos);
				if (!item) // split at nonexistent position?
					item = items.Get()[i];
				else
					update = true;

				tPos = TimeMap2_GetNextChangeTime(NULL, tPos);
				if (tPos > iEnd || tPos == -1 )
					break;
			}
		}
	}
	if (update)
	{
		Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_ITEMS, -1);
		UpdateArrange();
	}
}

void SplitItemAtStretchMarkers (COMMAND_T* ct)
{
	WDL_TypedBuf<MediaItem*> items;
	SWS_GetSelectedMediaItems(&items);
	if (!items.GetSize() || IsLocked(ITEM_FULL))
		return;

	bool update = false;
	for (int i = 0; i < items.GetSize(); ++i)
	{
		MediaItem* item = items.Get()[i];
		if (!IsItemLocked(item))
		{
			MediaItem_Take* take = GetActiveTake(item);
			double iStart        = GetMediaItemInfo_Value(item, "D_POSITION");
			double iEnd          = GetMediaItemInfo_Value(item, "D_LENGTH") + iStart;
			double playRate      = GetMediaItemTakeInfo_Value(take, "D_PLAYRATE");

			vector<double> stretchMarkers;
			for (int i = 0; i < GetTakeNumStretchMarkers(take); ++i)
			{
				double position;
				GetTakeStretchMarker(take, i, &position, NULL);
				position = (position / playRate) + iStart;

				if (position > iEnd)
					break;
				else
					stretchMarkers.push_back(position);
			}

			for (size_t i = 0; i < stretchMarkers.size(); ++i)
			{
				double position = stretchMarkers[i];


				item = SplitMediaItem(item, position);
				if (!item) // split at nonexistent position?
					item = items.Get()[i];
				else
					update = true;
			}
		}
	}
	if (update)
	{
		Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_ITEMS, -1);
		UpdateArrange();
	}
}

void MarkersAtTempo (COMMAND_T* ct)
{
	BR_Envelope tempoMap(GetTempoEnv());
	if (!tempoMap.CountSelected() || IsLocked(MARKERS))
		return;

	PreventUIRefresh(1);
	Undo_BeginBlock2(NULL);
	for (int i = 0; i < tempoMap.CountSelected(); ++i)
	{
		int id = tempoMap.GetSelected(i);
		double position; tempoMap.GetPoint(id, &position, NULL, NULL, NULL);
		AddProjectMarker(NULL, false, position, 0, NULL, -1);
	}
	Undo_EndBlock2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_MISCCFG);
	PreventUIRefresh(-1);
}

void MarkersAtNotes (COMMAND_T* ct)
{
	if (IsLocked(MARKERS))
		return;

	PreventUIRefresh(1);

	bool update = false;
	for (int i = 0; i < CountSelectedMediaItems(NULL); ++i)
	{
		MediaItem* item      = GetSelectedMediaItem(NULL, i);
		MediaItem_Take* take = GetActiveTake(item);
		double itemStart =  GetMediaItemInfo_Value(item, "D_POSITION");
		double itemEnd   = GetMediaItemInfo_Value(item, "D_POSITION") + GetMediaItemInfo_Value(item, "D_LENGTH");

		// Due to possible tempo changes, always work with PPQ, never time
		double itemStartPPQ = MIDI_GetPPQPosFromProjTime(take, itemStart);
		double itemEndPPQ = MIDI_GetPPQPosFromProjTime(take, itemEnd);
		double sourceLenPPQ = GetSourceLengthPPQ(take);

		int noteCount = 0;
		MIDI_CountEvts(take, &noteCount, NULL, NULL);
		if (noteCount != 0)
		{
			update = true;
			for (int j = 0; j < noteCount; ++j)
			{
				double position;
				MIDI_GetNote(take, j, NULL, NULL, &position, NULL, NULL, NULL, NULL);
				while (position <= itemEndPPQ) // in case source is looped
				{
					if (CheckBounds(position, itemStartPPQ, itemEndPPQ))
						AddProjectMarker(NULL, false, MIDI_GetProjTimeFromPPQPos(take, position), 0, NULL, -1);
					position += sourceLenPPQ;
				}
			}
		}
	}

	if (update)
		Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_MISCCFG, -1);
	PreventUIRefresh(-1);
}

void MarkersAtStretchMarkers (COMMAND_T* ct)
{
	if (IsLocked(MARKERS))
		return;

	PreventUIRefresh(1);
	bool update = false;
	for (int i = 0; i < CountSelectedMediaItems(NULL); ++i)
	{
		MediaItem_Take* take = GetActiveTake(GetSelectedMediaItem(NULL, i));
		double iStart     = GetMediaItemInfo_Value(GetSelectedMediaItem(NULL, i), "D_POSITION");
		double iEnd       = GetMediaItemInfo_Value(GetSelectedMediaItem(NULL, i), "D_LENGTH") + iStart;
		double playRate      = GetMediaItemTakeInfo_Value(take, "D_PLAYRATE");

		for (int i = 0; i < GetTakeNumStretchMarkers(take); ++i)
		{
			double position;
			GetTakeStretchMarker(take, i, &position, NULL);
			position = (position / playRate) + iStart;
			if (position <= iEnd && AddProjectMarker(NULL, false, position, 0, NULL, -1) != -1)
				update = true;
		}
	}

	if (update)
		Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_MISCCFG, -1);
	PreventUIRefresh(-1);
}

void MarkerAtMouse (COMMAND_T* ct)
{
	if (IsLocked(MARKERS))
		return;

	double position = PositionAtMouseCursor(true);
	if (position != -1)
	{
		if ((int)ct->user == 1)
			position = SnapToGrid(NULL, position);

		if (AddProjectMarker(NULL, false, position, 0, NULL, -1) != -1)
			Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_MISCCFG, -1);
		UpdateArrange();
	}
}

void MarkersRegionsAtItems (COMMAND_T* ct)
{
	if (!CountSelectedMediaItems(NULL) || ((int)ct->user == 0 && IsLocked(MARKERS)) || ((int)ct->user == 1 && IsLocked(REGIONS)))
		return;

	Undo_BeginBlock2(NULL);
	PreventUIRefresh(1);

	for (int i = 0; i < CountSelectedMediaItems(NULL); ++i)
	{
		MediaItem* item =  GetSelectedMediaItem(NULL, i);
		double iStart = *(double*)GetSetMediaItemInfo(item, "D_POSITION", NULL);
		double iEnd = iStart + *(double*)GetSetMediaItemInfo(item, "D_LENGTH", NULL);
		char* pNotes = (char*)GetSetMediaItemInfo(item, "P_NOTES", NULL);

		string notes(pNotes, strlen(pNotes)+1);
		ReplaceAll(notes, "\r\n", " ");

		if ((int)ct->user == 0)  // Markers
			AddProjectMarker(NULL, false, iStart, 0, notes.c_str(), -1);
		else                     // Regions
			AddProjectMarker(NULL, true, iStart, iEnd, notes.c_str(), -1);
	}

	PreventUIRefresh(-1);
	Undo_EndBlock2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_MISCCFG);
}

void MoveClosestMarker (COMMAND_T* ct)
{
	if (IsLocked(MARKERS))
		return;

	double position;
	if      (abs((int)ct->user) == 1) position = GetPlayPositionEx(NULL);
	else if (abs((int)ct->user) == 2) position = GetCursorPositionEx(NULL);
	else                              position = PositionAtMouseCursor(true);

	if (position >= 0)
	{
		int id = FindClosestProjMarkerIndex(position);
		if (id >= 0)
		{
			if ((int)ct->user < 0) position = SnapToGrid(NULL, position);
			int markerId;
			EnumProjectMarkers3(NULL, id, NULL, NULL, NULL, NULL, &markerId, NULL);

			SetProjectMarkerByIndex(NULL, id, NULL, position, NULL, markerId, NULL, NULL);
			Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_MISCCFG, -1);
		}
	}
}

void MidiItemTempo (COMMAND_T* ct)
{
	if (IsLocked(ITEM_FULL))
		return;

	vector<BR_MidiItemTimePos> items;
	if ((int)ct->user == 2)
	{
		items.reserve(CountSelectedMediaItems(NULL));
		for (int i = 0; i < CountSelectedMediaItems(NULL); ++i)
		{
			MediaItem* item = GetSelectedMediaItem(NULL, i);
			if (!IsItemLocked(item))
			{
				items.push_back(BR_MidiItemTimePos(item, false));
				SetMediaItemInfo_Value(item, "C_BEATATTACHMODE", 0);
			}
		}
	}

	PreventUIRefresh(1);
	bool update = false;
	for (int i = 0; i < CountSelectedMediaItems(NULL); ++i)
	{
		MediaItem* item = GetSelectedMediaItem(NULL, i);
		if (!IsItemLocked(item))
		{
			double bpm; int num, den;
			TimeMap_GetTimeSigAtTime(NULL, GetMediaItemInfo_Value(item, "D_POSITION"), &num, &den, &bpm);
			if ((int)ct->user == 2)
			{
				BR_MidiItemTimePos timePos(item, false);
				if (SetIgnoreTempo(item, !!(int)ct->user, bpm, num, den))
				{
					timePos.Restore(true);
					update = true;
				}
			}
			else
			{
				if (SetIgnoreTempo(item, !!(int)ct->user, bpm, num, den))
					update = true;
			}
		}
	}

	if (update)
		Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_ITEMS, -1);
	PreventUIRefresh(-1);
}

void MidiItemTrim (COMMAND_T* ct)
{
	if (IsLocked(ITEM_FULL) || IsLocked(ITEM_EDGES))
		return;

	bool update = false;
	for (int i = 0; i < CountSelectedMediaItems(0); ++i)
	{
		MediaItem* item = GetSelectedMediaItem(0, i);
		if (DoesItemHaveMidiEvents(item) && !IsItemLocked(item))
		{
			double start = -1;
			double end   = -1;

			for (int j = 0; j < CountTakes(item); ++j)
			{
				MediaItem_Take* take = GetTake(item, j);
				if (MIDI_CountEvts(take, NULL, NULL, NULL))
				{
					double currentStart = EffectiveMidiTakeStart(take, false, false);
					double currentEnd   = GetMediaItemInfo_Value(GetMediaItemTake_Item(take), "D_POSITION") + EffectiveMidiTakeLength(take, false, false);

					if (start == -1 && end == -1)
					{
						start = currentStart;
						end   = currentEnd;
					}
					else
					{
						start = min(currentStart, start);
						end   = max(currentEnd, end);
					}
				}
			}

			update = TrimItem(item, start, end);
		}
	}

	if (update)
		Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_ITEMS, -1);
}

void SnapFollowsGridVis (COMMAND_T* ct)
{
	int option;
	GetConfig("projshowgrid", option);
	SetConfig("projshowgrid", ToggleBit(option, 15));
	RefreshToolbar(0);
}

void PlaybackFollowsTempoChange (COMMAND_T*)
{
	int option; GetConfig("seekmodes", option);

	option = ToggleBit(option, 5);
	SetConfig("seekmodes", option);
	RefreshToolbar(0);

	char tmp[256];
	_snprintfSafe(tmp, sizeof(tmp), "%d", option);
	WritePrivateProfileString("reaper", "seekmodes", tmp, get_ini_file());
}

void TrimNewVolPanEnvs (COMMAND_T* ct)
{
	SetConfig("envtrimadjmode", (int)ct->user);
	RefreshToolbar(0);

	char tmp[256];
	_snprintfSafe(tmp, sizeof(tmp), "%d", (int)ct->user);
	WritePrivateProfileString("reaper", "envtrimadjmode", tmp, get_ini_file());
}

void CycleRecordModes (COMMAND_T*)
{
	int mode; GetConfig("projrecmode", mode);
	if (++mode > 2) mode = 0;

	if      (mode == 0) Main_OnCommandEx(40253, 0, NULL);
	else if (mode == 1) Main_OnCommandEx(40252, 0, NULL);
	else if (mode == 2) Main_OnCommandEx(40076, 0, NULL);
}

void FocusArrangeTracks (COMMAND_T* ct)
{
	if ((int)ct->user == 0)
	{
		TrackEnvelope* envelope = GetSelectedEnvelope(NULL);
		SetCursorContext(envelope ? 2 : 1, envelope);
	}
	else
		SetCursorContext(0, NULL);
}

void ToggleItemOnline (COMMAND_T* ct)
{
	if (IsLocked(ITEM_FULL))
		return;

	for (int i = 0; i < CountSelectedMediaItems(NULL); ++i)
	{
		MediaItem* item = GetSelectedMediaItem(NULL, i);
		if (!IsItemLocked(item))
		{
			for (int j = 0; j < CountTakes(item); ++j)
			{
				if (PCM_source* source = GetMediaItemTake_Source(GetMediaItemTake(item, j)))
					source->SetAvailable(!source->IsAvailable());
			}
		}
	}
	UpdateArrange();
}

void ItemSourcePathToClipBoard (COMMAND_T* ct)
{
	WDL_FastString sourceList;
	for (int i = 0; i < CountSelectedMediaItems(NULL); ++i)
	{
		MediaItem* item = GetSelectedMediaItem(NULL, i);
		if (PCM_source* source = GetMediaItemTake_Source(GetActiveTake(item)))
		{
			// If section, get the "real" source
			if (!strcmp(source->GetType(), "SECTION"))
				source = source->GetSource();

			if (const char* fileName = source->GetFileName())
				if (strcmp(fileName, "")) // skip in-project files
					sourceList.AppendFormatted(SNM_MAX_PATH, "%s\n", fileName);
		}
	}

	if (OpenClipboard(g_hwndParent))
	{
		EmptyClipboard();
		#ifdef _WIN32
			#if !defined(WDL_NO_SUPPORT_UTF8)
			if (WDL_HasUTF8(sourceList.Get()))
			{
				DWORD size;
				WCHAR* wc = WDL_UTF8ToWC(sourceList.Get(), false, 0, &size);
				if (HGLOBAL hglbCopy = GlobalAlloc(GMEM_MOVEABLE, size*sizeof(WCHAR)))
				{
					if (LPVOID cp = GlobalLock(hglbCopy))
						memcpy(cp, wc, size*sizeof(WCHAR));
					GlobalUnlock(hglbCopy);
					SetClipboardData(CF_UNICODETEXT, hglbCopy);
				}
				free(wc);

			}
			else
			#endif
		#endif
		{
			if (HGLOBAL hglbCopy = GlobalAlloc(GMEM_MOVEABLE, sourceList.GetLength() + 1))
			{
				if (LPVOID cp = GlobalLock(hglbCopy))
					memcpy(cp, sourceList.Get(), sourceList.GetLength() + 1);
				GlobalUnlock(hglbCopy);
				SetClipboardData(CF_TEXT, hglbCopy);
			}
		}
		CloseClipboard();
	}
}

void DeleteTakeUnderMouse (COMMAND_T* ct)
{
	BR_MouseInfo mouseInfo(BR_MouseInfo::MODE_ARRANGE);

	// Don't differentiate between things within the item, but ignore any envelopes
	if (!strcmp(mouseInfo.GetWindow(), "arrange") && !strcmp(mouseInfo.GetSegment(), "track") && mouseInfo.GetItem() && !mouseInfo.GetEnvelope())
	{
		if (CountTakes(mouseInfo.GetItem()) > 1 && !IsItemLocked(mouseInfo.GetItem()) && !IsLocked(ITEM_FULL))
		{
			SNM_TakeParserPatcher takePatcher(mouseInfo.GetItem());
			takePatcher.RemoveTake(mouseInfo.GetTakeId());
			if (takePatcher.Commit())
				Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_ITEMS, -1);
		}
	}
}

void SelectTrackUnderMouse (COMMAND_T* ct)
{
	BR_MouseInfo mouseInfo(BR_MouseInfo::MODE_MCP_TCP);
	if (!strcmp(mouseInfo.GetWindow(), ((int)ct->user == 0) ? "tcp" : "mcp") && !strcmp(mouseInfo.GetSegment(), "track"))
	{
		if ((int)GetMediaTrackInfo_Value(mouseInfo.GetTrack(), "I_SELECTED") == 0)
		{
			SetMediaTrackInfo_Value(mouseInfo.GetTrack(), "I_SELECTED", 1);
			Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_TRACKCFG, -1);
		}
	}
}

void PlaybackAtMouseCursor (COMMAND_T* ct)
{
	if (IsRecording())
		return;

	// Do both MIDI editor and arrange from here to prevent focusing issues (not unexpected in dual monitor situation)
	double mousePos = PositionAtMouseCursor(true);
	if (mousePos == -1)
		mousePos = ME_PositionAtMouseCursor(true, true);

	if (mousePos != -1)
	{
		if ((int)ct->user == 0)
		{
			StartPlayback(mousePos);
		}
		else
		{
			if (!IsPlaying() || IsPaused())
				StartPlayback(mousePos);
			else
			{
				if ((int)ct->user == 1) OnPauseButton();
				if ((int)ct->user == 2) OnStopButton();
			}
		}
	}
}

void SelectItemsByType (COMMAND_T* ct)
{
	if (IsLocked(ITEM_FULL))
		return;

	double tStart, tEnd;
	GetSet_LoopTimeRange(false, false, &tStart, &tEnd, false);

	bool checkTimeSel = ((int)ct->user > 0) ? false : ((tStart == tEnd) ? false : true);
	bool update = false;
	for (int i = 0; i < CountMediaItems(NULL); ++i)
	{
		MediaItem* item = GetMediaItem(NULL, i);
		if (!IsItemLocked(item))
		{
			if (checkTimeSel)
			{
				double itemStart = GetMediaItemInfo_Value(item, "D_POSITION");
				double itemEnd   = itemStart + GetMediaItemInfo_Value(item, "D_LENGTH");
				if (!AreOverlappedEx(itemStart, itemEnd, tStart, tEnd))
					continue;
			}

			if (MediaItem_Take* take = GetActiveTake(item))
			{
				bool select = false;
				int type = GetTakeType(take);

				if      (abs((int)ct->user) == 1) select = (type == 0) ? true : false;
				else if (abs((int)ct->user) == 2) select = (type == 1) ? true : false;
				else if (abs((int)ct->user) == 3) select = (type == 2) ? true : false;
				else if (abs((int)ct->user) == 4) select = (type == 3) ? true : false;
				else if (abs((int)ct->user) == 5) select = (type == 4) ? true : false;
				else if (abs((int)ct->user) == 6) select = (type == 5) ? true : false;
				if (select)
				{
					SetMediaItemInfo_Value(item, "B_UISEL", 1);
					update = true;
				}
			}
			else if (abs((int)ct->user) == 7)
			{
				SetMediaItemInfo_Value(item, "B_UISEL", 1);
				update = true;
			}
		}
	}

	if (update)
	{
		UpdateArrange();
		Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_ITEMS, -1);
	}
}

void SaveCursorPosSlot (COMMAND_T* ct)
{
	int slot = (int)ct->user;

	for (int i = 0; i < g_cursorPos.Get()->GetSize(); ++i)
	{
		if (slot == g_cursorPos.Get()->Get(i)->GetSlot())
			return g_cursorPos.Get()->Get(i)->Save();
	}

	g_cursorPos.Get()->Add(new BR_CursorPos(slot));
}

void RestoreCursorPosSlot (COMMAND_T* ct)
{
	int slot = (int)ct->user;

	for (int i = 0; i < g_cursorPos.Get()->GetSize(); ++i)
	{
		if (slot == g_cursorPos.Get()->Get(i)->GetSlot())
		{
			if (g_cursorPos.Get()->Get(i)->Restore())
			Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(ct), UNDO_STATE_MISCCFG, -1);
			break;
		}
	}
}

/******************************************************************************
* Commands: Misc - Media item preview                                         *
******************************************************************************/
void PreviewItemAtMouse (COMMAND_T* ct)
{
	double position;
	if (MediaItem* item = ItemAtMouseCursor(&position))
	{
		vector<int> options = GetDigits((int)ct->user);
		int toggle = options[0];
		int output = options[1];
		int type   = options[2];
		int pause  = options[3];

		MediaTrack* track     = NULL;
		double      volume    = 1;
		double      start     = (type  == 2) ? (position - GetMediaItemInfo_Value(item, "D_POSITION")) : 0;
		double      measure   = (type  == 3) ? 1 : 0;
		bool        pausePlay = (pause == 2) ? true : false;

		if (output == 2)
			volume = GetMediaTrackInfo_Value(GetMediaItem_Track(item), "D_VOL");
		else if (output == 3)
			track = GetMediaItem_Track(item);

		ItemPreview(toggle, item, track, volume, start, measure, pausePlay);
	}
}

/******************************************************************************
* Commands: Misc - Adjust playrate                                            *
******************************************************************************/
WDL_DLGRET AdjustPlayrateOptionsProc (HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (INT_PTR r = SNM_HookThemeColorsMessage(hwnd, uMsg, wParam, lParam))
		return r;

	static pair<double,pair<double,double> >* rangeValues;
	switch(uMsg)
	{
		case WM_INITDIALOG:
		{
			rangeValues = (pair<double,pair<double,double> >*)lParam;

			char tmp[128];
			_snprintfSafe(tmp, sizeof(tmp), "%.6g", rangeValues->second.first);  SetDlgItemText(hwnd, IDC_EDIT1, tmp);
			_snprintfSafe(tmp, sizeof(tmp), "%.6g", rangeValues->second.second); SetDlgItemText(hwnd, IDC_EDIT2, tmp);
			_snprintfSafe(tmp, sizeof(tmp), "%.6g", rangeValues->first);         SetDlgItemText(hwnd, IDC_EDIT3, tmp);

			RestoreWindowPos(hwnd, ADJUST_PLAYRATE_WND, false);
			ShowWindow(hwnd, SW_SHOW);
			SetFocus(hwnd);
		}
		break;

		case WM_COMMAND:
		{
			switch(LOWORD(wParam))
			{
				case IDOK:
				{
					char tmp [256];
					GetDlgItemText(hwnd, IDC_EDIT1, tmp, 128); rangeValues->second.first  = SetToBounds(AltAtof(tmp), 0.25, 4.0);
					GetDlgItemText(hwnd, IDC_EDIT2, tmp, 128); rangeValues->second.second = SetToBounds(AltAtof(tmp), 0.25, 4.0);
					GetDlgItemText(hwnd, IDC_EDIT3, tmp, 128); rangeValues->first         = SetToBounds(AltAtof(tmp), 0.00001, 4.0);

					_snprintfSafe(tmp, sizeof(tmp), "%lf %lf %lf", rangeValues->first, rangeValues->second.first, rangeValues->second.second);
					WritePrivateProfileString("SWS", ADJUST_PLAYRATE_KEY, tmp, get_ini_file());

					DestroyWindow(hwnd);
				}
				break;

				case IDCANCEL:
				{
					DestroyWindow(hwnd);
				}
				break;
			}
		}
		break;

		case WM_DESTROY:
		{
			SaveWindowPos(hwnd, ADJUST_PLAYRATE_WND);
			g_adjustPlayrateWnd = NULL;
		}
		break;
	}
	return 0;
}

void AdjustPlayrate (COMMAND_T* ct, int val, int valhw, int relmode, HWND hwnd)
{
	static pair<double,pair<double,double> > s_rangeValues;
	static bool                              s_initRangeValues = true;

	if (s_initRangeValues)
	{
		char tmp[256];
		GetPrivateProfileString("SWS", ADJUST_PLAYRATE_KEY, "", tmp, sizeof(tmp), get_ini_file());

		LineParser lp(false);
		lp.parse(tmp);
		s_rangeValues.first         = (lp.getnumtokens() > 0) ? SetToBounds(lp.gettoken_float(0), 0.01, 4.0) : 0.01;
		s_rangeValues.second.first  = (lp.getnumtokens() > 1) ? SetToBounds(lp.gettoken_float(1), 0.25, 4.0) : 0.25;
		s_rangeValues.second.second = (lp.getnumtokens() > 2) ? SetToBounds(lp.gettoken_float(2), 0.25,    4.0) : 4;

		s_initRangeValues = false;
	}

	if ((int)ct->user == 0)
	{
		double playrate = Master_GetPlayRateAtTime(GetPlayPosition2Ex(NULL), NULL);
		playrate = GetMidiOscVal(s_rangeValues.second.first, s_rangeValues.second.second, s_rangeValues.first, playrate, val, valhw, relmode);
		CSurf_OnPlayRateChange(playrate);
	}
	else
	{
		if (!g_adjustPlayrateWnd)
			g_adjustPlayrateWnd = CreateDialogParam(g_hInst, MAKEINTRESOURCE(IDD_BR_ADJUST_PLAYRATE), hwnd, AdjustPlayrateOptionsProc, (LPARAM)&s_rangeValues);
		else
		{
			DestroyWindow(g_adjustPlayrateWnd);
			g_adjustPlayrateWnd = NULL;
		}

		RefreshToolbar(NamedCommandLookup("_BR_ADJUST_PLAYRATE_MIDI"));
	}
}

/******************************************************************************
* Toggle states: Misc                                                         *
******************************************************************************/
int IsSnapFollowsGridVisOn (COMMAND_T* ct)
{
	int option; GetConfig("projshowgrid", option);
	return !GetBit(option, 15);
}

int IsPlaybackFollowingTempoChange (COMMAND_T* ct)
{
	int option; GetConfig("seekmodes", option);
	return GetBit(option, 5);
}

int IsTrimNewVolPanEnvsOn (COMMAND_T* ct)
{
	int option; GetConfig("envtrimadjmode", option);
	return (option == (int)ct->user);
}

int IsAdjustPlayrateOptionsVisible (COMMAND_T*)
{
	return !!g_adjustPlayrateWnd;
}