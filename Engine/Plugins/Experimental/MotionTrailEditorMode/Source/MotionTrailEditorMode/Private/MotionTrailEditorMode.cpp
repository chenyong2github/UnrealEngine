// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrailEditorMode.h"
#include "MotionTrailEditorModeToolkit.h"
#include "MotionTrailEditorModeCommands.h"
#include "Toolkits/ToolkitManager.h"
#include "EditorModeManager.h"
#include "EdModeInteractiveToolsContext.h"
#include "EdMode.h"

#include "LevelEditorSequencerIntegration.h"
#include "ISequencer.h"
#include "Sequencer/SequencerTrailHierarchy.h"

#include "Sequencer/MovieSceneTransformTrail.h"


#define LOCTEXT_NAMESPACE "MotionTrailEditorMode"

DEFINE_LOG_CATEGORY(LogMotionTrailEditorMode);

FEditorModeID UMotionTrailEditorMode::ModeName = TEXT("MotionTrailEditorMode");

FName UMotionTrailEditorMode::MotionTrailEditorMode_Default = FName(TEXT("Default"));

FString UMotionTrailEditorMode::DefaultToolName = TEXT("DefaultTool");

UMotionTrailEditorMode::UMotionTrailEditorMode()
{
	SettingsClass = UMotionTrailOptions::StaticClass();

	// TODO: make invisible, but for some reason when invisible the toolkit doesn't show
	// Not a todo, but when multiple modes are active they can't have their toolkits open at the same time
	Info = FEditorModeInfo(
		ModeName,
		LOCTEXT("ModeName", "Motion Trail Editor"),
		FSlateIcon(),
		true
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
	UE::MotionTrailEditor::FMotionTrailEditorModeCommands::Register();
	TrailTools.Add(UMotionTrailEditorMode::DefaultToolName);
	UTrailToolManagerBuilder* DefaultTrailToolManagerBuilder = NewObject<UTrailToolManagerBuilder>();
	DefaultTrailToolManagerBuilder->SetMotionTrailEditorMode(this);
	DefaultTrailToolManagerBuilder->SetTrailToolName(UMotionTrailEditorMode::DefaultToolName);
	RegisterTool(UE::MotionTrailEditor::FMotionTrailEditorModeCommands::Get().Default, UMotionTrailEditorMode::DefaultToolName, DefaultTrailToolManagerBuilder);

	OnSequencersChangedHandle = FLevelEditorSequencerIntegration::Get().GetOnSequencersChanged().AddLambda([this] {
		for (const TUniquePtr<UE::MotionTrailEditor::FTrailHierarchy>& TrailHierarchy : TrailHierarchies)
		{
			TrailHierarchy->Destroy();
		}

		TrailHierarchies.Reset();
		SequencerHierarchies.Reset();
		TrailTools[DefaultToolName].Reset();
		// TODO: kind of cheap for now, later should check with member TMap<ISequencer*, FTrailHierarchy*> TrackedSequencers
		for (TWeakPtr<ISequencer> WeakSequencer : FLevelEditorSequencerIntegration::Get().GetSequencers())
		{
			TrailHierarchies.Add_GetRef(MakeUnique<UE::MotionTrailEditor::FSequencerTrailHierarchy>(this, WeakSequencer))->Initialize();
			SequencerHierarchies.Add(WeakSequencer.Pin().Get(), TrailHierarchies.Last().Get());
		}
	});

	for (TWeakPtr<ISequencer> WeakSequencer : FLevelEditorSequencerIntegration::Get().GetSequencers())
	{
		TrailHierarchies.Add_GetRef(MakeUnique<UE::MotionTrailEditor::FSequencerTrailHierarchy>(this, WeakSequencer))->Initialize();
		SequencerHierarchies.Add(WeakSequencer.Pin().Get(), TrailHierarchies.Last().Get());
	}

	GetToolManager()->ConfigureChangeTrackingMode(EToolChangeTrackingMode::NoChangeTracking);

	ActivateDefaultTool();
}

void UMotionTrailEditorMode::Exit()
{
	for (TUniquePtr<UE::MotionTrailEditor::FTrailHierarchy>& TrailHierarchy : TrailHierarchies)
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
	if (!Toolkit.IsValid())
	{
		UE::MotionTrailEditor::FMotionTrailEditorModeToolkit* MotionTrailToolkit = new UE::MotionTrailEditor::FMotionTrailEditorModeToolkit;
		Toolkit = MakeShareable(MotionTrailToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}
	Super::CreateToolkit();
}


void UMotionTrailEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	if (!TrailOptions->bShowTrails)
	{
		return;
	}

	for (TUniquePtr<UE::MotionTrailEditor::FTrailHierarchy>& TrailHierarchy : TrailHierarchies)
	{
		TrailHierarchy->Update();
	}

	for (TUniquePtr<UE::MotionTrailEditor::FTrailHierarchy>& TrailHierarchy : TrailHierarchies)
	{
		TrailHierarchy->GetRenderer()->Render(View, Viewport, PDI);
	}

	TArray<TMap<FString, FTimespan>> HierarchyTimingStats;
	HierarchyTimingStats.Reserve(TrailHierarchies.Num());
	for (TUniquePtr<UE::MotionTrailEditor::FTrailHierarchy>& TrailHierarchy : TrailHierarchies)
	{
		HierarchyTimingStats.Add(TrailHierarchy->GetTimingStats());
	}
	StaticCastSharedPtr<UE::MotionTrailEditor::FMotionTrailEditorModeToolkit>(Toolkit)->SetTimingStats(HierarchyTimingStats);

	Super::Render(View, Viewport, PDI);
}

void UMotionTrailEditorMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	if (!TrailOptions->bShowTrails)
	{
		return;
	}

	for (TUniquePtr<UE::MotionTrailEditor::FTrailHierarchy>& TrailHierarchy : TrailHierarchies)
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
	const TMap<FName, TArray<TSharedPtr<FUICommandInfo>>>& Commands = UE::MotionTrailEditor::FMotionTrailEditorModeCommands::Get().GetCommands();
	if (Commands.Num() > 1)
	{
		return Commands;
	}

	return TMap<FName, TArray<TSharedPtr<FUICommandInfo>>>();
}

