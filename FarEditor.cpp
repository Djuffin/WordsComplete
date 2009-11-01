
#include"FarEditor.h"
#include<common/Logging.h>

FarEditor::FarEditor(PluginStartupInfo *info, ParserFactory *pf)
{
  parserFactory = pf;
  baseEditor = new BaseEditor(parserFactory, this);

  this->info = info;
  info->EditorControl(ECTL_GETINFO, &ei);
  oldFAR = info->StructSize < sizeof(PluginStartupInfo);

  cursorRegion = null;
  unicodeEncodingIndex = -1;
  farEncodingIndex = -3;
  prevLinePosition = 0;
  blockTopPosition = -1;
  inRedraw = false;
  inHandler = false;
  ignoreChange = false;
  idleCount = 0;

  ret_str = null;
  ret_strNumber = -1;

  maxLineLength = 0;
  fullBackground = true;

  showHorizontalCross = showVerticalCross = true;
  crossZOrder    = 0;
  horzCrossColor = 0x0E;
  vertCrossColor = 0x0E;

  drawPairs = drawSyntax = true;
  oldOutline = false;
  maxTime = 3000;

  newback = newfore = -1;
  rdBackground = null;
  visibleLevel = 100;

  const Region *def_Outlined = pf->getHRCParser()->getRegion(&DString("def:Outlined"));
  const Region *def_Error = pf->getHRCParser()->getRegion(&DString("def:Error"));
  structOutliner = new Outliner(baseEditor, def_Outlined);
  errorOutliner = new Outliner(baseEditor, def_Error);
}

FarEditor::~FarEditor(){
  delete cursorRegion;
  delete structOutliner;
  delete errorOutliner;
  delete baseEditor;
  delete ret_str;
}


void FarEditor::endJob(int lno){
  delete ret_str;
  ret_str = null;
}

String *FarEditor::getLine(int lno)
{
  EditorGetString es;
  int len = 0;
  if (ret_strNumber == lno && ret_str != null) return ret_str;
  ret_strNumber = lno;
  if (inHandler){
    EditorSetPosition esp;
    es.StringNumber = -1;
    es.StringText = null;
    esp.CurLine = lno;
    esp.CurPos = esp.CurTabPos = esp.TopScreenLine = esp.LeftPos = esp.Overtype = -1;
    info->EditorControl(ECTL_SETPOSITION, &esp);
    if (info->EditorControl(ECTL_GETSTRING, &es)) len = es.StringLength;
  }else{
    es.StringNumber = lno;
    es.StringText = null;
    if (info->EditorControl(ECTL_GETSTRING, &es)) len = es.StringLength;
  };
  if (len > maxLineLength && maxLineLength > 0) len = maxLineLength;
  delete ret_str;
  ret_str = new DString(es.StringText, 0, len);
  return ret_str;
}

void FarEditor::chooseFileType(String *fname){
  FileType *ftype = baseEditor->chooseFileType(fname);
  setFileType(ftype);
}


void FarEditor::setFileType(FileType *ftype){
  baseEditor->setFileType(ftype);

  reloadTypeSettings();
}

void FarEditor::reloadTypeSettings()
{
  //BUG ?
  //FileType *ftype = baseEditor->getFileType();
  HRCParser *hrcParser = parserFactory->getHRCParser();

  FileType *def = hrcParser->getFileType(&DString("default"));
  if (def == null){
    throw Exception(DString("No 'default' file type found"));
  }

  // installs custom file properties
  int backparse = def->getParamValueInt(DString("backparse"), 2000);
  maxLineLength = def->getParamValueInt(DString("maxlinelength"), 0);
  newfore = def->getParamValueInt(DString("default-fore"), -1);
  newback = def->getParamValueInt(DString("default-back"), -1);

  const String *value;
  value = def->getParamValue(DString("fullback"));
  if (value != null && value->equals("no")) fullBackground = false;
  value = def->getParamValue(DString("show-cross"));
  if (value != null && value->equals("none")){
    showHorizontalCross = false;
    showVerticalCross   = false;
  };
  if (value != null && value->equals("vertical")){
    showHorizontalCross = false;
    showVerticalCross   = true;
  };
  if (value != null && value->equals("horizontal")){
    showHorizontalCross = true;
    showVerticalCross   = false;
  };
  if (value != null && value->equals("both")){
    showHorizontalCross = true;
    showVerticalCross   = true;
  };
  value = def->getParamValue(DString("cross-zorder"));
  if (value != null && value->equals("top")) crossZOrder = 1;

  baseEditor->setBackParse(backparse);
}


