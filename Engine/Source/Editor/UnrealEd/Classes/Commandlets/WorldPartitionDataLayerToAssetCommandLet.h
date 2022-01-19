// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectAnnotation.h"
#include "Commandlets/Commandlet.h"
#include "Commandlets/WorldPartitionCommandletHelpers.h"
#include "PackageSourceControlHelper.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"

#include "WorldPartitionDataLayerToAssetCommandLet.generated.h"

UNREALED_API DECLARE_LOG_CATEGORY_EXTERN(LogDataLayerToAssetCommandlet, Log, All);

class UDataLayer;
class UDataLayerAsset;
class UDataLayerInstance;
class UDataLayerFactory;

UCLASS(Transient)
class UDataLayerConversionInfo : public UObject
{
	GENERATED_BODY()

	friend class UDataLayerToAssetCommandletContext;

public:
	bool IsConverting() const { return DataLayerToConvert != nullptr; }
	bool IsAPreviousConversion() const { return CurrentConvertingInfo != nullptr; }
	bool IsConverted() const { return DataLayerInstance != nullptr && PreviousConversionsInfo.IsEmpty(); }

	TArray<TWeakObjectPtr<UDataLayerConversionInfo>> const& GetPreviousConversions() const { return PreviousConversionsInfo; }
	const TWeakObjectPtr<UDataLayerConversionInfo>& GetCurrentConversion() const { return CurrentConvertingInfo; }

	void SetDataLayerToConvert(const TObjectPtr<UDataLayer>& InDataLayerToConvert);
	void SetDataLayerInstance(const TObjectPtr<UDataLayerInstance>& InDataLayerInstance);

	UPROPERTY()
	TObjectPtr<UDataLayerAsset> DataLayerAsset;

	UPROPERTY()
	TObjectPtr<UDataLayer> DataLayerToConvert;

	UPROPERTY()
	TObjectPtr<UDataLayerInstance> DataLayerInstance;

private:
	UPROPERTY()
	TArray<TWeakObjectPtr<UDataLayerConversionInfo>> PreviousConversionsInfo;

	UPROPERTY()
	TWeakObjectPtr<UDataLayerConversionInfo> CurrentConvertingInfo;
};

UCLASS(Transient)
class UDataLayerToAssetCommandletContext : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	const TArray<TObjectPtr<UDataLayerConversionInfo>>& GetDataLayerConversionInfos() const { return DataLayerConversionInfo; }
	const TArray<TWeakObjectPtr<UDataLayerConversionInfo>>& GetConvertingDataLayerConversionInfo() const { return ConvertingDataLayerInfo; }

	UDataLayerConversionInfo* GetDataLayerConversionInfo(const TObjectPtr<UDataLayer>& DataLayer) const;
	UDataLayerConversionInfo* GetDataLayerConversionInfo(const UDataLayerAsset* DataLayerAsset) const;
	UDataLayerConversionInfo* GetDataLayerConversionInfo(const TObjectPtr<UDataLayerInstance>& DataLayerInstance) const;
	UDataLayerConversionInfo* GetDataLayerConversionInfo(const FActorDataLayer& ActorDataLayer) const;

	UDataLayerConversionInfo* StoreExistingDataLayer(FAssetData& AssetData);
	UDataLayerConversionInfo* StoreDataLayerAssetConversion(const TObjectPtr<UDataLayer>& DataLayerToConvert, TObjectPtr<UDataLayerAsset>& NewDataLayerAsset);
	UDataLayerConversionInfo* StoreDataLayerInstanceConversion(const UDataLayerAsset* DataLayerAsset, UDataLayerInstance* NewDataLayerInstance);
	
	bool SetPreviousConversions(UDataLayerConversionInfo* CurrentConversion, TArray<TWeakObjectPtr<UDataLayerConversionInfo>>&& PreviousConversions);

	bool FindDataLayerConversionInfos(FName DataLayerAssetName, TArray<TWeakObjectPtr<UDataLayerConversionInfo>>& OutConversionInfos) const;

	void LogConversionInfos() const;

private:
	UPROPERTY()
	TArray<TObjectPtr<UDataLayerConversionInfo>> DataLayerConversionInfo;

	UPROPERTY()
	TArray<TWeakObjectPtr<UDataLayerConversionInfo>> ConvertingDataLayerInfo;
};

UCLASS(Config = Engine)
class UNREALED_API UDataLayerToAssetCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

public:
	enum EReturnCode
	{
		Success = 0,
		CommandletInitializationError,
		DataLayerConversionError,
		ActorDataLayerRemappingError,
	};

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

private:
	bool InitializeFromCommandLine(TArray<FString>& Tokens, TArray<FString> const& Switches, TMap<FString, FString> const& Params);

	bool BuildConversionInfos(TStrongObjectPtr<UDataLayerToAssetCommandletContext>& CommandletContext);
	bool CreateConversionFromDataLayer(TStrongObjectPtr<UDataLayerToAssetCommandletContext>& CommandletContext, const TObjectPtr<UDataLayer>& DataLayer);
	TObjectPtr<UDataLayerAsset> GetOrCreateDataLayerAssetForConversion(TStrongObjectPtr<UDataLayerToAssetCommandletContext>& CommandletContext, FName DataLayerName);

	bool ResolvePreviousConversionsToCurrent(TStrongObjectPtr<UDataLayerToAssetCommandletContext>& CommandletContext);

	bool RemapActorDataLayersToAssets(TStrongObjectPtr<UDataLayerToAssetCommandletContext>& CommandletContext);
	uint32 RemapActorDataLayers(TStrongObjectPtr<UDataLayerToAssetCommandletContext>& CommandletContext, AActor* Actor);
	uint32 RemapDataLayersAssetsFromPreviousConversions(TStrongObjectPtr<UDataLayerToAssetCommandletContext>& CommandletContext, AActor* Actor);

	bool CreateDataLayerInstances(TStrongObjectPtr<UDataLayerToAssetCommandletContext>& CommandletContext);
	
	bool DeletePreviousConversionsData(TStrongObjectPtr<UDataLayerToAssetCommandletContext>& CommandletContext);

	bool CommitConversion(TStrongObjectPtr<UDataLayerToAssetCommandletContext>& CommandletContext);

	FString GetConversionFolder() const;
	bool IsAssetInConversionFolder(const TObjectPtr<UDataLayerAsset> DataLayerAsset);

	UPROPERTY(Config)
	FString DestinationFolder;

	UPROPERTY(Transient)
	FString ConversionFolder;

	UPROPERTY(Config)
	bool bPerformSavePackages = true;

	UPROPERTY(Config)
	bool bIgnoreActorLoadingErrors = false;

	UPROPERTY()
	TObjectPtr<UDataLayerFactory> DataLayerFactory;

	UPROPERTY(Transient)
	TObjectPtr<UWorld> MainWorld;

	FPackageSourceControlHelper PackageHelper;
};
