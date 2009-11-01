
#include<misc/registry_wide.cpp>
#include<unicode/Encodings.h>
#include<common/Logging.h>
#include<colorer/viewer/TextConsoleViewer.h>

#include"FarEditorSet.h"

#define REG_DISABLED        L"disabled"
#define REG_HRD_NAME        L"hrdName"
#define REG_CATALOG         L"catalog"
#define REG_MAXTIME         L"maxTime"
#define REG_CROSSDONTDRAW   L"crossDontDraw"
#define REG_PAIRSDONTDRAW   L"pairsDontDraw"
#define REG_SYNTAXDONTDRAW  L"syntaxDontDraw"
#define REG_OLDOUTLINE      L"oldOutlineView"

wchar_t* rtrim(wchar_t* str)
{
    wchar_t* ptr = str;
    str += wcslen(str);
    while (iswspace(*(--str))) *str = 0;
    return ptr;
}

wchar_t* ltrim(wchar_t* str)
{
    while (iswspace(*(str++)));
    return str - 1;
}

wchar_t* trim(wchar_t* str)
{
    return ltrim(rtrim(str));
}


FarEditorSet::FarEditorSet(PluginStartupInfo *fedi)
{
  info = fedi;

  wchar_t key[255];
  _snwprintf(key, 255, L"%s\\ColorerSE", info->RootKey);
  hPluginRegistry = rOpenKey(HKEY_CURRENT_USER, key);

  parserFactory = null;
  regionMapper = null;
  reloadBase();
}

FarEditorSet::~FarEditorSet(){
  dropAllEditors();
  RegCloseKey(hPluginRegistry);
  delete regionMapper;
  delete parserFactory;
}


void FarEditorSet::openMenu(){
  int iMenuItems[] = {
    mListTypes, mMatchPair, mSelectBlock, mSelectPair,
    mListFunctions, mFindErrors, mSelectRegion, mLocateFunction, -1,
    mUpdateHighlight, mConfigure
  };
  FarMenuItem menuElements[sizeof(iMenuItems) / sizeof(iMenuItems[0])];
  memset(menuElements, 0, sizeof(menuElements));

  try{
    if (rDisabled){
      menuElements[0].Text = GetMsg(mConfigure);
      menuElements[0].Selected = 1;
      if(info->Menu(info->ModuleNumber, -1, -1, 0, FMENU_WRAPMODE, GetMsg(mName), 0, L"menu", NULL, NULL, menuElements, 1) == 0)
        configure();
      return;
    };
    for (int i = sizeof(iMenuItems) / sizeof(iMenuItems[0]) - 1; i >= 0; i--){
      if (iMenuItems[i] == -1)
        menuElements[i].Separator = 1;
      else
        menuElements[i].Text = GetMsg(iMenuItems[i]);
    };
    menuElements[0].Selected = 1;

    switch(info->Menu(info->ModuleNumber, -1, -1, 0, FMENU_WRAPMODE, GetMsg(mName), 0, L"menu", NULL, NULL,
                      menuElements, sizeof(iMenuItems) / sizeof(iMenuItems[0]))){
      case 0:
        chooseType();
        break;
      case 1:
        getCurrentEditor()->matchPair();
        break;
      case 2:
        getCurrentEditor()->selectBlock();
        break;
      case 3:
        getCurrentEditor()->selectPair();
        break;
      case 4:
        getCurrentEditor()->listFunctions();
        break;
      case 5:
        getCurrentEditor()->listErrors();
        break;
      case 6:
        getCurrentEditor()->selectRegion();
        break;
      case 7:
        getCurrentEditor()->locateFunction();
        break;
      case 9:
        getCurrentEditor()->updateHighlighting();
        break;
      case 10:
        configure();
        break;
    };
  }catch(Exception &e){
    const wchar_t* exceptionMessage[5];
    exceptionMessage[0] = GetMsg(mName);
    exceptionMessage[1] = GetMsg(mCantLoad);
    exceptionMessage[3] = GetMsg(mDie);
    StringBuffer msg("openMenu: ");
    exceptionMessage[2] = (msg+e.getMessage()).getWChars();
    if (getErrorHandler())
      getErrorHandler()->error(*e.getMessage());
    info->Message(info->ModuleNumber, 0, L"exception", &exceptionMessage[0], 4, 1);
    disableColorer();
  };
}


