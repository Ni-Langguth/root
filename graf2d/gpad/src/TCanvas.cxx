// @(#)root/gpad:$Id$
// Author: Rene Brun   12/12/94

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <fstream>

#include "TROOT.h"
#include "TBuffer.h"
#include "TCanvas.h"
#include "TCanvasImp.h"
#include "TDatime.h"
#include "TClass.h"
#include "TStyle.h"
#include "TBox.h"
#include "TCanvasImp.h"
#include "TDialogCanvas.h"
#include "TGuiFactory.h"
#include "TEnv.h"
#include "TError.h"
#include "TContextMenu.h"
#include "TControlBar.h"
#include "TInterpreter.h"
#include "TApplication.h"
#include "TColor.h"
#include "TSystem.h"
#include "TObjArray.h"
#include "TVirtualPadEditor.h"
#include "TVirtualViewer3D.h"
#include "TPadPainter.h"
#include "TVirtualGL.h"
#include "TVirtualPS.h"
#include "TVirtualX.h"
#include "TAxis.h"
#include "TH1.h"
#include "TGraph.h"
#include "TMath.h"
#include "TView.h"
#include "strlcpy.h"
#include "snprintf.h"

#include "TVirtualMutex.h"

class TCanvasInit {
public:
   TCanvasInit() { TApplication::NeedGraphicsLibs(); }
} gCanvasInit;

//*-*x16 macros/layout_canvas

Bool_t TCanvas::fgIsFolder = kFALSE;

const Size_t kDefaultCanvasSize   = 20;

ClassImpQ(TCanvas)


TString GetNewCanvasName(const char *arg = nullptr)
{
   if (arg && *arg)
      return arg;

   const char *defcanvas = gROOT->GetDefCanvasName();
   TString cdef = defcanvas;

   auto lc = (TList*)gROOT->GetListOfCanvases();
   Int_t n = lc->GetSize() + 1;

   while(lc->FindObject(cdef.Data()))
      cdef.Form("%s_n%d", defcanvas, n++);

   return cdef;
}


/** \class TCanvas
\ingroup gpad

The Canvas class.

A Canvas is an area mapped to a window directly under the control of the display
manager. A ROOT session may have several canvases open at any given time.

A Canvas may be subdivided into independent graphical areas: the __Pads__.
A canvas has a default pad which has the name of the canvas itself.
An example of a Canvas layout is sketched in the picture below.

\image html gpad_canvas.png

This canvas contains two pads named P1 and P2. Both Canvas, P1 and P2 can be
moved, grown, shrunk using the normal rules of the Display manager.

Once objects have been drawn in a canvas, they can be edited/moved by pointing
directly to them. The cursor shape is changed to suggest the type of action that
one can do on this object. Clicking with the right mouse button on an object
pops-up a contextmenu with a complete list of actions possible on this object.

A graphical editor may be started from the canvas "View" menu under the menu
entry "Toolbar".

An interactive HELP is available by clicking on the HELP button at the top right
of the canvas. It gives a short explanation about the canvas' menus.

A canvas may be automatically divided into pads via `TPad::Divide`.

At creation time, no matter if in interactive or batch mode, the constructor
defines the size of the canvas window (including the size of the window
manager's decoration). To define precisely the graphics area size of a canvas in
the interactive mode, the following four lines of code should be used:
~~~ {.cpp}
   {
      Double_t w = 600;
      Double_t h = 600;
      auto c = new TCanvas("c", "c", w, h);
      c->SetWindowSize(w + (w - c->GetWw()), h + (h - c->GetWh()));
   }
~~~
and in the batch mode simply do:
~~~ {.cpp}
      c->SetCanvasSize(w,h);
~~~

If the canvas size exceeds the window size, scroll bars will be added to the canvas
This allows to display very large canvases (even bigger than the screen size). The
Following example shows how to proceed.
~~~ {.cpp}
   {
      auto c = new TCanvas("c","c");
      c->SetCanvasSize(1500, 1500);
      c->SetWindowSize(500, 500);
   }
~~~
*/

////////////////////////////////////////////////////////////////////////////////
/// Canvas default constructor.

