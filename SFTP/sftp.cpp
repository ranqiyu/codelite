//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//
// copyright            : (C) 2014 Eran Ifrah
// file name            : sftp.cpp
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

#include "sftp.h"
#include <wx/xrc/xmlres.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include "SSHAccountManagerDlg.h"
#include "sftp_settings.h"
#include "SFTPBrowserDlg.h"
#include "event_notifier.h"
#include "sftp_workspace_settings.h"
#include "sftp_worker_thread.h"
#include <wx/xrc/xmlres.h>
#include "cl_command_event.h"
#include "json_node.h"
#include "SFTPStatusPage.h"
#include "SFTPTreeView.h"
#include <wx/log.h>
#include "sftp_settings.h"
#include "SFTPSettingsDialog.h"
#include "fileutils.h"
#include "dockablepane.h"
#include "detachedpanesinfo.h"

static SFTP* thePlugin = NULL;
const wxEventType wxEVT_SFTP_OPEN_SSH_ACCOUNT_MANAGER = ::wxNewEventType();
const wxEventType wxEVT_SFTP_SETTINGS = ::wxNewEventType();
const wxEventType wxEVT_SFTP_SETUP_WORKSPACE_MIRRORING = ::wxNewEventType();
const wxEventType wxEVT_SFTP_DISABLE_WORKSPACE_MIRRORING = ::wxNewEventType();

class SFTPClientData : public wxClientData
{
    wxString localPath;
    wxString remotePath;

public:
    SFTPClientData() {}
    virtual ~SFTPClientData() {}

    void SetLocalPath(const wxString& localPath) { this->localPath = localPath; }
    void SetRemotePath(const wxString& remotePath) { this->remotePath = remotePath; }
    const wxString& GetLocalPath() const { return localPath; }
    const wxString& GetRemotePath() const { return remotePath; }
};

// Exposed API (via events)
// SFTP plugin provides SFTP functionality for codelite based on events
// It uses the event type clCommandEvent to accept requests from codelite's code
// the SFTP uses the event GetString() method to read a string in the form of JSON format
// For example, to instruct the plugin to connect over SSH to a remote server and save a remote file:
// the GetString() should retrun this JSON string:
//  {
//      account : "account-name-to-use",
//      local_file : "/path/to/local/file",
//      remote_file : "/path/to/remote/file",
//  }

// Define the plugin entry point
CL_PLUGIN_API IPlugin* CreatePlugin(IManager* manager)
{
    if(thePlugin == 0) {
        thePlugin = new SFTP(manager);
    }
    return thePlugin;
}

CL_PLUGIN_API PluginInfo* GetPluginInfo()
{
    static PluginInfo info;
    info.SetAuthor(wxT("Eran Ifrah"));
    info.SetName(wxT("SFTP"));
    info.SetDescription(_("SFTP plugin for codelite IDE"));
    info.SetVersion(wxT("v1.0"));
    return &info;
}

CL_PLUGIN_API int GetPluginInterfaceVersion() { return PLUGIN_INTERFACE_VERSION; }

