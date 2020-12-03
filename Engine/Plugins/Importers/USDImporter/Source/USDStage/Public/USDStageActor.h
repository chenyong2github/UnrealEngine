// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Misc/SecureHash.h"
#include "Templates/Function.h"

#include "UnrealUSDWrapper.h"
#include "USDLevelSequenceHelper.h"
#include "USDListener.h"
#include "USDMemory.h"
#include "USDPrimTwin.h"
#include "USDSkeletalDataConversion.h"

#include "UsdWrappers/UsdStage.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/SdfPath.h"

#include "USDStageActor.generated.h"

class ALevelSequenceActor;
class IMeshBuilderModule;
class ULevelSequence;
class UMaterial;
class UUsdAsset;
enum class EMapChangeType : uint8;
enum class EUsdPurpose : int32;
struct FMeshDescription;
struct FUsdSchemaTranslationContext;

UCLASS( MinimalAPI )
class AUsdStageActor : public AActor
{
	GENERATED_BODY()

	friend struct FUsdStageActorImpl;
	friend class FUsdLevelSequenceHelperImpl;

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "USD")
	FFilePath RootLayer;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "USD")
	EUsdInitialLoadSet InitialLoadSet;

	/* Only load prims with these specific purposes from the USD file */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "USD", meta = (Bitmask, BitmaskEnum=EUsdPurpose))
	int32 PurposesToLoad;

public:
	UFUNCTION(BlueprintCallable, Category = "USD", meta = (CallInEditor = "true"))
	void SetRootLayer(const FString& RootFilePath );

	UFUNCTION(BlueprintCallable, Category = "USD", meta = (CallInEditor = "true"))
	void SetInitialLoadSet( EUsdInitialLoadSet NewLoadSet );

	UFUNCTION(BlueprintCallable, Category = "USD", meta = (CallInEditor = "true"))
	void SetPurposesToLoad( int32 NewPurposesToLoad );

	UFUNCTION(BlueprintCallable, Category = "USD", meta = (CallInEditor = "true"))
	float GetTime() const { return Time; }

	UFUNCTION(BlueprintCallable, Category = "USD", meta = (CallInEditor = "true"))
	void SetTime(float InTime);

private:
	UPROPERTY(Category = UsdStageActor, VisibleAnywhere, BlueprintReadOnly, meta = (ExposeFunctionCategories = "Mesh,Rendering,Physics,Components|StaticMesh", AllowPrivateAccess = "true"))
	class USceneComponent* SceneComponent;

	/* TimeCode to evaluate the USD stage at */
	UPROPERTY(EditAnywhere, Category = "USD")
	float Time;

	UPROPERTY()
	float StartTimeCode_DEPRECATED;

	UPROPERTY()
	float EndTimeCode_DEPRECATED;

	UPROPERTY()
	float TimeCodesPerSecond_DEPRECATED;

	UPROPERTY(VisibleAnywhere, Category = "USD", Transient)
	ULevelSequence* LevelSequence;

	UPROPERTY(Transient)
	TMap<FString, ULevelSequence*> LevelSequencesByIdentifier;

public:
	DECLARE_EVENT_OneParam( AUsdStageActor, FOnActorLoaded, AUsdStageActor* );
	USDSTAGE_API static FOnActorLoaded OnActorLoaded;

	DECLARE_EVENT( AUsdStageActor, FOnStageActorEvent );
	FOnStageActorEvent OnStageChanged;
	FOnStageActorEvent OnActorDestroyed;

	DECLARE_EVENT_TwoParams( AUsdStageActor, FOnPrimChanged, const FString&, bool );
	FOnPrimChanged OnPrimChanged;

	DECLARE_MULTICAST_DELEGATE(FOnUsdStageTimeChanged);
	FOnUsdStageTimeChanged OnTimeChanged;

public:
	AUsdStageActor();
	virtual ~AUsdStageActor();

	USDSTAGE_API void Reset() override;
	void Refresh() const;
	void ReloadAnimations();
	float GetTime() { return Time; }
	TMap< FString, UObject* > GetAssetsCache() { return AssetsCache; } // Intentional copies
	TMap< FString, UObject* > GetPrimPathsToAssets() { return PrimPathsToAssets; }
	TMap< FString, TMap< FString, int32 > > GetMaterialToPrimvarToUVIndex() { return MaterialToPrimvarToUVIndex; }

