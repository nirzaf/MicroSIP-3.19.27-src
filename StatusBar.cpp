// StatusBar.cpp : implementation file   
//   
#include "StdAfx.h"   
#include "StatusBar.h"   
#include "global.h"

// StatusBar   
IMPLEMENT_DYNAMIC(StatusBar, CStatusBar)
StatusBar::StatusBar()   
{   
	CStatusBar::CStatusBar();
}
   
StatusBar::~StatusBar()   
{   
}

BEGIN_MESSAGE_MAP(StatusBar, CStatusBar)
    //{{AFX_MSG_MAP(StatusBar)   
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
    //}}AFX_MSG_MAP   
END_MESSAGE_MAP()

void StatusBar::OnLButtonUp(UINT nFlags, CPoint point)
{
}

void StatusBar::OnMouseMove(UINT nFlags, CPoint point)
{
}