SFTP::SFTP(IManager* manager)
    : IPlugin(manager)
{
    m_longName = _("SFTP plugin for codelite IDE");
    m_shortName = wxT("SFTP");

    wxTheApp->Connect(
        wxEVT_SFTP_OPEN_SSH_ACCOUNT_MANAGER, wxEVT_MENU, wxCommandEventHandler(SFTP::OnAccountManager), NULL, this);
    wxTheApp->Connect(wxEVT_SFTP_SETTINGS, wxEVT_MENU, wxCommandEventHandler(SFTP::OnSettings), NULL, this);
    wxTheApp->Connect(wxEVT_SFTP_SETUP_WORKSPACE_MIRRORING, wxEVT_MENU,
        wxCommandEventHandler(SFTP::OnSetupWorkspaceMirroring), NULL, this);
    wxTheApp->Connect(wxEVT_SFTP_DISABLE_WORKSPACE_MIRRORING, wxEVT_MENU,
        wxCommandEventHandler(SFTP::OnDisableWorkspaceMirroring), NULL, this);
    wxTheApp->Connect(wxEVT_SFTP_DISABLE_WORKSPACE_MIRRORING, wxEVT_UPDATE_UI,
        wxUpdateUIEventHandler(SFTP::OnDisableWorkspaceMirroringUI), NULL, this);

    EventNotifier::Get()->Connect(wxEVT_WORKSPACE_LOADED, wxCommandEventHandler(SFTP::OnWorkspaceOpened), NULL, this);
    EventNotifier::Get()->Connect(wxEVT_WORKSPACE_CLOSED, wxCommandEventHandler(SFTP::OnWorkspaceClosed), NULL, this);
    EventNotifier::Get()->Connect(wxEVT_FILE_SAVED, clCommandEventHandler(SFTP::OnFileSaved), NULL, this);
    EventNotifier::Get()->Bind(wxEVT_FILES_MODIFIED_REPLACE_IN_FILES, &SFTP::OnReplaceInFiles, this);
    EventNotifier::Get()->Connect(wxEVT_EDITOR_CLOSING, wxCommandEventHandler(SFTP::OnEditorClosed), NULL, this);

    // API support
    EventNotifier::Get()->Bind(wxEVT_SFTP_SAVE_FILE, &SFTP::OnSaveFile, this);

    SFTPImages images;

    // Add the "SFTP" page to the workspace pane
    Notebook* book = m_mgr->GetWorkspacePaneNotebook();
    if(IsPaneDetached(_("SFTP"))) {
        // Make the window child of the main panel (which is the grand parent of the notebook)
        DockablePane* cp =
            new DockablePane(book->GetParent()->GetParent(), book, _("SFTP"), false, wxNullBitmap, wxSize(200, 200));
        m_treeView = new SFTPTreeView(cp, this);
        cp->SetChildNoReparent(m_treeView);

    } else {
        m_treeView = new SFTPTreeView(book, this);
        book->AddPage(m_treeView, _("SFTP"), false);
    }

    // Add the "SFTP Log" page to the output pane
    book = m_mgr->GetOutputPaneNotebook();
    if(IsPaneDetached(_("SFTP Log"))) {
        // Make the window child of the main panel (which is the grand parent of the notebook)
        DockablePane* cp = new DockablePane(
            book->GetParent()->GetParent(), book, _("SFTP Log"), false, images.Bitmap("sftp_tab"), wxSize(200, 200));
        m_outputPane = new SFTPStatusPage(cp, this);
        cp->SetChildNoReparent(m_outputPane);

    } else {
        m_outputPane = new SFTPStatusPage(book, this);
        book->AddPage(m_outputPane, _("SFTP Log"), false, images.Bitmap("sftp_tab"));
    }

    // Create the helper for adding our tabs in the "more" menu
    m_tabToggler.reset(new clTabTogglerHelper(_("SFTP Log"), m_outputPane, _("SFTP"), m_treeView));
    m_tabToggler->SetOutputTabBmp(m_mgr->GetStdIcons()->LoadBitmap("remote-folder"));

    SFTPWorkerThread::Instance()->SetNotifyWindow(m_outputPane);
    SFTPWorkerThread::Instance()->SetSftpPlugin(this);
    SFTPWorkerThread::Instance()->Start();
}

SFTP::~SFTP() {}

clToolBar* SFTP::CreateToolBar(wxWindow* parent)
{
    // Create the toolbar to be used by the plugin
    clToolBar* tb(NULL);
    return tb;
}

void SFTP::CreatePluginMenu(wxMenu* pluginsMenu)
{
    wxMenu* menu = new wxMenu();
    wxMenuItem* item(NULL);

    item = new wxMenuItem(menu, wxEVT_SFTP_OPEN_SSH_ACCOUNT_MANAGER, _("Open SSH Account Manager"),
        _("Open SSH Account Manager"), wxITEM_NORMAL);
    menu->Append(item);
    menu->AppendSeparator();
    item = new wxMenuItem(menu, wxEVT_SFTP_SETTINGS, _("Settings..."), _("Settings..."), wxITEM_NORMAL);
    menu->Append(item);
    pluginsMenu->Append(wxID_ANY, _("SFTP"), menu);
}

