#include "stdafx.h"
#include "WordsComplete.h"
#include "plugin.hpp"
#include <string>
#include <locale>
#include <vector>
#include <set>
#include <algorithm>

using std::wstring;
using std::vector;
using std::set;

#define PROCESS_EVENT 0
#define IGNORE_EVENT  1

bool IsItHotkey(INPUT_RECORD *rec);
wstring GetCurrentWord(int position);
bool IsDelimiter(wchar_t ch);
bool IsNotDelimiter(wchar_t ch);
vector<wstring> Split(wstring line);
vector<wstring> GatherWordsLikeThis(wstring word, int currentLine, int linesCount);
int ShowMenu(vector<wstring> items, int line, int position);
void WriteWord(wstring word);

const wchar_t *PluginName = L"Words Complete";

static PluginStartupInfo Info;

void WORDSCOMPLETE_API SetStartupInfoW(struct PluginStartupInfo *info)
{
	Info = *info;
}

int WORDSCOMPLETE_API ProcessEditorInputW(INPUT_RECORD *rec)
{
	if (!IsItHotkey(rec))
		return PROCESS_EVENT;
	
	EditorInfo editorInfo;
	Info.EditorControl(ECTL_GETINFO, &editorInfo);

	if (editorInfo.CurPos == 0)
		return PROCESS_EVENT;

	wstring wordToMatch = GetCurrentWord(editorInfo.CurPos);
	vector<wstring> words = GatherWordsLikeThis(wordToMatch, editorInfo.CurLine, editorInfo.TotalLines);

	if (words.empty())
		return PROCESS_EVENT;

	int choice;
	if (words.size() > 1)
	{
		int x = editorInfo.CurPos - editorInfo.LeftPos;
		int y = editorInfo.CurLine - editorInfo.TopScreenLine;
		choice = ShowMenu(words, x, y);
	}
	else
		choice = 0;

	if (choice < 0)
		return PROCESS_EVENT;

	wstring chosenWord = words[choice];
	WriteWord(chosenWord.substr(wordToMatch.length(), chosenWord.length() - wordToMatch.length()));

	return IGNORE_EVENT;
}

void   WINAPI _export GetPluginInfoW(struct PluginInfo *Info)
{
	Info->StructSize = sizeof(*Info);
	Info->Flags = PF_EDITOR | PF_DISABLEPANELS;
	Info->DiskMenuStringsNumber = 0;
	Info->PluginConfigStringsNumber = 0;
	Info->PluginMenuStrings = &PluginName;
	Info->PluginMenuStringsNumber = 0;
}

int ShowMenu(vector<wstring> items, int x, int y)
{
	const int MaxMenuSize = 20;
	FarMenuItem menu[MaxMenuSize];
	int menuSize = items.size() > MaxMenuSize ? MaxMenuSize : items.size();
	for (int i = 0; i < menuSize; i++)
	{
		menu[i].Checked = 0;
		menu[i].Selected = 0;
		menu[i].Separator = 0;
		menu[i].Text = items[i].c_str();
	}

	return Info.Menu(Info.ModuleNumber, x + 2, y + 2, MaxMenuSize + 2, 
		FMENU_WRAPMODE, 0, 0, 0, 0, 0, menu, menuSize);
}

void WriteWord(wstring word)
{
	Info.EditorControl(ECTL_INSERTTEXT, (void *)word.c_str());
	Info.EditorControl(ECTL_REDRAW, 0);
}

vector<wstring> GatherWordsLikeThis(wstring wordToMatch, int currentLine, int linesCount)
{
	set<wstring> result;
	const int ScanRadius = 2000;
	int firstLineToScan = currentLine > ScanRadius 
							? currentLine - ScanRadius 
							: 0;
	int lastLineToScan = linesCount > currentLine + ScanRadius 
							? currentLine + ScanRadius 
							: linesCount;

	EditorGetString getStringInfo;
	for (int lineNumber = firstLineToScan; lineNumber < lastLineToScan; lineNumber++)
	{
		getStringInfo.StringNumber = lineNumber;
		Info.EditorControl(ECTL_GETSTRING, &getStringInfo);
		vector<wstring> wordsOfLine = Split(wstring(getStringInfo.StringText));
		for (vector<wstring>::const_iterator i = wordsOfLine.begin(); i != wordsOfLine.end(); ++i)
		{
			wstring word = *i;
			if (word.length() > wordToMatch.length() 
				&& word.substr(0, wordToMatch.length()) == wordToMatch)
			{
				result.insert(word);
			}
		}
	}
	return vector<wstring>(result.begin(), result.end());
}

bool IsItHotkey(INPUT_RECORD *rec)
{
	if (rec->EventType != KEY_EVENT)
		return false;

	if (!rec->Event.KeyEvent.bKeyDown)
		return false;

	//Ctrl-Space
	bool controlIsPressed = 
		(rec->Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED) ||
		(rec->Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED);

	if (controlIsPressed && (rec->Event.KeyEvent.wVirtualKeyCode == VK_SPACE))
		return true;

	//App key
	return rec->Event.KeyEvent.wVirtualKeyCode == VK_APPS;
}

vector<wstring> Split(wstring line)
{
	vector<wstring> result;

	wstring::iterator wordStart = find_if(line.begin(), line.end(), IsNotDelimiter);
	while (wordStart != line.end())
	{
		wstring::iterator wordEnd = find_if(wordStart, line.end(), IsDelimiter);
		if (wordEnd != wordStart)
		{
			result.push_back(wstring(wordStart, wordEnd));
			wordStart = find_if(wordEnd, line.end(), IsNotDelimiter);
		}
	}

	return result;
}

wstring GetCurrentWord(int position)
{
	EditorGetString getStringInfo;
	getStringInfo.StringNumber = -1; 
	Info.EditorControl(ECTL_GETSTRING, &getStringInfo);
	wstring line(getStringInfo.StringText, position);

	std::reverse(line.begin(), line.end());
	wstring::const_iterator end = find_if(line.begin(), line.end(), IsDelimiter);
	wstring word(line.begin(), end);
	std::reverse(word.begin(), word.end());

	return word;
}

bool IsDelimiter(wchar_t ch)
{
	std::locale defaultLocale;
	return std::isspace(ch, defaultLocale) || (ch != L'_' && std::ispunct(ch, defaultLocale));
}

bool IsNotDelimiter(wchar_t ch)
{
	return !IsDelimiter(ch);
}