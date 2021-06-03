// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConnectionWindow.h"
#include "ResourcesIDs.h"
#include "Synchronizer.h"
#include "Commander.h"
#include "Menus.h"
#include "Utils/Pasteboard.h"
#include "Utils/Error.h"

DISABLE_SDK_WARNINGS_START
#include "DGDialog.hpp"
#include "DGModule.hpp"
DISABLE_SDK_WARNINGS_END

BEGIN_NAMESPACE_UE_AC

TArray< IConnectionListener* > ConnectionListeners;

void IConnectionListener::RegisterListener(IConnectionListener* InListener)
{
	ConnectionListeners.AddUnique(InListener);
}

void IConnectionListener::UnregisterListener(IConnectionListener* InListener)
{
	ConnectionListeners.Remove(InListener);
}

class FConnectionDialog : public DG::Palette,
						  public DG::PanelObserver,
						  public DG::ButtonItemObserver,
						  public DG::CompoundItemObserver
{
	enum
	{
		kConnectionsTextEditId = 1,
		kChooseCacheFolderButtonId,
		kCacheFolderTextId
	};

	DG::MultiLineEdit ConnectionsTextEdit;
	DG::Button		  ChooseCacheFolderButton;
	DG::LeftText	  CacheFolderText;

  public:
	FConnectionDialog();
	~FConnectionDialog();

	virtual void PanelClosed(const DG::PanelCloseEvent& /* ev */) override {}

	virtual void PanelCloseRequested(const DG::PanelCloseRequestEvent& /* ev */, bool* /* accepted */) override {}

	virtual void PanelResized(const DG::PanelResizeEvent& ev) override
	{
		if (ev.GetSource() == this)
		{
			ChooseCacheFolderButton.MoveAndResize(ev.GetHorizontalChange(), ev.GetVerticalChange(), 0, 0);
			CacheFolderText.MoveAndResize(0, ev.GetVerticalChange(), ev.GetHorizontalChange(), 0);
			ConnectionsTextEdit.MoveAndResize(0, 0, ev.GetHorizontalChange(), ev.GetVerticalChange());
		}
	}

	virtual void ButtonClicked(const DG::ButtonClickEvent& ev) override
	{
		if (ev.GetSource() == &ChooseCacheFolderButton)
		{
		}
	}
};

FConnectionDialog::FConnectionDialog()
	: DG::Palette(ACAPI_GetOwnResModule(), LocalizeResId(kDlgReport), ACAPI_GetOwnResModule())
	, ConnectionsTextEdit(GetReference(), kConnectionsTextEditId)
	, ChooseCacheFolderButton(GetReference(), kChooseCacheFolderButtonId)
	, CacheFolderText(GetReference(), kCacheFolderTextId)
{
	Attach(*this);
	AttachToAllItems(*this);
}

FConnectionDialog::~FConnectionDialog()
{
	DetachFromAllItems(*this);
	Detach(*this);
}

void FConnectionWindow::ConnectionsChanged(const TSharedRef< TArray< FConnection > >& FConnection)
{
	{
		GS::Guard< GS::Lock > Lock(AccessControl);
		Connections = FConnection;
	}
	//	PostAnUndateMessage();
}

static FConnectionWindow* ConnectionWindow = nullptr;

void FConnectionWindow::Create()
{
	if (ConnectionWindow == nullptr)
	{
		new FConnectionWindow();
	}
	if (ConnectionWindow != nullptr)
	{
		ConnectionWindow->ConnectionDialog->Show();
		ConnectionWindow->ConnectionDialog->BringToFront();
	}
}

void FConnectionWindow::Delete()
{
	if (ConnectionWindow != nullptr)
	{
		UnregisterListener(ConnectionWindow);
		delete ConnectionWindow;
	}
}

FConnectionWindow::FConnectionWindow()
	: AccessCondition(AccessControl)
{
	ConnectionWindow = this;
	ConnectionDialog = new FConnectionDialog();
	RegisterListener(this);
}

FConnectionWindow::~FConnectionWindow()
{
	delete ConnectionDialog;
	ConnectionDialog = nullptr;
	ConnectionWindow = nullptr;
}

void FConnectionWindow::Start()
{
	ConnectionDialog->BeginEventProcessing();
	ConnectionDialog->Show();
}

void FConnectionWindow::Stop()
{
	//	ReportDialog->SendCloseRequest();
	//	ReportDialog->EndEventProcessing();
	delete this;
}

END_NAMESPACE_UE_AC
