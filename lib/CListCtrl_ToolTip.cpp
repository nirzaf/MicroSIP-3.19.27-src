#include "stdafx.h"
#include "CListCtrl_ToolTip.h"

void CListCtrl_ToolTip::CellHitTest(const CPoint& pt, int& nRow, int& nCol) const
{
	nRow = -1;
	nCol = -1;

	LVHITTESTINFO lvhti = {0};
	lvhti.pt = pt;
	nRow = ListView_SubItemHitTest(m_hWnd, &lvhti);	// SubItemHitTest is non-const
	nCol = lvhti.iSubItem;
	if (!(lvhti.flags & LVHT_ONITEMLABEL))
		nRow = -1;
}

BOOL CListCtrl_ToolTip::OnToolNeedText(UINT id, NMHDR* pNMHDR, LRESULT* pResult)
{
	CPoint pt(GetMessagePos());
	ScreenToClient(&pt);

	int nRow, nCol;
	CellHitTest(pt, nRow, nCol);

	CString tooltip = GetToolTipText(nRow, nCol);
	if (tooltip.IsEmpty())
		return FALSE;

	// Non-unicode applications can receive requests for tooltip-text in unicode
	TOOLTIPTEXTA* pTTTA = (TOOLTIPTEXTA*)pNMHDR;
	TOOLTIPTEXTW* pTTTW = (TOOLTIPTEXTW*)pNMHDR;
#ifndef _UNICODE
	if (pNMHDR->code == TTN_NEEDTEXTA)
		lstrcpyn(pTTTA->szText, static_cast<LPCTSTR>(tooltip), sizeof(pTTTA->szText));
	else
		_mbstowcs(pTTTW->szText, static_cast<LPCTSTR>(tooltip), sizeof(pTTTW->szText)/sizeof(WCHAR));
#else
	if (pNMHDR->code == TTN_NEEDTEXTA)
		_wcstombsz(pTTTA->szText, static_cast<LPCTSTR>(tooltip), sizeof(pTTTA->szText));
	else
		lstrcpyn(pTTTW->szText, static_cast<LPCTSTR>(tooltip), sizeof(pTTTW->szText)/sizeof(WCHAR));
#endif
	// If wanting to display a tooltip which is longer than 80 characters,
	// then one must allocate the needed text-buffer instead of using szText,
	// and point the TOOLTIPTEXT::lpszText to this text-buffer.
	// When doing this, then one is required to release this text-buffer again
	return TRUE;
}

bool CListCtrl_ToolTip::ShowToolTip(const CPoint& pt) const
{
	// Lookup up the cell
	int nRow, nCol;
	CellHitTest(pt, nRow, nCol);

	if (nRow!=-1 && nCol!=-1)
		return true;
	else
		return false;
}

CString CListCtrl_ToolTip::GetToolTipText(int nRow, int nCol)
{
	if (nRow!=-1 && nCol!=-1)
		return GetItemText(nRow, nCol);	// Cell-ToolTip
	else
		return CString("");
}
