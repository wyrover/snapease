/*

  Todo: 
    load/save set
    browse sets? (tabs for sets along top ?)
    disk-based thumbnail cache? sqlite?
    
    keys: l/r to navigate items, esc to exit single item viewing, generic shortcuts too

    export set
      disk folder | onlineetc
      format (png,jpeg) (button to estimate size?)
      limit width, height
      other stuff

  clone button on item

  tab to nav between renaming items (autoscroll or switch g_fullmode_item, if necessary)

    slideshows etc?
    confirm to remove item from set?


*/

#define VERSTRING "0.01"

#include "main.h"

#include "../WDL/ptrlist.h"
#include "../WDL/lice/lice.h"
#include "../WDL/dirscan.h"

#include "../jmde/coolsb/coolscroll.h"

#include "resource.h"

WDL_PtrList<ImageRecord> g_images;
WDL_Mutex g_images_mutex;

HWND g_hwnd;
WDL_VWnd_Painter g_hwnd_painter;
WDL_VWnd g_vwnd; // owns all children windows

void ClearImageList()
{
  int x;
  WDL_PtrList<ImageRecord> r = g_images;
  g_images_mutex.Enter();
  g_images.Empty(); // does not free -- g_vwnd owns all images!
  g_images_mutex.Leave();

  for(x=0;x<r.GetSize();x++)
    g_vwnd.RemoveChild(r.Get(x),true);
}


ImageRecord *g_fullmode_item;

int g_vwnd_scrollpos;

int OrganizeWindow(HWND hwndDlg)
{
  RECT r;
  GetClientRect(hwndDlg,&r);

  bool hasScrollBar=false;
  // decide whether we will need a scrollbar?

  int border_size_x = 8, border_size_y=8;

  int preview_w = (r.right-border_size_x);

  if (preview_w > 256) preview_w = 256;
  if (preview_w < 32) preview_w = 32;

  int preview_h = (preview_w*3) / 4;

  int xpos=border_size_x/2, ypos=border_size_y/2;
  int x;
  bool hadFullItem=false;
  for (x = 0; x < g_images.GetSize(); x ++)
  {
    ImageRecord *rec = g_images.Get(x);
    if (rec)
    {
      if (g_fullmode_item&&g_fullmode_item!=rec)
      {
        rec->SetVisible(false);
      }
      else
      {
        rec->SetVisible(true);
        if (g_fullmode_item)
        {
          hadFullItem=true;
          RECT rr={12,12,r.right-12,r.bottom-12};
          rec->SetPosition(&rr);
        }
        else
        {
          if (xpos + preview_w > r.right)
          {
            xpos=border_size_x/2;
            ypos += preview_h + border_size_y;
          }   

          RECT rr = {xpos,ypos - g_vwnd_scrollpos,xpos+preview_w,ypos+preview_h - g_vwnd_scrollpos};
          rec->SetPosition(&rr);
        }
      }
    }
    xpos += preview_w + border_size_x;
  }
  if (!hadFullItem&&g_fullmode_item)
  {
    g_images_mutex.Enter();
    g_fullmode_item=0;
    g_images_mutex.Leave();
    return OrganizeWindow(hwndDlg);
  }

  if (hadFullItem) return r.bottom - 4;

  int maxPos = ypos + preview_h + border_size_y/2;
  if (maxPos < 0) maxPos=0;

  return maxPos;
}

void UpdateMainWindowWithSizeChanged()
{
  EditImageLabelEnd();

  HWND hwndDlg=g_hwnd;
  RECT r;
  GetClientRect(hwndDlg,&r);
  if (g_vwnd_scrollpos<0)g_vwnd_scrollpos=0;
  int wh = OrganizeWindow(hwndDlg);

  if (!g_fullmode_item)
  {
    if (g_vwnd_scrollpos > wh - r.bottom && wh > r.bottom) 
    {
      g_vwnd_scrollpos=wh - r.bottom;
      OrganizeWindow(hwndDlg);
    }

    SCROLLINFO si={sizeof(si),SIF_PAGE|SIF_POS|SIF_RANGE,0,wh, r.bottom, g_vwnd_scrollpos,};
    CoolSB_SetScrollInfo(hwndDlg,SB_VERT,&si,TRUE);
  }
  else
  {
    SCROLLINFO si={sizeof(si),SIF_PAGE|SIF_POS|SIF_RANGE,0,0,0, 0,};
    CoolSB_SetScrollInfo(hwndDlg,SB_VERT,&si,TRUE);
  }
  InvalidateRect(hwndDlg,NULL,FALSE);
}


