// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrailEditorMode.h"
#include "MotionTrailEditorModeToolkit.h"
#include "MotionTrailEditorModeCommands.h"
#include "Toolkits/ToolkitManager.h"
#include "EditorModeManager.h"
#include "EdModeInteractiveToolsContext.h"

#include "LevelEditorSequencerIntegration.h"
#include "ISequencer.h"
#include "SequencerTrailHierarchy.h"

#include "MovieSceneTransformTrail.h"


#define LOCTEXT_NAMESPACE "MotionTrailEditorMode"

DEFINE_LOG_CATEGORY(LogMotionTrailEditorMode);

FName UMotionTrailEditorMode::MotionTrailEditorMode_Default = FName(TEXT("Default"));

FString UMotionTrailEditorMode::DefaultToolName = TEXT("DefaultTool");

UMotionTrailEditorMode::UMotionTrailEditorMode()
{
	SettingsClass = UMotionTrailOptions::StaticClass();

	Info = FEditorModeInfo(
		FName(TEXT("MotionTrailEditorMode")),
		LOCTEXT("ModeName", "Motion Trail Editor"),
		FSlateIcon(),
		false
	);
}

UMotionTrailEditorMode::~UMotionTrailEditorMode()
{

}

void UMotionTrailEditorMode::Enter()
{
	UEdMode::Enter();

	TrailOptions = Cast<UMotionTrailOptions>(SettingsObject);

	// Add default tool
	FMotionTrailEditorModeCommands::Register();
	TrailTools.Add(UMotionTrailEditorMode::DefaultToolName);
	UTrailToolManagerBuilder* DefaultTrailToolManagerBuilder = NewObject<UTrailToolManagerBuilder>();
	DefaultTrailToolManagerBuilder->SetMotionTrailEditorMode(this);
	DefaultTrailToolManagerBuilder->SetTrailToolName(UMotionTrailEditorMode::DefaultToolName);
	RegisterTool(FMotionTrailEditorModeCommands::Get().Default, UMotionTrailEditorMode::DefaultToolName, DefaultTrailToolManagerBuilder);

	OnSequencersChangedHandle = FLevelEditorSequencerIntegration::Get().GetOnSequencersChanged().AddLambda([this] {
		TrailHierarchies.Reset();
		TrailTools[DefaultToolName].Reset();
		// TODO: kind of cheap for now, later should check with member TMap<ISequencer*, FTrailHierarchy*> TrackedSequencers
		for (TWeakPtr<ISequencer> WeakSequencer : FLevelEditorSequencerIntegration::Get().GetSequencers())
		{
			TrailHierarchies.Add_GetRef(MakeUnique<FSequencerTrailHierarchy>(this, WeakSequencer))->Initialize();
		}
	});

	for (TWeakPtr<ISequencer> WeakSequencer : FLevelEditorSequencerIntegration::Get().GetSequencers())
	{
		TrailHierarchies.Add_GetRef(MakeUnique<FSequencerTrailHierarchy>(this, WeakSequencer))->Initialize();
	}

	GetToolManager()->ConfigureChangeTrackingMode(EToolChangeTrackingMode::NoChangeTracking);

	ActivateDefaultTool();
}

void UMotionTrailEditorMode::Exit()
{
	for (TUniquePtr<FTrailHierarchy>& TrailHierarchy : TrailHierarchies)
	{
		// TODO: just use dtor?
		TrailHierarchy->Destroy();
	}
	TrailHierarchies.Reset();
	TrailTools.Reset();

	TrailOptions->OnDisplayPropertyChanged.Clear();

	FLevelEditorSequencerIntegration::Get().GetOnSequencersChanged().Remove(OnSequencersChangedHandle);

	// Call base Exit method to ensure proper cleanup
	Super::Exit();
}

void UMotionTrailEditorMode::CreateToolkit()
{
	FMotionTrailEditorModeToolkit* MotionTrailToolkit = new FMotionTrailEditorModeToolkit;
	Toolkit = MakeShareable(MotionTrailToolkit);
}


void UMotionTrailEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	if (!TrailOptions->bShowTrails)
	{
		return;
	}

	for (TUniquePtr<FTrailHierarchy>& TrailHierarchy : TrailHierarchies)
	{
		TrailHierarchy->Update();
	}

	for (TUniquePtr<FTrailHierarchy>& TrailHierarchy : TrailHierarchies)
	{
		TrailHierarchy->GetRenderer()->Render(View, Viewport, PDI);
	}

	Super::Render(View, Viewport, PDI);
}

void UMotionTrailEditorMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	for (TUniquePtr<FTrailHierarchy>& TrailHierarchy : TrailHierarchies)
	{
		TrailHierarchy->GetRenderer()->DrawHUD(ViewportClient, Viewport, View, Canvas);
	}

	Super::DrawHUD(ViewportClient, Viewport, View, Canvas);
}

bool UMotionTrailEditorMode::UsesToolkits() const
{
	return true;
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UMotionTrailEditorMode::GetModeCommands() const
{
	const TMap<FName, TArray<TSharedPtr<FUICommandInfo>>>& Commands = FMotionTrailEditorModeCommands::Get().GetCommands();
	if (Commands.Num() > 1)
	{
		return Commands;
	}

	return TMap<FName, TArray<TSharedPtr<FUICommandInfo>>>();
}

void UMotionTrailEditorMode::AddTrailTool(const FString& ToolType, FInteractiveTrailTool* TrailTool)
{
	checkf(ToolType.Equals(DefaultToolName), TEXT("Only default tool supported for now"))

	TrailTools[ToolType].Add(TrailTool);
	if (UTrailToolManager* ToolManager = Cast<UTrailToolManager>(GetToolManager()->GetActiveTool(EToolSide::Mouse)))
	{
		TrailTool->SetMotionTrailEditorMode(this);
		TrailTool->Setup();
	}
}

void UMotionTrailEditorMode::RemoveTrailTool(const FString& ToolType, FInteractiveTrailTool* TrailTool)
{
	TrailTools[ToolType].Remove(TrailTool);
}

void UMotionTrailEditorMode::RefreshNonDefaultToolset()
{
	TArray<TSharedPtr<FUICommandInfo>> NewNonDefaultCommands;
	for (const TPair<FString, TSet<FInteractiveTrailTool*>>& ToolPair : TrailTools)
	{
		if (ToolPair.Key.Equals(UMotionTrailEditorMode::DefaultToolName))
		{
			continue;
		}

		TSharedPtr<FUICommandInfo> NewUICommand = (*ToolPair.Value.CreateConstIterator())->GetStaticUICommandInfo();
		NewNonDefaultCommands.Add(NewUICommand);
		
		UTrailToolManagerBuilder* NewTrailToolManagerBuilder = NewObject<UTrailToolManagerBuilder>();
		NewTrailToolManagerBuilder->SetMotionTrailEditorMode(this);
		NewTrailToolManagerBuilder->SetTrailToolName(ToolPair.Key);
		RegisterTool(NewUICommand, ToolPair.Key, NewTrailToolManagerBuilder);

	}
	FMotionTrailEditorModeCommands::RegisterDynamic("Curve Specific Tools", NewNonDefaultCommands);
}

void UMotionTrailEditorMode::ActivateDefaultTool()
{
	ToolsContext->StartTool(UMotionTrailEditorMode::DefaultToolName);
}

bool UMotionTrailEditorMode::IsCompatibleWith(FEditorModeID OtherModeID) const
{
	return OtherModeID == TEXT("EM_SequencerMode") || OtherModeID == TEXT("EditMode.ControlRig");
}

#undef LOCTEXT_NAMESPACE