void SFTP::HookPopupMenu(wxMenu* menu, MenuType type)
{
    if(type == MenuTypeFileView_Workspace) {
        // Create the popup menu for the virtual folders
        wxMenuItem* item(NULL);

        wxMenu* sftpMenu = new wxMenu();
        item = new wxMenuItem(
            sftpMenu, wxEVT_SFTP_SETUP_WORKSPACE_MIRRORING, _("&Setup..."), wxEmptyString, wxITEM_NORMAL);
        sftpMenu->Append(item);

        item = new wxMenuItem(
            sftpMenu, wxEVT_SFTP_DISABLE_WORKSPACE_MIRRORING, _("&Disable"), wxEmptyString, wxITEM_NORMAL);
        sftpMenu->Append(item);

        item = new wxMenuItem(menu, wxID_SEPARATOR);
        menu->Prepend(item);
        menu->Prepend(wxID_ANY, _("Workspace Mirroring"), sftpMenu);
    }
}

bool SFTP::IsPaneDetached(const wxString& name) const
{
    DetachedPanesInfo dpi;
    m_mgr->GetConfigTool()->ReadObject(wxT("DetachedPanesList"), &dpi);
    const wxArrayString& detachedPanes = dpi.GetPanes();
    return detachedPanes.Index(name) != wxNOT_FOUND;
}

void SFTP::UnHookPopupMenu(wxMenu* menu, MenuType type)
{
    wxUnusedVar(menu);
    wxUnusedVar(type);
}

void SFTP::UnPlug()
{
    // Find our page and release it
    // before this plugin is un-plugged we must remove the tab we added
    for(size_t i = 0; i < m_mgr->GetOutputPaneNotebook()->GetPageCount(); ++i) {
        if(m_outputPane == m_mgr->GetOutputPaneNotebook()->GetPage(i)) {
            m_mgr->GetOutputPaneNotebook()->RemovePage(i);
            break;
        }
    }
    m_outputPane->Destroy();

    for(size_t i = 0; i < m_mgr->GetWorkspacePaneNotebook()->GetPageCount(); ++i) {
        if(m_treeView == m_mgr->GetWorkspacePaneNotebook()->GetPage(i)) {
            m_mgr->GetWorkspacePaneNotebook()->RemovePage(i);
            break;
        }
    }
    m_treeView->Destroy();

    SFTPWorkerThread::Release();
    wxTheApp->Disconnect(
        wxEVT_SFTP_OPEN_SSH_ACCOUNT_MANAGER, wxEVT_MENU, wxCommandEventHandler(SFTP::OnAccountManager), NULL, this);
    wxTheApp->Disconnect(wxEVT_SFTP_SETTINGS, wxEVT_MENU, wxCommandEventHandler(SFTP::OnSettings), NULL, this);
    wxTheApp->Disconnect(wxEVT_SFTP_SETUP_WORKSPACE_MIRRORING, wxEVT_MENU,
        wxCommandEventHandler(SFTP::OnSetupWorkspaceMirroring), NULL, this);
    wxTheApp->Disconnect(wxEVT_SFTP_DISABLE_WORKSPACE_MIRRORING, wxEVT_MENU,
        wxCommandEventHandler(SFTP::OnDisableWorkspaceMirroring), NULL, this);
    wxTheApp->Disconnect(wxEVT_SFTP_DISABLE_WORKSPACE_MIRRORING, wxEVT_UPDATE_UI,
        wxUpdateUIEventHandler(SFTP::OnDisableWorkspaceMirroringUI), NULL, this);

    EventNotifier::Get()->Disconnect(
        wxEVT_WORKSPACE_LOADED, wxCommandEventHandler(SFTP::OnWorkspaceOpened), NULL, this);
    EventNotifier::Get()->Disconnect(
        wxEVT_WORKSPACE_CLOSED, wxCommandEventHandler(SFTP::OnWorkspaceClosed), NULL, this);
    EventNotifier::Get()->Disconnect(wxEVT_FILE_SAVED, clCommandEventHandler(SFTP::OnFileSaved), NULL, this);
    EventNotifier::Get()->Disconnect(wxEVT_EDITOR_CLOSING, wxCommandEventHandler(SFTP::OnEditorClosed), NULL, this);

    EventNotifier::Get()->Unbind(wxEVT_SFTP_SAVE_FILE, &SFTP::OnSaveFile, this);
    EventNotifier::Get()->Unbind(wxEVT_FILES_MODIFIED_REPLACE_IN_FILES, &SFTP::OnReplaceInFiles, this);
    m_tabToggler.reset(NULL);
}

