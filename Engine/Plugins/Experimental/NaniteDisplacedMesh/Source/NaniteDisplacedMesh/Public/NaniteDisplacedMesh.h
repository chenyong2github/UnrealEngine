// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Async/Future.h"
#include "NaniteDisplacedMesh.generated.h"

class UTexture;

UCLASS(config=Engine, defaultconfig)
class NANITEDISPLACEDMESH_API UNaniteDisplacedMeshSettings : public UObject
{
	GENERATED_BODY()

public:

};

UCLASS()
class NANITEDISPLACEDMESH_API UNaniteDisplacedMesh : public UObject
{
	GENERATED_BODY()

public:
	UNaniteDisplacedMesh(const FObjectInitializer& Init);

	/** UObject Interface */
#if WITH_EDITOR
	//virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//virtual bool Modify(bool bAlwaysMarkDirty = true) override;
#endif
	//virtual void PostLoad() override;
	//virtual void BeginDestroy() override;
	/** End UObject Interface */

	//virtual void Serialize(FArchive& Ar) override;

	//void InitResources();
	//void ReleaseResources();

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mesh)
	TObjectPtr<class UStaticMesh> BaseMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mesh)
	int32 TessellationLevel;

	// Displacement 1

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement1)
	TObjectPtr<class UTexture2D> DisplacementMap1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement1)
	float Magnitude1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement1)
	float Bias1;

	// Displacement 2

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement2)
	TObjectPtr<class UTexture2D> DisplacementMap2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement2)
	float Magnitude2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement2)
	float Bias2;

	// Displacement 3

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement3)
	TObjectPtr<class UTexture2D> DisplacementMap3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement3)
	float Magnitude3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement3)
	float Bias3;

	// Displacement 4

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement4)
	TObjectPtr<class UTexture2D> DisplacementMap4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement4)
	float Magnitude4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement4)
	float Bias4;
#endif

public:
	/** Pointer to the data used to render this displaced mesh with Nanite. */
	//TUniquePtr<class FNaniteDisplacedMeshData> NaniteData;
};