TCanvas::TCanvas(Bool_t build) : TPad(), fDoubleBuffer(0)
{
   fPainter          = nullptr;
   fWindowTopX       = 0;
   fWindowTopY       = 0;
   fWindowWidth      = 0;
   fWindowHeight     = 0;
   fCw               = 0;
   fCh               = 0;
   fXsizeUser        = 0;
   fYsizeUser        = 0;
   fXsizeReal        = kDefaultCanvasSize;
   fYsizeReal        = kDefaultCanvasSize;
   fHighLightColor   = gEnv->GetValue("Canvas.HighLightColor", kRed);
   fEvent            = -1;
   fEventX           = -1;
   fEventY           = -1;
   fSelectedX        = 0;
   fSelectedY        = 0;
   fRetained         = kTRUE;
   fDrawn            = kFALSE;
   fUpdated          = kFALSE;
   fSelected         = nullptr;
   fClickSelected    = nullptr;
   fSelectedPad      = nullptr;
   fClickSelectedPad = nullptr;
   fPadSave          = nullptr;
   fCanvasImp        = nullptr;
   fContextMenu      = nullptr;

   fUseGL = gStyle->GetCanvasPreferGL();

   if (!build || TClass::IsCallingNew() != TClass::kRealNew) {
      Constructor();
   } else {
      auto cdef = GetNewCanvasName();

      Constructor(cdef.Data(), cdef.Data(), 1);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Canvas default constructor

void TCanvas::Constructor()
{
   if (gThreadXAR) {
      void *arr[2];
      arr[1] = this;
      if ((*gThreadXAR)("CANV", 2, arr, nullptr)) return;
   }

   fCanvas    = nullptr;
   fCanvasID  = -1;
   fCanvasImp = nullptr;
   fBatch     = kTRUE;
   fUpdating  = kFALSE;

   fContextMenu   = nullptr;
   fSelected      = nullptr;
   fClickSelected = nullptr;
   fSelectedPad   = nullptr;
   fClickSelectedPad = nullptr;
   fPadSave       = nullptr;
   SetBit(kAutoExec);
   SetBit(kShowEditor);
   SetBit(kShowToolBar);
}

////////////////////////////////////////////////////////////////////////////////
/// Create an embedded canvas, i.e. a canvas that is in a TGCanvas widget
/// which is placed in a TGFrame. This ctor is only called via the
/// TRootEmbeddedCanvas class.
///
/// If "name" starts with "gl" the canvas is ready to receive GL output.

TCanvas::TCanvas(const char *name, Int_t ww, Int_t wh, Int_t winid) : TPad(), fDoubleBuffer(0)
{
   fCanvasImp = nullptr;
   fPainter = nullptr;
   Init();

   fCanvasID     = winid;
   fWindowTopX   = 0;
   fWindowTopY   = 0;
   fWindowWidth  = ww;
   fWindowHeight = wh;
   fCw           = ww + 4;
   fCh           = wh +28;
   fBatch        = kFALSE;
   fUpdating     = kFALSE;

   //This is a very special ctor. A window exists already!
   //Can create painter now.
   fUseGL = gStyle->GetCanvasPreferGL();

   if (fUseGL) {
      fGLDevice = gGLManager->CreateGLContext(winid);
      if (fGLDevice == -1)
         fUseGL = kFALSE;
   }

   fCanvasImp = gBatchGuiFactory->CreateCanvasImp(this, name, fCw, fCh);
   if (!fCanvasImp) return;

   CreatePainter();
   fName = GetNewCanvasName(name); // avoid Modified() signal from SetName
   Build();
}

////////////////////////////////////////////////////////////////////////////////
/// Create a new canvas with a predefined size form.
/// If form < 0  the menubar is not shown.
///
/// - form = 1    700x500 at 10,10 (set by TStyle::SetCanvasDefH,W,X,Y)
/// - form = 2    500x500 at 20,20
/// - form = 3    500x500 at 30,30
/// - form = 4    500x500 at 40,40
/// - form = 5    500x500 at 50,50
///
/// If "name" starts with "gl" the canvas is ready to receive GL output.

TCanvas::TCanvas(const char *name, const char *title, Int_t form) : TPad(), fDoubleBuffer(0)
{
   fPainter = nullptr;
   fUseGL = gStyle->GetCanvasPreferGL();

   Constructor(name, title, form);
}

////////////////////////////////////////////////////////////////////////////////
/// Create a new canvas with a predefined size form.
/// If form < 0  the menubar is not shown.
///
/// - form = 1    700x500 at 10,10 (set by TStyle::SetCanvasDefH,W,X,Y)
/// - form = 2    500x500 at 20,20
/// - form = 3    500x500 at 30,30
/// - form = 4    500x500 at 40,40
/// - form = 5    500x500 at 50,50

void TCanvas::Constructor(const char *name, const char *title, Int_t form)
{
   if (gThreadXAR) {
      void *arr[6];
      static Int_t ww = 500;
      static Int_t wh = 500;
      arr[1] = this; arr[2] = (void*)name; arr[3] = (void*)title; arr[4] =&ww; arr[5] = &wh;
      if ((*gThreadXAR)("CANV", 6, arr, nullptr)) return;
   }

   Init();
   SetBit(kMenuBar,true);
   if (form < 0) {
      form     = -form;
      SetBit(kMenuBar,false);
   }

   fCanvas = this;

   fCanvasID = -1;
   TCanvas *old = (TCanvas*)gROOT->GetListOfCanvases()->FindObject(name);
   if (old && old->IsOnHeap()) {
      Warning("Constructor","Deleting canvas with same name: %s",name);
      delete old;
   }
   if (gROOT->IsBatch()) {   //We are in Batch mode
      fWindowTopX = fWindowTopY = 0;
      if (form == 1) {
         fWindowWidth  = gStyle->GetCanvasDefW();
         fWindowHeight = gStyle->GetCanvasDefH();
      } else {
         fWindowWidth  = 500;
         fWindowHeight = 500;
      }
      fCw           = fWindowWidth;
      fCh           = fWindowHeight;
      fCanvasImp    = gBatchGuiFactory->CreateCanvasImp(this, name, fCw, fCh);
      if (!fCanvasImp) return;
      fBatch        = kTRUE;
   } else {                  //normal mode with a screen window
      Float_t cx = gStyle->GetScreenFactor();
      if (form < 1 || form > 20) form = 1;
      auto factory = gROOT->IsWebDisplay() ? gBatchGuiFactory : gGuiFactory;
      Int_t ux, uy, cw, ch;
      if (form == 1) {
         cw = gStyle->GetCanvasDefW();
         ch = gStyle->GetCanvasDefH();
         ux = gStyle->GetCanvasDefX();
         uy = gStyle->GetCanvasDefY();
      } else {
         cw = ch = 500;
         ux = uy = form * 10;
      }

      fCanvasImp = factory->CreateCanvasImp(this, name, Int_t(cx*ux), Int_t(cx*uy), UInt_t(cx*cw),  UInt_t(cx*ch));
      if (!fCanvasImp) return;

      if (!gROOT->IsBatch() && fCanvasID == -1)
         fCanvasID = fCanvasImp->InitWindow();

      fCanvasImp->ShowMenuBar(TestBit(kMenuBar));
      fBatch = kFALSE;
   }

   CreatePainter();

   fName = GetNewCanvasName(name); // avoid Modified() signal from SetName
   SetTitle(title); // requires fCanvasImp set
   Build();

   // Popup canvas
   fCanvasImp->Show();
}

////////////////////////////////////////////////////////////////////////////////
/// Create a new canvas at a random position.
///
/// \param[in] name    canvas name
/// \param[in] title   canvas title
/// \param[in] ww      is the window size in pixels along X
///                    (if ww < 0  the menubar is not shown)
/// \param[in] wh      is the window size in pixels along Y
///
/// If "name" starts with "gl" the canvas is ready to receive GL output.

TCanvas::TCanvas(const char *name, const char *title, Int_t ww, Int_t wh) : TPad(), fDoubleBuffer(0)
{
   fPainter = nullptr;
   fUseGL = gStyle->GetCanvasPreferGL();

   Constructor(name, title, ww, wh);
}

////////////////////////////////////////////////////////////////////////////////
/// Create a new canvas at a random position.
///
/// \param[in] name    canvas name
/// \param[in] title   canvas title
/// \param[in] ww      is the window size in pixels along X
///                    (if ww < 0  the menubar is not shown)
/// \param[in] wh      is the window size in pixels along Y

void TCanvas::Constructor(const char *name, const char *title, Int_t ww, Int_t wh)
{
   if (gThreadXAR) {
      void *arr[6];
      arr[1] = this; arr[2] = (void*)name; arr[3] = (void*)title; arr[4] =&ww; arr[5] = &wh;
      if ((*gThreadXAR)("CANV", 6, arr, nullptr)) return;
   }

   Init();
   SetBit(kMenuBar,true);
   if (ww < 0) {
      ww       = -ww;
      SetBit(kMenuBar,false);
   }
   if (wh <= 0) {
      Error("Constructor", "Invalid canvas height: %d",wh);
      return;
   }
   fCw       = ww;
   fCh       = wh;
   fCanvasID = -1;
   TCanvas *old = (TCanvas*)gROOT->GetListOfCanvases()->FindObject(name);
   if (old && old->IsOnHeap()) {
      Warning("Constructor","Deleting canvas with same name: %s",name);
      delete old;
   }
   if (gROOT->IsBatch()) {   //We are in Batch mode
      fWindowTopX   = fWindowTopY = 0;
      fWindowWidth  = ww;
      fWindowHeight = wh;
      fCw           = ww;
      fCh           = wh;
      fCanvasImp    = gBatchGuiFactory->CreateCanvasImp(this, name, fCw, fCh);
      if (!fCanvasImp) return;
      fBatch        = kTRUE;
   } else {
      Float_t cx = gStyle->GetScreenFactor();
      auto factory = gROOT->IsWebDisplay() ? gBatchGuiFactory : gGuiFactory;
      fCanvasImp = factory->CreateCanvasImp(this, name, UInt_t(cx*ww), UInt_t(cx*wh));
      if (!fCanvasImp) return;

      if (!gROOT->IsBatch() && fCanvasID == -1)
         fCanvasID = fCanvasImp->InitWindow();

      fCanvasImp->ShowMenuBar(TestBit(kMenuBar));
      fBatch = kFALSE;
   }

   CreatePainter();

   fName = GetNewCanvasName(name); // avoid Modified() signal from SetName
   SetTitle(title); // requires fCanvasImp set
   Build();

   // Popup canvas
   fCanvasImp->Show();
}

////////////////////////////////////////////////////////////////////////////////
/// Create a new canvas.
///
/// \param[in] name         canvas name
/// \param[in] title        canvas title
/// \param[in] wtopx,wtopy  are the pixel coordinates of the top left corner of
///                         the canvas (if wtopx < 0) the menubar is not shown)
/// \param[in] ww           is the window size in pixels along X
/// \param[in] wh           is the window size in pixels along Y
///
/// If "name" starts with "gl" the canvas is ready to receive GL output.

TCanvas::TCanvas(const char *name, const char *title, Int_t wtopx, Int_t wtopy, Int_t ww, Int_t wh)
        : TPad(), fDoubleBuffer(0)
{
   fPainter = nullptr;
   fUseGL = gStyle->GetCanvasPreferGL();

   Constructor(name, title, wtopx, wtopy, ww, wh);
}

////////////////////////////////////////////////////////////////////////////////
/// Create a new canvas.
///
/// \param[in] name         canvas name
/// \param[in] title        canvas title
/// \param[in] wtopx,wtopy  are the pixel coordinates of the top left corner of
///                         the canvas  (if wtopx < 0) the menubar is not shown)
/// \param[in] ww           is the window size in pixels along X
/// \param[in] wh           is the window size in pixels along Y

void TCanvas::Constructor(const char *name, const char *title, Int_t wtopx,
                          Int_t wtopy, Int_t ww, Int_t wh)
{
   if (gThreadXAR) {
      void *arr[8];
      arr[1] = this;   arr[2] = (void*)name;   arr[3] = (void*)title;
      arr[4] = &wtopx; arr[5] = &wtopy; arr[6] = &ww; arr[7] = &wh;
      if ((*gThreadXAR)("CANV", 8, arr, nullptr)) return;
   }

   Init();
   SetBit(kMenuBar,true);
   if (wtopx < 0) {
      wtopx    = -wtopx;
      SetBit(kMenuBar,false);
   }
   fCw       = ww;
   fCh       = wh;
   fCanvasID = -1;
   TCanvas *old = (TCanvas*)gROOT->GetListOfCanvases()->FindObject(name);
   if (old && old->IsOnHeap()) {
      Warning("Constructor","Deleting canvas with same name: %s",name);
      delete old;
   }
   if (gROOT->IsBatch()) {   //We are in Batch mode
      fWindowTopX   = fWindowTopY = 0;
      fWindowWidth  = ww;
      fWindowHeight = wh;
      fCw           = ww;
      fCh           = wh;
      fCanvasImp    = gBatchGuiFactory->CreateCanvasImp(this, name, fCw, fCh);
      if (!fCanvasImp) return;
      fBatch        = kTRUE;
   } else {                   //normal mode with a screen window
      Float_t cx = gStyle->GetScreenFactor();
      auto factory = gROOT->IsWebDisplay() ? gBatchGuiFactory : gGuiFactory;
      fCanvasImp = factory->CreateCanvasImp(this, name, Int_t(cx*wtopx), Int_t(cx*wtopy), UInt_t(cx*ww), UInt_t(cx*wh));
      if (!fCanvasImp) return;

      if (!gROOT->IsBatch() && fCanvasID == -1)
         fCanvasID = fCanvasImp->InitWindow();

      fCanvasImp->ShowMenuBar(TestBit(kMenuBar));
      fBatch = kFALSE;
   }

   CreatePainter();

   fName = GetNewCanvasName(name); // avoid Modified() signal from SetName
   SetTitle(title); // requires fCanvasImp set
   Build();

   // Popup canvas
   fCanvasImp->Show();
}

////////////////////////////////////////////////////////////////////////////////
/// Initialize the TCanvas members. Called by all constructors.

void TCanvas::Init()
{
   // Make sure the application environment exists. It is need for graphics
   // (colors are initialized in the TApplication ctor).
   if (!gApplication)
      TApplication::CreateApplication();

   // Load and initialize graphics libraries if
   // TApplication::NeedGraphicsLibs() has been called by a
   // library static initializer.
   if (gApplication)
      gApplication->InitializeGraphics(gROOT->IsWebDisplay());

   // Get some default from .rootrc. Used in fCanvasImp->InitWindow().
   fHighLightColor     = gEnv->GetValue("Canvas.HighLightColor", kRed);
   SetBit(kMoveOpaque,   gEnv->GetValue("Canvas.MoveOpaque", 0));
   SetBit(kResizeOpaque, gEnv->GetValue("Canvas.ResizeOpaque", 0));
   if (gEnv->GetValue("Canvas.ShowEventStatus", kFALSE)) SetBit(kShowEventStatus);
   if (gEnv->GetValue("Canvas.ShowToolTips", kFALSE)) SetBit(kShowToolTips);
   if (gEnv->GetValue("Canvas.ShowToolBar", kFALSE)) SetBit(kShowToolBar);
   if (gEnv->GetValue("Canvas.ShowEditor", kFALSE)) SetBit(kShowEditor);
   if (gEnv->GetValue("Canvas.AutoExec", kTRUE)) SetBit(kAutoExec);

   // Fill canvas ROOT data structure
   fXsizeUser = 0;
   fYsizeUser = 0;
   fXsizeReal = kDefaultCanvasSize;
   fYsizeReal = kDefaultCanvasSize;

   fDISPLAY         = "$DISPLAY";
   fUpdating        = kFALSE;
   fRetained        = kTRUE;
   fSelected        = nullptr;
   fClickSelected   = nullptr;
   fSelectedX       = 0;
   fSelectedY       = 0;
   fSelectedPad     = nullptr;
   fClickSelectedPad= nullptr;
   fPadSave         = nullptr;
   fEvent           = -1;
   fEventX          = -1;
   fEventY          = -1;
   fContextMenu     = nullptr;
   fDrawn           = kFALSE;
   fUpdated          = kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Build a canvas. Called by all constructors.

void TCanvas::Build()
{
   // Get window identifier
   if (fCanvasID == -1 && fCanvasImp)
      fCanvasID = fCanvasImp->InitWindow();
   if (fCanvasID == -1) return;

   if (fCw !=0 && fCh !=0) {
      if (fCw < fCh) fXsizeReal = fYsizeReal*Float_t(fCw)/Float_t(fCh);
      else           fYsizeReal = fXsizeReal*Float_t(fCh)/Float_t(fCw);
   }

   // Set Pad parameters
   gPad            = this;
   fCanvas         = this;
   fMother         = (TPad*)gPad;

   if (IsBatch()) {
      // Make sure that batch interactive canvas sizes are the same
      fCw -= 4;
      fCh -= 28;
   } else if (IsWeb()) {
      // mark canvas as batch - avoid gVirtualX in many places
      SetBatch(kTRUE);
   } else {
      //normal mode with a screen window
      // Set default physical canvas attributes
      //Should be done via gVirtualX, not via fPainter (at least now). No changes here.
      gVirtualX->SelectWindow(fCanvasID);
      gVirtualX->SetFillColor(1);         //Set color index for fill area
      gVirtualX->SetLineColor(1);         //Set color index for lines
      gVirtualX->SetMarkerColor(1);       //Set color index for markers
      gVirtualX->SetTextColor(1);         //Set color index for text
      // Clear workstation
      gVirtualX->ClearWindow();

      // Set Double Buffer on by default
      SetDoubleBuffer(1);

      // Get effective window parameters (with borders and menubar)
      fCanvasImp->GetWindowGeometry(fWindowTopX, fWindowTopY,
                                    fWindowWidth, fWindowHeight);

      // Get effective canvas parameters without borders
      Int_t dum1, dum2;
      gVirtualX->GetGeometry(fCanvasID, dum1, dum2, fCw, fCh);

      fContextMenu = new TContextMenu("ContextMenu");
   }

   gROOT->GetListOfCanvases()->Add(this);

   if (!fPrimitives) {
      fPrimitives     = new TList;
      SetFillColor(gStyle->GetCanvasColor());
      SetFillStyle(1001);
      SetGrid(gStyle->GetPadGridX(),gStyle->GetPadGridY());
      SetTicks(gStyle->GetPadTickX(),gStyle->GetPadTickY());
      SetLogx(gStyle->GetOptLogx());
      SetLogy(gStyle->GetOptLogy());
      SetLogz(gStyle->GetOptLogz());
      SetBottomMargin(gStyle->GetPadBottomMargin());
      SetTopMargin(gStyle->GetPadTopMargin());
      SetLeftMargin(gStyle->GetPadLeftMargin());
      SetRightMargin(gStyle->GetPadRightMargin());
      SetBorderSize(gStyle->GetCanvasBorderSize());
      SetBorderMode(gStyle->GetCanvasBorderMode());
      fBorderMode=gStyle->GetCanvasBorderMode(); // do not call SetBorderMode (function redefined in TCanvas)
      SetPad(0, 0, 1, 1);
      Range(0, 0, 1, 1);   //pad range is set by default to [0,1] in x and y

      TVirtualPadPainter *vpp = GetCanvasPainter();
      if (vpp) vpp->SelectDrawable(fPixmapID);//gVirtualX->SelectPixmap(fPixmapID);    //pixmap must be selected
      PaintBorder(GetFillColor(), kTRUE);    //paint background
   }

   // transient canvases have typically no menubar and should not get
   // by default the event status bar (if set by default)
   if (TestBit(kMenuBar) && fCanvasImp) {
      if (TestBit(kShowEventStatus)) fCanvasImp->ShowStatusBar(kTRUE);
      // ... and toolbar + editor
      if (TestBit(kShowToolBar))     fCanvasImp->ShowToolBar(kTRUE);
      if (TestBit(kShowEditor))      fCanvasImp->ShowEditor(kTRUE);
      if (TestBit(kShowToolTips))    fCanvasImp->ShowToolTips(kTRUE);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Canvas destructor

TCanvas::~TCanvas()
{
   Destructor();
}

////////////////////////////////////////////////////////////////////////////////
/// Browse.

void TCanvas::Browse(TBrowser *b)
{
   Draw();
   cd();
   if (fgIsFolder) fPrimitives->Browse(b);
}

////////////////////////////////////////////////////////////////////////////////
/// Actual canvas destructor.

void TCanvas::Destructor()
{
   if (gThreadXAR) {
      void *arr[2];
      arr[1] = this;
      if ((*gThreadXAR)("CDEL", 2, arr, nullptr)) return;
   }

   if (ROOT::Detail::HasBeenDeleted(this)) return;

   SafeDelete(fContextMenu);
   if (!gPad) return;

   Close();

   //If not yet (batch mode?).
   SafeDelete(fPainter);
}

////////////////////////////////////////////////////////////////////////////////
/// Set current canvas & pad. Returns the new current pad,
/// or 0 in case of failure.
/// See TPad::cd() for an explanation of the parameter.

TVirtualPad *TCanvas::cd(Int_t subpadnumber)
{
   if (fCanvasID == -1) return nullptr;

   TPad::cd(subpadnumber);

   // in case doublebuffer is off, draw directly onto display window
   if (!IsBatch() && !IsWeb() && !fDoubleBuffer)
      gVirtualX->SelectWindow(fCanvasID);//Ok, does not matter for glpad.

   return gPad;
}

////////////////////////////////////////////////////////////////////////////////
/// Remove all primitives from the canvas.
/// If option "D" is specified, direct sub-pads are cleared but not deleted.
/// This option is not recursive, i.e. pads in direct sub-pads are deleted.

void TCanvas::Clear(Option_t *option)
{
   if (fCanvasID == -1) return;

   R__LOCKGUARD(gROOTMutex);

   TString opt = option;
   opt.ToLower();
   if (opt.Contains("d")) {
      // clear subpads, but do not delete pads in case the canvas
      // has been divided (note: option "D" is propagated so could cause
      // conflicts for primitives using option "D" for something else)
      if (fPrimitives) {
         TIter next(fPrimitives);
         TObject *obj;
         while ((obj=next())) {
            obj->Clear(option);
         }
      }
   } else {
      //default, clear everything in the canvas. Subpads are deleted
      TPad::Clear(option);   //Remove primitives from pad
   }

   fSelected      = nullptr;
   fClickSelected = nullptr;
   fSelectedPad   = nullptr;
   fClickSelectedPad = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// Emit pad Cleared signal.

void TCanvas::Cleared(TVirtualPad *pad)
{
   Emit("Cleared(TVirtualPad*)", (Longptr_t)pad);
}

////////////////////////////////////////////////////////////////////////////////
/// Emit Closed signal.

void TCanvas::Closed()
{
   Emit("Closed()");
}

////////////////////////////////////////////////////////////////////////////////
/// Close canvas.
///
/// Delete window/pads data structure

void TCanvas::Close(Option_t *option)
{
   auto padsave = gPad;
   TCanvas *cansave = padsave ? padsave->GetCanvas() : nullptr;

   if (fCanvasID != -1) {

      if (!gROOT->IsLineProcessing() && !gVirtualX->IsCmdThread()) {
         gInterpreter->Execute(this, IsA(), "Close", option);
         return;
      }

      R__LOCKGUARD(gROOTMutex);

      FeedbackMode(kFALSE);

      cd();
      TPad::Close(option);

      if (!IsBatch() && !IsWeb()) {
         gVirtualX->SelectWindow(fCanvasID);    //select current canvas

         DeleteCanvasPainter();

         if (fCanvasImp)
            fCanvasImp->Close();
      }
      fCanvasID = -1;
      fBatch    = kTRUE;

      gROOT->GetListOfCanvases()->Remove(this);

      // Close actual window on screen
      SafeDelete(fCanvasImp);
   }

   if (cansave == this) {
      gPad = (TCanvas *) gROOT->GetListOfCanvases()->First();
   } else {
      gPad = padsave;
   }

   Closed();
}

////////////////////////////////////////////////////////////////////////////////
/// Copy the canvas pixmap of the pad to the canvas.

void TCanvas::CopyPixmaps()
{
   if (!IsBatch()) {
      CopyPixmap();
      TPad::CopyPixmaps();
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Draw a canvas.
/// If a canvas with the name is already on the screen, the canvas is repainted.
/// This function is useful when a canvas object has been saved in a Root file.
/// One can then do:
/// ~~~ {.cpp}
///     Root > TFile::Open("file.root");
///     Root > canvas->Draw();
/// ~~~

void TCanvas::Draw(Option_t *)
{
   // Load and initialize graphics libraries if
   // TApplication::NeedGraphicsLibs() has been called by a
   // library static initializer.
   if (gApplication)
      gApplication->InitializeGraphics(gROOT->IsWebDisplay());

   fDrawn = kTRUE;

   TCanvas *old = (TCanvas*)gROOT->GetListOfCanvases()->FindObject(GetName());
   if (old == this) {
      if (IsWeb()) {
         Modified();
         UpdateAsync();
      } else {
         Paint();
      }
      return;
   }
   if (old) { gROOT->GetListOfCanvases()->Remove(old); delete old;}

   if (fWindowWidth  == 0) {
      if (fCw !=0) fWindowWidth = fCw+4;
      else         fWindowWidth = 800;
   }
   if (fWindowHeight == 0) {
      if (fCh !=0) fWindowHeight = fCh+28;
      else         fWindowHeight = 600;
   }
   if (gROOT->IsBatch()) {   //We are in Batch mode
      fCanvasImp  = gBatchGuiFactory->CreateCanvasImp(this, GetName(), fWindowWidth, fWindowHeight);
      if (!fCanvasImp) return;
      fBatch = kTRUE;

   } else {                   //normal mode with a screen window
      auto factory = gROOT->IsWebDisplay() ? gBatchGuiFactory : gGuiFactory;
      fCanvasImp = factory->CreateCanvasImp(this, GetName(), fWindowTopX, fWindowTopY,
                                                fWindowWidth, fWindowHeight);
      if (!fCanvasImp) return;
      fCanvasImp->ShowMenuBar(TestBit(kMenuBar));
   }
   Build();
   ResizePad();
   fCanvasImp->SetWindowTitle(fTitle);
   fCanvasImp->Show();
   Modified();
}

////////////////////////////////////////////////////////////////////////////////
/// Draw a clone of this canvas
/// A new canvas is created that is a clone of this canvas

TObject *TCanvas::DrawClone(Option_t *option) const
{
   TCanvas *newCanvas = (TCanvas*)Clone();
   newCanvas->SetName();

   newCanvas->Draw(option);
   newCanvas->Update();
   return newCanvas;
}

////////////////////////////////////////////////////////////////////////////////
/// Draw a clone of this canvas into the current pad
/// In an interactive session, select the destination/current pad
/// with the middle mouse button, then point to the canvas area to select
/// the canvas context menu item DrawClonePad.
/// Note that the original canvas may have subpads.

TObject *TCanvas::DrawClonePad()
{
   auto padsav = gPad;
   auto selpad = gROOT->GetSelectedPad();
   auto pad = padsav;
   if (pad == this) pad = selpad;
   if (!padsav || !pad || pad == this) {
      TCanvas *newCanvas = (TCanvas*)DrawClone();
      newCanvas->SetWindowSize(GetWindowWidth(),GetWindowHeight());
      return newCanvas;
   }
   if (fCanvasID == -1) {
      auto factory = gROOT->IsWebDisplay() ? gBatchGuiFactory : gGuiFactory;
      fCanvasImp = factory->CreateCanvasImp(this, GetName(), fWindowTopX, fWindowTopY,
                                             fWindowWidth, fWindowHeight);
      if (!fCanvasImp) return nullptr;
      fCanvasImp->ShowMenuBar(TestBit(kMenuBar));
      fCanvasID = fCanvasImp->InitWindow();
   }
   this->cd();
   //copy pad attributes
   pad->Range(fX1,fY1,fX2,fY2);
   pad->SetTickx(GetTickx());
   pad->SetTicky(GetTicky());
   pad->SetGridx(GetGridx());
   pad->SetGridy(GetGridy());
   pad->SetLogx(GetLogx());
   pad->SetLogy(GetLogy());
   pad->SetLogz(GetLogz());
   pad->SetBorderSize(GetBorderSize());
   pad->SetBorderMode(GetBorderMode());
   TAttLine::Copy((TAttLine&)*pad);
   TAttFill::Copy((TAttFill&)*pad);
   TAttPad::Copy((TAttPad&)*pad);

   //copy primitives
   TIter next(GetListOfPrimitives());
   while (auto obj = next()) {
      pad->cd();
      pad->Add(obj->Clone(), next.GetOption(), kFALSE); // do not issue modified for each object
   }
   pad->ResizePad();
   pad->Modified();
   pad->Update();
   if (padsav) padsav->cd();
   return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// Report name and title of primitive below the cursor.
///
/// This function is called when the option "Event Status"
/// in the canvas menu "Options" is selected.

void TCanvas::DrawEventStatus(Int_t event, Int_t px, Int_t py, TObject *selected)
{
   const Int_t kTMAX=256;
   static char atext[kTMAX];

   if (!TestBit(kShowEventStatus) || !selected) return;

   if (!fCanvasImp) return; //this may happen when closing a TAttCanvas

   TContext ctxt(GetSelectedPad(), kFALSE);

   fCanvasImp->SetStatusText(selected->GetTitle(),0);
   fCanvasImp->SetStatusText(selected->GetName(),1);
   if (event == kKeyPress)
      snprintf(atext, kTMAX, "%c", (char) px);
   else
      snprintf(atext, kTMAX, "%d,%d", px, py);
   fCanvasImp->SetStatusText(atext,2);

   // Show date/time if TimeDisplay is selected
   TAxis *xaxis = nullptr;
   if ( selected->InheritsFrom("TH1") )
      xaxis = ((TH1*)selected)->GetXaxis();
   else if ( selected->InheritsFrom("TGraph") )
      xaxis = ((TGraph*)selected)->GetXaxis();
   else if ( selected->InheritsFrom("TAxis") )
      xaxis = (TAxis*)selected;
   if ( xaxis != nullptr && xaxis->GetTimeDisplay()) {
      TString objinfo = selected->GetObjectInfo(px,py);
      // check if user has overwritten GetObjectInfo and altered
      // the default text from TObject::GetObjectInfo "x=.. y=.."
      if (objinfo.Contains("x=") && objinfo.Contains("y=") ) {
         UInt_t toff = 0;
         TString time_format(xaxis->GetTimeFormat());
         // TimeFormat may contain offset: %F2000-01-01 00:00:00
         Int_t idF = time_format.Index("%F");
         if (idF>=0) {
            Int_t lnF = time_format.Length();
            // minimal check for correct format
            if (lnF - idF == 21) {
               time_format = time_format(idF+2, lnF);
               TDatime dtoff(time_format);
               toff = dtoff.Convert();
            }
         } else {
            toff = (UInt_t)gStyle->GetTimeOffset();
         }
         TDatime dt((UInt_t)gPad->AbsPixeltoX(px) + toff);
         snprintf(atext, kTMAX, "%s, y=%g",
            dt.AsSQLString(),gPad->AbsPixeltoY(py));
         fCanvasImp->SetStatusText(atext,3);
         return;
      }
   }
   // default
   fCanvasImp->SetStatusText(selected->GetObjectInfo(px,py),3);
}

////////////////////////////////////////////////////////////////////////////////
/// Get editor bar.

void TCanvas::EditorBar()
{
   TVirtualPadEditor::GetPadEditor();
}

////////////////////////////////////////////////////////////////////////////////
/// Embedded a canvas into a TRootEmbeddedCanvas. This method is only called
/// via TRootEmbeddedCanvas::AdoptCanvas.

void TCanvas::EmbedInto(Int_t winid, Int_t ww, Int_t wh)
{
   // If fCanvasImp already exists, no need to go further.
   if(fCanvasImp) return;

   fCanvasID     = winid;
   fWindowTopX   = 0;
   fWindowTopY   = 0;
   fWindowWidth  = ww;
   fWindowHeight = wh;
   fCw           = ww;
   fCh           = wh;
   fBatch        = kFALSE;
   fUpdating     = kFALSE;

   fCanvasImp    = gBatchGuiFactory->CreateCanvasImp(this, GetName(), fCw, fCh);
   if (!fCanvasImp) return;
   Build();
   Resize();
}

////////////////////////////////////////////////////////////////////////////////
/// Generate kMouseEnter and kMouseLeave events depending on the previously
/// selected object and the currently selected object. Does nothing if the
/// selected object does not change.

void TCanvas::EnterLeave(TPad *prevSelPad, TObject *prevSelObj)
{
   if (prevSelObj == fSelected) return;

   TContext ctxt(kFALSE);
   Int_t sevent = fEvent;

   if (prevSelObj) {
      gPad = prevSelPad;
      prevSelObj->ExecuteEvent(kMouseLeave, fEventX, fEventY);
      fEvent = kMouseLeave;
      RunAutoExec();
      ProcessedEvent(kMouseLeave, fEventX, fEventY, prevSelObj);  // emit signal
   }

   gPad = fSelectedPad;

   if (fSelected) {
      fSelected->ExecuteEvent(kMouseEnter, fEventX, fEventY);
      fEvent = kMouseEnter;
      RunAutoExec();
      ProcessedEvent(kMouseEnter, fEventX, fEventY, fSelected);  // emit signal
   }

   fEvent = sevent;
}

////////////////////////////////////////////////////////////////////////////////
/// Execute action corresponding to one event.
///
/// This member function must be implemented to realize the action
/// corresponding to the mouse click on the object in the canvas
///
/// Only handle mouse motion events in TCanvas, all other events are
/// ignored for the time being

void TCanvas::ExecuteEvent(Int_t event, Int_t px, Int_t py)
{
   if (gROOT->GetEditorMode()) {
      TPad::ExecuteEvent(event,px,py);
      return;
   }

   switch (event) {

   case kMouseMotion:
      SetCursor(kCross);
      break;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Turn rubberband feedback mode on or off.

void TCanvas::FeedbackMode(Bool_t set)
{
   if (IsWeb())
      return;

   if (set) {
      SetDoubleBuffer(0);             // turn off double buffer mode
      gVirtualX->SetDrawMode(TVirtualX::kInvert);  // set the drawing mode to XOR mode
   } else {
      SetDoubleBuffer(1);             // turn on double buffer mode
      gVirtualX->SetDrawMode(TVirtualX::kCopy); // set drawing mode back to normal (copy) mode
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Flush canvas buffers.

void TCanvas::Flush()
{
   if ((fCanvasID == -1) || IsWeb()) return;

   TContext ctxt(this, kTRUE);
   if (!IsBatch()) {
      if (!UseGL() || fGLDevice == -1) {
         gVirtualX->SelectWindow(fCanvasID);
         gPad = ctxt.GetSaved(); //don't do cd() because than also the pixmap is changed
         CopyPixmaps();
         gVirtualX->UpdateWindow(1);
      } else {
         TVirtualPS *tvps = gVirtualPS;
         gVirtualPS = nullptr;
         gGLManager->MakeCurrent(fGLDevice);
         fPainter->InitPainter();
         Paint();
         if (ctxt.GetSaved() && ctxt.GetSaved()->GetCanvas() == this) {
            ctxt.GetSaved()->cd();
            ctxt.GetSaved()->HighLight(ctxt.GetSaved()->GetHighLightColor());
            //cd();
         }
         fPainter->LockPainter();
         gGLManager->Flush(fGLDevice);
         gVirtualPS = tvps;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Force canvas update

void TCanvas::ForceUpdate()
{
   if (fCanvasImp) fCanvasImp->ForceUpdate();
}

////////////////////////////////////////////////////////////////////////////////
/// Force a copy of current style for all objects in canvas.

void TCanvas::UseCurrentStyle()
{
   if (!gROOT->IsLineProcessing() && !gVirtualX->IsCmdThread()) {
      gInterpreter->Execute(this, IsA(), "UseCurrentStyle", "");
      return;
   }

   R__LOCKGUARD(gROOTMutex);

   TPad::UseCurrentStyle();

   if (gStyle->IsReading()) {
      SetFillColor(gStyle->GetCanvasColor());
      fBorderSize = gStyle->GetCanvasBorderSize();
      fBorderMode = gStyle->GetCanvasBorderMode();
   } else {
      gStyle->SetCanvasColor(GetFillColor());
      gStyle->SetCanvasBorderSize(fBorderSize);
      gStyle->SetCanvasBorderMode(fBorderMode);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Returns current top x position of window on screen.

Int_t TCanvas::GetWindowTopX()
{
   if (fCanvasImp) fCanvasImp->GetWindowGeometry(fWindowTopX, fWindowTopY,
                                                 fWindowWidth,fWindowHeight);

   return fWindowTopX;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns current top y position of window on screen.

Int_t TCanvas::GetWindowTopY()
{
   if (fCanvasImp) fCanvasImp->GetWindowGeometry(fWindowTopX, fWindowTopY,
                                                 fWindowWidth,fWindowHeight);

   return fWindowTopY;
}

////////////////////////////////////////////////////////////////////////////////
/// Handle Input Events.
///
/// Handle input events, like button up/down in current canvas.

void TCanvas::HandleInput(EEventType event, Int_t px, Int_t py)
{
   TPad    *pad;
   TPad    *prevSelPad = fSelectedPad;
   TObject *prevSelObj = fSelected;

   fPadSave = (TPad*)gPad;
   cd();        // make sure this canvas is the current canvas

   fEvent  = event;
   fEventX = px;
   fEventY = py;

   switch (event) {

   case kMouseMotion:
      // highlight object tracked over
      pad = Pick(px, py, prevSelObj);
      if (!pad) return;

      EnterLeave(prevSelPad, prevSelObj);

      gPad = pad;   // don't use cd() we will use the current
                    // canvas via the GetCanvas member and not via
                    // gPad->GetCanvas

      if (fSelected) {
         fSelected->ExecuteEvent(event, px, py);
         RunAutoExec();
      }

      break;

   case kMouseEnter:
      // mouse enters canvas
      if (!fDoubleBuffer) FeedbackMode(kTRUE);
      break;

   case kMouseLeave:
      // mouse leaves canvas
      {
         // force popdown of tooltips
         TObject *sobj = fSelected;
         TPad    *spad = fSelectedPad;
         fSelected     = nullptr;
         fSelectedPad  = nullptr;
         EnterLeave(prevSelPad, prevSelObj);
         fSelected     = sobj;
         fSelectedPad  = spad;
         if (!fDoubleBuffer) FeedbackMode(kFALSE);
      }
      break;

   case kButton1Double:
      // triggered on the second button down within 350ms and within
      // 3x3 pixels of the first button down, button up finishes action

   case kButton1Down:
      // find pad in which input occurred
      pad = Pick(px, py, prevSelObj);
      if (!pad) return;

      gPad = pad;   // don't use cd() because we won't draw in pad
                    // we will only use its coordinate system

      if (fSelected) {
         FeedbackMode(kTRUE);   // to draw in rubberband mode
         fSelected->ExecuteEvent(event, px, py);

         RunAutoExec();
      }

      break;

   case kArrowKeyPress:
   case kArrowKeyRelease:
   case kButton1Motion:
   case kButton1ShiftMotion: //8 == kButton1Motion + shift modifier
      if (fSelected) {
         gPad = fSelectedPad;

         fSelected->ExecuteEvent(event, px, py);
         if (!IsWeb())
            gVirtualX->Update();
         if (fSelected && !fSelected->InheritsFrom(TAxis::Class())) {
            Bool_t resize = kFALSE;
            if (fSelected->InheritsFrom(TBox::Class()))
               resize = ((TBox*)fSelected)->IsBeingResized();
            if (fSelected->InheritsFrom(TVirtualPad::Class()))
               resize = ((TVirtualPad*)fSelected)->IsBeingResized();

            if ((!resize && TestBit(kMoveOpaque)) || (resize && TestBit(kResizeOpaque))) {
               gPad = fPadSave;
               Update();
               FeedbackMode(kTRUE);
            }
         }

         RunAutoExec();
      }

      break;

   case kButton1Up:

      if (fSelected) {
         gPad = fSelectedPad;

         fSelected->ExecuteEvent(event, px, py);

         RunAutoExec();

         if (fPadSave)
            gPad = fPadSave;
         else {
            gPad     = this;
            fPadSave = this;
         }

         Update();    // before calling update make sure gPad is reset
      }
      break;

//*-*----------------------------------------------------------------------

   case kButton2Down:
      // find pad in which input occurred
      pad = Pick(px, py, prevSelObj);
      if (!pad) return;

      gPad = pad;   // don't use cd() because we won't draw in pad
                    // we will only use its coordinate system

      FeedbackMode(kTRUE);

      if (fSelected) fSelected->Pop();  // pop object to foreground
      pad->cd();                        // and make its pad the current pad
      if (gDebug)
         printf("Current Pad: %s / %s\n", pad->GetName(), pad->GetTitle());

      // loop over all canvases to make sure that only one pad is highlighted
      {
         TIter next(gROOT->GetListOfCanvases());
         TCanvas *tc;
         while ((tc = (TCanvas *)next()))
            tc->Update();
      }

      //if (pad->GetGLDevice() != -1 && fSelected)
      //   fSelected->ExecuteEvent(event, px, py);

      break;   // don't want fPadSave->cd() to be executed at the end

   case kButton2Motion:
      //was empty!
   case kButton2Up:
      if (fSelected) {
         gPad = fSelectedPad;

         fSelected->ExecuteEvent(event, px, py);
         RunAutoExec();
      }
      break;

   case kButton2Double:
      break;

//*-*----------------------------------------------------------------------

   case kButton3Down:
      // popup context menu
      pad = Pick(px, py, prevSelObj);
      if (!pad) return;

      if (!fDoubleBuffer) FeedbackMode(kFALSE);

      if (fContextMenu && fSelected && !fSelected->TestBit(kNoContextMenu) &&
         !pad->TestBit(kNoContextMenu) && !TestBit(kNoContextMenu))
         fContextMenu->Popup(px, py, fSelected, this, pad);

      break;

   case kButton3Motion:
      break;

   case kButton3Up:
      if (!fDoubleBuffer) FeedbackMode(kTRUE);
      break;

   case kButton3Double:
      break;

   case kKeyPress:
      if (!fSelectedPad || !fSelected) return;
      gPad = fSelectedPad;   // don't use cd() because we won't draw in pad
                    // we will only use its coordinate system
      fSelected->ExecuteEvent(event, px, py);

      RunAutoExec();

      break;

   case kButton1Shift:
      // Try to select
      pad = Pick(px, py, prevSelObj);

      if (!pad) return;

      EnterLeave(prevSelPad, prevSelObj);

      gPad = pad;   // don't use cd() we will use the current
                    // canvas via the GetCanvas member and not via
                    // gPad->GetCanvas
      if (fSelected) {
         fSelected->ExecuteEvent(event, px, py);
         RunAutoExec();
      }
      break;

   case kWheelUp:
   case kWheelDown:
      pad = Pick(px, py, prevSelObj);
      if (!pad) return;

      gPad = pad;
      if (fSelected)
         fSelected->ExecuteEvent(event, px, py);
      break;

   default:
      break;
   }

   if (fPadSave && event != kButton2Down)
      fPadSave->cd();

   if (event != kMouseLeave) { // signal was already emitted for this event
      ProcessedEvent(event, px, py, fSelected);  // emit signal
      DrawEventStatus(event, px, py, fSelected);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Iconify canvas

void TCanvas::Iconify()
{
   if (fCanvasImp)
      fCanvasImp->Iconify();
}

////////////////////////////////////////////////////////////////////////////////
/// Is folder ?

Bool_t TCanvas::IsFolder() const
{
   return fgIsFolder;
}

////////////////////////////////////////////////////////////////////////////////
/// Is web canvas

Bool_t TCanvas::IsWeb() const
{
   return fCanvasImp ? fCanvasImp->IsWeb() : kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// List all pads.

void TCanvas::ls(Option_t *option) const
{
   TROOT::IndentLevel();
   std::cout <<"Canvas Name=" <<GetName()<<" Title="<<GetTitle()<<" Option="<<option<<std::endl;
   TROOT::IncreaseDirLevel();
   TPad::ls(option);
   TROOT::DecreaseDirLevel();
}

////////////////////////////////////////////////////////////////////////////////
/// Static function to build a default canvas.

TCanvas *TCanvas::MakeDefCanvas()
{
   auto cdef = GetNewCanvasName();

   auto c = new TCanvas(cdef.Data(), cdef.Data(), 1);

   ::Info("TCanvas::MakeDefCanvas"," created default TCanvas with name %s", cdef.Data());
   return c;
}

////////////////////////////////////////////////////////////////////////////////
/// Set option to move objects/pads in a canvas.
///
///  - set = 1 (default) graphics objects are moved in opaque mode
///  - set = 0 only the outline of objects is drawn when moving them
///
/// The option opaque produces the best effect. It requires however a
/// a reasonably fast workstation or response time.

void TCanvas::MoveOpaque(Int_t set)
{
   SetBit(kMoveOpaque,set);
}

////////////////////////////////////////////////////////////////////////////////
/// Paint canvas.

void TCanvas::Paint(Option_t *option)
{
   if (fCanvas)
      TPad::Paint(option);
}

////////////////////////////////////////////////////////////////////////////////
/// Prepare for pick, call TPad::Pick() and when selected object
/// is different from previous then emit Picked() signal.

TPad *TCanvas::Pick(Int_t px, Int_t py, TObject *prevSelObj)
{
   TObjLink *pickobj = nullptr;

   fSelected    = nullptr;
   fSelectedOpt = "";
   fSelectedPad = nullptr;

   TPad *pad = Pick(px, py, pickobj);
   if (!pad) return nullptr;

   if (!pickobj) {
      fSelected    = pad;
      fSelectedOpt = "";
   } else {
      if (!fSelected) {   // can be set via TCanvas::SetSelected()
         fSelected    = pickobj->GetObject();
         fSelectedOpt = pickobj->GetOption();
      }
   }
   fSelectedPad = pad;

   if (fSelected != prevSelObj)
      Picked(fSelectedPad, fSelected, fEvent);  // emit signal

   if ((fEvent == kButton1Down) || (fEvent == kButton2Down) || (fEvent == kButton3Down)) {
      if (fSelected && !fSelected->InheritsFrom(TView::Class())) {
         fClickSelected = fSelected;
         fClickSelectedPad = fSelectedPad;
         Selected(fSelectedPad, fSelected, fEvent);  // emit signal
         fSelectedX = px;
         fSelectedY = py;
      }
   }
   return pad;
}

////////////////////////////////////////////////////////////////////////////////
/// Emit Picked() signal.

void TCanvas::Picked(TPad *pad, TObject *obj, Int_t event)
{
   Longptr_t args[3];

   args[0] = (Longptr_t) pad;
   args[1] = (Longptr_t) obj;
   args[2] = event;

   Emit("Picked(TPad*,TObject*,Int_t)", args);
}

////////////////////////////////////////////////////////////////////////////////
/// Emit Highlighted() signal.
///
///  - pad is pointer to pad with highlighted histogram or graph
///  - obj is pointer to highlighted histogram or graph
///  - x is highlighted x bin for 1D histogram or highlighted x-th point for graph
///  - y is highlighted y bin for 2D histogram (for 1D histogram or graph not in use)

void TCanvas::Highlighted(TVirtualPad *pad, TObject *obj, Int_t x, Int_t y)
{
   Longptr_t args[4];

   args[0] = (Longptr_t) pad;
   args[1] = (Longptr_t) obj;
   args[2] = x;
   args[3] = y;

   Emit("Highlighted(TVirtualPad*,TObject*,Int_t,Int_t)", args);
}

////////////////////////////////////////////////////////////////////////////////
/// This is "simplification" for function TCanvas::Connect with Highlighted
/// signal for specific slot.
///
/// Slot has to be defined "UserFunction(TVirtualPad *pad, TObject *obj, Int_t x, Int_t y)"
/// all parameters of UserFunction are taken from TCanvas::Highlighted

void TCanvas::HighlightConnect(const char *slot)
{
   Connect("Highlighted(TVirtualPad*,TObject*,Int_t,Int_t)", nullptr, nullptr, slot);
}

////////////////////////////////////////////////////////////////////////////////
/// Emit Selected() signal.

void TCanvas::Selected(TVirtualPad *pad, TObject *obj, Int_t event)
{
   Longptr_t args[3];

   args[0] = (Longptr_t) pad;
   args[1] = (Longptr_t) obj;
   args[2] = event;

   Emit("Selected(TVirtualPad*,TObject*,Int_t)", args);
}

////////////////////////////////////////////////////////////////////////////////
/// Emit ProcessedEvent() signal.

void TCanvas::ProcessedEvent(Int_t event, Int_t x, Int_t y, TObject *obj)
{
   Longptr_t args[4];

   args[0] = event;
   args[1] = x;
   args[2] = y;
   args[3] = (Longptr_t) obj;

   Emit("ProcessedEvent(Int_t,Int_t,Int_t,TObject*)", args);
}

////////////////////////////////////////////////////////////////////////////////
/// Recompute canvas parameters following a X11 Resize.

void TCanvas::Resize(Option_t *)
{
   if (fCanvasID == -1) return;

   if (!gROOT->IsLineProcessing() && !gVirtualX->IsCmdThread()) {
      gInterpreter->Execute(this, IsA(), "Resize", "");
      return;
   }

   R__LOCKGUARD(gROOTMutex);

   TContext ctxt(this, kTRUE);

   if (!IsBatch() && !IsWeb()) {
      gVirtualX->SelectWindow(fCanvasID);      //select current canvas
      gVirtualX->ResizeWindow(fCanvasID);      //resize canvas and off-screen buffer

      // Get effective window parameters including menubar and borders
      fCanvasImp->GetWindowGeometry(fWindowTopX, fWindowTopY,
                                    fWindowWidth, fWindowHeight);

      // Get effective canvas parameters without borders
      Int_t dum1, dum2;
      gVirtualX->GetGeometry(fCanvasID, dum1, dum2, fCw, fCh);
   }

   if (fXsizeUser && fYsizeUser) {
      UInt_t nwh = fCh;
      UInt_t nww = fCw;
      Double_t rxy = fXsizeUser/fYsizeUser;
      if (rxy < 1) {
         UInt_t twh = UInt_t(Double_t(fCw)/rxy);
         if (twh > fCh)
            nww = UInt_t(Double_t(fCh)*rxy);
         else
            nwh = twh;
         if (nww > fCw) {
            nww = fCw; nwh = twh;
         }
         if (nwh > fCh) {
            nwh = fCh; nww = UInt_t(Double_t(fCh)/rxy);
         }
      } else {
         UInt_t twh = UInt_t(Double_t(fCw)*rxy);
         if (twh > fCh)
            nwh = UInt_t(Double_t(fCw)/rxy);
         else
            nww = twh;
         if (nww > fCw) {
            nww = fCw; nwh = twh;
         }
         if (nwh > fCh) {
            nwh = fCh; nww = UInt_t(Double_t(fCh)*rxy);
         }
      }
      fCw = nww;
      fCh = nwh;
   }

   if (fCw < fCh) {
      fYsizeReal = kDefaultCanvasSize;
      fXsizeReal = fYsizeReal*Double_t(fCw)/Double_t(fCh);
   }
   else {
      fXsizeReal = kDefaultCanvasSize;
      fYsizeReal = fXsizeReal*Double_t(fCh)/Double_t(fCw);
   }

//*-*- Loop on all pads to recompute conversion coefficients
   TPad::ResizePad();
}


////////////////////////////////////////////////////////////////////////////////
/// Raise canvas window

void TCanvas::RaiseWindow()
{
   if (fCanvasImp)
      fCanvasImp->RaiseWindow();
}

////////////////////////////////////////////////////////////////////////////////
/// Set option to resize objects/pads in a canvas.
///
///  - set = 1 (default) graphics objects are resized in opaque mode
///  - set = 0 only the outline of objects is drawn when resizing them
///
/// The option opaque produces the best effect. It requires however a
/// a reasonably fast workstation or response time.

void TCanvas::ResizeOpaque(Int_t set)
{
   SetBit(kResizeOpaque,set);
}

////////////////////////////////////////////////////////////////////////////////
/// Execute the list of TExecs in the current pad.

void TCanvas::RunAutoExec()
{
   if (!TestBit(kAutoExec))
      return;
   if (gPad)
      ((TPad*)gPad)->AutoExec();
}

////////////////////////////////////////////////////////////////////////////////
/// Save primitives in this canvas in C++ macro file with GUI.

void TCanvas::SavePrimitive(std::ostream &out, Option_t *option /*= ""*/)
{
   // Write canvas options (in $TROOT or $TStyle)
   out << "   gStyle->SetOptFit(" << gStyle->GetOptFit() << ");\n";
   out << "   gStyle->SetOptStat(" << gStyle->GetOptStat() << ");\n";
   out << "   gStyle->SetOptTitle(" << gStyle->GetOptTitle() << ");\n";

   if (gROOT->GetEditHistograms())
      out << "   gROOT->SetEditHistograms();\n";

   if (GetShowEventStatus())
      out << "   " << GetName() << "->ToggleEventStatus();\n";

   if (GetShowToolTips())
      out << "   " << GetName() << "->ToggleToolTips();\n";

   if (GetShowToolBar())
      out << "   " << GetName() << "->ToggleToolBar();\n";
   if (GetHighLightColor() != 5)
      out << "   " << GetName() << "->SetHighLightColor(" << TColor::SavePrimitiveColor(GetHighLightColor()) << ");\n";

   // Now recursively scan all pads of this canvas
   cd();
   TPad::SavePrimitive(out, option);
}

////////////////////////////////////////////////////////////////////////////////
/// Save primitives in this canvas as a C++ macro file.
/// This function loops on all the canvas primitives and for each primitive
/// calls the object SavePrimitive function.
/// When outputting floating point numbers, the default precision is 7 digits.
/// The precision can be changed (via system.rootrc) by changing the value
/// of the environment variable "Canvas.SavePrecision"

void TCanvas::SaveSource(const char *filename, Option_t * /*option*/)
{
   // Reset the ClassSaved status of all classes
   gROOT->ResetClassSaved();

   TString cname0 = GetName();
   Bool_t invalid = kFALSE;

   TString cname = cname0.Strip(TString::kBoth);
   if (cname.IsNull()) {
      invalid = kTRUE;
      cname = "c1";
   }

   //  if filename is given, open this file, otherwise create a file
   //  with a name equal to the canvasname.C
   TString fname;
   if (filename && *filename) {
      fname = filename;
   } else {
      fname = cname + ".C";
   }

   std::ofstream out;
   out.open(fname.Data(), std::ios::out);
   if (!out.good()) {
      Error("SaveSource", "Cannot open file: %s", fname.Data());
      return;
   }

   //set precision
   Int_t precision = gEnv->GetValue("Canvas.SavePrecision",7);
   out.precision(precision);

   //   Write macro header and date/time stamp
   TDatime t;
   Float_t cx = gStyle->GetScreenFactor();
   Int_t topx,topy;
   UInt_t w, h;
   if (!fCanvasImp) {
      Error("SaveSource", "Cannot open TCanvas");
      return;
   }
   UInt_t editorWidth = fCanvasImp->GetWindowGeometry(topx,topy,w,h);
   w = UInt_t((fWindowWidth - editorWidth)/cx);
   h = UInt_t((fWindowHeight)/cx);
   topx = GetWindowTopX();
   topy = GetWindowTopY();

   if (w == 0) {
      w = GetWw()+4; h = GetWh()+4;
      topx = 1;    topy = 1;
   }

   TString mname = fname;
   out << R"CODE(#ifdef __CLING__
#pragma cling optimize(0)
#endif
)CODE";
   Int_t p = mname.Last('.');
   Int_t s = mname.Last('/')+1;

   // A named macro is generated only if the function name is valid. If not, the
   // macro is unnamed.
   TString first(mname(s,s+1));
   if (!first.IsDigit()) out <<"void " << mname(s,p-s) << "()" << std::endl;

   out <<"{"<<std::endl;
   out <<"//=========Macro generated from canvas: "<<GetName()<<"/"<<GetTitle()<<std::endl;
   out <<"//=========  ("<<t.AsString()<<") by ROOT version "<<gROOT->GetVersion()<<std::endl;

   if (gStyle->GetCanvasPreferGL())
      out <<std::endl<<"   gStyle->SetCanvasPreferGL(kTRUE);"<<std::endl<<std::endl;

   //   Write canvas parameters (TDialogCanvas case)
   if (InheritsFrom(TDialogCanvas::Class())) {
      out << "   " << ClassName() << " *" << cname << " = new " << ClassName() << "(\"" << GetName() << "\", \""
          << TString(GetTitle()).ReplaceSpecialCppChars() << "\", " << w << ", " << h << ");\n";
   } else {
      //   Write canvas parameters (TCanvas case)
      out << "   TCanvas *" << cname << " = new TCanvas(\"" << GetName() << "\", \""
          << TString(GetTitle()).ReplaceSpecialCppChars() << "\", " << (HasMenuBar() ? topx : -topx) << ", " << topy
          << ", " << w << ", " << h << ");\n";
   }
   //   Write canvas options (in $TROOT or $TStyle)
   out << "   gStyle->SetOptFit(" << gStyle->GetOptFit() << ");\n";
   out << "   gStyle->SetOptStat(" << gStyle->GetOptStat() << ");\n";
   out << "   gStyle->SetOptTitle(" << gStyle->GetOptTitle() << ");\n";
   if (gROOT->GetEditHistograms())
      out << "   gROOT->SetEditHistograms();\n";
   if (GetShowEventStatus())
      out << "   " << GetName() << "->ToggleEventStatus();\n";
   if (GetShowToolTips())
      out << "   " << GetName() << "->ToggleToolTips();\n";
   if (GetHighLightColor() != 5)
      out << "   " << GetName() << "->SetHighLightColor(" << TColor::SavePrimitiveColor(GetHighLightColor()) << ");\n";

   TColor::SaveColorsPalette(out);

   //   Now recursively scan all pads of this canvas
   cd();
   if (invalid) fName = cname;
   TPad::SavePrimitive(out,"toplevel");

   //   Write canvas options related to pad editor
   out << "   " << GetName() << "->SetSelected(" << GetName() << ");\n";
   if (GetShowToolBar())
      out << "   " << GetName() << "->ToggleToolBar();\n";
   if (invalid)
      fName = cname0;

   out <<"}\n";
   out.close();
   Info("SaveSource","C++ Macro file: %s has been generated", fname.Data());

   // Reset the ClassSaved status of all classes
   gROOT->ResetClassSaved();
}

////////////////////////////////////////////////////////////////////////////////
/// Toggle batch mode. However, if the canvas is created without a window
/// then batch mode always stays set.

void TCanvas::SetBatch(Bool_t batch)
{
   if (gROOT->IsBatch() || IsWeb())
      fBatch = kTRUE;
   else
      fBatch = batch;
}

////////////////////////////////////////////////////////////////////////////////
/// Set Width and Height of canvas to ww and wh respectively. If ww and/or wh
/// are greater than the current canvas window a scroll bar is automatically
/// generated. Use this function to zoom in a canvas and navigate via
/// the scroll bars. The Width and Height in this method are different from those
/// given in the TCanvas constructors where these two dimension include the size
/// of the window decoration whereas they do not in this method.
/// When both ww==0 and wh==0, auto resize mode will be enabled again and
/// canvas drawing area will automatically fit available window size

void TCanvas::SetCanvasSize(UInt_t ww, UInt_t wh)
{
   if (fCanvasImp) {
      fCw = ww;
      fCh = wh;
      fCanvasImp->SetCanvasSize(ww, wh);
      TContext ctxt(this, kTRUE);
      ResizePad();
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Set cursor.

void TCanvas::SetCursor(ECursor cursor)
{
   if (!IsBatch() && !IsWeb())
      gVirtualX->SetCursor(fCanvasID, cursor);
}

////////////////////////////////////////////////////////////////////////////////
/// Set Double Buffer On/Off.

void TCanvas::SetDoubleBuffer(Int_t mode)
{
   if (IsBatch() || IsWeb())
      return;
   fDoubleBuffer = mode;
   gVirtualX->SetDoubleBuffer(fCanvasID, mode);

   // depending of the buffer mode set the drawing window to either
   // the canvas pixmap or to the canvas on-screen window
   if (fDoubleBuffer) {
      if (fPixmapID != -1) fPainter->SelectDrawable(fPixmapID);
   } else
      if (fCanvasID != -1) fPainter->SelectDrawable(fCanvasID);
}

////////////////////////////////////////////////////////////////////////////////
/// Fix canvas aspect ratio to current value if fixed is true.

void TCanvas::SetFixedAspectRatio(Bool_t fixed)
{
   if (fixed) {
      if (!fFixedAspectRatio) {
         if (fCh != 0)
            fAspectRatio = Double_t(fCw) / fCh;
         else {
            Error("SetAspectRatio", "cannot fix aspect ratio, height of canvas is 0");
            return;
         }
         fFixedAspectRatio = kTRUE;
      }
   } else {
      fFixedAspectRatio = kFALSE;
      fAspectRatio = 0;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// If isfolder=kTRUE, the canvas can be browsed like a folder
/// by default a canvas is not browsable.

void TCanvas::SetFolder(Bool_t isfolder)
{
   fgIsFolder = isfolder;
}

////////////////////////////////////////////////////////////////////////////////
/// Set canvas name. In case `name` is an empty string, a default name is set.
/// Canvas automatically marked as modified when SetName method called

void TCanvas::SetName(const char *name)
{
   fName = GetNewCanvasName(name);

   Modified();
}


////////////////////////////////////////////////////////////////////////////////
/// Function to resize a canvas so that the plot inside is shown in real aspect
/// ratio
///
/// \param[in] axis 1 for resizing horizontally (x-axis) in order to get real
///            aspect ratio, 2 for the resizing vertically (y-axis)
/// \return false if error is encountered, true otherwise
///
/// ~~~ {.cpp}
/// hpxpy->Draw();
/// c1->SetRealAspectRatio();
/// ~~~
///
///  - For defining the concept of real aspect ratio, it is assumed that x and y
///    axes are in same units, e.g. both in MeV or both in ns.
///  - You can resize either the width of the canvas or the height, but not both
///    at the same time
///  - Call this function AFTER drawing AND zooming (SetUserRange) your TGraph or
///    Histogram, otherwise it cannot infer your actual axes lengths
///  - This function ensures that the TFrame has a real aspect ratio, this does not
///    mean that the full pad (i.e. the canvas or png output) including margins has
///    exactly the same ratio
///  - This function does not work if the canvas is divided in several subpads

bool TCanvas::SetRealAspectRatio(const Int_t axis)
{
   Update();

   //Get how many pixels are occupied by the canvas
   Int_t npx = GetWw();
   Int_t npy = GetWh();

   //Get x-y coordinates at the edges of the canvas (extrapolating outside the axes, NOT at the edges of the histogram)
   Double_t x1 = GetX1();
   Double_t y1 = GetY1();
   Double_t x2 = GetX2();
   Double_t y2 = GetY2();

   //Get the length of extrapolated x and y axes
   Double_t xlength2 = x2 - x1;
   Double_t ylength2 = y2 - y1;
   Double_t ratio2   = xlength2/ylength2;

   //Now get the number of pixels including the canvas borders
   Int_t bnpx = GetWindowWidth();
   Int_t bnpy = GetWindowHeight();

   if (axis==1) {
      SetCanvasSize(TMath::Nint(npy*ratio2), npy);
      SetWindowSize((bnpx-npx)+TMath::Nint(npy*ratio2), bnpy);
   } else if (axis==2) {
      SetCanvasSize(npx, TMath::Nint(npx/ratio2));
      SetWindowSize(bnpx, (bnpy-npy)+TMath::Nint(npx/ratio2));
   } else {
      Error("SetRealAspectRatio", "axis value %d is neither 1 (resize along x-axis) nor 2 (resize along y-axis).",axis);
      return false;
   }

   //Check now that resizing has worked

   Update();

   //Get how many pixels are occupied by the canvas
   npx = GetWw();
   npy = GetWh();

   //Get x-y coordinates at the edges of the canvas (extrapolating outside the axes,
   //NOT at the edges of the histogram)
   x1 = GetX1();
   y1 = GetY1();
   x2 = GetX2();
   y2 = GetY2();

   //Get the length of extrapolated x and y axes
   xlength2 = x2 - x1;
   ylength2 = y2 - y1;
   ratio2 = xlength2/ylength2;

   //Check accuracy +/-1 pixel due to rounding
   if (abs(TMath::Nint(npy*ratio2) - npx)<2) {
      return true;
   } else {
      Error("SetRealAspectRatio", "Resizing failed.");
      return false;
   }
}


////////////////////////////////////////////////////////////////////////////////
/// Set selected canvas.

void TCanvas::SetSelected(TObject *obj)
{
   fSelected = obj;
   if (obj) obj->SetBit(kMustCleanup);
}

////////////////////////////////////////////////////////////////////////////////
/// Set canvas title.

void TCanvas::SetTitle(const char *title)
{
   fTitle = title;
   if (fCanvasImp) fCanvasImp->SetWindowTitle(title);
}

////////////////////////////////////////////////////////////////////////////////
/// Set canvas window position

void TCanvas::SetWindowPosition(Int_t x, Int_t y)
{
   if (fCanvasImp)
      fCanvasImp->SetWindowPosition(x, y);
}

////////////////////////////////////////////////////////////////////////////////
/// Set canvas window size

void TCanvas::SetWindowSize(UInt_t ww, UInt_t wh)
{
   if (fBatch && !IsWeb())
      SetCanvasSize((ww + fCw) / 2, (wh + fCh) / 2);
   else if (fCanvasImp)
      fCanvasImp->SetWindowSize(ww, wh);
}

////////////////////////////////////////////////////////////////////////////////
/// Set canvas implementation
/// If web-based implementation provided, some internal fields also initialized

void TCanvas::SetCanvasImp(TCanvasImp *imp)
{
   Bool_t was_web = IsWeb();

   fCanvasImp = imp;

   if (!was_web && IsWeb()) {
      fCanvasID = fCanvasImp->InitWindow();
      fPixmapID = 0;
      fMother = this;
      if (!fCw) fCw = 800;
      if (!fCh) fCh = 600;
   } else if (was_web && !imp) {
      fCanvasID = -1;
      fPixmapID = -1;
      fMother = nullptr;
      fCw = fCh = 0;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Set the canvas scale in centimeters.
///
/// This information is used by PostScript to set the page size.
///
/// \param[in] xsize   size of the canvas in centimeters along X
/// \param[in] ysize   size of the canvas in centimeters along Y
///
///  if xsize and ysize are not equal to 0, then the scale factors will
///  be computed to keep the ratio ysize/xsize independently of the canvas
///  size (parts of the physical canvas will be unused).
///
///  if xsize = 0 and ysize is not zero, then xsize will be computed
///     to fit to the current canvas scale. If the canvas is resized,
///     a new value for xsize will be recomputed. In this case the aspect
///     ratio is not preserved.
///
///  if both xsize = 0 and ysize = 0, then the scaling is automatic.
///  the largest dimension will be allocated a size of 20 centimeters.

void TCanvas::Size(Float_t xsize, Float_t ysize)
{
   fXsizeUser = xsize;
   fYsizeUser = ysize;

   Resize();
}

////////////////////////////////////////////////////////////////////////////////
/// Show canvas

void TCanvas::Show()
{
   if (fCanvasImp)
      fCanvasImp->Show();
}

////////////////////////////////////////////////////////////////////////////////
/// Stream a class object.

void TCanvas::Streamer(TBuffer &b)
{
   UInt_t R__s, R__c;
   if (b.IsReading()) {
      Version_t v = b.ReadVersion(&R__s, &R__c);
      gPad    = this;
      fCanvas = this;
      if (v>7) b.ClassBegin(TCanvas::IsA());
      if (v>7) b.ClassMember("TPad");
      TPad::Streamer(b);
      gPad    = this;
      //restore the colors
      auto colors = dynamic_cast<TObjArray *>(fPrimitives->FindObject("ListOfColors"));
      if (colors) {
         auto root_colors = dynamic_cast<TObjArray *>(gROOT->GetListOfColors());

         TIter next(colors);
         while (auto colold = static_cast<TColor *>(next())) {
            Int_t cn = colold->GetNumber();
            TColor *colcur = gROOT->GetColor(cn);
            if (colcur && (colcur->IsA() == TColor::Class()) && (colold->IsA() == TColor::Class())) {
               colcur->SetName(colold->GetName());
               colcur->SetRGB(colold->GetRed(), colold->GetGreen(), colold->GetBlue());
               colcur->SetAlpha(colold->GetAlpha());
            } else {
               if (colcur) {
                  if (root_colors) root_colors->Remove(colcur);
                  delete colcur;
               }
               colors->Remove(colold);
               if (root_colors) {
                  if (colcur) {
                     root_colors->AddAtAndExpand(colold, cn);
                  }
                  else {
                     // Copy to current session
                     // do not use copy constructor which does not update highest color index
                     [[maybe_unused]] TColor* const colnew = new TColor(cn, colold->GetRed(), colold->GetGreen(), colold->GetBlue(), colold->GetName(), colold->GetAlpha());
                     delete colold;
                     // No need to delete colnew, as the constructor adds it to global list of colors
                     assert(root_colors->At(cn) == colnew);
                  }
               }
            }
         }
         //restore the palette if needed
         auto palette = dynamic_cast<TObjArray *>(fPrimitives->FindObject("CurrentColorPalette"));
         if (palette) {
            TIter nextcol(palette);
            Int_t number = palette->GetEntries();
            TArrayI palcolors(number);
            Int_t i = 0;
            while (auto col = static_cast<TColor *>(nextcol()))
               palcolors[i++] = col->GetNumber();
            gStyle->SetPalette(number, palcolors.GetArray());
            fPrimitives->Remove(palette);
            delete palette;
         }
         fPrimitives->Remove(colors);
         colors->Delete();
         delete colors;
      }

      if (v>7) b.ClassMember("fDISPLAY","TString");
      fDISPLAY.Streamer(b);
      if (v>7) b.ClassMember("fDoubleBuffer", "Int_t");
      b >> fDoubleBuffer;
      if (v>7) b.ClassMember("fRetained", "Bool_t");
      b >> fRetained;
      if (v>7) b.ClassMember("fXsizeUser", "Size_t");
      b >> fXsizeUser;
      if (v>7) b.ClassMember("fYsizeUser", "Size_t");
      b >> fYsizeUser;
      if (v>7) b.ClassMember("fXsizeReal", "Size_t");
      b >> fXsizeReal;
      if (v>7) b.ClassMember("fYsizeReal", "Size_t");
      b >> fYsizeReal;
      fCanvasID = -1;
      if (v>7) b.ClassMember("fWindowTopX", "Int_t");
      b >> fWindowTopX;
      if (v>7) b.ClassMember("fWindowTopY", "Int_t");
      b >> fWindowTopY;
      if (v > 2) {
         if (v>7) b.ClassMember("fWindowWidth", "UInt_t");
         b >> fWindowWidth;
         if (v>7) b.ClassMember("fWindowHeight", "UInt_t");
         b >> fWindowHeight;
      }
      if (v>7) b.ClassMember("fCw", "UInt_t");
      b >> fCw;
      if (v>7) b.ClassMember("fCh", "UInt_t");
      b >> fCh;
      if (v <= 2) {
         fWindowWidth  = fCw;
         fWindowHeight = fCh;
      }
      if (v>7) b.ClassMember("fCatt", "TAttCanvas");
      fCatt.Streamer(b);
      Bool_t dummy;
      if (v>7) b.ClassMember("kMoveOpaque", "Bool_t");
      b >> dummy; if (dummy) MoveOpaque(1);
      if (v>7) b.ClassMember("kResizeOpaque", "Bool_t");
      b >> dummy; if (dummy) ResizeOpaque(1);
      if (v>7) b.ClassMember("fHighLightColor", "Color_t");
      b >> fHighLightColor;
      if (v>7) b.ClassMember("fBatch", "Bool_t");
      b >> dummy; //was fBatch
      if (v < 2) return;
      if (v>7) b.ClassMember("kShowEventStatus", "Bool_t");
      b >> dummy; if (dummy) SetBit(kShowEventStatus);

      if (v > 3) {
         if (v>7) b.ClassMember("kAutoExec", "Bool_t");
         b >> dummy; if (dummy) SetBit(kAutoExec);
      }
      if (v>7) b.ClassMember("kMenuBar", "Bool_t");
      b >> dummy; if (dummy) SetBit(kMenuBar);
      fBatch = gROOT->IsBatch();
      if (v>7) b.ClassEnd(TCanvas::IsA());
      b.CheckByteCount(R__s, R__c, TCanvas::IsA());
   } else {
      //save list of colors
      //we must protect the case when two or more canvases are saved
      //in the same buffer. If the list of colors has already been saved
      //in the buffer, do not add the list of colors to the list of primitives.
      TObjArray *colors = nullptr;
      TObjArray *CurrentColorPalette = nullptr;
      if (TColor::DefinedColors()) {
         if (!b.CheckObject(gROOT->GetListOfColors(),TObjArray::Class())) {
            colors = (TObjArray*)gROOT->GetListOfColors();
            fPrimitives->Add(colors);
         }
         //save the current palette
         TArrayI pal = TColor::GetPalette();
         Int_t palsize = pal.GetSize();
         CurrentColorPalette = new TObjArray();
         CurrentColorPalette->SetName("CurrentColorPalette");
         for (Int_t i=0; i<palsize; i++) CurrentColorPalette->Add(gROOT->GetColor(pal[i]));
         fPrimitives->Add(CurrentColorPalette);
      }

      R__c = b.WriteVersion(TCanvas::IsA(), kTRUE);
      b.ClassBegin(TCanvas::IsA());
      b.ClassMember("TPad");
      TPad::Streamer(b);
      if (colors) fPrimitives->Remove(colors);
      if (CurrentColorPalette) { fPrimitives->Remove(CurrentColorPalette); delete CurrentColorPalette; }
      b.ClassMember("fDISPLAY","TString");
      fDISPLAY.Streamer(b);
      b.ClassMember("fDoubleBuffer", "Int_t");
      b << fDoubleBuffer;
      b.ClassMember("fRetained", "Bool_t");
      b << fRetained;
      b.ClassMember("fXsizeUser", "Size_t");
      b << fXsizeUser;
      b.ClassMember("fYsizeUser", "Size_t");
      b << fYsizeUser;
      b.ClassMember("fXsizeReal", "Size_t");
      b << fXsizeReal;
      b.ClassMember("fYsizeReal", "Size_t");
      b << fYsizeReal;
      UInt_t w   = fWindowWidth,  h    = fWindowHeight;
      Int_t topx = fWindowTopX,   topy = fWindowTopY;
      UInt_t editorWidth = 0;
      if(fCanvasImp) editorWidth = fCanvasImp->GetWindowGeometry(topx,topy,w,h);
      b.ClassMember("fWindowTopX", "Int_t");
      b << topx;
      b.ClassMember("fWindowTopY", "Int_t");
      b << topy;
      b.ClassMember("fWindowWidth", "UInt_t");
      b << (UInt_t)(w-editorWidth);
      b.ClassMember("fWindowHeight", "UInt_t");
      b << h;
      b.ClassMember("fCw", "UInt_t");
      b << fCw;
      b.ClassMember("fCh", "UInt_t");
      b << fCh;
      b.ClassMember("fCatt", "TAttCanvas");
      fCatt.Streamer(b);
      b.ClassMember("kMoveOpaque", "Bool_t");
      b << TestBit(kMoveOpaque);      //please remove in ROOT version 6
      b.ClassMember("kResizeOpaque", "Bool_t");
      b << TestBit(kResizeOpaque);    //please remove in ROOT version 6
      b.ClassMember("fHighLightColor", "Color_t");
      b << fHighLightColor;
      b.ClassMember("fBatch", "Bool_t");
      b << fBatch;                    //please remove in ROOT version 6
      b.ClassMember("kShowEventStatus", "Bool_t");
      b << TestBit(kShowEventStatus); //please remove in ROOT version 6
      b.ClassMember("kAutoExec", "Bool_t");
      b << TestBit(kAutoExec);        //please remove in ROOT version 6
      b.ClassMember("kMenuBar", "Bool_t");
      b << TestBit(kMenuBar);         //please remove in ROOT version 6
      b.ClassEnd(TCanvas::IsA());
      b.SetByteCount(R__c, kTRUE);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Toggle pad auto execution of list of TExecs.

void TCanvas::ToggleAutoExec()
{
   Bool_t autoExec = TestBit(kAutoExec);
   SetBit(kAutoExec,!autoExec);
}

////////////////////////////////////////////////////////////////////////////////
/// Toggle event statusbar.

void TCanvas::ToggleEventStatus()
{
   Bool_t showEventStatus = !TestBit(kShowEventStatus);
   SetBit(kShowEventStatus,showEventStatus);

   if (fCanvasImp) fCanvasImp->ShowStatusBar(showEventStatus);
}

////////////////////////////////////////////////////////////////////////////////
/// Toggle toolbar.

void TCanvas::ToggleToolBar()
{
   Bool_t showToolBar = !TestBit(kShowToolBar);
   SetBit(kShowToolBar,showToolBar);

   if (fCanvasImp) fCanvasImp->ShowToolBar(showToolBar);
}

////////////////////////////////////////////////////////////////////////////////
/// Toggle editor.

void TCanvas::ToggleEditor()
{
   Bool_t showEditor = !TestBit(kShowEditor);
   SetBit(kShowEditor,showEditor);

   if (fCanvasImp) fCanvasImp->ShowEditor(showEditor);
}

////////////////////////////////////////////////////////////////////////////////
/// Toggle tooltip display.

void TCanvas::ToggleToolTips()
{
   Bool_t showToolTips = !TestBit(kShowToolTips);
   SetBit(kShowToolTips, showToolTips);

   if (fCanvasImp) fCanvasImp->ShowToolTips(showToolTips);
}


////////////////////////////////////////////////////////////////////////////////
/// Static function returning "true" if transparency is supported.

Bool_t TCanvas::SupportAlpha()
{
   return gPad && (gVirtualX->InheritsFrom("TGQuartz") ||
                   (gPad->GetGLDevice() != -1) || (gPad->GetCanvas() && gPad->GetCanvas()->IsWeb()));
}

extern "C" void ROOT_TCanvas_Update(void* TheCanvas) {
   static_cast<TCanvas*>(TheCanvas)->Update();
}

////////////////////////////////////////////////////////////////////////////////
/// Update canvas pad buffers.

void TCanvas::Update()
{
   fUpdated = kTRUE;

   if (fUpdating) return;

   if (fPixmapID == -1) return;

   static const union CastFromFuncToVoidPtr_t {
      CastFromFuncToVoidPtr_t(): fFuncPtr(ROOT_TCanvas_Update) {}
      void (*fFuncPtr)(void*);
      void* fVoidPtr;
   } castFromFuncToVoidPtr;

   if (gThreadXAR) {
      void *arr[3];
      arr[1] = this;
      arr[2] = castFromFuncToVoidPtr.fVoidPtr;
      if ((*gThreadXAR)("CUPD", 3, arr, nullptr)) return;
   }

   if (!fCanvasImp) return;

   if (!gVirtualX->IsCmdThread()) {
      // Why do we have this (which uses the interpreter to funnel the Update()
      // through the main thread) when the gThreadXAR mechanism does seemingly
      // the same?
      gInterpreter->Execute(this, IsA(), "Update", "");
      return;
   }

   R__LOCKGUARD(gROOTMutex);

   fUpdating = kTRUE;

   if (!fCanvasImp->PerformUpdate(kFALSE)) {

      if (!IsBatch()) FeedbackMode(kFALSE); // Goto double buffer mode

      if (!UseGL() || fGLDevice == -1) PaintModified(); // Repaint all modified pad's

      Flush(); // Copy all pad pixmaps to the screen

      SetCursor(kCross);
   }

   fUpdating = kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Asynchronous pad update.
/// In case of web-based canvas triggers update of the canvas on the client side,
/// but does not wait that real update is completed. Avoids blocking of caller thread.
/// Have to be used if called from other web-based widget to avoid logical dead-locks.
/// In case of normal canvas just canvas->Update() is performed.

void TCanvas::UpdateAsync()
{
   fUpdated = kTRUE;

   if (IsWeb())
      fCanvasImp->PerformUpdate(kTRUE);
   else
      Update();
}

////////////////////////////////////////////////////////////////////////////////
/// Used by friend class TCanvasImp.

void TCanvas::DisconnectWidget()
{
   fCanvasID    = 0;
   fContextMenu = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// Check whether this canvas is to be drawn in grayscale mode.

Bool_t TCanvas::IsGrayscale()
{
   return TestBit(kIsGrayscale);
}

////////////////////////////////////////////////////////////////////////////////
/// Set whether this canvas should be painted in grayscale, and re-paint
/// it if necessary.

void TCanvas::SetGrayscale(Bool_t set /*= kTRUE*/)
{
   if (IsGrayscale() == set) return;
   SetBit(kIsGrayscale, set);
   if (IsWeb()) {
      Modified();
      UpdateAsync();
   } else {
      Paint(); // update canvas and all sub-pads, unconditionally!
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Probably, TPadPainter must be placed in a separate ROOT module -
/// "padpainter" (the same as "histpainter"). But now, it's directly in a
/// gpad dir, so, in case of default painter, no *.so should be loaded,
/// no need in plugin managers.
/// May change in future.

void TCanvas::CreatePainter()
{
   //Even for batch mode painter is still required, just to delegate
   //some calls to batch "virtual X".
   if (!UseGL() || fBatch) {
      fPainter = nullptr;
      if (fCanvasImp) fPainter = fCanvasImp->CreatePadPainter();
      if (!fPainter) fPainter = new TPadPainter; // Do not need plugin manager for this!
   } else {
      fPainter = TVirtualPadPainter::PadPainter("gl");
      if (!fPainter) {
         Error("CreatePainter", "GL Painter creation failed! Will use default!");
         fPainter = new TPadPainter;
         fUseGL = kFALSE;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Access and (probably) creation of pad painter.

TVirtualPadPainter *TCanvas::GetCanvasPainter()
{
   if (!fPainter) CreatePainter();
   return fPainter;
}


////////////////////////////////////////////////////////////////////////////////
///assert on IsBatch() == false?

void TCanvas::DeleteCanvasPainter()
{
   if (fGLDevice != -1) {
      //fPainter has a font manager.
      //Font manager will delete textures.
      //If context is wrong (we can have several canvases) -
      //wrong texture will be deleted, damaging some of our fonts.
      gGLManager->MakeCurrent(fGLDevice);
   }

   SafeDelete(fPainter);

   if (fGLDevice != -1) {
      gGLManager->DeleteGLContext(fGLDevice);//?
      fGLDevice = -1;
   }
}


////////////////////////////////////////////////////////////////////////////////
/// Save provided pads/canvases into the image file(s)
/// Filename can include printf argument for image number - like "image%03d.png".
/// In this case images: "image000.png", "image001.png", "image002.png" will be created.
/// If pattern is not provided - it will be automatically inserted before extension except PDF and ROOT files.
/// In last case PDF or ROOT file will contain all pads.
/// Parameter option only used when output into PDF/PS files
/// If TCanvas::SaveAll() called without arguments - all existing canvases will be stored in allcanvases.pdf file.

Bool_t TCanvas::SaveAll(const std::vector<TPad *> &pads, const char *filename, Option_t *option)
{
   if (pads.empty()) {
      std::vector<TPad *> canvases;
      TIter iter(gROOT->GetListOfCanvases());
      while (auto c = dynamic_cast<TCanvas *>(iter()))
         canvases.emplace_back(c);

      if (canvases.empty()) {
         ::Warning("TCanvas::SaveAll", "No pads are provided");
         return kFALSE;
      }

      return TCanvas::SaveAll(canvases, filename && *filename ? filename : "allcanvases.pdf", option);
   }

   TString fname = filename, ext;

   Bool_t hasArg = fname.Contains("%");

   if ((pads.size() == 1) && !hasArg) {
      pads[0]->SaveAs(filename);
      return kTRUE;
   }

   auto p = fname.Last('.');
   if (p != kNPOS) {
      ext = fname(p+1, fname.Length() - p - 1);
      ext.ToLower();
   } else {
      p = fname.Length();
      ::Warning("TCanvas::SaveAll", "Extension is not provided in file name %s, append .png", filename);
      fname.Append(".png");
      ext = "png";
   }

   if (ext != "pdf" && ext != "ps" && ext != "root" && ext != "xml" && !hasArg) {
      fname.Insert(p, "%d");
      hasArg = kTRUE;
   }

   static std::vector<TString> webExtensions = { "png", "json", "svg", "pdf", "jpg", "jpeg", "webp" };

   if (gROOT->IsWebDisplay()) {
      Bool_t isSupported = kFALSE;
      for (auto &wext : webExtensions) {
         if ((isSupported = (wext == ext)))
            break;
      }

      if (isSupported) {
         auto cmd = TString::Format("TWebCanvas::ProduceImages( *((std::vector<TPad *> *) 0x%zx), \"%s\")", (size_t) &pads, fname.Data());

         return (Bool_t) gROOT->ProcessLine(cmd);
      }

      if ((ext != "root") && (ext != "xml"))
         ::Warning("TCanvas::SaveAll", "TWebCanvas does not support image format %s - using normal ROOT functionality", fname.Data());
   }

   // store all pads into single PDF/PS files
   if (ext == "pdf" || ext == "ps") {
      for (unsigned n = 0; n < pads.size(); ++n) {
         TString fn = fname;
         if (hasArg)
            fn = TString::Format(fname.Data(), (int) n);
         else if (n == 0)
            fn.Append("(");
         else if (n == pads.size() - 1)
            fn.Append(")");

         pads[n]->Print(fn.Data(), option && *option ? option : ext.Data());
      }

      return kTRUE;
   }

   // store all pads in single ROOT file
   if ((ext == "root" || ext == "xml") && !hasArg) {
      TString fn = fname;
      gSystem->ExpandPathName(fn);
      if (fn.IsNull()) {
         fn.Form("%s.%s", pads[0]->GetName(), ext.Data());
         ::Warning("TCanvas::SaveAll", "Filename %s cannot be used - use pad name %s as pattern", fname.Data(), fn.Data());
      }

      Bool_t isError = kFALSE;

      if (!gDirectory) {
         isError = kTRUE;
      } else {
         for (unsigned n = 0; n < pads.size(); ++n) {
            auto sz = gDirectory->SaveObjectAs(pads[n], fn.Data(), n==0 ? "q" : "qa");
            if (!sz) { isError = kTRUE; break; }
         }
      }

      if (isError)
         ::Error("TCanvas::SaveAll", "Failure to store pads in %s", filename);
      else
         ::Info("TCanvas::SaveAll", "ROOT file %s has been created", filename);

      return !isError;
   }

   for (unsigned n = 0; n < pads.size(); ++n) {
      TString fn = TString::Format(fname.Data(), (int) n);
      gSystem->ExpandPathName(fn);
      if (fn.IsNull()) {
         fn.Form("%s%d.%s", pads[n]->GetName(), (int) n, ext.Data());
         ::Warning("TCanvas::SaveAll", "Filename %s cannot be used - use pad name %s as pattern", fname.Data(), fn.Data());
      }

      pads[n]->SaveAs(fn.Data());
   }

   return kTRUE;

}
