// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SClothEditor3DViewport.h"
#include "ChaosClothAsset/ClothEditor3DViewportClient.h"
#include "ChaosClothAsset/SClothEditor3DViewportToolBar.h"
#include "ChaosClothAsset/ClothEditorCommands.h"
#include "ChaosClothAsset/ClothEditorMode.h"
#include "ChaosClothAsset/ClothEditorPreviewScene.h"
#include "ChaosClothAsset/SClothAnimationScrubPanel.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "EditorModeTools.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "SChaosClothAssetEditor3DViewport"

void SChaosClothAssetEditor3DViewport::Construct(const FArguments& InArgs, const FAssetEditorViewportConstructionArgs& InViewportConstructionArgs)
{
	SAssetEditorViewport::FArguments ParentArgs;
	ParentArgs._EditorViewportClient = InArgs._EditorViewportClient;
	if (InArgs._ViewportSize.IsSet())
	{
		ParentArgs._ViewportSize = InArgs._ViewportSize;
	}
	SAssetEditorViewport::Construct(ParentArgs, InViewportConstructionArgs);

	ViewportOverlay->AddSlot()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Bottom)
		.FillWidth(1)
		.Padding(10.0f, 0.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("EditorViewport.OverlayBrush"))
			.Visibility_Raw(this, &SChaosClothAssetEditor3DViewport::GetAnimControlVisibility)
			.Padding(10.0f, 2.0f)
			[
				SNew(SClothAnimationScrubPanel, GetPreviewScene())
				.ViewInputMin(this, &SChaosClothAssetEditor3DViewport::GetViewMinInput)
				.ViewInputMax(this, &SChaosClothAssetEditor3DViewport::GetViewMaxInput)
			]
		]
	];
}

TWeakPtr<FChaosClothPreviewScene> SChaosClothAssetEditor3DViewport::GetPreviewScene()
{
	const TSharedPtr<FChaosClothAssetEditor3DViewportClient> ClothViewportClient = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client);
	return ClothViewportClient->GetClothPreviewScene();
}

TWeakPtr<const FChaosClothPreviewScene> SChaosClothAssetEditor3DViewport::GetPreviewScene() const
{
	const TSharedPtr<const FChaosClothAssetEditor3DViewportClient> ClothViewportClient = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client);
	return ClothViewportClient->GetClothPreviewScene();
}


void SChaosClothAssetEditor3DViewport::BindCommands()
{
	SAssetEditorViewport::BindCommands();
	const FChaosClothAssetEditorCommands& CommandInfos = FChaosClothAssetEditorCommands::Get();

	CommandList->MapAction(
		CommandInfos.ToggleSimMeshWireframe,
		FExecuteAction::CreateLambda([this]() 
		{
			TSharedPtr<FChaosClothAssetEditor3DViewportClient> ClothViewportClient = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client);
			ClothViewportClient->EnableSimMeshWireframe(!ClothViewportClient->SimMeshWireframeEnabled());
		}),
		FCanExecuteAction::CreateLambda([this]() { return true; }),
		FIsActionChecked::CreateLambda([this]() { return StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client)->SimMeshWireframeEnabled(); }));

	CommandList->MapAction(
		CommandInfos.ToggleRenderMeshWireframe,
		FExecuteAction::CreateLambda([this]()
		{
			TSharedPtr<FChaosClothAssetEditor3DViewportClient> ClothViewportClient = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client);
			ClothViewportClient->EnableRenderMeshWireframe(!ClothViewportClient->RenderMeshWireframeEnabled());
		}),
		FCanExecuteAction::CreateLambda([this]() { return true; }),
		FIsActionChecked::CreateLambda([this]() { return StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client)->RenderMeshWireframeEnabled(); }));

	CommandList->MapAction(
		CommandInfos.SoftResetSimulation,
		FExecuteAction::CreateLambda([this]()
		{
			TSharedPtr<FChaosClothAssetEditor3DViewportClient> ClothViewportClient = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client);
			ClothViewportClient->SoftResetSimulation();
		}),
		FCanExecuteAction::CreateLambda([this]() { return true; }),
		FIsActionChecked::CreateLambda([this]() { return false; }));

	CommandList->MapAction(
		CommandInfos.HardResetSimulation,
		FExecuteAction::CreateLambda([this]()
		{
			TSharedPtr<FChaosClothAssetEditor3DViewportClient> ClothViewportClient = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client);
			ClothViewportClient->HardResetSimulation();
		}),
		FCanExecuteAction::CreateLambda([this]() { return true; }),
		FIsActionChecked::CreateLambda([this]() { return false; }));


	CommandList->MapAction(
		CommandInfos.ToggleSimulationSuspended,
		FExecuteAction::CreateLambda([this]()
		{
			TSharedPtr<FChaosClothAssetEditor3DViewportClient> ClothViewportClient = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client);

			const bool bIsSuspended = ClothViewportClient->IsSimulationSuspended();
			if (bIsSuspended)
			{
				ClothViewportClient->ResumeSimulation();
			}
			else
			{
				ClothViewportClient->SuspendSimulation();
			}
		}),
		FCanExecuteAction::CreateLambda([this]() { return true; }),
		FIsActionChecked::CreateLambda([this]() { return StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client)->IsSimulationSuspended(); }) );

}

TSharedPtr<SWidget> SChaosClothAssetEditor3DViewport::MakeViewportToolbar()
{
	return SNew(SChaosClothAssetEditor3DViewportToolBar, SharedThis(this))
		.CommandList(CommandList);
}

void SChaosClothAssetEditor3DViewport::OnFocusViewportToSelection()
{
	const FBox PreviewBoundingBox = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client)->PreviewBoundingBox();
	Client->FocusViewportOnBox(PreviewBoundingBox);
}

TSharedRef<class SEditorViewport> SChaosClothAssetEditor3DViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SChaosClothAssetEditor3DViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

float SChaosClothAssetEditor3DViewport::GetViewMinInput() const
{
	return 0.0f;
}

float SChaosClothAssetEditor3DViewport::GetViewMaxInput() const
{
	// (these are non-const because UAnimSingleNodeInstance::GetLength() is non-const)
	const TSharedPtr<FChaosClothAssetEditor3DViewportClient> ClothViewportClient = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client);
	const TSharedPtr<FChaosClothPreviewScene> Scene = ClothViewportClient->GetClothPreviewScene().Pin();
	if (Scene)
	{
		if (UAnimSingleNodeInstance* const PreviewInstance = Scene->GetPreviewAnimInstance())
		{
			return PreviewInstance->GetLength();
		}
	}

	return 0.0f;
}

EVisibility SChaosClothAssetEditor3DViewport::GetAnimControlVisibility() const
{
	const TSharedPtr<const FChaosClothPreviewScene> Scene = GetPreviewScene().Pin();
	return (Scene && Scene->GetSkeletalMeshComponent() && Scene->GetPreviewAnimInstance()) ? EVisibility::Visible : EVisibility::Hidden;
}

#undef LOCTEXT_NAMESPACE
