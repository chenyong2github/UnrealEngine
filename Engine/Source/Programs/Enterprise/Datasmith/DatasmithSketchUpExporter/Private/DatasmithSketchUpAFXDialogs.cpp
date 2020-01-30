// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpDialogs.h"

// SketchUp to Datasmith exporter resources.
#include "Resources/Windows/resource.h"

#ifdef SHOW_DIALOG
// Begin Datasmith platform include gard.
// #include "Windows/AllowWindowsPlatformTypes.h"

#pragma warning(push)
#pragma warning(disable: 4668) // C4100: unreferenced formal parameter

// Using the shared MFC DLL.
#define _AFXDLL

//#include "SDKDDKVer.h" // Windows platform to support
#include "afxwin.h"    // MFC core and standard components

// End Datasmith platform include guard.
// #include "Windows/HideWindowsPlatformTypes.h"

//=====================================================================================================================
// Unreal Datasmith Export Options Dialog
//=====================================================================================================================

class FDatasmithSketchUpOptionsDialog :
	public CDialog
{
	DECLARE_DYNAMIC(FDatasmithSketchUpOptionsDialog)

public:

	FDatasmithSketchUpOptionsDialog(CWnd* InParentWnd = NULL);
	virtual ~FDatasmithSketchUpOptionsDialog() {}

	void SetModelHasSelection(
		bool bInModelHasSelection // indicates if the SketchUp model has a current selection set
	);

	virtual BOOL OnInitDialog();

protected:

	virtual void DoDataExchange(CDataExchange* InDataExchange);

	DECLARE_MESSAGE_MAP()

private:

	// Indicates if the SketchUp model has a current selection set.
	bool bModelHasSelection;
};

IMPLEMENT_DYNAMIC(FDatasmithSketchUpOptionsDialog, CDialog)

FDatasmithSketchUpOptionsDialog::FDatasmithSketchUpOptionsDialog(
	CWnd* InParentWnd
):
	CDialog(IDD_EXPORT_OPTIONS_DIALOG, InParentWnd)
{
}

void FDatasmithSketchUpOptionsDialog::SetModelHasSelection(
	bool bInModelHasSelection
)
{
	bModelHasSelection = bInModelHasSelection;
}

BOOL FDatasmithSketchUpOptionsDialog::OnInitDialog()
{
	CDialog::OnInitDialog();

	return TRUE;
}

void FDatasmithSketchUpOptionsDialog::DoDataExchange(
	CDataExchange* InDataExchange
)
{
	CDialog::DoDataExchange(InDataExchange);
}

BEGIN_MESSAGE_MAP(FDatasmithSketchUpOptionsDialog, CDialog)
END_MESSAGE_MAP()


//=====================================================================================================================
// Unreal Datasmith Export Summary Dialog
//=====================================================================================================================

class FDatasmithSketchUpSummaryDialog :
	public CDialog
{
	DECLARE_DYNAMIC(FDatasmithSketchUpSummaryDialog)

public:

	FDatasmithSketchUpSummaryDialog(CWnd* InParentWnd = NULL);
	virtual ~FDatasmithSketchUpSummaryDialog() {}

	void SetExportSummary(
		const std::wstring& InExportSummary // summary of the last export process
	);

	virtual BOOL OnInitDialog();

protected:

	virtual void DoDataExchange(CDataExchange* InDataExchange);

	DECLARE_MESSAGE_MAP()

private:

	// Summary of the last export process.
	CString ExportSummary;
};

IMPLEMENT_DYNAMIC(FDatasmithSketchUpSummaryDialog, CDialog)

FDatasmithSketchUpSummaryDialog::FDatasmithSketchUpSummaryDialog(
	CWnd* InParentWnd
):
	CDialog(IDD_EXPORT_SUMMARY_DIALOG, InParentWnd)
{
}

void FDatasmithSketchUpSummaryDialog::SetExportSummary(
	const std::wstring& InExportSummary
)
{
	ExportSummary = InExportSummary.c_str();
	ExportSummary.Replace(TEXT("\n"), TEXT("\r\n"));
}

BOOL FDatasmithSketchUpSummaryDialog::OnInitDialog()
{
	CDialog::OnInitDialog();

	return TRUE;
}

void FDatasmithSketchUpSummaryDialog::DoDataExchange(
	CDataExchange* InDataExchange
)
{
	CDialog::DoDataExchange(InDataExchange);

	DDX_Text(InDataExchange, IDC_EXPORT_SUMMARY_TEXT, ExportSummary);
}

BEGIN_MESSAGE_MAP(FDatasmithSketchUpSummaryDialog, CDialog)
END_MESSAGE_MAP()


//=====================================================================================================================
// Unreal Datasmith Export Dialogs
//=====================================================================================================================

void FDatasmithSketchUpDialogs::ShowOptionsDialog(
	bool bInModelHasSelection
)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	// Create and set up the dialog.
	FDatasmithSketchUpOptionsDialog OptionsDialog;
	OptionsDialog.SetModelHasSelection(bInModelHasSelection);

	// Display the dialog.
	if (OptionsDialog.DoModal() == IDOK)
	{
		// Update the options here.
	}
}

void FDatasmithSketchUpDialogs::ShowSummaryDialog(
	FString const& InExportSummary
)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	// Create and set up the dialog.
	FDatasmithSketchUpSummaryDialog SummaryDialog;
	SummaryDialog.SetExportSummary(InExportSummary);

	// Display the dialog.
	SummaryDialog.DoModal();
}

#pragma warning(pop)
#else

//=====================================================================================================================
// Unreal Datasmith Export Dialogs
//=====================================================================================================================

void FDatasmithSketchUpDialogs::ShowOptionsDialog(
	bool /*bInModelHasSelection*/
)
{
}

void FDatasmithSketchUpDialogs::ShowSummaryDialog(
	FString const& /*InExportSummary*/
)
{
}

#endif
