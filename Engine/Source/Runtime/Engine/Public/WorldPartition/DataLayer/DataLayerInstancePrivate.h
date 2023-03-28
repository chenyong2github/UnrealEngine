// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

#include "DataLayerInstancePrivate.generated.h"

UCLASS(Config = Engine, PerObjectConfig, Within = WorldDataLayers, AutoExpandCategories = ("Data Layer|Editor"))
class ENGINE_API UDataLayerInstancePrivate : public UDataLayerInstance
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	static FName MakeName();
	void OnCreated();

	virtual bool CanEditChange(const FProperty* InProperty) const;
	virtual bool IsLocked() const override;
	virtual bool IsReadOnly() const override;
		
	virtual bool SupportsActorFilters() const override { return GetAsset()->SupportsActorFilters(); }
	virtual bool IsIncludedInActorFilterDefault() const override { return bIsIncludedInActorFilterDefault; }
	virtual bool CanEditDataLayerShortName() const override { return true; }
#endif

	virtual const UDataLayerAsset* GetAsset() const override { return DataLayerAsset; }

	virtual EDataLayerType GetType() const override { return DataLayerAsset->GetType(); }
	virtual bool IsRuntime() const override { return DataLayerAsset->IsRuntime(); }
	virtual FColor GetDebugColor() const override { return DataLayerAsset->GetDebugColor(); }

	virtual FString GetDataLayerShortName() const override { return ShortName; }
	virtual FString GetDataLayerFullName() const override { return DataLayerAsset->GetPathName(); }
protected:
#if WITH_EDITOR
	virtual bool PerformAddActor(AActor* InActor) const override;
	virtual bool PerformRemoveActor(AActor* InActor) const override;
	virtual void PerformSetDataLayerShortName(const FString& InNewShortName) override { ShortName = InNewShortName; }
#endif

private:
	UPROPERTY()
	FString ShortName;

	UPROPERTY(Category = "Asset", VisibleAnywhere, Instanced)
	TObjectPtr<UDataLayerAsset> DataLayerAsset;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Category = "Actor Filter", EditAnywhere, meta = (DisplayName = "Is Included", ToolTip = "Whether actors assigned to this DataLayer are included by default when used in a filter"))
	bool bIsIncludedInActorFilterDefault;
#endif
};