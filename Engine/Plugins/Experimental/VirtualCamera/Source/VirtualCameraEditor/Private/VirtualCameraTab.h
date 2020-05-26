// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UnrealEdMisc.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "VirtualCameraTab.generated.h"

class AVirtualCameraActor;
class FWorkspaceItem;
class IDetailsView;
class SSplitter;
class UWorld;


UCLASS()
class UVirtualCameraTabUserData : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "VirtualCamera")
	FVector2D TargetDeviceResolution;

	UPROPERTY(EditAnywhere, Category = "VirtualCamera")
	TSoftObjectPtr<AVirtualCameraActor> VirtualCameraActor;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "VirtualCamera")
	int16 Port;
};


class SVirtualCameraTab : public SCompoundWidget, FGCObject
{
public:

	static void RegisterNomadTabSpawner(TSharedRef<FWorkspaceItem> InWorkspaceGroup);
	static void UnregisterNomadTabSpawner();

public:

	SLATE_BEGIN_ARGS(SVirtualCameraTab){}
	SLATE_END_ARGS()

	virtual ~SVirtualCameraTab() override;

	void Construct(const FArguments& InArgs);

	//~ Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//~ End FGCObject interface

	bool IsStreaming() const;
	bool CanStream() const;
	bool StartStreaming();
	bool StopStreaming();

private:

	TSharedRef<class SWidget> MakeToolBar();
	void OnMapChanged(UWorld* World, EMapChangeType ChangeType);
	void OnPropertyChanged(const FPropertyChangedEvent& InEvent);

private:

	TSharedPtr<IDetailsView> DetailView;
	TSharedPtr<SSplitter> Splitter;

	//~ Begin GC by AddReferencedObjects
	UVirtualCameraTabUserData* WidgetUserData;
	//~ End GC by AddReferencedObjects
};