FileType *FarEditor::getFileType(){
  return baseEditor->getFileType();
}

void FarEditor::setDrawCross(bool drawCross)
{
  if (!drawCross){
    showHorizontalCross = false;
    showVerticalCross   = false;
  }else{
    reloadTypeSettings();
  }
}

void FarEditor::setDrawPairs(bool drawPairs)
{
  this->drawPairs = drawPairs;
}

void FarEditor::setDrawSyntax(bool drawSyntax)
{
  this->drawSyntax = drawSyntax;
}

void FarEditor::setOutlineStyle(bool oldStyle)
{
  this->oldOutline = oldStyle;
}

void FarEditor::setMaxTime(int maxTime)
{
  this->maxTime = maxTime;
}

void FarEditor::setRegionMapper(RegionMapper *rs)
{
  baseEditor->setRegionMapper(rs);

  rdBackground = StyledRegion::cast(baseEditor->rd_def_Text);
  horzCrossColor = convert(StyledRegion::cast(baseEditor->rd_def_HorzCross));
  vertCrossColor = convert(StyledRegion::cast(baseEditor->rd_def_VertCross));
  if (horzCrossColor == 0) horzCrossColor = 0x0E;
  if (vertCrossColor == 0) vertCrossColor = 0x0E;
}



void FarEditor::matchPair()
{
  EditorSetPosition esp;

  enterHandler();
  PairMatch *pm = baseEditor->searchGlobalPair(ei.CurLine, ei.CurPos);
  if (pm == null){
    leaveHandler();
    return;
  }
  leaveHandler();

  if (pm->eline == -1) return;

  esp.CurTabPos = -1;
  esp.LeftPos = -1;
  esp.Overtype = -1;
  esp.TopScreenLine = -1;
  esp.CurLine = pm->eline;
  esp.CurPos = pm->end->end-1;
  if (!pm->topPosition) esp.CurPos = pm->end->start;
  if (esp.CurLine < ei.TopScreenLine || esp.CurLine > ei.TopScreenLine+ei.WindowSizeY){
     esp.TopScreenLine = pm->eline - ei.WindowSizeY/2;
     if (esp.TopScreenLine < 0) esp.TopScreenLine = 0;
  };
  info->EditorControl(ECTL_SETPOSITION, &esp);
  baseEditor->releasePairMatch(pm);
  ignoreChange = true;
}

void FarEditor::selectPair()
{
  EditorSelect es;
  int X1, X2, Y1, Y2;
  enterHandler();
  PairMatch *pm = baseEditor->searchGlobalPair(ei.CurLine, ei.CurPos);
  if (pm == null){
    leaveHandler();
    return;
  }
  leaveHandler();
  if (pm->eline == -1) return;

  X1 = pm->start->end;
  X2 = pm->end->start-1;
  Y1 = pm->sline;
  Y2 = pm->eline;
  if (!pm->topPosition){
    X2 = pm->start->start-1;
    X1 = pm->end->end;
    Y2 = pm->sline;
    Y1 = pm->eline;
  };
  es.BlockType = BTYPE_STREAM;
  es.BlockStartLine = Y1;
  es.BlockStartPos = X1;
  es.BlockHeight = Y2 - Y1 + 1;
  es.BlockWidth = X2 - X1 + 1;
  info->EditorControl(ECTL_SELECT, &es);

  baseEditor->releasePairMatch(pm);
  ignoreChange = true;
}