void FarEditorSet::viewFile(const String &path){
  try{
    if (rDisabled) throw Exception(DString("Colorer is disabled"));
    String *newPath = null;
    if (path[0] == '\"') newPath = new DString(path, 1, path.length()-2);
    else newPath = new DString(path);

    // Creates store of text lines
    TextLinesStore textLinesStore;
    textLinesStore.loadFile(newPath, null, true);
    // HRC loading
    //BUG ?
    //HRCParser *hrcParser = parserFactory->getHRCParser();
    // Base editor to make primary parse
    BaseEditor baseEditor(parserFactory, &textLinesStore);
    baseEditor.setRegionMapper(regionMapper);
    baseEditor.chooseFileType(newPath);
    // initial event
    baseEditor.lineCountEvent(textLinesStore.getLineCount());

    // computing background color
    int background = 0x1F;
    const StyledRegion *rd = StyledRegion::cast(regionMapper->getRegionDefine(DString("def:Text")));
    if (rd != null && rd->bfore && rd->bback) background = rd->fore + (rd->back<<4);
    // File viewing in console window
    TextConsoleViewer viewer(&baseEditor, &textLinesStore, background, -1);
    viewer.view();
    delete newPath;
  }catch(Exception &e){
    const wchar_t* exceptionMessage[5];
    exceptionMessage[0] = GetMsg(mName);
    exceptionMessage[1] = GetMsg(mCantOpenFile);
    exceptionMessage[3] = GetMsg(mDie);
    exceptionMessage[2] = e.getMessage()->getWChars();
    info->Message(info->ModuleNumber, 0, L"exception", &exceptionMessage[0], 4, 1);
  };
}


void FarEditorSet::chooseType()
{
  FarEditor *fe = getCurrentEditor();

  int num = 0;
  int i = 0;
  int idx = 0;
  const String *group = null;
  FileType *type = null;

  for(idx = 0;; idx++, num++){
    type = hrcParser->enumerateFileTypes(idx);
    if (type == null) break;
    if (group != null && !group->equals(type->getGroup())) num++;
    group = type->getGroup();
  };

  char MapThis[] = "1234567890QWERTYUIOPASDFGHJKLZXCVBNMxx";
  FarMenuItem *menuels = new FarMenuItem[num+2];
  memset(menuels, 0, sizeof(FarMenuItem)*(num+2));

  group = null;
  for(idx = 0, i = 0;; idx++, i++){
    type = hrcParser->enumerateFileTypes(idx);
    if (type == null) break;


    if (group != null && !group->equals(type->getGroup())){
      menuels[i].Separator = 1;
      i++;
    };
    group = type->getGroup();

    const wchar_t *groupChars = null;
    if (group != null) groupChars = group->getWChars();
    else groupChars = L"<no group>";
    menuels[i].Text = new wchar_t[255];
	_snwprintf((wchar_t*)menuels[i].Text, 255, L"%c. %s: %s", idx < 37?MapThis[idx]:'x', groupChars, type->getDescription()->getWChars());
    if(type == fe->getFileType()) menuels[i].Selected = 1;
  };

  wchar_t bottom[20];
  _snwprintf(bottom, 20, GetMsg(mTotalTypes), idx);

  i = info->Menu(info->ModuleNumber, -1, -1, 0, FMENU_WRAPMODE | FMENU_AUTOHIGHLIGHT,
                     GetMsg(mSelectSyntax), bottom, L"contents", null, null, menuels, i);
  for(int idx = 0; idx < num+2; idx++)
    if (menuels[idx].Text) delete[] menuels[idx].Text;
  delete[] menuels;

  if (i == -1) return;
  i++;
  type = null;
  group = null;
  for(int ti = 0; i; i--, ti++){
    type = hrcParser->enumerateFileTypes(ti);
    if (group != null && !group->equals(type->getGroup())) i--;
    group = type->getGroup();
    if (!type) break;
  };
  if (type != null) fe->setFileType(type);
}


