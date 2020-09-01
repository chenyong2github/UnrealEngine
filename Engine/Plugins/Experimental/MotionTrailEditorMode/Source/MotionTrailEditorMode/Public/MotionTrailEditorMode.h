// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"
#include "ISequencer.h"

#include "TrailHierarchy.h"

#include "MotionTrailEditorMode.generated.h"

class FInteractiveTrailTool;

DECLARE_LOG_CATEGORY_EXTERN(LogMotionTrailEditorMode, Log, All);

// TODO: option to make tick size proportional to distance from camera to get a sense of perspective and scale
UCLASS()
class UMotionTrailOptions : public UObject
{
	GENERATED_BODY()

public:
	UMotionTrailOptions()
		: bShowTrails(true)
		, bShowFullTrail(true)
		, FramesBefore(10)
		, FramesAfter(10)
		, Subdivisions(100)
		, bLockTicksToFrames(true)
		, SecondsPerTick(0.1)
		, TickSize(4.0)
		, TrailThickness(0.0f)
	{}

	UPROPERTY(EditAnywhere, Category = ShowOptions)
	bool bShowTrails;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "bShowTrails"))
	bool bShowFullTrail;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "!bShowFullTrail && bShowTrails", ClampMin = "0"))
	int32 FramesBefore;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "!bShowFullTrail && bShowTrails", ClampMin = "0"))
	int32 FramesAfter;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "bShowTrails", ClampMin = "2"))
	int32 Subdivisions;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "bShowTrails"))
	bool bLockTicksToFrames;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "bShowTrails && !bLockTicksToFrames", ClampMin = "0.01"))
	double SecondsPerTick;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "bShowTrails", ClampMin = "0.0"))
	double TickSize;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "bShowTrails", ClampMin = "0.0"))
	float TrailThickness;

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override
	{
		FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
		OnDisplayPropertyChanged.Broadcast(PropertyName);
	}

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDisplayPropertyChanged, FName);
	FOnDisplayPropertyChanged OnDisplayPropertyChanged;
};

UCLASS()
class UMotionTrailEditorMode : public UEdMode
{
	GENERATED_BODY()
public:
	static FEditorModeID ModeName;


	static FName MotionTrailEditorMode_Default; // Palette name
	static FString DefaultToolName;

	UMotionTrailEditorMode();
	virtual ~UMotionTrailEditorMode();

	// UEdMode interface
	virtual void Enter() override;
	virtual void Exit() override;
	void CreateToolkit() override;
	
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	
	bool UsesToolkits() const override;

	virtual TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> GetModeCommands() const override;
	virtual void ActivateDefaultTool() override;

	virtual class FEdMode* AsLegacyMode() override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override;
	// End of UEdMode interface

	void AddTrailTool(const FString& ToolType, UE::MotionTrailEditor::FInteractiveTrailTool* TrailTool);
	void RemoveTrailTool(const FString& ToolType, UE::MotionTrailEditor::FInteractiveTrailTool* TrailTool);

	const TMap<FString, TSet<UE::MotionTrailEditor::FInteractiveTrailTool*>>& GetTrailTools() const { return TrailTools; }
	UMotionTrailOptions* GetTrailOptions() const { return TrailOptions; }

	UE::MotionTrailEditor::FTrailHierarchy* GetHierarchyForSequencer(ISequencer* Sequencer) const { return SequencerHierarchies[Sequencer]; }
	const TArray<TUniquePtr<UE::MotionTrailEditor::FTrailHierarchy>>& GetHierarchies() const { return TrailHierarchies; }

private:
	
	void RefreshNonDefaultToolset();

	UPROPERTY(Transient)
	UMotionTrailOptions* TrailOptions;

	TMap<FString, TSet<UE::MotionTrailEditor::FInteractiveTrailTool*>> TrailTools;

	TArray<TUniquePtr<UE::MotionTrailEditor::FTrailHierarchy>> TrailHierarchies;
	TMap<ISequencer*, UE::MotionTrailEditor::FTrailHierarchy*> SequencerHierarchies;

	FDelegateHandle OnSequencersChangedHandle;
};