void FarEditor::selectBlock()
{
  EditorSelect es;
  int X1, X2, Y1, Y2;
  enterHandler();
  PairMatch *pm = baseEditor->searchGlobalPair(ei.CurLine, ei.CurPos);
  if (pm == null){
    leaveHandler();
    return;
  }
  leaveHandler();

  if (pm->eline == -1) return;

  X1 = pm->start->start;
  X2 = pm->end->end-1;
  Y1 = pm->sline;
  Y2 = pm->eline;
  if (!pm->topPosition){
    X2 = pm->start->end-1;
    X1 = pm->end->start;
    Y2 = pm->sline;
    Y1 = pm->eline;
  };
  es.BlockType = BTYPE_STREAM;
  es.BlockStartLine = Y1;
  es.BlockStartPos = X1;
  es.BlockHeight = Y2 - Y1 + 1;
  es.BlockWidth = X2 - X1 + 1;
  info->EditorControl(ECTL_SELECT, &es);

  baseEditor->releasePairMatch(pm);
  ignoreChange = true;
}

void FarEditor::selectRegion()
{
  EditorSelect es;
  EditorGetString egs;

  egs.StringNumber = ei.CurLine;
  info->EditorControl(ECTL_GETSTRING, &egs);
  int end = cursorRegion->end;
  if (end == -1) end = egs.StringLength;

  if (cursorRegion != null && end - cursorRegion->start > 0){
    es.BlockType = BTYPE_STREAM;
    es.BlockStartLine = ei.CurLine;
    es.BlockStartPos = cursorRegion->start;
    es.BlockHeight = 1;
    es.BlockWidth = end - cursorRegion->start;
    info->EditorControl(ECTL_SELECT, &es);
  };
  ignoreChange = true;
}

void FarEditor::listFunctions(){
  enterHandler();
  baseEditor->validate(-1, false);
  leaveHandler();
  showOutliner(structOutliner);
  ignoreChange = true;
}

void FarEditor::listErrors(){
  enterHandler();
  baseEditor->validate(-1, false);
  leaveHandler();
  showOutliner(errorOutliner);
  ignoreChange = true;
}


void FarEditor::locateFunction(){
  // extract word
  String &curLine = *getLine(ei.CurLine);
  int cpos = ei.CurPos;
  int sword = cpos;
  int eword = cpos;

  while (cpos < curLine.length() &&
     (Character::isLetterOrDigit(curLine[cpos]) || curLine[cpos] != '_'))
  {
    while(Character::isLetterOrDigit(curLine[eword]) || curLine[eword] == '_')
    {
      if (eword == curLine.length()-1)
        break;
      eword++;
    }
    while(Character::isLetterOrDigit(curLine[sword]) || curLine[sword] == '_'){
      if (sword == 0)
        break;
      sword--;
    }
    SString funcname(curLine, sword+1, eword-sword-1);

    CLR_INFO("FC", "Letter %s", funcname.getChars());

    enterHandler();
    baseEditor->validate(-1, false);
    leaveHandler();
    ignoreChange = true;
   
    EditorSetPosition esp;
    OutlineItem *item_found = null;
    OutlineItem *item_last = null;
    int items_num = structOutliner->itemCount();

    if (items_num == 0) break;
    
    //search through the outliner
    for (int idx = 0; idx < items_num; idx++){
      OutlineItem *item = structOutliner->getItem(idx);
      if (item->token->indexOfIgnoreCase(DString(funcname)) != -1){
        if (item->lno == ei.CurLine){
          item_last = item;
        }else{
          item_found = item;
        }
      }
    }
    if (!item_found) item_found = item_last;

    if (!item_found) break;

    esp.CurTabPos = esp.LeftPos = esp.Overtype = esp.TopScreenLine = -1;
    esp.CurLine = item_found->lno;
    esp.CurPos = item_found->pos;
    esp.TopScreenLine = esp.CurLine - ei.WindowSizeY/2;
    if (esp.TopScreenLine < 0) esp.TopScreenLine = 0;
    info->EditorControl(ECTL_SETPOSITION, &esp);
    info->EditorControl(ECTL_REDRAW, NULL);
    info->EditorControl(ECTL_GETINFO, &ei);
    return;
  };
  
  const wchar_t *msg[2] = { GetMsg(mNothingFound), GetMsg(mGotcha) };
  info->Message(info->ModuleNumber, 0, 0, msg, 2, 1);
}

