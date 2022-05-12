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

USTRUCT(BlueprintType)
struct FNaniteDisplacedMeshParams
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA

	UPROPERTY(EditAnywhere, Category = Mesh)
	TObjectPtr<class UStaticMesh> BaseMesh;

	UPROPERTY(EditAnywhere, Category = Mesh)
	int32 TessellationLevel;

	// Displacement 1

	UPROPERTY(EditAnywhere, Category = Displacement1)
	TObjectPtr<class UTexture2D> DisplacementMap1;

	UPROPERTY(EditAnywhere, Category = Displacement1)
	float Magnitude1;

	UPROPERTY(EditAnywhere, Category = Displacement1)
	float Bias1;

	// Displacement 2

	UPROPERTY(EditAnywhere, Category = Displacement2)
	TObjectPtr<class UTexture2D> DisplacementMap2;

	UPROPERTY(EditAnywhere, Category = Displacement2)
	float Magnitude2;

	UPROPERTY(EditAnywhere, Category = Displacement2)
	float Bias2;

	// Displacement 3

	UPROPERTY(EditAnywhere, Category = Displacement3)
	TObjectPtr<class UTexture2D> DisplacementMap3;

	UPROPERTY(EditAnywhere, Category = Displacement3)
	float Magnitude3;

	UPROPERTY(EditAnywhere, Category = Displacement3)
	float Bias3;

	// Displacement 4

	UPROPERTY(EditAnywhere, Category = Displacement4)
	TObjectPtr<class UTexture2D> DisplacementMap4;

	UPROPERTY(EditAnywhere, Category = Displacement4)
	float Magnitude4;

	UPROPERTY(EditAnywhere, Category = Displacement4)
	float Bias4;

	/** Default settings. */
	FNaniteDisplacedMeshParams()
		: BaseMesh(nullptr)
		, TessellationLevel(0)
		, DisplacementMap1(nullptr)
		, Magnitude1(0.0f)
		, Bias1(0.0f)
		, DisplacementMap2(nullptr)
		, Magnitude2(0.0f)
		, Bias2(0.0f)
		, DisplacementMap3(nullptr)
		, Magnitude3(0.0f)
		, Bias3(0.0f)
		, DisplacementMap4(nullptr)
		, Magnitude4(0.0f)
		, Bias4(0.0f)
	{
	}

	FNaniteDisplacedMeshParams(const FNaniteDisplacedMeshParams& Other)
		: BaseMesh(Other.BaseMesh)
		, TessellationLevel(Other.TessellationLevel)
		, DisplacementMap1(Other.DisplacementMap1)
		, Magnitude1(Other.Magnitude1)
		, Bias1(Other.Bias1)
		, DisplacementMap2(Other.DisplacementMap2)
		, Magnitude2(Other.Magnitude2)
		, Bias2(Other.Bias2)
		, DisplacementMap3(Other.DisplacementMap3)
		, Magnitude3(Other.Magnitude3)
		, Bias3(Other.Bias3)
		, DisplacementMap4(Other.DisplacementMap4)
		, Magnitude4(Other.Magnitude4)
		, Bias4(Other.Bias4)
	{
	}

	/** Equality operator. */
	bool operator==(const FNaniteDisplacedMeshParams& Other) const
	{
		return BaseMesh == Other.BaseMesh
			&& TessellationLevel == Other.TessellationLevel
			&& DisplacementMap1 == Other.DisplacementMap1
			&& Magnitude1 == Other.Magnitude1
			&& Bias1 == Other.Bias1
			&& DisplacementMap2 == Other.DisplacementMap2
			&& Magnitude2 == Other.Magnitude2
			&& Bias2 == Other.Bias2
			&& DisplacementMap3 == Other.DisplacementMap3
			&& Magnitude3 == Other.Magnitude3
			&& Bias3 == Other.Bias3
			&& DisplacementMap4 == Other.DisplacementMap4
			&& Magnitude4 == Other.Magnitude4
			&& Bias4 == Other.Bias4;
	}

	/** Inequality operator. */
	bool operator!=(const FNaniteDisplacedMeshParams& Other) const
	{
		return !(*this == Other);
	}
#endif // WITH_EDITORONLY_DATA
};

class FNaniteDisplacedMeshData
{
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
	/** Pointer to the data used to render this displaced mesh with Nanite. */
	TUniquePtr<class FNaniteDisplacedMeshData> NaniteData;

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Parameters)
	FNaniteDisplacedMeshParams Parameters;
#endif
};
