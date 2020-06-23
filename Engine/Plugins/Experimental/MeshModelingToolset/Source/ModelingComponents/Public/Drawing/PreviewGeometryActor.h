// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolObjects.h"
#include "Drawing/LineSetComponent.h"

#include "PreviewGeometryActor.generated.h"

/**
 * An actor suitable for attaching components used to draw preview elements, such as LineSetComponent and TriangleSetComponent.
 */
UCLASS()
class MODELINGCOMPONENTS_API APreviewGeometryActor : public AInternalToolFrameworkActor
{
	GENERATED_BODY()
private:
	APreviewGeometryActor()
	{
#if WITH_EDITORONLY_DATA
		// hide this actor in the scene outliner
		bListedInSceneOutliner = false;
#endif
	}

public:
};


/**
 * UPreviewGeometry creates and manages an APreviewGeometryActor and a set of preview geometry Components.
 * Preview geometry Components are identified by strings.
 */
UCLASS(Transient)
class MODELINGCOMPONENTS_API UPreviewGeometry : public UObject
{
	GENERATED_BODY()

public:
	virtual ~UPreviewGeometry();

	/**
	 * Create preview mesh in the World with the given transform
	 */
	UFUNCTION()
	void CreateInWorld(UWorld* World, const FTransform& WithTransform);

	/**
	 * Remove and destroy preview mesh
	 */
	UFUNCTION()
	void Disconnect();

	/**
	 *
	 */
	UFUNCTION()
	APreviewGeometryActor* GetActor() const { return ParentActor;  }



	//
	// Line Sets
	//
	UFUNCTION()
	ULineSetComponent* AddLineSet(const FString& LineSetIdentifier);

	UFUNCTION()
	ULineSetComponent* FindLineSet(const FString& LineSetIdentifier);

	UFUNCTION()
	bool RemoveLineSet(const FString& LineSetIdentifier, bool bDestroy = true);

	UFUNCTION()
	bool SetLineSetVisibility(const FString& LineSetIdentifier, bool bVisible);


	//
	// Line Set Utilities
	//

	/**
	 * Find the identified line set and call UpdateFuncType(ULineSetComponent*)
	 */
	template<typename UpdateFuncType>
	void UpdateLineSet(const FString& LineSetIdentifier, UpdateFuncType UpdateFunc)
	{
		ULineSetComponent* LineSet = FindLineSet(LineSetIdentifier);
		if (LineSet)
		{
			UpdateFunc(LineSet);
		}
	}

	/**
	 * Add a set of lines produced by calling LineGenFunc for each index in range [0,NumIndices)
	 */
	void CreateOrUpdateLineSet(const FString& LineSetIdentifier, int32 NumIndices,
		TFunctionRef<void(int32 Index, TArray<FRenderableLine>& LinesOut)> LineGenFunc,
		int32 LinesPerIndexHint = -1);


public:

	/**  */
	UPROPERTY()
	APreviewGeometryActor* ParentActor = nullptr;

	UPROPERTY()
	TMap<FString, ULineSetComponent*> LineSets;

};