void FarEditor::updateHighlighting(){
  baseEditor->validate(ei.TopScreenLine, true);
}

int FarEditor::editorInput(const INPUT_RECORD *ir)
{
  if (ir->EventType == KEY_EVENT && ir->Event.KeyEvent.wVirtualKeyCode == 0){
    idleCount++;
    if (idleCount > 10){
      idleCount = 10;
    }
    baseEditor->idleJob(idleCount*10);
    info->EditorControl(ECTL_REDRAW, NULL);
  }else if(ir->EventType == KEY_EVENT){
    idleCount = 0;
  };
  return 0;
}

int FarEditor::editorEvent(int event, void *param)
{
  // ignore event
  if (event != EE_REDRAW || inRedraw) return 0;

  enterHandler();

  if (WindowSizeX != ei.WindowSizeX || WindowSizeY != ei.WindowSizeY){
    WindowSizeX = ei.WindowSizeX;
    WindowSizeY = ei.WindowSizeY;
  };

  baseEditor->visibleTextEvent(ei.TopScreenLine, WindowSizeY);
  baseEditor->lineCountEvent(ei.TotalLines);

  // Hack against FAR's bad EEREDRAW_CHANGE events
  if (param == EEREDRAW_CHANGE && ignoreChange == true){
    param = EEREDRAW_ALL;
    ignoreChange = false;
  };

  if (oldFAR || param == EEREDRAW_CHANGE){
    int ml = (prevLinePosition < ei.CurLine ? prevLinePosition : ei.CurLine)-1;
    if (ml < 0) ml = 0;
    if (blockTopPosition != -1 && ml > blockTopPosition) ml = blockTopPosition;
    baseEditor->modifyEvent(ml);
  };
  prevLinePosition = ei.CurLine;
  blockTopPosition = -1;
  if (ei.BlockType != BTYPE_NONE) blockTopPosition = ei.BlockStartLine;

  // hack against tabs in FAR's editor
  EditorConvertPos ecp, ecp_cl;

  ecp.StringNumber = -1;
  ecp.SrcPos = ei.CurPos;
  info->EditorControl(ECTL_REALTOTAB, &ecp);

  delete cursorRegion;
  cursorRegion = null;

  if (rdBackground == null) throw Exception(DString("HRD Background region 'def:Text' not found"));

  for(int lno = ei.TopScreenLine; lno < ei.TopScreenLine + WindowSizeY; lno++){
    if (lno >= ei.TotalLines) break;
    LineRegion *l1 = null;
    if (drawSyntax || drawPairs){
      l1 = baseEditor->getLineRegions(lno);
    }

    addFARColor(lno, -1, 0, 0);

    EditorGetString egs;
    egs.StringNumber = lno;
    info->EditorControl(ECTL_GETSTRING, &egs);
    int llen = egs.StringLength;

    // fills back
    if (lno == ei.CurLine && showHorizontalCross)
      addFARColor(lno, 0, ei.LeftPos + ei.WindowSizeX, horzCrossColor);
    else
      addFARColor(lno, 0, ei.LeftPos + ei.WindowSizeX, convert(null));

    if (showVerticalCross){
      ecp_cl.StringNumber = lno;
      ecp_cl.SrcPos = ecp.DestPos;
      info->EditorControl(ECTL_TABTOREAL, &ecp_cl);
      addFARColor(lno, ecp_cl.DestPos, ecp_cl.DestPos+1, vertCrossColor+0x10000);
    };
    bool vertCrossDone = false;

    if (drawSyntax)
    for(; l1; l1 = l1->next){
      if (l1->special) continue;
      if (l1->start == l1->end) continue;
      if (l1->start > ei.LeftPos+ei.WindowSizeX) continue;
      if (l1->end != -1 && l1->end < ei.LeftPos-ei.WindowSizeX) continue;

      if ((lno != ei.CurLine || !showHorizontalCross || crossZOrder == 0)){
        int color = convert(l1->styled());
        if (lno == ei.CurLine && showHorizontalCross){
          if (foreDefault(color)) color = (color&0xF0) + (horzCrossColor&0xF);
          if (backDefault(color)) color = (color&0xF) + (horzCrossColor&0xF0);
        };
        if (!color) continue;

        int lend = l1->end;
        if (lend == -1) lend = fullBackground ? ei.LeftPos+ei.WindowSizeX : llen;
        addFARColor(lno, l1->start, lend, color);

        if (lno == ei.CurLine && (l1->start <= ei.CurPos) && (ei.CurPos <= lend)){
          delete cursorRegion;
          cursorRegion = new LineRegion(*l1);
        };
        // column
        if (showVerticalCross && crossZOrder == 0 && l1->start <= ecp_cl.DestPos && ecp_cl.DestPos < lend) {
          color = convert(l1->styled());
          if (foreDefault(color)) color = (color&0xF0) + (vertCrossColor&0xF);
          if (backDefault(color)) color = (color&0xF) + (vertCrossColor&0xF0);
          ecp_cl.StringNumber = lno;
          ecp_cl.SrcPos = ecp.DestPos;
          info->EditorControl(ECTL_TABTOREAL, &ecp_cl);
          addFARColor(lno, ecp_cl.DestPos, ecp_cl.DestPos+1, color+0x10000);
          vertCrossDone = true;
        };

      };
    };

    if (showVerticalCross && !vertCrossDone){
      ecp_cl.StringNumber = lno;
      ecp_cl.SrcPos = ecp.DestPos;
      info->EditorControl(ECTL_TABTOREAL, &ecp_cl);
      addFARColor(lno, ecp_cl.DestPos, ecp_cl.DestPos+1, vertCrossColor+0x10000);
    };
  };


  /// pair brackets
  PairMatch *pm = null;
  if (drawPairs){
    pm = baseEditor->searchLocalPair(ei.CurLine, ei.CurPos);
  }
  if (pm != null){
    int color = convert(pm->start->styled());
    if (showHorizontalCross){
      if (foreDefault(color)) color = (color&0xF0) + (horzCrossColor&0xF);
      if (backDefault(color)) color = (color&0xF) + (horzCrossColor&0xF0);
    };
    addFARColor(ei.CurLine, pm->start->start, pm->start->end, color);
    if (showVerticalCross && !showHorizontalCross && pm->start->start <= ei.CurPos && ei.CurPos < pm->start->end){
      color = convert(pm->start->styled());
      if (foreDefault(color)) color = (color&0xF0) + (vertCrossColor&0xF);
      if (backDefault(color)) color = (color&0xF) + (vertCrossColor&0xF0);
      addFARColor(pm->sline, ei.CurPos, ei.CurPos+1, color+0x10000);
    };

    if (pm->eline != -1){
      color = convert(pm->end->styled());
      if (showHorizontalCross && pm->eline == ei.CurLine){
        if (foreDefault(color)) color = (color&0xF0) + (horzCrossColor&0xF);
        if (backDefault(color)) color = (color&0xF) + (horzCrossColor&0xF0);
      };
      addFARColor(pm->eline, pm->end->start, pm->end->end, color);

      ecp.StringNumber = pm->eline;
      ecp.SrcPos = ecp.DestPos;
      info->EditorControl(ECTL_TABTOREAL, &ecp);
      if (showVerticalCross && pm->end->start <= ecp.DestPos && ecp.DestPos < pm->end->end) {
        color = convert(pm->end->styled());
        if (foreDefault(color)) color = (color&0xF0) + (vertCrossColor&0xF);
        if (backDefault(color)) color = (color&0xF) + (vertCrossColor&0xF0);
        addFARColor(pm->eline, ecp.DestPos, ecp.DestPos+1, color+0x10000);
      };
    };
    baseEditor->releasePairMatch(pm);
  };

  leaveHandler();

  if (param != EEREDRAW_ALL){
    inRedraw = true;
    info->EditorControl(ECTL_REDRAW, NULL);
    inRedraw = false;
  };
  return true;
}


