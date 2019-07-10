// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "USDImportOptions.h"
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

#if USE_USD_SDK
#include "USDIncludesStart.h"
#include "pxr/pxr.h"
#include "USDIncludesEnd.h"
#endif // #if USE_USD_SDK

#include "USDImporter.generated.h"

USTRUCT()
struct FUsdImportContext
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


UCLASS(transient)
class UUSDImporter : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	bool ShowImportOptions(UObject& ImportOptions);

#if USE_USD_SDK
	TUsdStore< pxr::UsdStageRefPtr > ReadUSDFile(FUsdImportContext& ImportContext, const FString& Filename);

	TArray<UObject*> ImportMeshes(FUsdImportContext& ImportContext, const TArray<FUsdAssetPrimToImport>& PrimsToImport);

	UObject* ImportSingleMesh(FUsdImportContext& ImportContext, EUsdMeshImportType ImportType, const FUsdAssetPrimToImport& PrimToImport);
#endif // #if USE_USD_SDK
};
