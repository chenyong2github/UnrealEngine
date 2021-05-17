// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReportWindow.h"
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

class FReportDialog : public DG::Palette,
					  public DG::PanelObserver,
					  public DG::ButtonItemObserver,
					  public DG::CompoundItemObserver
{
	enum
	{
		kCloseButtonId = 1,
		kClearButtonId,
		kMessagesTextEditId,
		kCopyAllButtonId,
		kCopySelectionButtonId
	};

	DG::Button		  CloseButton;
	DG::Button		  ClearButton;
	DG::MultiLineEdit MessagesTextEdit;
	DG::Button		  CopyAllButton;
	DG::Button		  CopySelectionButton;

  public:
	FReportDialog();
	~FReportDialog();

	virtual void PanelClosed(const DG::PanelCloseEvent& /* ev */) override {}

	virtual void PanelCloseRequested(const DG::PanelCloseRequestEvent& /* ev */, bool* /* accepted */) override
	{
		Hide();
	}

	virtual void PanelResized(const DG::PanelResizeEvent& ev) override
	{
		if (ev.GetSource() == this)
		{
			DG::Point Position = CloseButton.GetPosition();
			Position.Set(Position.GetX() + ev.GetHorizontalChange(), Position.GetY() + ev.GetVerticalChange());
			CloseButton.SetPosition(Position);

			Position = ClearButton.GetPosition();
			Position.SetY(Position.GetY() + ev.GetVerticalChange());
			ClearButton.SetPosition(Position);

			Position = CopyAllButton.GetPosition();
			Position.SetY(Position.GetY() + ev.GetVerticalChange());
			CopyAllButton.SetPosition(Position);

			Position = CopySelectionButton.GetPosition();
			Position.SetY(Position.GetY() + ev.GetVerticalChange());
			CopySelectionButton.SetPosition(Position);

			MessagesTextEdit.SetSize(MessagesTextEdit.GetWidth() + ev.GetHorizontalChange(),
									 MessagesTextEdit.GetHeight() + ev.GetVerticalChange());
		}
	}

	virtual void ButtonClicked(const DG::ButtonClickEvent& ev) override
	{
		if (ev.GetSource() == &CloseButton)
		{
			SendCloseRequest();
		}
		else if (ev.GetSource() == &ClearButton)
		{
			FTraceListener::Get().Traces.clear();
			MessagesTextEdit.SetText(GS::UniString(""));
		}
		else if (ev.GetSource() == &CopyAllButton)
		{
			SetPasteboardWithString(FTraceListener::Get().Traces.c_str());
		}
		else if (ev.GetSource() == &CopySelectionButton)
		{
			DG::CharRange Selection(MessagesTextEdit.GetSelection());
			GS::UniString SelectedText(
				MessagesTextEdit.GetText().GetSubstring(Selection.GetMin(), Selection.GetLength()));
			SetPasteboardWithString(SelectedText.ToUtf8());
		}
	}

	// Update the text content with the collected traces
	void Update()
	{
		DG::CharRange Selection(MessagesTextEdit.GetSelection());

		GS::UniString NewText(FTraceListener::Get().Traces.c_str(), CC_UTF8);
		MessagesTextEdit.SetText(NewText);

		// On empty selection, we set selection to the end, otherwise we restore previous one
		if (Selection.GetLength() == 0)
		{
			Selection.SetWithLength(NewText.GetLength(), 0);
		}
		MessagesTextEdit.SetSelection(Selection);
	}
};

FReportDialog::FReportDialog()
	: DG::Palette(ACAPI_GetOwnResModule(), LocalizeResId(kDlgReport), ACAPI_GetOwnResModule())
	, CloseButton(GetReference(), kCloseButtonId)
	, ClearButton(GetReference(), kClearButtonId)
	, MessagesTextEdit(GetReference(), kMessagesTextEditId)
	, CopyAllButton(GetReference(), kCopyAllButtonId)
	, CopySelectionButton(GetReference(), kCopySelectionButtonId)
{
	Attach(*this);
	AttachToAllItems(*this);
	MessagesTextEdit.SetText(GS::UniString(FTraceListener::Get().Traces.c_str(), CC_UTF8));
}