void FarEditorSet::configure()
{
  try{
    enum{
      IDX_BOX,
      IDX_DISABLED,
      IDX_CROSS,
      IDX_PAIRS,
      IDX_SYNTAX,
      IDX_OLDOUTLINE,
      IDX_CATALOG,
      IDX_CATALOG_EDIT,
      IDX_HRD,
      IDX_HRD_SELECT,
      IDX_TIME,
      IDX_TIME_EDIT,
      IDX_RELOAD,//8
      IDX_RELOAD_ALL,
      IDX_OK,
      IDX_CANCEL,
    };

	FarDialogItem fdi[] = {
      { DI_DOUBLEBOX,3,1,49,19,0,0,0,0,L""},                                 //IDX_BOX,        
                                                                             
      { DI_CHECKBOX,6,3,0,0,TRUE,0,0,0,L""},                                 //IDX_DISABLED,    
                                                                               
      { DI_CHECKBOX,6,5,0,0,FALSE,0,0,0,L""},                                //IDX_CROSS,         
      { DI_CHECKBOX,19,5,0,0,FALSE,0,0,0,L""},                               //IDX_PAIRS,       
      { DI_CHECKBOX,32,5,0,0,FALSE,0,0,0,L""},                               //IDX_SYNTAX,      
                                                                               
      { DI_CHECKBOX,6,7,0,0,FALSE,0,0,0,L""},                                //IDX_OLDOUTLINE,    
                                                                               
      { DI_TEXT,6,9,0,0,FALSE,0,0,0,L""},                                    //IDX_CATALOG,        
      { DI_EDIT,10,10,40,5,FALSE,(DWORD_PTR)L"catalog",DIF_HISTORY,0,L""},       //IDX_CATALOG_EDIT    
      { DI_TEXT,6,11,0,0,FALSE,0,0,0,L""},    // hrd                         //IDX_HRD,            
      { DI_BUTTON,12,12,0,0,FALSE,0,0,0,L""}, // hrd button                  //IDX_HRD_SELECT,     
      { DI_TEXT,6,13,0,0,FALSE,0,0,0,L""},                                   //IDX_TIME,           
      { DI_EDIT,10,14,25,5,FALSE,(DWORD_PTR)L"clr_time",DIF_HISTORY,0,L""},      //IDX_TIME_EDIT,      
      { DI_BUTTON,6,16,0,0,FALSE,0,0,0,L""},    // reload                    //IDX_RELOAD,//8      
      { DI_BUTTON,26,16,0,0,FALSE,0,0,0,L""},   // all                       //IDX_RELOAD_ALL,     
      { DI_BUTTON,30,18,0,0,FALSE,0,0,TRUE,L""}, // ok                       //IDX_OK,             
      { DI_BUTTON,38,18,0,0,FALSE,0,0,0,L""},   // cancel                    //IDX_CANCEL,       
    }; // type, x1, y1, x2, y2, focus, sel, fl, def, data

  fdi[IDX_BOX].PtrData = GetMsg(mSetup);
  fdi[IDX_DISABLED].PtrData = GetMsg(mTurnOff);
  fdi[IDX_DISABLED].Selected = !rGetValue(hPluginRegistry, REG_DISABLED);

  fdi[IDX_CROSS].PtrData = GetMsg(mCross);
  fdi[IDX_CROSS].Selected = !rGetValue(hPluginRegistry, REG_CROSSDONTDRAW);

  fdi[IDX_PAIRS].PtrData = GetMsg(mPairs);
  fdi[IDX_PAIRS].Selected = !rGetValue(hPluginRegistry, REG_PAIRSDONTDRAW);

  fdi[IDX_SYNTAX].PtrData = GetMsg(mSyntax);
  fdi[IDX_SYNTAX].Selected = !rGetValue(hPluginRegistry, REG_SYNTAXDONTDRAW);

  fdi[IDX_OLDOUTLINE].PtrData = GetMsg(mOldOutline);
  fdi[IDX_OLDOUTLINE].Selected = rGetValue(hPluginRegistry, REG_OLDOUTLINE);

  fdi[IDX_CATALOG].PtrData = GetMsg(mCatalogFile);


  wchar_t tempCatalogEdit[255] = {0};
  rGetValue(hPluginRegistry, REG_CATALOG, tempCatalogEdit, 255);
  fdi[IDX_CATALOG_EDIT].PtrData = trim(tempCatalogEdit);

  fdi[IDX_HRD].PtrData = GetMsg(mHRDName);

  wchar_t hrdName[32]  = {0};
  int len=rGetValue(hPluginRegistry, REG_HRD_NAME, hrdName, 32);
  DString shrdName = DString("default");
  if (len > 1) {
    wchar_t* temp=trim(hrdName);
    if (wcslen(temp)>1)
      shrdName = DString(temp); 
  }

  const String *descr = null;
  if (parserFactory != null){
    descr = parserFactory->getHRDescription(DString("console"), shrdName);
  }
  if (descr == null){
    descr = &shrdName;
  }
  fdi[IDX_HRD_SELECT].PtrData = descr->getWChars();

  fdi[IDX_TIME].PtrData = GetMsg(mMaxTime);

  wchar_t tempMaxTime[255] = {0};
  rGetValue(hPluginRegistry, REG_MAXTIME, tempMaxTime, 512);
  fdi[IDX_TIME_EDIT].PtrData = trim(tempMaxTime);

  fdi[IDX_RELOAD].PtrData = GetMsg(mReload);
  fdi[IDX_RELOAD_ALL].PtrData = GetMsg(mReloadAll);
  fdi[IDX_OK].PtrData = GetMsg(mOk);
  fdi[IDX_CANCEL].PtrData = GetMsg(mCancel);

  /*
   * Dialog activation
   */
  HANDLE hDlg = info->DialogInit(info->ModuleNumber, -1, -1, 53, 21, L"config", fdi, ARRAY_SIZE(fdi), 0, 0, info->DefDlgProc, 0);
  int i = info->DialogRun(hDlg);

  if (i == IDX_CANCEL || i == -1){
    info->DialogFree(hDlg);
  return;
  }

  fdi[IDX_DISABLED].Selected = (int)info->SendDlgMessage(hDlg, DM_GETCHECK, IDX_DISABLED, 0);
  fdi[IDX_CROSS].Selected = (int)info->SendDlgMessage(hDlg, DM_GETCHECK, IDX_CROSS, 0);
  fdi[IDX_PAIRS].Selected = (int)info->SendDlgMessage(hDlg, DM_GETCHECK, IDX_PAIRS, 0);
  fdi[IDX_SYNTAX].Selected = (int)info->SendDlgMessage(hDlg, DM_GETCHECK, IDX_SYNTAX, 0);
  fdi[IDX_OLDOUTLINE].Selected = (int)info->SendDlgMessage(hDlg, DM_GETCHECK, IDX_OLDOUTLINE, 0);

  fdi[IDX_CATALOG_EDIT].PtrData = (const wchar_t*)trim((wchar_t*)info->SendDlgMessage(hDlg,DM_GETCONSTTEXTPTR,IDX_CATALOG_EDIT,0));
  fdi[IDX_TIME_EDIT].PtrData = (const wchar_t*)trim((wchar_t*)info->SendDlgMessage(hDlg,DM_GETCONSTTEXTPTR,IDX_TIME_EDIT,0));

  if (i == IDX_OK)
  {
  wchar_t oName[512] = {0};
    rGetValue(hPluginRegistry, REG_CATALOG, oName, 512);
    if (_wcsicmp(oName, fdi[IDX_CATALOG_EDIT].PtrData)) i = IDX_RELOAD;

  wchar_t oTime[32] = {0};
    rGetValue(hPluginRegistry, REG_MAXTIME, oTime, 32);
    if (_wcsicmp(oTime, fdi[IDX_TIME_EDIT].PtrData)) i = IDX_RELOAD;

    if (rDisabled != !fdi[IDX_DISABLED].Selected){
      disableColorer();
      i = IDX_RELOAD;
    }

  }

  rSetValue(hPluginRegistry, REG_CROSSDONTDRAW, !fdi[IDX_CROSS].Selected);
  rSetValue(hPluginRegistry, REG_PAIRSDONTDRAW, !fdi[IDX_PAIRS].Selected);
  rSetValue(hPluginRegistry, REG_SYNTAXDONTDRAW, !fdi[IDX_SYNTAX].Selected);
  rSetValue(hPluginRegistry, REG_OLDOUTLINE, fdi[IDX_OLDOUTLINE].Selected);

  rSetValue(hPluginRegistry, REG_DISABLED, !fdi[IDX_DISABLED].Selected);

  rSetValue(hPluginRegistry, REG_CATALOG, REG_SZ, fdi[IDX_CATALOG_EDIT].PtrData, (DWORD)( 2 * (wcslen(fdi[IDX_CATALOG_EDIT].PtrData)+1)));
  rSetValue(hPluginRegistry, REG_MAXTIME, REG_SZ, fdi[IDX_TIME_EDIT].PtrData, (DWORD) (2 * (wcslen(fdi[IDX_TIME_EDIT].PtrData)+1)));

  readRegistry();

  if (i == IDX_HRD_SELECT){
    const String *newname = chooseHRDName(&shrdName);
    rSetValue(hPluginRegistry, REG_HRD_NAME, REG_SZ, newname->getWChars(), 2 * (newname->length()+1));
    configure();
    i = IDX_RELOAD;
  };
  if (i == IDX_RELOAD){
    reloadBase();
  }
  if (i == IDX_RELOAD_ALL){
    reloadBase();
    if (rDisabled){
      info->DialogFree(hDlg);
      return;
    }
    const wchar_t *marr[2] = { GetMsg(mName), GetMsg(mReloading) };
    for(int idx = 0;; idx++){
      FileType *type = hrcParser->enumerateFileTypes(idx);
      if (type == null) break;
      StringBuffer tname;
      if (type->getGroup() != null) {
          tname.append(type->getGroup());
          tname.append(DString(": "));
      }
      tname.append(type->getDescription());
      marr[1] = tname.getWChars();
      HANDLE scr = info->SaveScreen(0, 0, -1, -1);
      info->Message(info->ModuleNumber, 0, null, &marr[0], 2, 0);
      type->getBaseScheme();
      info->RestoreScreen(scr);
    };
  };

  }catch(Exception &e){
    const wchar_t* exceptionMessage[5];
    exceptionMessage[0] = GetMsg(mName);
    exceptionMessage[1] = GetMsg(mCantLoad);
    exceptionMessage[2] = 0;
    exceptionMessage[3] = GetMsg(mDie);
    StringBuffer msg("configure: ");
    exceptionMessage[2] = (msg+e.getMessage()).getWChars();
    if (getErrorHandler() != null)
      getErrorHandler()->error(*e.getMessage());
    info->Message(info->ModuleNumber, 0, L"exception", &exceptionMessage[0], 4, 1);
    disableColorer();
  };

}