void OpenFullItemView(ImageRecord *w)
{
  RemoveFullItemView(false);

  g_images_mutex.Enter();
  w->m_want_fullimage=true;
  LICE_IBitmap *old = w->m_fullimage;
  w->m_fullimage = 0;
  g_fullmode_item=w;
  g_images_mutex.Leave();
  delete old;
  delete w->m_fullimage_rendercached;
  w->m_fullimage_rendercached=0;
  UpdateMainWindowWithSizeChanged();

  // tell thread to background-load full quality imagea for this
}

bool RemoveFullItemView(bool refresh)
{
  if (g_fullmode_item)
  {
    g_images_mutex.Enter();
    ImageRecord *r = g_fullmode_item;
    if (g_images.Find(g_fullmode_item)>=0)
    {
      LICE_IBitmap *old = g_fullmode_item->m_fullimage;
      g_fullmode_item->m_want_fullimage = false;
      g_fullmode_item->m_fullimage = 0;
      delete old;
      delete g_fullmode_item->m_fullimage_rendercached;
      g_fullmode_item->m_fullimage_rendercached=0;

    }
    g_fullmode_item=0;
    g_images_mutex.Leave();
    if (refresh) 
    {
      UpdateMainWindowWithSizeChanged();
      if (g_images.Find(r)>=0) EnsureImageRecVisible(r);
    }
    return true;
  }
  return false;
}


void SetMainScrollPosition(float pos, int relative=0) // relative=1 for lines, 2 for pages
{
  if (relative)
  {
    SCROLLINFO si = { sizeof(SCROLLINFO), SIF_POS|SIF_PAGE|SIF_RANGE|SIF_TRACKPOS, 0 };
    CoolSB_GetScrollInfo(g_hwnd, SB_VERT, &si);

    if (relative == 2) pos *= (int)si.nPage;
    pos += si.nPos;

    if (pos > si.nMax-(int)si.nPage) pos = si.nMax-(int)si.nPage;
    if (pos < 0) pos = 0;
  }
  if (pos != g_vwnd_scrollpos)
  {
    g_vwnd_scrollpos=(int)(pos+0.5);

    UpdateMainWindowWithSizeChanged();
  }
}

void EnsureImageRecVisible(ImageRecord *rec)
{
  if (g_fullmode_item) 
  {
    if (rec!=g_fullmode_item)
      OpenFullItemView(rec);
  }
  else
  {
    RECT cr,r;
    GetClientRect(g_hwnd,&r);
    rec->GetPosition(&cr);

    int pos = g_vwnd_scrollpos;
    if (cr.bottom > r.bottom) pos += cr.bottom-r.bottom;
    else if (cr.top < r.top) pos -= r.top-cr.top;

    if (pos<0)pos=0;
    SetMainScrollPosition(pos);
    
    // do any necessary scrolling
  }
}


void AddImage(const char *fn)
{
  ImageRecord *w = new ImageRecord(fn);
  g_vwnd.AddChild(w);
  g_images_mutex.Enter();
  g_images.Add(w);
  g_images_mutex.Leave();
 
  SetImageListIsDirty(true);
  if (g_fullmode_item) EnsureImageRecVisible(w);
}



WDL_DLGRET MainWindowProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
#ifdef _WIN32
  if (Scroll_Message && uMsg == Scroll_Message)
  {
    uMsg=WM_MOUSEWHEEL;
    wParam<<=16; 
  }
#endif
  switch (uMsg)
  {
    case WM_INITDIALOG:

      g_vwnd_scrollpos=0;
#ifndef _WIN32 // need to fix some coolsb top level window bugs
      InitializeCoolSB(hwndDlg);
#endif
      g_hwnd = hwndDlg;
      g_vwnd.SetRealParent(hwndDlg);
      
      UpdateMainWindowWithSizeChanged();

      {
        RECT r={config_readint("wndx",15),config_readint("wndy",15),};

        r.right = r.left + config_readint("wndw",0);
        r.bottom = r.top + config_readint("wndh",0);
       
        if (r.bottom != r.top && r.right != r.left)
          SetWindowPos(hwndDlg,NULL,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);         
      }

#ifdef _WIN32
      if (config_readint("wndmax",0))
        ShowWindow(hwndDlg,SW_SHOWMAXIMIZED);
      else
#endif
        ShowWindow(hwndDlg,SW_SHOW);

      SetTimer(hwndDlg,1,100,NULL);
    return 0;
    case WM_DESTROY:

      {
        RECT r;
        GetWindowRect(hwndDlg,&r);

#ifdef _WIN32
        WINDOWPLACEMENT wp={sizeof(wp),};
        if (GetWindowPlacement(hwndDlg,&wp)) r=wp.rcNormalPosition;
        config_writeint("wndmax",wp.showCmd == SW_SHOWMAXIMIZED);
#endif
        config_writeint("wndx",r.left);
        config_writeint("wndy",r.top);
        config_writeint("wndw",r.right-r.left);
        config_writeint("wndh",abs(r.bottom-r.top));

      }

      g_images_mutex.Enter();
      g_images.Empty(); // does not free -- g_vwnd owns all images!
      g_images_mutex.Leave();

      g_vwnd.RemoveAllChildren(true);

#ifndef _WIN32
      UninitializeCoolSB(hwndDlg);
#endif
      PostQuitMessage(0);
    return 0;
    case WM_TIMER:
      if (wParam==1)
      {
        EditImageRunTimer();
        if (g_DecodeDidSomething)
        {
          g_DecodeDidSomething=false;
          InvalidateRect(hwndDlg,NULL,FALSE);
        }
      }
    return 0;
    case WM_CLOSE:
      SendMessage(hwndDlg,WM_COMMAND,ID_QUIT,0);
    break;
    case WM_INITMENUPOPUP:
      {
        HMENU hm=(HMENU)wParam;
        EnableMenuItem(hm,ID_SAVE,MF_BYCOMMAND|(g_imagelist_fn.Get()[0]? MF_ENABLED:MF_GRAYED));
        EnableMenuItem(hm,ID_EXPORT,MF_BYCOMMAND|(g_images.GetSize()? MF_ENABLED:MF_GRAYED));
      }
    break;
#ifdef _WIN32
    case WM_ENDSESSION:
      DestroyWindow(hwndDlg);
    break;
    case WM_QUERYENDSESSION:

      if (SavePromptForClose("Save image list before shutting down?"))
        SetWindowLongPtr(hwndDlg,DWLP_MSGRESULT,TRUE);
      else 
        SetWindowLongPtr(hwndDlg,DWLP_MSGRESULT,0);

    return TRUE;
#endif
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case ID_ABOUT:
          MessageBox(hwndDlg,"SnapEase v" VERSTRING "\r\n"
            "Copyright (C) 2009, Cockos Incorporated",
            "About SnapEase",
              MB_OK);
        break;
        case ID_LOAD_ADD:
        case ID_LOAD:
          if (LOWORD(wParam) == ID_LOAD_ADD || SavePromptForClose("Save image list before loading?"))
          {
            LoadImageList(LOWORD(wParam) == ID_LOAD_ADD);
          }
        break;
          
        case ID_SAVE:
          SaveImageList(false);
        break;
        case ID_SAVEAS:
          SaveImageList(true);
        break;
        case ID_QUIT:
          if (SavePromptForClose("Save image list before exit?"))
          {
            DestroyWindow(hwndDlg);
          }
        break;
        case ID_NEWLIST:
          if (SavePromptForClose("Save image list before creating new list?"))
          {
            ClearImageList();
            g_imagelist_fn.Set("");
            g_imagelist_fn_dirty=false;

            UpdateMainWindowWithSizeChanged();

            UpdateCaption();
          }
        break;
        case ID_IMPORT:

          {
            static char cwd[4096];

            if (!cwd[0]) config_readstr("cwd",cwd,sizeof(cwd));
            if (!cwd[0]) GetCurrentDirectory(sizeof(cwd),cwd);

            char *extlist=LICE_GetImageExtensionList();

          #ifdef _WIN32
            int temp_size=256*1024;
            char *temp=(char *)calloc(temp_size,1);
            OPENFILENAME l={sizeof(l),};
            l.hwndOwner = hwndDlg;
            l.lpstrFilter = (char*)extlist;
            l.lpstrFile = temp;
            l.nMaxFile = temp_size-1;
            l.lpstrTitle = "Add images:";
            l.lpstrDefExt = "jpg";
            l.lpstrInitialDir = cwd;
            l.Flags = OFN_HIDEREADONLY|OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_ALLOWMULTISELECT|OFN_NOCHANGEDIR;
            if (GetOpenFileName(&l))          
          #else
            char *temp=BrowseForFiles("Add images:", dir, fn, false, extlist);
            if (temp)
          #endif     
            {
              WDL_String path;
              path.Set(temp); 
              if (!temp[strlen(temp)+1]) // if single file, remove filename portion
              {
                char *p=path.Get();
                while (*p) p++;
                while (p > path.Get() && *p != '\\' && *p != '/') p--;
                *p=0;
              }            
              config_writestr("cwd",path.Get());
              lstrcpyn(cwd,path.Get(),sizeof(cwd));

              if (!temp[strlen(temp)+1])
              {
                AddImage(temp);
                UpdateMainWindowWithSizeChanged();
              }
              else
              {
                char *p = temp+strlen(temp)+1;

                if (temp[0] && p[-2]==PREF_DIRCH) p[-2]=0;

                WDL_PtrList<ImageRecord> newimages;
                while (*p)
                {
                  path.Set(temp);
                  path.Append(PREF_DIRSTR);
                  path.Append(p);
                  p+=strlen(p)+1;
                  newimages.Add(new ImageRecord(path.Get()));
                }
                if (newimages.GetSize()>1) qsort(newimages.GetList(),newimages.GetSize(),sizeof(ImageRecord *),ImageRecord::sortByFN);

                g_images_mutex.Enter();
                int x;
                for(x=0;x<newimages.GetSize();x++)
                {
                  ImageRecord *w = newimages.Get(x);
                  g_vwnd.AddChild(w);
                  g_images.Add(w);
                }
                g_images_mutex.Leave();
  
                if (g_fullmode_item && newimages.Get(0)) EnsureImageRecVisible(newimages.Get(0));
                else if (newimages.GetSize()) UpdateMainWindowWithSizeChanged();
              }
              free(temp);

            }
        
          }

        break;
      }
    return 0;
    case WM_SIZE:

      if (wParam != SIZE_MINIMIZED)
      {
        UpdateMainWindowWithSizeChanged();
      }

    return 0;
    case WM_LBUTTONDBLCLK:
      if (g_vwnd.OnMouseDown(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)))
      {
        g_vwnd.OnMouseUp(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
      }
      else
      {
        if (!RemoveFullItemView())
        {
          WDL_VWnd *w = g_vwnd.VirtWndFromPoint(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
          if (w && g_images.Find((ImageRecord*)w)>=0)
          {
            OpenFullItemView((ImageRecord*)w);
          }
        }
      }
    return 0;
    case WM_SETCURSOR:
      {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwndDlg,&p);
        if (g_vwnd.UpdateCursor(p.x,p.y))
        {
          return TRUE;
        }
        else 
        {
          SetCursor(LoadCursor(NULL,MAKEINTRESOURCE(IDC_ARROW)));
          return TRUE;
        }
      }
    return 0;
    case WM_LBUTTONDOWN:
      EditImageLabelEnd();
      SetFocus(hwndDlg);
      if (g_vwnd.OnMouseDown(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)))
      {
        SetCapture(hwndDlg);
      }
    return 0;
    case WM_MOUSEMOVE:
      if (GetCapture()==hwndDlg)
      {
        g_vwnd.OnMouseMove(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
      }
    return 0;
    case WM_LBUTTONUP:
      if (GetCapture()==hwndDlg)
      {
        g_vwnd.OnMouseUp(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
        ReleaseCapture();
      }
    return 0;
    case WM_PAINT:
      {
        RECT r;
        GetClientRect(hwndDlg,&r);
        g_vwnd.SetPosition(&r);
        g_hwnd_painter.PaintBegin(hwndDlg,RGB(0,0,0));
        g_hwnd_painter.PaintVirtWnd(&g_vwnd);
        g_hwnd_painter.PaintEnd();
      }
    return 0;
    case WM_MOUSEWHEEL:
      if (!g_fullmode_item)
      {
        int l=(short)HIWORD(wParam);
        SetMainScrollPosition(-l/400.0,2);
      }
      else
      {
        // todo some fullmode thing?
      }
    return -1;
    case WM_VSCROLL:
      if (!g_fullmode_item)
      {
        SCROLLINFO si = { sizeof(SCROLLINFO), SIF_POS|SIF_PAGE|SIF_RANGE|SIF_TRACKPOS, 0 };
        CoolSB_GetScrollInfo(hwndDlg, SB_VERT, &si);
        switch (LOWORD(wParam))
        {
          case SB_THUMBTRACK:        
            si.nPos = si.nTrackPos;
          break;
          case SB_LINEUP:
            si.nPos -= 30;
          break;
          case SB_LINEDOWN:
            si.nPos += 30;
          break;
          case SB_PAGEUP:
            si.nPos -= (int)si.nPage;
          break;
          case SB_PAGEDOWN:
            si.nPos += (int)si.nPage;
          break;
          case SB_TOP:
            si.nPos = 0;
          break;
          case SB_BOTTOM:
            si.nPos = si.nMax-(int)si.nPage;
          break;
        }
        if (si.nPos > si.nMax-(int)si.nPage) si.nPos = si.nMax-(int)si.nPage;
        if (si.nPos < 0) si.nPos = 0;
        SetMainScrollPosition(si.nPos);
      }
    return 0;
    case WM_DROPFILES:

      {
        WDL_PtrList<ImageRecord> newimages;
        HDROP hDrop = (HDROP) wParam;
        int x;
        int n=DragQueryFile(hDrop,-1,NULL,0);
        int cnt=0;
        for (x = 0; x < n; x ++)
        {
          char buf[4096];
          buf[0]=0;
          DragQueryFile(hDrop,x,buf,sizeof(buf));
          if (buf[0])
          {
            if (!LICE_ImageIsSupported(buf))
            {
              WDL_String tmp;
              WDL_DirScan ds;
              if (!ds.First(buf))
              {
                WDL_PtrList<WDL_String> dirstack;
                for (;;)
                {
                  if (ds.GetCurrentFN()[0] != '.')
                  {
                    ds.GetCurrentFullFN(&tmp);
                    if (ds.GetCurrentIsDirectory())
                    {
                      WDL_String *s = new WDL_String;
                      s->Set(tmp.Get());
                      dirstack.Add(s);
                    }
                    else if (LICE_ImageIsSupported(tmp.Get()))
                    {
                      newimages.Add(new ImageRecord(tmp.Get()));
                    }
                  }

                  if (!ds.Next()) continue;

                  bool didNew = false;

                  while (dirstack.GetSize() && !didNew)
                  {
                    WDL_String *s = dirstack.Get(0);
                    dirstack.Delete(0);

                    didNew = !ds.First(s->Get());

                    delete s;
                  }
                  
                  if (!didNew) break;
                }
              
              }
            }
            else // image
            {
              newimages.Add(new ImageRecord(buf));
            }
          }
        }

        DragFinish(hDrop);

        if (newimages.GetSize()>1) qsort(newimages.GetList(),newimages.GetSize(),sizeof(ImageRecord *),ImageRecord::sortByFN);

        g_images_mutex.Enter();
        for(x=0;x<newimages.GetSize();x++)
        {
          ImageRecord *w = newimages.Get(x);
          g_vwnd.AddChild(w);
          g_images.Add(w);
        }
        g_images_mutex.Leave();

        if (g_fullmode_item && newimages.Get(0)) EnsureImageRecVisible(newimages.Get(0));
        else
        {
          if (newimages.GetSize())
            UpdateMainWindowWithSizeChanged();
        }
      }

    return 0;
  }

  return 0;
}