void FarEditor::showOutliner(Outliner *outliner)
{
FarMenuItem *menu;
EditorSetPosition esp;
bool moved = false;
int i, code = 0;

const int FILTER_SIZE = 40;

  int breakKeys[] =
  {
    VK_BACK, VK_RETURN, 0xBA, 0xBD, VK_TAB,
    (PKF_CONTROL<<16)+VK_UP, (PKF_CONTROL<<16)+VK_DOWN,
    (PKF_CONTROL<<16)+VK_LEFT, (PKF_CONTROL<<16)+VK_RIGHT,
    (PKF_CONTROL<<16)+VK_RETURN,
    '0','1','2','3','4','5','6','7','8','9',
    'A','B','C','D','E','F','G','H','I','J',
    'K','L','M','N','O','P','Q','R','S','T',
    'U','V','W','X','Y','Z',' ', 0,
  };

  int keys_size = 0;
  while (breakKeys[keys_size] !=0) keys_size++;

  // BUG ?
  //int outputEnc = Encodings::getEncodingIndex("cp866");

  wchar_t prefix[FILTER_SIZE+1];
  wchar_t autofilter[FILTER_SIZE+1];
  wchar_t filter[FILTER_SIZE+1];
  int  flen = 0, autoIncremented = 0;
  *filter = 0;

  int maxLevel = -1;

  bool stopMenu = false;
  int items_num = outliner->itemCount();
  if (items_num == 0) stopMenu = true;
  menu = new FarMenuItem[items_num];

  while (!stopMenu) {
    memset(menu, 0, sizeof(FarMenuItem)*items_num);

    // items in FAR's menu;
    int menu_size = 0;
    int selectedItem = 0;

    Vector<int> treeStack;
    for(i = 0; i < items_num; i++){
      OutlineItem *item = outliner->getItem(i);
      if (item->token->indexOfIgnoreCase(DString(filter)) != -1){

        int treeLevel = Outliner::manageTree(treeStack, item->level);
        if (maxLevel < treeLevel) maxLevel = treeLevel;
        if (treeLevel > visibleLevel) continue;

        wchar_t * menuItem = new wchar_t[255];
        if (!oldOutline){
          int si = _snwprintf(menuItem, 255, L"%4d ", item->lno+1);
          for(int lIdx = 0; lIdx < treeLevel; lIdx++){
            menuItem[si++] = ' ';
            menuItem[si++] = ' ';
          };
          const String *region = item->region->getName();
          wchar_t cls = Character::toLowerCase((*region)[region->indexOf(':')+1]);
          si += _snwprintf(menuItem+si, 255-si, L"%c ", cls);

          int labelLength = item->token->length();
          if (labelLength+si > 110) labelLength = 110;
          wcsncpy(menuItem+si, item->token->getWChars(), labelLength);
          menuItem[si+labelLength] = 0;
        }else{
          String *line = getLine(item->lno);
          int labelLength = line->length();
          if (labelLength > 110) labelLength = 110;
          wcsncpy(menuItem, line->getWChars(), labelLength);
          menuItem[labelLength] = 0;
        }

        *(OutlineItem**)(&menuItem[124]) = item;
        // set position on nearest top function
        menu[menu_size].Text = menuItem;

        if (ei.CurLine >= item->lno) selectedItem = menu_size;
        menu_size++;
      };
    }
    if (selectedItem > 0) menu[selectedItem].Selected = 1;

    if (menu_size == 0 && flen > 0){
      flen--;
      filter[flen] = 0;
      continue;
    }

    int aflen = flen;
    // Find same function prefix
    bool same = true;
    int plen = 0;
    wcscpy(autofilter, filter);

    while (code != 0 && menu_size > 1 && same && plen < FILTER_SIZE){
      plen = aflen + 1;
      int auto_ptr  = DString(menu[0].Text).indexOfIgnoreCase(DString(autofilter));
      if (int(wcslen(menu[0].Text)-auto_ptr) < plen) break;
      wcsncpy(prefix, menu[0].Text+auto_ptr, plen);
      prefix[plen] = 0;
      for (int j = 1 ; j < menu_size ; j++ ){
        if (DString(menu[j].Text).indexOfIgnoreCase(DString(prefix)) == -1){
          same = false;
          break;
        }
      }
      if (same){
        aflen++;
        wcscpy(autofilter, prefix);
      }
    }
    autoIncremented = (int)(wcslen(autofilter) - wcslen(filter));


    wchar_t top[128];
    const wchar_t *topline = GetMsg(mOutliner);
    wchar_t captionfilter[FILTER_SIZE+1];
    wcsncpy(captionfilter, filter, flen);
    captionfilter[flen] = 0;
    if (aflen > flen) {
      wcscat(captionfilter, L"?");
      wcsncat(captionfilter, autofilter+flen, aflen-flen);
      captionfilter[aflen+1] = 0;
    }
    _snwprintf(top, 128, topline, captionfilter);

    int sel = 0;
    sel = info->Menu(info->ModuleNumber, -1, -1, 0, FMENU_SHOWAMPERSAND|FMENU_WRAPMODE,
                  top, GetMsg(mChoose), L"add", breakKeys, &code, menu, menu_size);

    // handle mouse selection
    if (sel != -1 && code == -1){
      code = 1;
    }

    switch (code){
      case -1:
        stopMenu = true;
        break;
      case 0: // VK_BACK
        if (flen > 0) flen--;
        filter[flen] = 0;
        break;
      case 1:{ // VK_RETURN
        if (menu_size == 0) break;
        esp.CurTabPos = esp.LeftPos = esp.Overtype = esp.TopScreenLine = -1;
        OutlineItem *item = *(OutlineItem**)(&menu[sel].Text[124]);
        esp.CurLine = item->lno;
        esp.CurPos = item->pos;
        esp.TopScreenLine = esp.CurLine - ei.WindowSizeY/2;
        if (esp.TopScreenLine < 0) esp.TopScreenLine = 0;
        info->EditorControl(ECTL_SETPOSITION, &esp);
        stopMenu = true;
        moved = true;
        break;
      }
      case 2: // :
        if (flen == FILTER_SIZE) break;
        filter[flen]= ':';
        filter[++flen]= 0;
        break;

      case 3: // _
        if (flen == FILTER_SIZE) break;
        filter[flen]= '_';
        filter[++flen]= 0;
        break;

      case 4: // VK_TAB
        wcscpy(filter, autofilter);
        flen = aflen;
        break;

      case 5:{ // ctrl-up
        if (menu_size == 0) break;
        if (sel == 0) sel = menu_size-1;
        else sel--;
        esp.CurTabPos = esp.LeftPos = esp.Overtype = esp.TopScreenLine = -1;
        OutlineItem *item = *(OutlineItem**)(&menu[sel].Text[124]);
        esp.CurLine = item->lno;
        esp.CurPos = item->pos;
        esp.TopScreenLine = esp.CurLine - ei.WindowSizeY/2;
        if (esp.TopScreenLine < 0) esp.TopScreenLine = 0;
        info->EditorControl(ECTL_SETPOSITION, &esp);
        info->EditorControl(ECTL_REDRAW, NULL);
        info->EditorControl(ECTL_GETINFO, &ei);
        break;
      }
      case 6:{ // ctrl-down
        if (menu_size == 0) break;
        if (sel == menu_size-1) sel = 0;
        else sel++;
        esp.CurTabPos = esp.LeftPos = esp.Overtype = esp.TopScreenLine = -1;
        OutlineItem *item = *(OutlineItem**)(&menu[sel].Text[124]);
        esp.CurLine = item->lno;
        esp.CurPos = item->pos;
        esp.TopScreenLine = esp.CurLine - ei.WindowSizeY/2;
        if (esp.TopScreenLine < 0) esp.TopScreenLine = 0;
        info->EditorControl(ECTL_SETPOSITION, &esp);
        info->EditorControl(ECTL_REDRAW, NULL);
        info->EditorControl(ECTL_GETINFO, &ei);
        break;
      }
      case 7:{ // ctrl-left
        if (visibleLevel > maxLevel) visibleLevel = maxLevel-1;
        else if (visibleLevel > 0) visibleLevel--;
        if (visibleLevel < 0) visibleLevel = 0;
        break;
      }
      case 8:{ // ctrl-right
        visibleLevel++;
        break;
      }
      case 9:{ // ctrl-return
        OutlineItem *item = *(OutlineItem**)(&menu[sel].Text[124]);
        info->EditorControl(ECTL_INSERTTEXT, (void*)item->token->getChars());
        stopMenu = true;
        break;
      }
      default:
        if (flen == FILTER_SIZE || code > keys_size) break;
        filter[flen] = (char)Character::toLowerCase(breakKeys[code]);
        filter[++flen] = 0;
        break;
    }
  }

  for(int i = 0; i < items_num; i++)
    delete[] menu[i].Text;
  delete[] menu;

  if (!moved) {
    // restoring position
    esp.CurLine = ei.CurLine;
    esp.CurPos = ei.CurPos;
    esp.CurTabPos = ei.CurTabPos;
    esp.TopScreenLine = ei.TopScreenLine;
    esp.LeftPos = ei.LeftPos;
    esp.Overtype = ei.Overtype;
    info->EditorControl(ECTL_SETPOSITION, &esp);
  }

  if (items_num == 0) {
    const wchar_t *msg[2] = { GetMsg(mNothingFound), GetMsg(mGotcha) };
    info->Message(info->ModuleNumber, 0, 0, msg, 2, 1);
  }
}

