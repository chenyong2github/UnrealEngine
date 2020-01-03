// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Editor/EditorEngine.h"
#include "Logging/TokenizedMessage.h"
#include "USDPrimResolver.h"

#include "UnrealUSDWrapper.h"
#include "USDMemory.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUSDImport, Log, All);

namespace USDKindTypes
{
	// Note: std::string for compatiblity with USD

	const std::string Component("component"); 
	const std::string Group("group");
	const std::string SubComponent("subcomponent");
}

enum class EUsdMeshImportType : uint8;
class UUSDImportOptions;

#if USE_USD_SDK
#include "USDIncludesStart.h"
#include "pxr/pxr.h"
#include "USDIncludesEnd.h"
#endif // #if USE_USD_SDK

#include "USDImporter.generated.h"

USTRUCT()
struct USDIMPORTER_API FUsdImportContext
{
	GENERATED_BODY()
	
	/** Mapping of path to imported assets  */
	TMap<FString, UObject*> PathToImportAssetMap;

	/** Parent package to import a single mesh to */
	UPROPERTY()
	UObject* Parent;

	/** Name to use when importing a single mesh */
	UPROPERTY()
	FString ObjectName;

	UPROPERTY()
	FString ImportPathName;

	UPROPERTY()
	UUSDImportOptions* ImportOptions;

	UPROPERTY()
	UUSDPrimResolver* PrimResolver;

#if USE_USD_SDK
	TUsdStore< pxr::UsdStageRefPtr > Stage;

	/** Root Prim of the USD file */
	TUsdStore< pxr::UsdPrim > RootPrim;
#endif // #if USE_USD_SDK

	/** Object flags to apply to newly imported objects */
	EObjectFlags ImportObjectFlags;

	/** Whether or not to apply world transformations to the actual geometry */
	bool bApplyWorldTransformToGeometry;

	/** If true stop at any USD prim that has an unreal asset reference.  Geometry that is a child such prims will be ignored */
	bool bFindUnrealAssetReferences;

	/** If true, options dialog won't be shown */
	bool bIsAutomated;

	virtual ~FUsdImportContext() { }

#if USE_USD_SDK
	virtual void Init(UObject* InParent, const FString& InName, const TUsdStore< pxr::UsdStageRefPtr >& InStage);
#endif // #if USE_USD_SDK

	void AddErrorMessage(EMessageSeverity::Type MessageSeverity, FText ErrorMessage);
	void DisplayErrorMessages(bool bAutomated);
	void ClearErrorMessages();

private:
	/** Error messages **/
	TArray<TSharedRef<FTokenizedMessage>> TokenizedErrorMessages;
};

USTRUCT()
struct USDIMPORTER_API FUSDSceneImportContext : public FUsdImportContext
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	UWorld* World;

	UPROPERTY()
	TMap<FName, AActor*> ExistingActors;

	UPROPERTY()
	TArray<FName> ActorsToDestroy;

	UPROPERTY()
	class UActorFactory* EmptyActorFactory;

	UPROPERTY()
	TMap<UClass*, UActorFactory*> UsedFactories;

	FCachedActorLabels ActorLabels;

#if USE_USD_SDK
	virtual void Init(UObject* InParent, const FString& InName, const TUsdStore< pxr::UsdStageRefPtr >& InStage);
#endif // #if USE_USD_SDK
};

// Used to make ImportContext visible to the garbage collector so that it doesn't unload its references
UCLASS(transient)
class USDIMPORTER_API UUsdSceneImportContextContainer : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FUSDSceneImportContext ImportContext;
};

UCLASS(transient)
class USDIMPORTER_API UUSDImporter : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/** Returns if we should proceed with the import */
	bool ShowImportOptions(FUsdImportContext& ImportContext);
	bool ShowImportOptions(UObject& ImportOptions);

#if USE_USD_SDK
	TUsdStore< pxr::UsdStageRefPtr > ReadUsdFile(FUsdImportContext& ImportContext, const FString& Filename);

	void ImportUsdStage(FUSDSceneImportContext& ImportContext);

	TArray<UObject*> ImportMeshes(FUsdImportContext& ImportContext, const TArray<FUsdAssetPrimToImport>& PrimsToImport);
	UObject* ImportSingleMesh(FUsdImportContext& ImportContext, EUsdMeshImportType ImportType, const FUsdAssetPrimToImport& PrimToImport);

protected:
	void RemoveExistingActors(FUSDSceneImportContext& ImportContext);
	void SpawnActors(FUSDSceneImportContext& ImportContext, const TArray<FActorSpawnData>& SpawnDatas, FScopedSlowTask& SlowTask);
	void OnActorSpawned(FUsdImportContext& ImportContext, AActor* SpawnedActor, const FActorSpawnData& SpawnData);
#endif // #if USE_USD_SDK
};