FReportDialog::~FReportDialog()
{
	DetachFromAllItems(*this);
	Detach(*this);
}

static FReportWindow* ReportWindow;

void FReportWindow::Create()
{
	if (ReportWindow == nullptr)
	{
		new FReportWindow();
	}
	if (ReportWindow != nullptr)
	{
		ReportWindow->ReportDialog->Show();
		ReportWindow->ReportDialog->BringToFront();
	}
}

void FReportWindow::Delete()
{
	if (ReportWindow != nullptr)
	{
		ReportWindow->Stop();
	}
}

void FReportWindow::Update()
{
	if (ReportWindow != nullptr)
	{
		ReportWindow->ReportDialog->Update();
	}
}

FReportWindow::FReportWindow()
{
	ReportWindow = this;
	ReportDialog = new FReportDialog();
	Start();
}

FReportWindow::~FReportWindow()
{
	delete ReportDialog;
	ReportDialog = nullptr;
	ReportWindow = nullptr;
}

void FReportWindow::Start()
{
	ReportDialog->BeginEventProcessing();
	ReportDialog->Show();
}

void FReportWindow::Stop()
{
	//	ReportDialog->SendCloseRequest();
	//	ReportDialog->EndEventProcessing();
	delete this;
}

enum : GSType
{
	UEACTraces = 'UETr'
};
enum : Int32
{
	CmdUpdateTraces = 1
};

static std::unique_ptr< FTraceListener > TraceListener;

FTraceListener& FTraceListener::Get()
{
	if (!TraceListener)
	{
		TraceListener = std::make_unique< FTraceListener >();
	}
	return *TraceListener;
}

void FTraceListener::Delete()
{
	TraceListener.reset();
}

// Register service
GSErrCode FTraceListener::Register()
{
	// register supported command
	GSErrCode GSErr = ACAPI_Register_SupportedService(UEACTraces, CmdUpdateTraces);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("CVersionChecker::Register - Error %d\n", GSErr);
	}
	return GSErr;
}

FTraceListener::FTraceListener()
{
	Traces.reserve(100 * 1024);
	AddTraceListener(this);
	// register supported command
	GSErrCode err = ACAPI_Install_ModulCommandHandler(UEACTraces, CmdUpdateTraces, UpdateTraces);
	if (err == NoError)
	{
		ACAPI_KeepInMemory(true);
	}
	else
	{
		UE_AC_DebugF("FTraceListener::FTraceListener - Error %d\n", err);
	}
}

void FTraceListener::NewTrace(EP2DB InTraceLevel, const utf8_string& InMsg)
{
#ifdef DEBUG
	const EP2DB MessageLevel = kP2DB_Trace;
#else
	const EP2DB MessageLevel = kP2DB_Debug; // Put kP2DB_Report for final release
#endif

	if (InTraceLevel <= MessageLevel)
	{
		if (InTraceLevel != kP2DB_Report)
		{
			Traces.append("* ");
		}
		Traces.append(InMsg);
		if (bScheduledForUpdate == false)
		{
			// Update content on the main thread
			API_ModulID mdid;
			Zap(&mdid);
			mdid.developerID = kEpicGamesDevId;
			mdid.localID = kDatasmithExporterId;
			GSHandle h = BMAllocateHandle(sizeof(this), 0, 0);
			**reinterpret_cast< FTraceListener*** >(h) = this;
			GSErrCode err = ACAPI_Command_CallFromEventLoop(&mdid, UEACTraces, CmdUpdateTraces, h, false, nullptr);
			if (err != NoError)
			{
				printf("FTraceListener::NewTrace - ACAPI_Command_CallFromEventLoop error %d\n", err);
			}
			bScheduledForUpdate = true;
		}
	}
}

GSErrCode FTraceListener::UpdateTraces(GSHandle paramHandle, GSPtr /*resultData*/, bool /*silentMode*/)
{
	if (paramHandle)
	{
		FTraceListener* TracesListener = **reinterpret_cast< FTraceListener*** >(paramHandle);
		FReportWindow::Update();
		TracesListener->bScheduledForUpdate = false;
		return NoError;
	}
	return ErrParam;
}

END_NAMESPACE_UE_AC