void UMotionTrailEditorMode::AddTrailTool(const FString& ToolType, UE::MotionTrailEditor::FInteractiveTrailTool* TrailTool)
{
	checkf(ToolType.Equals(DefaultToolName), TEXT("Only default tool supported for now"))

	TrailTools[ToolType].Add(TrailTool);
	if (UTrailToolManager* ToolManager = Cast<UTrailToolManager>(GetToolManager()->GetActiveTool(EToolSide::Mouse)))
	{
		TrailTool->SetMotionTrailEditorMode(this);
		TrailTool->Setup();
	}
}

void UMotionTrailEditorMode::RemoveTrailTool(const FString& ToolType, UE::MotionTrailEditor::FInteractiveTrailTool* TrailTool)
{
	TrailTools[ToolType].Remove(TrailTool);
}

void UMotionTrailEditorMode::RefreshNonDefaultToolset()
{
	TArray<TSharedPtr<FUICommandInfo>> NewNonDefaultCommands;
	for (const TPair<FString, TSet<UE::MotionTrailEditor::FInteractiveTrailTool*>>& ToolPair : TrailTools)
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
	UE::MotionTrailEditor::FMotionTrailEditorModeCommands::RegisterDynamic("Curve Specific Tools", NewNonDefaultCommands);
}

void UMotionTrailEditorMode::ActivateDefaultTool()
{
	ToolsContext->StartTool(UMotionTrailEditorMode::DefaultToolName);
}

FEdMode* UMotionTrailEditorMode::AsLegacyMode()
{
	class FMotionTrailEditorMode : public FEdMode
	{
	public:
		FMotionTrailEditorMode()
			: FEdMode()
		{
			Owner = &GLevelEditorModeTools();
		}

		virtual ~FMotionTrailEditorMode() {};
		virtual bool UsesTransformWidget() const { return true; }
	};

	static TUniquePtr<FMotionTrailEditorMode> LegacyEdMode = MakeUnique<FMotionTrailEditorMode>();

	return LegacyEdMode.Get();
}

bool UMotionTrailEditorMode::IsCompatibleWith(FEditorModeID OtherModeID) const
{
	return OtherModeID == FName(TEXT("EM_SequencerMode"), FNAME_Find) || OtherModeID == FName(TEXT("EditMode.ControlRig"), FNAME_Find) || OtherModeID == FName(TEXT("EditMode.ControlRigEditor"), FNAME_Find);
}

#undef LOCTEXT_NAMESPACE