void FarEditor::enterHandler(){
  inHandler = true;
  info->EditorControl(ECTL_GETINFO, &ei);
  ret_strNumber = -1;
}

void FarEditor::leaveHandler(){
  // restoring position
  EditorSetPosition esp;
  esp.CurLine = ei.CurLine;
  esp.CurPos = ei.CurPos;
  esp.TopScreenLine = ei.TopScreenLine;
  esp.LeftPos = ei.LeftPos;
  esp.CurTabPos = -1;
  esp.Overtype = -1;
  info->EditorControl(ECTL_SETPOSITION, &esp);
  inHandler = false;
}

int FarEditor::convert(const StyledRegion *rd){
  int color;
  if (rdBackground == null) return 0;
  if (rd != null) color = rd->fore + (rd->back << 4);
  int fore = (newfore != -1)?newfore:rdBackground->fore;
  int back = (newback != -1)?newback:rdBackground->back;
  if (rd == null || !rd->bfore) color = (color&0xF0) + fore;
  if (rd == null || !rd->bback) color = (color&0xF) + (back << 4);
  return color;
}

bool FarEditor::foreDefault(int color){
  return (color&0xF) == rdBackground->fore;
}

bool FarEditor::backDefault(int color){
  return (color>>4) == rdBackground->back;
}

