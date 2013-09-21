//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//
// copyright            : (C) 2008 by Eran Ifrah
// file name            : cscopetab.h
//
// -------------------------------------------------------------------------
// A
//              _____           _      _     _ _
//             /  __ \         | |    | |   (_) |
//             | /  \/ ___   __| | ___| |    _| |_ ___
//             | |    / _ \ / _  |/ _ \ |   | | __/ _ )
//             | \__/\ (_) | (_| |  __/ |___| | ||  __/
//              \____/\___/ \__,_|\___\_____/_|\__\___|
//
//                                                  F i l e
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
#ifndef __cscopetab__
#define __cscopetab__

/**
@file
Subclass of CscopeTabBase, which is generated by wxFormBuilder.
*/

#include "globals.h"
#include "CscopeTabBase.h"
#include "cscopeentrydata.h"
#include "cscopedbbuilderthread.h"
#include "bitmap_loader.h"

class IManager;
class CscopeTabClientData : public wxClientData
{
    CscopeEntryData _entry;

public:
    CscopeTabClientData(const CscopeEntryData& entry) : _entry(entry) {}
    ~CscopeTabClientData() {}

    //Setters
    void SetEntry(const CscopeEntryData& _entry) {
        this->_entry = _entry;
    }
    //Getters
    const CscopeEntryData& GetEntry() const {
        return _entry;
    }
};


/** Implementing CscopeTabBase */
class CscopeTab : public CscopeTabBase
{
    CScopeResultTable_t *m_table;
    IManager *         m_mgr;
    wxString           m_findWhat;
    StringManager      m_stringManager;
    wxFont             m_font;
    BitmapLoader::BitmapMap_t m_bitmaps;

protected:
    virtual void OnItemSelected(wxDataViewEvent& event);
    wxBitmap GetBitmap(const wxString &filename) const;

protected:
    // Handlers for CscopeTabBase events.
    void OnItemActivated( wxDataViewEvent& event );
    void DoItemActivated( const wxDataViewItem& item );
    void FreeTable();
    void OnClearResults(wxCommandEvent &e);
    void OnClearResultsUI(wxUpdateUIEvent &e);
    void OnChangeSearchScope(wxCommandEvent &e);
    void OnCreateDB(wxCommandEvent &e);
    void OnWorkspaceOpenUI(wxUpdateUIEvent &e);
    void OnThemeChanged(wxCommandEvent &e);

public:
    /** Constructor */
    CscopeTab( wxWindow* parent, IManager *mgr );
    virtual ~CscopeTab();

    void BuildTable(CScopeResultTable_t *table);
    void Clear();
    void SetMessage(const wxString &msg, int percent);


    void SetFindWhat(const wxString& findWhat) {
        this->m_findWhat = findWhat;
    }
    const wxString& GetFindWhat() const {
        return m_findWhat;
    }
};

#endif // __cscopetab__
