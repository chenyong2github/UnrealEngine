// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"

#include "TrailHierarchy.h"

#include "MotionTrailEditorMode.generated.h"

class FInteractiveTrailTool;

DECLARE_LOG_CATEGORY_EXTERN(LogMotionTrailEditorMode, Log, All);

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
		, SecondsPerSegment(0.1)
		, bLockTicksToFrames(true)
		, SecondsPerTick(0.1)
		, TickSize(4.0)
	{}

	UPROPERTY(EditAnywhere, Category = ShowOptions)
	bool bShowTrails;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "bShowTrails"))
	bool bShowFullTrail;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "!bShowFullTrail && bShowTrails", ClampMin = "0"))
	int32 FramesBefore;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "!bShowFullTrail && bShowTrails", ClampMin = "0"))
	int32 FramesAfter;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "bShowTrails", ClampMin = "0.0001"))
	double SecondsPerSegment;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "bShowTrails"))
	bool bLockTicksToFrames;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "bShowTrails && !bLockTicksToFrames", ClampMin = "0.0001"))
	double SecondsPerTick;

	UPROPERTY(EditAnywhere, Category = ShowOptions, Meta = (EditCondition = "bShowTrails", ClampMin = "0.0"))
	double TickSize;

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
	const static FEditorModeID EM_MotionTrailEditorModeId;


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

	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override;
	// End of FEdMode interface

	void AddTrailTool(const FString& ToolType, FInteractiveTrailTool* TrailTool);
	void RemoveTrailTool(const FString& ToolType, FInteractiveTrailTool* TrailTool);

	const TMap<FString, TSet<FInteractiveTrailTool*>>& GetTrailTools() const { return TrailTools; }
	UMotionTrailOptions* GetTrailOptions() const { return TrailOptions; }

private:
	
	void RefreshNonDefaultToolset();

	UPROPERTY(Transient)
	UMotionTrailOptions* TrailOptions;

	TMap<FString, TSet<FInteractiveTrailTool*>> TrailTools;

	TArray<TUniquePtr<FTrailHierarchy>> TrailHierarchies;

	FDelegateHandle OnSequencersChangedHandle;
};