void FarEditor::addFARColor(int lno, int s, int e, int col)
{
  EditorColor ec;

  ec.StringNumber = lno;
  ec.StartPos = s;
  ec.EndPos = e-1;
  ec.Color = col;
  CLR_TRACE("FarEditor", "line:%d, %d-%d, color:%x", lno, s, e, col);
  info->EditorControl(ECTL_ADDCOLOR, &ec);
  CLR_TRACE("FarEditor", "line %d: %d-%d: color=%x", lno, s, e, col);
}

const wchar_t *FarEditor::GetMsg(int msg){
  return(info->GetMsg(info->ModuleNumber, msg));
}

void FarEditor::cleanEditor()
{
  int col=(int)info->AdvControl(info->ModuleNumber,ACTL_GETCOLOR,(void *)COL_EDITORTEXT );
  for (int i=0;i<ei.TotalLines;i++){
    EditorGetString egs;
    egs.StringNumber=i;
    info->EditorControl(ECTL_GETSTRING,&egs);
    if (ei.LeftPos + ei.WindowSizeX>egs.StringLength)
      addFARColor(i,0,ei.LeftPos + ei.WindowSizeX,col);
    else
      addFARColor(i,0,egs.StringLength,col);
  }

}

/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Colorer Library.
 *
 * The Initial Developer of the Original Code is
 * Cail Lomecb <cail@nm.ru>.
 * Portions created by the Initial Developer are Copyright (C) 1999-2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */