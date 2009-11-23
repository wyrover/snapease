#ifndef _IMAGERECORD_H_
#define _IMAGERECORD_H_

#include "../WDL/wdlstring.h"

class ImageRecord : public WDL_VWnd
{
public:
  ImageRecord(const char *srcfn);
  ~ImageRecord();

  ImageRecord *Duplicate();
  ////// WDL_VWnd impl

  virtual void OnPaint(LICE_IBitmap *drawbm, int origin_x, int origin_y, RECT *cliprect);
  virtual void SetPosition(const RECT *r);
  virtual INT_PTR SendCommand(int command, INT_PTR parm1, INT_PTR parm2, WDL_VWnd *src);
  virtual int OnMouseDown(int xpos, int ypos);
  virtual void OnMouseMove(int xpos, int ypos);
  virtual void OnMouseUp(int xpos, int ypos);
  virtual int UpdateCursor(int xpos, int ypos);
  virtual bool GetToolTipString(int xpos, int ypos, char *bufOut, int bufOutSz);

  //////

  bool ProcessImageToBitmap(LICE_IBitmap *srcimage, LICE_IBitmap *destimage, int max_w, int max_h); // resizes destimage, return false on error

  void SetIsFullscreen(bool isFS);

  void GetCropRectForScreen(int w, int h, RECT *cr); // scales + rotates m_croprect for output to w,h
  bool SetCropRectFromScreen(int w, int h, const RECT *cr); // return true on update

  void GetSizeInfoString(char *buf, int bufsz); // WxH or WxH cropped to WxH

  void SetDefaultTitle();
  void UpdateButtonStates();
  ///

  WDL_String m_fn;
  WDL_String m_outname;

#define IR_STATE_NEEDLOAD 0
#define IR_STATE_DECODING 1
#define IR_STATE_LOADED 2
#define IR_STATE_ERROR 3

  bool m_bw;
  char m_rot; // 90deg steps (0..3)
  bool m_crop_active;

  RECT m_croprect;

  bool m_transform_active;
  class TransformTriangle
  {
    public:
      TransformTriangle(double x1,double y1,double x2, double y2, double x3, double y3) 
      { 
        x[0]=u[0]=x1;  
        y[0]=v[0]=y1;
        x[1]=u[1]=x2;  
        y[1]=v[1]=y2;
        x[2]=u[2]=x3;  
        y[2]=v[2]=y3;
        cap[0]=cap[1]=cap[2]=false;
        memset(cap_offs,0,sizeof(cap_offs));
      }
      ~TransformTriangle() { }
      double x[3],y[3]; // vertices

      double u[3],v[3]; 

      bool cap[3]; // whether each vertex is in mouse capture
      POINT cap_offs[3];
  };
  WDL_PtrList<TransformTriangle> m_transform;

  // state
  int m_state;
  int m_srcimage_w, m_srcimage_h;

  LICE_IBitmap *m_preview_image;

  bool m_want_fullimage;
  LICE_IBitmap *m_fullimage;

  LICE_IBitmap *m_fullimage_rendercached;
  bool m_fullimage_rendercached_valid;
  bool m_is_fs;

  RECT m_last_drawrect; // set by drawing
  RECT m_last_crop_drawrect; // set by drawing, read by UI code
  int m_crop_capmode; // 1=left,2=top,4=right,8=bottom, only certain combinations will be used, obviously
  POINT m_crop_capmode_lastpos; // offsets from actual pt

  RECT m_lastlbl_rect; // last rect of text label (for editing)

  static int sortByFN(const void *a, const void *b)
  {
    ImageRecord *r1 = *(ImageRecord**)a;
    ImageRecord *r2 = *(ImageRecord**)b;
    return stricmp(r1->m_fn.Get(),r2->m_fn.Get());
  }
};


#endif