void SFTP::OnAccountManager(wxCommandEvent& e)
{
    wxUnusedVar(e);
    SSHAccountManagerDlg dlg(wxTheApp->GetTopWindow());
    if(dlg.ShowModal() == wxID_OK) {

        SFTPSettings settings;
        settings.Load();
        settings.SetAccounts(dlg.GetAccounts());
        settings.Save();
    }
}

void SFTP::OnSetupWorkspaceMirroring(wxCommandEvent& e)
{
    SFTPBrowserDlg dlg(wxTheApp->GetTopWindow(), _("Select the remote workspace"), "*.workspace");
    dlg.Initialize(m_workspaceSettings.GetAccount(), m_workspaceSettings.GetRemoteWorkspacePath());
    if(dlg.ShowModal() == wxID_OK) {
        m_workspaceSettings.SetRemoteWorkspacePath(dlg.GetPath());
        m_workspaceSettings.SetAccount(dlg.GetAccount());
        SFTPWorkspaceSettings::Save(m_workspaceSettings, m_workspaceFile);
    }
}

void SFTP::OnWorkspaceOpened(wxCommandEvent& e)
{
    e.Skip();
    m_workspaceFile = e.GetString();
    SFTPWorkspaceSettings::Load(m_workspaceSettings, m_workspaceFile);
}

void SFTP::OnWorkspaceClosed(wxCommandEvent& e)
{
    e.Skip();
    m_workspaceFile.Clear();
    m_workspaceSettings.Clear();
}

void SFTP::OnFileSaved(clCommandEvent& e)
{
    e.Skip();

    // --------------------------------------
    // Sanity
    // --------------------------------------
    wxString local_file = e.GetString();
    local_file.Trim().Trim(false);
    DoFileSaved(local_file);
}

void SFTP::OnFileWriteOK(const wxString& message) { wxLogMessage(message); }

void SFTP::OnFileWriteError(const wxString& errorMessage) { wxLogMessage(errorMessage); }

void SFTP::OnDisableWorkspaceMirroring(wxCommandEvent& e)
{
    m_workspaceSettings.Clear();
    SFTPWorkspaceSettings::Save(m_workspaceSettings, m_workspaceFile);
}

void SFTP::OnDisableWorkspaceMirroringUI(wxUpdateUIEvent& e)
{
    e.Enable(m_workspaceFile.IsOk() && m_workspaceSettings.IsOk());
}

void SFTP::OnSaveFile(clSFTPEvent& e)
{
    SFTPSettings settings;
    settings.Load();

    wxString accName = e.GetAccount();
    wxString localFile = e.GetLocalFile();
    wxString remoteFile = e.GetRemoteFile();

    SSHAccountInfo account;
    if(settings.GetAccount(accName, account)) {
        SFTPWorkerThread::Instance()->Add(new SFTPThreadRequet(account, remoteFile, localFile));

    } else {
        wxString msg;
        msg << _("Failed to synchronize file '") << localFile << "'\n" << _("with remote server\n")
            << _("Could not locate account: ") << accName;
        ::wxMessageBox(msg, _("SFTP"), wxOK | wxICON_ERROR);
    }
}

void SFTP::DoSaveRemoteFile(const RemoteFileInfo& remoteFile)
{
    SFTPWorkerThread::Instance()->Add(
        new SFTPThreadRequet(remoteFile.GetAccount(), remoteFile.GetRemoteFile(), remoteFile.GetLocalFile()));
}

void SFTP::FileDownloadedSuccessfully(const wxString& localFileName, const wxString& remotePath)
{
    wxString tooltip;
    tooltip << "Local: " << localFileName << "\n"
            << "Remote: " << remotePath;
    
    wxBitmap bmp = m_mgr->GetStdIcons()->LoadBitmap("download");
    IEditor* editor = m_mgr->OpenFile(localFileName, bmp, tooltip);
    if(editor) {
        // Tag this editor as a remote file
        SFTPClientData* cd = new SFTPClientData();
        cd->SetLocalPath(localFileName);
        cd->SetRemotePath(remotePath);
        editor->SetClientData("sftp", cd);
    }
}

