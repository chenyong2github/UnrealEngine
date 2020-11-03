// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "DestructibleFractureSettings.h"
#include "DestructibleChunkParamsProxy.generated.h"

class IDestructibleMeshEditor;
class UDestructibleMesh;

UCLASS()
class UE_DEPRECATED(4.26, "APEX is deprecated. Destruction in future will be supported using Chaos Destruction.") UDestructibleChunkParamsProxy : public UObject
{
	GENERATED_UCLASS_BODY()

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UPROPERTY()
	UDestructibleMesh* DestructibleMesh;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	UPROPERTY()
	int32 ChunkIndex;

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UPROPERTY(EditAnywhere, Category=Chunks)
	FDestructibleChunkParameters ChunkParams;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#if WITH_EDITOR
	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	// End of UObject interface

	TWeakPtr<IDestructibleMeshEditor> DestructibleMeshEditorPtr;
#endif
};
