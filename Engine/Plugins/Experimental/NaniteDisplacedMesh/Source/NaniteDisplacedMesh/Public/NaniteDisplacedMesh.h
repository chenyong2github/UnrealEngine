// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "RenderCommandFence.h"
#include "Rendering/NaniteResources.h"
#include "UObject/ObjectMacros.h"

#include "NaniteDisplacedMesh.generated.h"

class FNaniteBuildAsyncCacheTask;
class UNaniteDisplacedMesh;
class UTexture;

USTRUCT(BlueprintType)
struct FNaniteDisplacedMeshParams
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA

	UPROPERTY(EditAnywhere, Category = Mesh)
	TObjectPtr<class UStaticMesh> BaseMesh;

	//UPROPERTY(EditAnywhere, Category = Mesh)
	//float RelativeError;

	// Displacement 1

	UPROPERTY(EditAnywhere, Category = Displacement1)
	TObjectPtr<class UTexture2D> Displacement1;

	UPROPERTY(EditAnywhere, Category = Displacement1)
	float Magnitude1;

	UPROPERTY(EditAnywhere, Category = Displacement1)
	float Center1;

	// Displacement 2

	UPROPERTY(EditAnywhere, Category = Displacement2)
	TObjectPtr<class UTexture2D> Displacement2;

	UPROPERTY(EditAnywhere, Category = Displacement2)
	float Magnitude2;

	UPROPERTY(EditAnywhere, Category = Displacement2)
	float Center2;

	// Displacement 3

	UPROPERTY(EditAnywhere, Category = Displacement3)
	TObjectPtr<class UTexture2D> Displacement3;

	UPROPERTY(EditAnywhere, Category = Displacement3)
	float Magnitude3;

	UPROPERTY(EditAnywhere, Category = Displacement3)
	float Center3;

	// Displacement 4

	UPROPERTY(EditAnywhere, Category = Displacement4)
	TObjectPtr<class UTexture2D> Displacement4;

	UPROPERTY(EditAnywhere, Category = Displacement4)
	float Magnitude4;

	UPROPERTY(EditAnywhere, Category = Displacement4)
	float Center4;

	/** Default settings. */
	FNaniteDisplacedMeshParams()
		: BaseMesh(nullptr)
		//, RelativeError(0.0f)
		, Displacement1(nullptr)
		, Magnitude1(0.0f)
		, Center1(0.0f)
		, Displacement2(nullptr)
		, Magnitude2(0.0f)
		, Center2(0.0f)
		, Displacement3(nullptr)
		, Magnitude3(0.0f)
		, Center3(0.0f)
		, Displacement4(nullptr)
		, Magnitude4(0.0f)
		, Center4(0.0f)
	{
	}

	FNaniteDisplacedMeshParams(const FNaniteDisplacedMeshParams& Other)
		: BaseMesh(Other.BaseMesh)
		//, RelativeError(Other.RelativeError)
		, Displacement1(Other.Displacement1)
		, Magnitude1(Other.Magnitude1)
		, Center1(Other.Center1)
		, Displacement2(Other.Displacement2)
		, Magnitude2(Other.Magnitude2)
		, Center2(Other.Center2)
		, Displacement3(Other.Displacement3)
		, Magnitude3(Other.Magnitude3)
		, Center3(Other.Center3)
		, Displacement4(Other.Displacement4)
		, Magnitude4(Other.Magnitude4)
		, Center4(Other.Center4)
	{
	}

	/** Equality operator. */
	bool operator==(const FNaniteDisplacedMeshParams& Other) const
	{
		return BaseMesh == Other.BaseMesh
			//&& RelativeError == Other.RelativeError
			&& Displacement1 == Other.Displacement1
			&& Magnitude1 == Other.Magnitude1
			&& Center1 == Other.Center1
			&& Displacement2 == Other.Displacement2
			&& Magnitude2 == Other.Magnitude2
			&& Center2 == Other.Center2
			&& Displacement3 == Other.Displacement3
			&& Magnitude3 == Other.Magnitude3
			&& Center3 == Other.Center3
			&& Displacement4 == Other.Displacement4
			&& Magnitude4 == Other.Magnitude4
			&& Center4 == Other.Center4;
	}

	/** Inequality operator. */
	bool operator!=(const FNaniteDisplacedMeshParams& Other) const
	{
		return !(*this == Other);
	}
#endif // WITH_EDITORONLY_DATA
};

struct FNaniteData
{
	Nanite::FResources Resources;

	// Material section information that matches displaced mesh.
	FStaticMeshSectionArray MeshSections;
};

UCLASS()
class NANITEDISPLACEDMESH_API UNaniteDisplacedMesh : public UObject
{
	GENERATED_BODY()

	friend class FNaniteBuildAsyncCacheTask;

public:
	UNaniteDisplacedMesh(const FObjectInitializer& Init);

	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual bool IsReadyForFinishDestroy() override;

	void InitResources();
	void ReleaseResources();

	bool HasValidNaniteData() const;

	inline Nanite::FResources* GetNaniteData()
	{
		return &Data.Resources;
	}

	inline const Nanite::FResources* GetNaniteData() const
	{
		return &Data.Resources;
	}

	inline const FStaticMeshSectionArray& GetMeshSections() const
	{
		return Data.MeshSections;
	}

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
	virtual bool IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform) override;
	virtual void ClearAllCachedCookedPlatformData() override;
#endif

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Parameters, meta=(EditCondition = "bIsEditable"))
	FNaniteDisplacedMeshParams Parameters;

	/**
	 * Was this asset created by a procedural tool?
	 * This flag is generally set by tool that created the asset.
	 * It's used to tell the users that they shouldn't modify the asset by themselves.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Asset, AdvancedDisplay)
	bool bIsEditable = true;
#endif

private:
	bool bIsInitialized = false;

	// Data used to render this displaced mesh with Nanite.
	FNaniteData Data;

	FRenderCommandFence ReleaseResourcesFence;

#if WITH_EDITOR
	FIoHash CreateDerivedDataKeyHash(const ITargetPlatform* TargetPlatform);
	FIoHash BeginCacheDerivedData(const ITargetPlatform* TargetPlatform);
	bool PollCacheDerivedData(const FIoHash& KeyHash) const;
	void EndCacheDerivedData(const FIoHash& KeyHash);

	/** Synchronously cache and return derived data for the target platform. */
	FNaniteData& CacheDerivedData(const ITargetPlatform* TargetPlatform);

	FIoHash DataKeyHash;
	TMap<FIoHash, TUniquePtr<FNaniteData>> DataByPlatformKeyHash;
	TMap<FIoHash, TPimplPtr<FNaniteBuildAsyncCacheTask>> CacheTasksByKeyHash;
#endif
};

