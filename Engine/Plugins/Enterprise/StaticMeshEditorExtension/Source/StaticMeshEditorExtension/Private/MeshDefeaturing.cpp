// Copyright Epic Games, Inc. All Rights Reserved.

#include "PolygonEditingToolbar.h"
#include "MeshEditingContext.h"

#include "CoreGlobals.h"
#include "EditableMesh.h"
#include "Editor.h"
#include "IStaticMeshEditor.h"
#include "StaticMeshAttributes.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "RawMesh.h"
#include "ScopedTransaction.h"

#include "Misc/MessageDialog.h"
#include "Editor.h"
#include "Interfaces/IMainFrameModule.h"
#include "Framework/Application/SlateApplication.h"

#include "IDetailsView.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SBox.h"
#include "EditorStyleSet.h"
#include "PropertyEditorModule.h"
#include "HAL/PlatformProcess.h"
#include "Modules/ModuleManager.h"

#if PLATFORM_WINDOWS
	#include "MeshProcessingLibrary.h" // TODO: MeshProcessingLibrary should be platform agnostic
#endif

#define LOCTEXT_NAMESPACE "StaticMeshEditorExtensionToolbar"

namespace PolygonEditingToolbarUI
{
	/**
	 * Display re-import options, static mesh's and tessellation.
	 * @return	false if user canceled re-import.
	 */
	bool DisplayDialog(class UMeshDefeaturingParameterObject* DefeaturingParameter);
}

bool FPolygonEditingToolbar::IsMeshProcessingAvailable() const
{
	return FModuleManager::Get().IsModuleLoaded( TEXT("MeshProcessingLibrary") )
		&& WITH_MESH_SIMPLIFIER;
}

void FPolygonEditingToolbar::OnDefeaturing()
{
#if PLATFORM_WINDOWS // OpenVDB is only available on Windows. We need a better way of handling platform specific features here.
	if (StaticMeshEditor == nullptr || StaticMesh == nullptr || !EditingContext.IsValid())
	{
		return;
	}

	int LODIndex = StaticMeshEditor->GetCurrentLODLevel();

	if (!bIsEditing && EditableLODs.Num() > 2 && !(EditableLODs.IsValidIndex(LODIndex) && EditableLODs[LODIndex]))
	{
		if (LODIndex == 0)
		{
			FMessageDialog::Open( EAppMsgType::Ok, LOCTEXT("FPolygonEditingToolbarNoLODAuto", "Cannot edit mesh when 'LOD Auto' is selected.\nPlease select LOD 0.") );
		}
		else
		{
			FMessageDialog::Open( EAppMsgType::Ok, LOCTEXT("FPolygonEditingToolbarBadLOD", "Selected LOD cannot be edited.\nPlease select LOD 0.") );
		}

		return;
	}

	// Check that the static mesh has a valid mesh description.
	// Do not take hold on the mesh description yet as the call to UStaticMesh::PreEditChange could change things
	if (!StaticMesh->IsMeshDescriptionValid(LODIndex))
	{
		return;
	}

	TStrongObjectPtr<UMeshDefeaturingParameterObject> Parameters = TStrongObjectPtr<UMeshDefeaturingParameterObject>(NewObject<UMeshDefeaturingParameterObject>(GetTransientPackage(), TEXT("Mesh Defeaturing Parameters")));

	if (!PolygonEditingToolbarUI::DisplayDialog(Parameters.Get()))
	{
		return;
	}

	// Save parameters to config file
	Parameters->SaveConfig(CPF_Config, *Parameters->GetDefaultConfigFilename());

	FScopedTransaction Transaction(LOCTEXT("MeshSimplification", "Defeature"));

	StaticMesh->PreEditChange(nullptr);

	// Proceed with defeaturing
	if (FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(LODIndex))
	{
		UMeshProcessingLibrary::DefeatureMesh(*MeshDescription, *Parameters);

		// Update RawMesh of LOD's source model with modification
		StaticMesh->CommitMeshDescription(LODIndex);

		StaticMesh->PostEditChange();

		StaticMeshEditor->RefreshTool();
	}
#endif // PLATFORM_WINDOWS
}

class SPolygonEditingParamterWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPolygonEditingParamterWindow)
	{}

	SLATE_ARGUMENT(UObject*, Parameters)
	SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			return OnCancel();
		}

		return FReply::Unhandled();
	}

	bool ShouldProceed() const
	{
		return bShouldProceed;
	}

private:
	FReply OnProceed()
	{
		bShouldProceed = true;
		if (Window.IsValid())
		{
			Window.Pin()->RequestDestroyWindow();
		}
		return FReply::Handled();
	}


	FReply OnCancel()
	{
		if (Window.IsValid())
		{
			Window.Pin()->RequestDestroyWindow();
		}
		return FReply::Handled();
	}

	FReply OnHelp(const FGeometry& SenderGeometry, const FPointerEvent& MouseEvent)
	{
		return FReply::Handled();
	}

private:
	UObject* Parameters;
	TWeakPtr< SWindow > Window;
	bool bShouldProceed;
};

void SPolygonEditingParamterWindow::Construct(const FArguments& InArgs)
{
	Parameters = InArgs._Parameters;
	Window = InArgs._WidgetWindow;
	bShouldProceed = false;

	TSharedPtr<SBox> DetailsViewBox;
	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SAssignNew(DetailsViewBox, SBox)
			]
			+ SVerticalBox::Slot()
			.MaxHeight(50)
			.HAlign(HAlign_Right)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(5)
				+ SUniformGridPanel::Slot(0, 0)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("PolygonEditingParamterWindow_ImportCurLevel", "Proceed"))
					.OnClicked(this, &SPolygonEditingParamterWindow::OnProceed)
				]
				+ SUniformGridPanel::Slot(1, 0)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("PolygonEditingParamterWindow_Cancel", "Cancel"))
					.OnClicked(this, &SPolygonEditingParamterWindow::OnCancel)
				]
			]
		];

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	TSharedPtr<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	DetailsViewBox->SetContent(DetailsView.ToSharedRef());

	TArray<UObject*> Objects;
	Objects.Add(Parameters);
	DetailsView->SetObjects(Objects);
}

bool PolygonEditingToolbarUI::DisplayDialog(UMeshDefeaturingParameterObject* DefeaturingParameter)
{
#if PLATFORM_WINDOWS
	TSharedPtr<SWindow> ParentWindow;

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("MeshDefeaturing_Title", "Remove solid features from the static mesh"))
		.SizingRule(ESizingRule::Autosized);

	TSharedPtr<SPolygonEditingParamterWindow> ParameterWindow;
	Window->SetContent
	(
		SAssignNew(ParameterWindow, SPolygonEditingParamterWindow)
		.Parameters(DefeaturingParameter)
		.WidgetWindow(Window)
	);

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	return ParameterWindow->ShouldProceed();
#else
	return false;
#endif // !PLATFORM_WINDOWS
}

#undef LOCTEXT_NAMESPACE

