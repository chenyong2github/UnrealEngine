// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Camera/PlayerCameraManager.h"

#include "DisplayClusterEnums.h"

#include "DisplayClusterRootActor.generated.h"

class UDisplayClusterRootComponent;
class UMaterial;


/**
 * VR root. This contains nDisplay VR hierarchy in the game.
 */
UCLASS()
class DISPLAYCLUSTER_API ADisplayClusterRootActor
	: public AActor
{
	GENERATED_BODY()

public:
	ADisplayClusterRootActor(const FObjectInitializer& ObjectInitializer);

	inline UDisplayClusterRootComponent* GetDisplayClusterRootComponent() const
	{ return DisplayClusterRootComponent; }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// AActor
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;
	virtual void Tick(float DeltaSeconds) override;

	
public:
	bool GetShowProjectionScreens() const
	{ return bShowProjectionScreens; }

	UMaterial* GetProjectionScreenMaterial() const
	{ return ProjectionScreensMaterial; }

protected:
	UPROPERTY(EditAnywhere, Category = "DisplayCluster", meta = (DisplayName = "Exit when ESC pressed"))
	bool bExitOnEsc;

	UPROPERTY(EditAnywhere, Category = "DisplayCluster", meta = (DisplayName = "Show projection screens"))
	bool bShowProjectionScreens;

	UPROPERTY(EditAnywhere, Category = "DisplayCluster", meta = (DisplayName = "Projection screens material"))
	UMaterial* ProjectionScreensMaterial;

#if WITH_EDITORONLY_DATA
public:
	FString GetEditorConfigPath() const
	{ return EditorConfigPath; }

	FString GetEditorNodeId() const
	{ return EditorNodeId; }

protected:
	UPROPERTY(EditAnywhere, Category = "DisplayCluster (Editor only)", meta = (DisplayName = "Config file"))
	FString EditorConfigPath;

	UPROPERTY(EditAnywhere, Category = "DisplayCluster (Editor only)", meta = (DisplayName = "Node ID"))
	FString EditorNodeId;
#endif

protected:
	/** Camera component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DisplayCluster")
	UDisplayClusterRootComponent* DisplayClusterRootComponent;

private:
	// Current operation mode
	EDisplayClusterOperationMode OperationMode = EDisplayClusterOperationMode::Disabled;
};