const String *FarEditorSet::chooseHRDName(const String *current){
  int count = 0;
  if (parserFactory == null) return current;
  while(parserFactory->enumerateHRDInstances(DString("console"), count) != null) count++;

  FarMenuItem *menuElements = new FarMenuItem[count];
  memset(menuElements, 0, sizeof(FarMenuItem)*count);

  for (int i = 0; i < count; i++){
    const String *name = parserFactory->enumerateHRDInstances(DString("console"), i);
    const String *descr = parserFactory->getHRDescription(DString("console"), *name);
    if (descr == null) descr = name;
    menuElements[i].Text = descr->getWChars();
    if (current->equals(name)) menuElements[i].Selected = 1;
  };
  int result = info->Menu(info->ModuleNumber, -1, -1, 0, FMENU_WRAPMODE|FMENU_AUTOHIGHLIGHT,
                          GetMsg(mSelectHRD), 0, L"hrd",
                          NULL, NULL, menuElements, count);
  delete[] menuElements;
  if (result == -1) return current;
  return parserFactory->enumerateHRDInstances(DString("console"), result);
}




int FarEditorSet::editorInput(const INPUT_RECORD *ir)
{
  if (rDisabled){
    return 0;
  }
  return getCurrentEditor()->editorInput(ir);
}

