#pragma once

#include "CListCtrl_ToolTip.h"

class CListCtrl_LabelTip : public CListCtrl_ToolTip
{
public:
	virtual void PreSubclassWindow()
	{
		// Display entire cell-text as tooltip,
		// when cell-text is only partially visible
		CListCtrl_ToolTip::PreSubclassWindow();
#if (_WIN32_IE >= 0x0500)
		SetExtendedStyle(LVS_EX_LABELTIP | GetExtendedStyle());
#endif
	}
};