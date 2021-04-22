// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DisplayClusterConfigurationTypes.h"
#include "Engine/Blueprint.h"
#include "DisplayClusterBlueprint.generated.h"



UCLASS(BlueprintType)
class DISPLAYCLUSTER_API UDisplayClusterBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
	UDisplayClusterBlueprint();
	
#if WITH_EDITOR
	// UBlueprint
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
	virtual UClass* GetBlueprintClass() const override;
	virtual void GetReparentingRules(TSet<const UClass*>& AllowedChildrenOfClasses, TSet<const UClass*>& DisallowedChildrenOfClasses) const override;
	// ~UBlueprint
#endif // WITH_EDITOR
	
	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface

	class UDisplayClusterBlueprintGeneratedClass* GetGeneratedClass() const;

	UDisplayClusterConfigurationData* GetOrLoadConfig();
	UDisplayClusterConfigurationData* GetConfig() const { return ConfigData; }
	
	void SetConfigData(UDisplayClusterConfigurationData* InConfigData);

	const FString& GetConfigPath() const { return PathToConfig; }
	void SetConfigPath(const FString& InPath);
	
public:
	// Holds the last saved config export. In the AssetRegistry to allow parsing without loading.
	UPROPERTY(AssetRegistrySearchable)
	FString ConfigExport;

private:

	//** Updates the ConfigExport property. Called when saving the asset.
	void UpdateConfigExportProperty();

protected:
	UPROPERTY()
	FString PathToConfig;

	UPROPERTY(Export)
	UDisplayClusterConfigurationData* ConfigData;

private:
	friend class FDisplayClusterConfiguratorVersionUtils;
	
	UPROPERTY(AssetRegistrySearchable)
	int32 AssetVersion;
};
