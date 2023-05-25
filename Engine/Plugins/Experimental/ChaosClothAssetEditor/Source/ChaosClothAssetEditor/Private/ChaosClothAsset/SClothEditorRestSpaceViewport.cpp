// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SClothEditorRestSpaceViewport.h"
#include "SViewportToolBar.h"
#include "ChaosClothAsset/SClothEditorRestSpaceViewportToolBar.h"
#include "ChaosClothAsset/ClothEditorCommands.h"
#include "ChaosClothAsset/ClothEditorMode.h"
#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "SChaosClothAssetEditorRestSpaceViewport"

UChaosClothAssetEditorMode* SChaosClothAssetEditorRestSpaceViewport::GetEdMode() const
{
	if (const FEditorModeTools* const EditorModeTools = Client->GetModeTools())
	{
		if (UChaosClothAssetEditorMode* const ClothEdMode = Cast<UChaosClothAssetEditorMode>(EditorModeTools->GetActiveScriptableMode(UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId)))
		{
			return ClothEdMode;
		}
	}
	return nullptr;
}

void SChaosClothAssetEditorRestSpaceViewport::BindCommands()
{
	using namespace UE::Chaos::ClothAsset;

	SAssetEditorViewport::BindCommands();

	const FChaosClothAssetEditorCommands& CommandInfos = FChaosClothAssetEditorCommands::Get();

	CommandList->MapAction(
		CommandInfos.SetConstructionMode2D,
		FExecuteAction::CreateLambda([this]()
		{
			if (UChaosClothAssetEditorMode* const ClothEdMode = GetEdMode())
			{
				ClothEdMode->SetConstructionViewMode(EClothPatternVertexType::Sim2D);
			}
		}),
		FCanExecuteAction::CreateLambda([this]() 
		{ 
			if (const UChaosClothAssetEditorMode* const ClothEdMode = GetEdMode())
			{
				return ClothEdMode->CanChangeConstructionViewMode();
			}
			return false; 
		}),
		FIsActionChecked::CreateLambda([this]() 
		{
			if (const UChaosClothAssetEditorMode* const ClothEdMode = GetEdMode())
			{
				return ClothEdMode->GetConstructionViewMode() == EClothPatternVertexType::Sim2D;
			}
			return false;
		}));


	CommandList->MapAction(
		CommandInfos.SetConstructionMode3D,
		FExecuteAction::CreateLambda([this]()
		{
			if (UChaosClothAssetEditorMode* const ClothEdMode = GetEdMode()) 
			{
				ClothEdMode->SetConstructionViewMode(EClothPatternVertexType::Sim3D);
			}
		}),
		FCanExecuteAction::CreateLambda([this]() 
		{ 
			if (const UChaosClothAssetEditorMode* const ClothEdMode = GetEdMode())
			{
				return ClothEdMode->CanChangeConstructionViewMode();
			}
			return false; 
		}),
		FIsActionChecked::CreateLambda([this]() 
		{
			if (const UChaosClothAssetEditorMode* const ClothEdMode = GetEdMode())
			{
				return ClothEdMode->GetConstructionViewMode() == EClothPatternVertexType::Sim3D;
			}
			return false;
		}));



	CommandList->MapAction(
		CommandInfos.SetConstructionModeRender,
		FExecuteAction::CreateLambda([this]()
		{
			if (UChaosClothAssetEditorMode* ClothEdMode = GetEdMode()) 
			{
				ClothEdMode->SetConstructionViewMode(EClothPatternVertexType::Render);
			}
		}),
		FCanExecuteAction::CreateLambda([this]() 
		{ 
			if (const UChaosClothAssetEditorMode* const ClothEdMode = GetEdMode())
			{
				return ClothEdMode->CanChangeConstructionViewMode();
			}
			return false; 
		}),
		FIsActionChecked::CreateLambda([this]() 
		{
			if (const UChaosClothAssetEditorMode* const ClothEdMode = GetEdMode())
			{
				return ClothEdMode->GetConstructionViewMode() == EClothPatternVertexType::Render;
			}
			return false;
		}));

	CommandList->MapAction(
		CommandInfos.ToggleConstructionViewWireframe,
		FExecuteAction::CreateLambda([this]()
		{
			const FEditorModeTools* const EditorModeTools = Client->GetModeTools();
			UChaosClothAssetEditorMode* const ClothEdMode = Cast<UChaosClothAssetEditorMode>(EditorModeTools->GetActiveScriptableMode(UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId));

			if (ClothEdMode)
			{
				ClothEdMode->ToggleConstructionViewWireframe();
			}
		}),
		FCanExecuteAction::CreateLambda([this]() 
		{ 
			return true; 
		}),
		FIsActionChecked::CreateLambda([this]() 
		{
			if (const UChaosClothAssetEditorMode* const ClothEdMode = GetEdMode())
			{
				return ClothEdMode->IsConstructionViewWireframeActive();
			}
			return false;
		}));


}

TSharedPtr<SWidget> SChaosClothAssetEditorRestSpaceViewport::MakeViewportToolbar()
{
	return SNew(SChaosClothAssetEditorRestSpaceViewportToolBar, SharedThis(this))
		.CommandList(CommandList);
}


void SChaosClothAssetEditorRestSpaceViewport::OnFocusViewportToSelection()
{
	const UChaosClothAssetEditorMode* const ClothEdMode = GetEdMode();

	if (ClothEdMode)
	{
		Client->FocusViewportOnBox(ClothEdMode->SelectionBoundingBox());

		// Reset any changes to the clip planes by the scroll zoom behavior
		Client->OverrideNearClipPlane(UE_KINDA_SMALL_NUMBER);
		Client->OverrideFarClipPlane(0);
	}
}

TSharedRef<class SEditorViewport> SChaosClothAssetEditorRestSpaceViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SChaosClothAssetEditorRestSpaceViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SChaosClothAssetEditorRestSpaceViewport::OnFloatingButtonClicked()
{
}

#undef LOCTEXT_NAMESPACE