int FarEditorSet::editorEvent(int Event, void *Param)
{
  if (rDisabled){
    return 0;
  }
  try{
    FarEditor *editor = null;
    if (Event == EE_CLOSE){
      editor = farEditorInstances.get(&SString(*((int*)Param)));
      farEditorInstances.remove(&SString(*((int*)Param)));
      delete editor;
      return 0;
    };
    if (Event != EE_REDRAW) return 0;
    editor = getCurrentEditor();
    return editor->editorEvent(Event, Param);
  }catch(Exception &e){
    const wchar_t* exceptionMessage[5];
    exceptionMessage[0] = GetMsg(mName);
    exceptionMessage[1] = GetMsg(mCantLoad);
    exceptionMessage[2] = 0;
    exceptionMessage[3] = GetMsg(mDie);
    StringBuffer msg("editorEvent: ");
    exceptionMessage[2] = (msg+e.getMessage()).getWChars();
    if (getErrorHandler())
      getErrorHandler()->error(*e.getMessage());
    info->Message(info->ModuleNumber, 0, L"exception", &exceptionMessage[0], 4, 1);
    disableColorer();
  };
  return 0;
}



void FarEditorSet::reloadBase()
{
  const wchar_t *marr[2] = { GetMsg(mName), GetMsg(mReloading) };
  const wchar_t *errload[5] = { GetMsg(mName), GetMsg(mCantLoad), 0, GetMsg(mFatal), GetMsg(mDie) };

  dropAllEditors();
  readRegistry();

  delete regionMapper;
  delete parserFactory;
  parserFactory = null;
  regionMapper = null;

  if (rDisabled) return;

  HANDLE scr = info->SaveScreen(0, 0, -1, -1);
  info->Message(info->ModuleNumber, 0, null, &marr[0], 2, 0);

  int len;
  wchar_t regstring[512];
  len = rGetValue(hPluginRegistry, REG_HRD_NAME, regstring, 512);
  SString *hrdName = null;
  if (len > 1) {
    wchar_t* temp=trim(regstring);
    if (wcslen(temp)>1)
      hrdName = new SString(DString(temp));
  }

  regstring[0]='\0';
  len = rGetValue(hPluginRegistry, REG_CATALOG, regstring, 512);
  SString *catalogPath = null;
  if (len > 1) {
      if (wcslen(trim(regstring))>1)
        catalogPath = new SString(DString(regstring));
  }

  try{
    parserFactory = new ParserFactory(catalogPath);
    hrcParser = parserFactory->getHRCParser();
    try{
      regionMapper = parserFactory->createStyledMapper(&DString("console"), hrdName);
    }catch(ParserFactoryException &e){
      if (getErrorHandler() != null) getErrorHandler()->error(*e.getMessage());
      regionMapper = parserFactory->createStyledMapper(&DString("console"), null);
    };
  }catch(Exception &e){
	  errload[2] = e.getMessage()->getWChars();
    if (getErrorHandler() != null) getErrorHandler()->error(*e.getMessage());
    info->Message(info->ModuleNumber, 0, null, &errload[0], 5, 1);
    disableColorer();
  };

  delete catalogPath;
  delete hrdName;

  info->RestoreScreen(scr);
}


