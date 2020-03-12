#pragma once 

#include "const.h"

class StatusBar : public CStatusBar
{ 
	DECLARE_DYNAMIC(StatusBar) 
public: 
	StatusBar();
	virtual ~StatusBar();
protected: 
	DECLARE_MESSAGE_MAP()
public: 
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
};
