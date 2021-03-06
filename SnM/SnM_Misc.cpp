/******************************************************************************
/ SnM_Misc.cpp
/
/ Copyright (c) 2009-2013 Jeffos
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
#include "SnM.h"
#include "SnM_Chunk.h"
#include "SnM_Item.h"
#include "SnM_Misc.h"
#include "SnM_Track.h"
#ifdef _SNM_HOST_AW
#include "../Misc/Adam.h"
#endif


///////////////////////////////////////////////////////////////////////////////
// Reascript export, funcs made dumb-proof!
///////////////////////////////////////////////////////////////////////////////

WDL_FastString* SNM_CreateFastString(const char* _str) {
	return new WDL_FastString(_str);
}

void SNM_DeleteFastString(WDL_FastString* _str) { 
	DELETE_NULL(_str);
}

const char* SNM_GetFastString(WDL_FastString* _str) {
	return _str?_str->Get():"";
}

int SNM_GetFastStringLength(WDL_FastString* _str) {
	return _str?_str->GetLength():0;
}

WDL_FastString* SNM_SetFastString(WDL_FastString* _str, const char* _newStr) {
	if (_str) _str->Set(_newStr?_newStr:"");
	return _str;
}

MediaItem_Take* SNM_GetMediaItemTakeByGUID(ReaProject* _project, const char* _guid)
{
	if (_guid && *_guid)
	{
		GUID g;
		stringToGuid(_guid, &g);
		return GetMediaItemTakeByGUID(_project, &g);
	}
	return NULL;
}

bool SNM_GetSourceType(MediaItem_Take* _tk, WDL_FastString* _type)
{
	if (_tk && _type)
		if (PCM_source* source = GetMediaItemTake_Source(_tk)) {
			_type->Set(source->GetType());
			return true;
		}
	return false;
}

// note: PCM_source.Load/SaveState() won't always work
//       e.g. getting an empty take src, turning wav src into midi, etc..
bool SNM_GetSetSourceState(MediaItem* _item, int _takeIdx, WDL_FastString* _state, bool _setnewvalue)
{
	bool ok = false;
	if (_item && _state)
	{
		if (_takeIdx<0)
			_takeIdx = *(int*)GetSetMediaItemInfo(_item, "I_CURTAKE", NULL);

		int tkPos, tklen;
		WDL_FastString takeChunk;
		SNM_TakeParserPatcher p(_item, CountTakes(_item));
		if (p.GetTakeChunk(_takeIdx, &takeChunk, &tkPos, &tklen))
		{
			SNM_ChunkParserPatcher ptk(&takeChunk, false);

			// set
			if (_setnewvalue)
			{
				// standard case: a source is defined
				if (ptk.ReplaceSubChunk("SOURCE", 1, 0, _state->Get())) // no break keyword here: we're already at the end of the item..
					ok = p.ReplaceTake(tkPos, tklen, ptk.GetChunk());
				// replacing an empty take
				else
				{
					WDL_FastString newTkChunk("TAKE\n");
					newTkChunk.Append(_state);
					ok = p.ReplaceTake(tkPos, tklen, &newTkChunk);
				}
			}
			// get
			else
			{
				if (ptk.GetSubChunk("SOURCE", 1, 0, _state)<0)
					_state->Set(""); // empty take
				ok = true;
			}
		}
	}
	return ok;
}

bool SNM_GetSetSourceState2(MediaItem_Take* _tk, WDL_FastString* _state, bool _setnewvalue)
{
	if (_tk)
		if (MediaItem* item = GetMediaItemTake_Item(_tk)) {
			int tkIdx = GetTakeIndex(item, _tk);
			if (tkIdx >= 0)
				return SNM_GetSetSourceState(item, tkIdx, _state, _setnewvalue);
		}
	return false;
}

bool SNM_GetSetObjectState(void* _obj, WDL_FastString* _state, bool _setnewvalue, bool _minstate)
{
	bool ok = false;
	if (_obj && _state)
	{
		int fxstate = SNM_PreObjectState(_setnewvalue ? _state : NULL, _minstate);
		char* p = GetSetObjectState(_obj, _setnewvalue ? _state->Get() : NULL);
		if (_setnewvalue)
		{
			ok = (p==NULL);
		}
		else if (p)
		{
			_state->Set(p);
			FreeHeapPtr((void*)p);
			ok = true;
		}
		SNM_PostObjectState(fxstate);
	}
	return ok;
}

int SNM_GetIntConfigVar(const char* _varName, int _errVal) {
	if (int* pVar = (int*)(GetConfigVar(_varName)))
		return *pVar;
	return _errVal;
}

bool SNM_SetIntConfigVar(const char* _varName, int _newVal) {
	if (int* pVar = (int*)(GetConfigVar(_varName))) {
		*pVar = _newVal;
		return true;
	}
	return false;
}

double SNM_GetDoubleConfigVar(const char* _varName, double _errVal) {
	if (double* pVar = (double*)(GetConfigVar(_varName)))
		return *pVar;
	return _errVal;
}

bool SNM_SetDoubleConfigVar(const char* _varName, double _newVal) {
	if (double* pVar = (double*)(GetConfigVar(_varName))) {
		*pVar = _newVal;
		return true;
	}
	return false;
}

// host some funcs from Ultraschall, https://github.com/Ultraschall
const char* ULT_GetMediaItemNote(MediaItem* _item) {
	if (_item)
		return (const char*)GetSetMediaItemInfo(_item, "P_NOTES", NULL);
	return "";
}

void ULT_SetMediaItemNote(MediaItem* _item, char* _str) {
	if (_str && _item)
		GetSetMediaItemInfo(_item, "P_NOTES", _str);
}


///////////////////////////////////////////////////////////////////////////////
// Toolbars auto refresh option
///////////////////////////////////////////////////////////////////////////////

DWORD g_toolbarRefreshTime=0, g_offscreenItemsRefreshTime=0;  // really approx (updated on timer)

void EnableToolbarsAutoRefesh(COMMAND_T* _ct) {
	g_SNM_ToolbarRefresh = !g_SNM_ToolbarRefresh;
}

int IsToolbarsAutoRefeshEnabled(COMMAND_T* _ct) {
	return g_SNM_ToolbarRefresh;
}

void RefreshToolbars()
{
	// offscreen item sel. buttons
	for (int i=0; i<SNM_ITEM_SEL_COUNT; i++)
		RefreshToolbar(SWSGetCommandID(ToggleOffscreenSelItems, i));
	RefreshToolbar(SWSGetCommandID(UnselectOffscreenItems, -1));

	// write automation button
	RefreshToolbar(SWSGetCommandID(ToggleWriteEnvExists));

#ifdef _SNM_HOST_AW
	// host AW's grid toolbar buttons auto refresh and track timebase auto refresh
	UpdateGridToolbar();
	UpdateTrackTimebaseToolbar();
#endif
}

// polled via SNM_CSurfRun()
void AutoRefreshToolbarRun()
{
	if (g_SNM_ToolbarRefresh)
	{
		if (GetTickCount() > g_offscreenItemsRefreshTime) {
			g_offscreenItemsRefreshTime = GetTickCount() + SNM_OFFSCREEN_UPDATE_FREQ;
			RefreshOffscreenItems();
		}
		if (GetTickCount() > g_toolbarRefreshTime) {
			g_toolbarRefreshTime = GetTickCount() + g_SNM_ToolbarRefreshFreq; // custom freq (from S&M.ini)
			RefreshToolbars();
		}
	}
}


///////////////////////////////////////////////////////////////////////////////
// Misc actions / helpers
///////////////////////////////////////////////////////////////////////////////

//increase/decrease the metronome volume
void ChangeMetronomeVolume(COMMAND_T* _ct) {
	KBD_OnMainActionEx(999, 0x40+(int)_ct->user, -1, 2, GetMainHwnd(), NULL);
}

void WinWaitForEvent(DWORD _event, DWORD _timeOut=500, DWORD _minReTrigger=500)
{
#ifdef _WIN32
	static DWORD sWaitTime = 0;
//	if ((GetTickCount() - sWaitTime) > _minReTrigger)
	{
		sWaitTime = GetTickCount();
		while((GetTickCount() - sWaitTime) < _timeOut) // for safety
		{
			MSG msg;
			if(PeekMessage(&msg, NULL, NULL, NULL, PM_REMOVE))
			{
				// new message to be processed
				TranslateMessage(&msg);
				DispatchMessage(&msg);
				if(msg.message == _event)
					break;
			}
		}
	}
#endif
}

// http://forum.cockos.com/showthread.php?p=612065
void SimulateMouseClick(COMMAND_T* _ct)
{
	POINT p;
	GetCursorPos(&p);
	mouse_event(MOUSEEVENTF_LEFTDOWN, p.x, p.y, 0, 0);
	mouse_event(MOUSEEVENTF_LEFTUP, p.x, p.y, 0, 0);
	WinWaitForEvent(WM_LBUTTONUP);
}