int MainProcessMessage(MSG *msg)
{
  int a = EditImageProcessMessage(msg);
  if (a) return a;;

  if (msg->hwnd == g_hwnd||IsChild(g_hwnd,msg->hwnd))
  {
    if (msg->message == WM_KEYDOWN||msg->message==WM_CHAR)
    {
      // todo: kbd table etc? :)
      if (msg->wParam == 'A' || (msg->wParam == VK_INSERT && msg->message == WM_KEYDOWN))
      {
        if (!(GetAsyncKeyState(VK_CONTROL)&0x8000) && 
            !(GetAsyncKeyState(VK_MENU)&0x8000) &&
            !(GetAsyncKeyState(VK_SHIFT)&0x8000))
        {
          SendMessage(g_hwnd,WM_COMMAND,ID_IMPORT,0);
          return 1;
        }
      }
      else if (msg->wParam == 'Q')
      {
        if ((GetAsyncKeyState(VK_CONTROL)&0x8000) && 
            !(GetAsyncKeyState(VK_MENU)&0x8000) &&
            !(GetAsyncKeyState(VK_SHIFT)&0x8000))
        {
          SendMessage(g_hwnd,WM_COMMAND,ID_QUIT,0);
          return 1;
        }
      }
      else if (msg->wParam == 'N')
      {
        if ((GetAsyncKeyState(VK_CONTROL)&0x8000) && 
            !(GetAsyncKeyState(VK_MENU)&0x8000) &&
            !(GetAsyncKeyState(VK_SHIFT)&0x8000))
        {
          SendMessage(g_hwnd,WM_COMMAND,ID_NEWLIST,0);
          return 1;
        }
      }

      if (g_fullmode_item)
      {
        if (msg->wParam == VK_ESCAPE)
        {
          RemoveFullItemView(true);
          return 1;
        }

        if (msg->wParam == VK_NEXT || msg->wParam == VK_PRIOR)
        {
          int a = g_images.Find(g_fullmode_item);
          if (a>=0)
          {
            a += (msg->wParam == VK_PRIOR) ? -1 : 1;
            if (g_images.Get(a)) EnsureImageRecVisible(g_images.Get(a));
          }
        }
      }
      else
      {
        if (msg->wParam == VK_NEXT || msg->wParam == VK_PRIOR)
        {
          SetMainScrollPosition(msg->wParam == VK_NEXT ? 0.5 : -0.5, 2);
        }
      }

    }
  }


  return 0;
}


// an app should implement these
int WDL_STYLE_WantGlobalButtonBorders() { return 0; }
bool WDL_STYLE_WantGlobalButtonBackground(int *col) { return false; }
int WDL_STYLE_GetSysColor(int p) { return GetSysColor(p); }
void WDL_STYLE_ScaleImageCoords(int *x, int *y) { }
bool WDL_Style_WantTextShadows(int *col) { return false; }
bool WDL_STYLE_GetBackgroundGradient(double *gradstart, double *gradslope) { return false; } // return values 0.0-1.0 for each, return false if no gradient desired

LICE_IBitmap *WDL_STYLE_GetSliderBitmap2(bool vert) { return NULL; }
bool WDL_STYLE_AllowSliderMouseWheel() { return true; }
int WDL_STYLE_GetSliderDynamicCenterPos() { return 500; }



// for coolsb
void *GetIconThemePointer(const char *name)
{
  if (!strcmp(name,"scrollbar"))
  {
    // todo: load/cache scrollbar image
  }
  return NULL;
}

extern "C" {
int GSC_mainwnd(int p) { return GetSysColor(p); }
};