void SFTP::OpenWithDefaultApp(const wxString& localFileName) { ::wxLaunchDefaultApplication(localFileName); }

void SFTP::AddRemoteFile(const RemoteFileInfo& remoteFile)
{
    if(m_remoteFiles.count(remoteFile.GetLocalFile())) {
        m_remoteFiles.erase(remoteFile.GetLocalFile());
    }
    m_remoteFiles.insert(std::make_pair(remoteFile.GetLocalFile(), remoteFile));
}

void SFTP::OnEditorClosed(wxCommandEvent& e)
{
    e.Skip();
    IEditor* editor = (IEditor*)e.GetClientData();
    if(editor) {
        wxString localFile = editor->GetFileName().GetFullPath();
        if(m_remoteFiles.count(localFile)) {

            wxLogNull noLog;

            // Remove the file from our cache
            ::wxRemoveFile(localFile);
            m_remoteFiles.erase(localFile);
        }
    }
}

void SFTP::MSWInitiateConnection()
{
#ifdef __WXMSW__
    // Under Windows, there seems to be a small problem with the connection establishment
    // only the first connection seems to be unstable (i.e. it takes up to 30 seconds to create it)
    // to workaround this, we initiate a dummy connection to the first connection we can find
    SFTPSettings settings;
    settings.Load();
    const SSHAccountInfo::Vect_t& accounts = settings.GetAccounts();
    if(accounts.empty()) return;
    const SSHAccountInfo& account = accounts.at(0);
    SFTPWorkerThread::Instance()->Add(new SFTPThreadRequet(account));
#endif
}

void SFTP::OnSettings(wxCommandEvent& e)
{
    // Show the SFTP settings dialog
    SFTPSettingsDialog dlg(EventNotifier::Get()->TopFrame());
    dlg.ShowModal();
}

void SFTP::DoFileSaved(const wxString& filename)
{
    if(filename.IsEmpty()) return;

    // Check to see if this file is part of a remote files managed by our plugin
    if(m_remoteFiles.count(filename)) {
        // ----------------------------------------------------------------------------------------------
        // this file was opened by the SFTP explorer
        // ----------------------------------------------------------------------------------------------
        DoSaveRemoteFile(m_remoteFiles.find(filename)->second);

    } else {
        // ----------------------------------------------------------------------------------------------
        // Not a remote file, see if have a sychronization setup between this workspace and a remote one
        // ----------------------------------------------------------------------------------------------

        // check if we got a workspace file opened
        if(!m_workspaceFile.IsOk()) return;

        // check if mirroring is setup for this workspace
        if(!m_workspaceSettings.IsOk()) return;

        wxFileName file(filename);
        file.MakeRelativeTo(m_workspaceFile.GetPath());
        file.MakeAbsolute(wxFileName(m_workspaceSettings.GetRemoteWorkspacePath(), wxPATH_UNIX).GetPath());
        wxString remoteFile = file.GetFullPath(wxPATH_UNIX);

        SFTPSettings settings;
        settings.Load();

        SSHAccountInfo account;
        if(settings.GetAccount(m_workspaceSettings.GetAccount(), account)) {
            SFTPWorkerThread::Instance()->Add(new SFTPThreadRequet(account, remoteFile, filename));

        } else {

            wxString msg;
            msg << _("Failed to synchronize file '") << filename << "'\n" << _("with remote server\n")
                << _("Could not locate account: ") << m_workspaceSettings.GetAccount();
            ::wxMessageBox(msg, _("SFTP"), wxOK | wxICON_ERROR);

            // Disable the workspace mirroring for this workspace
            m_workspaceSettings.Clear();
            SFTPWorkspaceSettings::Save(m_workspaceSettings, m_workspaceFile);
        }
    }
}

void SFTP::OnReplaceInFiles(clFileSystemEvent& e)
{
    e.Skip();
    const wxArrayString& files = e.GetStrings();
    for(size_t i = 0; i < files.size(); ++i) {
        DoFileSaved(files.Item(i));
    }
}

void SFTP::OpenContainingFolder(const wxString& localFileName) { FileUtils::OpenFileExplorerAndSelect(localFileName); }