ErrorHandler *FarEditorSet::getErrorHandler(){
  if (parserFactory == null) return null;
  return parserFactory->getErrorHandler();
}


FarEditor *FarEditorSet::getCurrentEditor(){
  EditorInfo ei;
  info->EditorControl(ECTL_GETINFO, &ei);
  FarEditor *editor = farEditorInstances.get(&SString(ei.EditorID));
  if (editor == null){
    editor = new FarEditor(info, parserFactory);
    farEditorInstances.put(&SString(ei.EditorID), editor);

    
    LPWSTR FileName=NULL;
    size_t FileNameSize=info->EditorControl(ECTL_GETFILENAME,NULL);
    if(FileNameSize)
    {
      FileName=new wchar_t[FileNameSize];
      if(FileName)
      {
        info->EditorControl(ECTL_GETFILENAME,FileName);
      }
    }
    
    DString fnpath(FileName);

    int slash_idx = fnpath.lastIndexOf('\\');
    if (slash_idx == -1) slash_idx = fnpath.lastIndexOf('/');
    DString fn = DString(fnpath, slash_idx+1);
    editor->chooseFileType(&fn);
    editor->setRegionMapper(regionMapper);
    editor->setDrawCross(drawCross);
    editor->setDrawPairs(drawPairs);
    editor->setDrawSyntax(drawSyntax);
    editor->setOutlineStyle(oldOutline);
    editor->setMaxTime(rMaxTime);
    delete[] FileName;
  };
  return editor;
}