public:
#if WITH_EDITOR
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
#endif // WITH_EDITOR
	virtual void PostDuplicate( bool bDuplicateForPIE ) override;
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void Destroyed() override;

	void OnLevelAddedToWorld(ULevel* Level, UWorld* World);
	void OnLevelRemovedFromWorld(ULevel* Level, UWorld* World);

	void OnPreUsdImport( FString FilePath );
	void OnPostUsdImport( FString FilePath );

private:
	void Clear();
	void OpenUsdStage();
	void LoadUsdStage();

#if WITH_EDITOR
	void OnMapChanged(UWorld* World, EMapChangeType ChangeType);
	void OnBeginPIE(bool bIsSimulating);
	void OnPostPIEStarted(bool bIsSimulating);
#endif // WITH_EDITOR

	void UpdateSpawnedObjectsTransientFlag( bool bTransient );

	void OnPrimsChanged( const TMap< FString, bool >& PrimsChangedList );
	void OnUsdPrimTwinDestroyed( const UUsdPrimTwin& UsdPrimTwin );

	void OnObjectPropertyChanged( UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent );
	void HandlePropertyChangedEvent( FPropertyChangedEvent& PropertyChangedEvent );
	bool HasAuthorityOverStage() const;

private:
	UPROPERTY(Transient)
	UUsdPrimTwin* RootUsdTwin;

	UPROPERTY(Transient)
	TSet< FString > PrimsToAnimate;

	UPROPERTY( Transient )
	TMap< UObject*, FString > ObjectsToWatch;

private:
	/** Hash based assets cache */
	UPROPERTY(Transient)
	TMap< FString, UObject* > AssetsCache;

	/** Map of USD Prim Paths to UE assets */
	UPROPERTY(Transient)
	TMap< FString, UObject* > PrimPathsToAssets;

	/** Keep track of blend shapes so that we can map 'inbetween shapes' to their separate morph targets when animating */
	UsdUtils::FBlendShapeMap BlendShapesByPath;

	/**
	 * When parsing materials, we keep track of which primvar we mapped to which UV channel.
	 * When parsing meshes later, we use this data to place the correct primvar values in each UV channel.
	 * We keep this here as these are generated when the materials stored in AssetsCache are parsed, so it should accompany them
	 */
	TMap< FString, TMap< FString, int32 > > MaterialToPrimvarToUVIndex;

public:
	USDSTAGE_API UE::FUsdStage& GetUsdStage();
	USDSTAGE_API const UE::FUsdStage& GetUsdStage() const;

	FUsdListener& GetUsdListener() { return UsdListener; }
	const FUsdListener& GetUsdListener() const { return UsdListener; }

	/** Prevents writing back data to the USD stage whenever our LevelSequences are modified */
	USDSTAGE_API void StopMonitoringLevelSequence();
	USDSTAGE_API void ResumeMonitoringLevelSequence();

	UUsdPrimTwin* GetOrCreatePrimTwin( const UE::FSdfPath& UsdPrimPath );
	UUsdPrimTwin* ExpandPrim( const UE::FUsdPrim& Prim, FUsdSchemaTranslationContext& TranslationContext );
	void UpdatePrim( const UE::FSdfPath& UsdPrimPath, bool bResync, FUsdSchemaTranslationContext& TranslationContext );

protected:
	/** Loads the asset for a single prim */
	void LoadAsset( FUsdSchemaTranslationContext& TranslationContext, const UE::FUsdPrim& Prim );

	/** Loads the assets for all prims from StartPrim and its children */
	void LoadAssets( FUsdSchemaTranslationContext& TranslationContext, const UE::FUsdPrim& StartPrim );

	void AnimatePrims();

private:
	UE::FUsdStage UsdStage;
	FUsdListener UsdListener;

	FUsdLevelSequenceHelper LevelSequenceHelper;

	FDelegateHandle OnRedoHandle;
};