const wchar_t *FarEditorSet::GetMsg(int msg){
  return(info->GetMsg(info->ModuleNumber, msg));
}


void FarEditorSet::disableColorer(){
  rDisabled = true;
  for(FarEditor *fe = farEditorInstances.enumerate(); fe != null; fe = farEditorInstances.next()){
    fe->cleanEditor();
    delete fe;
  };
  farEditorInstances.clear();
  
  rSetValue(hPluginRegistry, REG_DISABLED, rDisabled);
}


void FarEditorSet::readRegistry()
{
  wchar_t mt[64];

  rDisabled = rGetValue(hPluginRegistry, REG_DISABLED) != 0;

  drawCross = !rGetValue(hPluginRegistry, REG_CROSSDONTDRAW);
  drawPairs = !rGetValue(hPluginRegistry, REG_PAIRSDONTDRAW);
  drawSyntax = !rGetValue(hPluginRegistry, REG_SYNTAXDONTDRAW);
  oldOutline = rGetValue(hPluginRegistry, REG_OLDOUTLINE) == TRUE;

  rMaxTime = 3000;
  int len = rGetValue(hPluginRegistry, REG_MAXTIME, mt, 64);
  if (len>0){
    wchar_t* temp=trim(mt);
    if (wcslen(temp)>0)
      rMaxTime = _wtoi(mt);
  }

  for(FarEditor *fe = farEditorInstances.enumerate(); fe != null; fe = farEditorInstances.next()){
    fe->setDrawCross(drawCross);
    fe->setDrawPairs(drawPairs);
    fe->setDrawSyntax(drawSyntax);
    fe->setMaxTime(rMaxTime);
    fe->setOutlineStyle(oldOutline);
  }
}

void FarEditorSet::dropAllEditors(){
  for(FarEditor *fe = farEditorInstances.enumerate(); fe != null; fe = farEditorInstances.next()){
    delete fe;
  };
  farEditorInstances.clear();
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