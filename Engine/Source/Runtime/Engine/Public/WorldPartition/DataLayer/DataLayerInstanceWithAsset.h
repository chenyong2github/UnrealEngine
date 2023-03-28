// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

#include "DataLayerInstanceWithAsset.generated.h"

UCLASS(Config = Engine, PerObjectConfig, AutoCollapseCategories = ("Data Layer|Advanced"), AutoExpandCategories = ("Data Layer|Editor", "Data Layer|Advanced|Runtime"))
class ENGINE_API UDataLayerInstanceWithAsset : public UDataLayerInstance
{
	GENERATED_UCLASS_BODY()

		friend class UDataLayerConversionInfo;

public:
#if WITH_EDITOR
	static FName MakeName(const UDataLayerAsset* DeprecatedDataLayer);
	void OnCreated(const UDataLayerAsset* Asset);

	virtual bool CanEditChange(const FProperty* InProperty) const;
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;
	virtual bool IsLocked() const override;
	virtual bool IsReadOnly() const override;
	virtual bool CanAddActor(AActor* InActor) const override;
	virtual bool CanRemoveActor(AActor* InActor) const override;

	virtual bool Validate(IStreamingGenerationErrorHandler* ErrorHandler) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	bool SupportsActorFilters() const;
	bool IsIncludedInActorFilterDefault() const;
#endif

	const UDataLayerAsset* GetAsset() const override { return DataLayerAsset; }

	virtual EDataLayerType GetType() const override { return DataLayerAsset != nullptr ? DataLayerAsset->GetType() : EDataLayerType::Unknown; }

	virtual bool IsRuntime() const override { return DataLayerAsset != nullptr ? DataLayerAsset->IsRuntime() : false; }

	virtual FColor GetDebugColor() const override { return DataLayerAsset != nullptr ? DataLayerAsset->GetDebugColor() : FColor::Black; }

	virtual FString GetDataLayerShortName() const override { return DataLayerAsset != nullptr ? DataLayerAsset->GetName() : GetDataLayerFName().ToString(); }
	virtual FString GetDataLayerFullName() const override { return DataLayerAsset != nullptr ? DataLayerAsset->GetPathName() : GetDataLayerFName().ToString(); }

protected:
#if WITH_EDITOR
	virtual bool PerformAddActor(AActor* InActor) const;
	virtual bool PerformRemoveActor(AActor* InActor) const;
#endif

private:
	UPROPERTY(Category = "Data Layer", EditAnywhere)
	TObjectPtr<const UDataLayerAsset> DataLayerAsset;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Category = "Data Layer|Actor Filter", EditAnywhere, meta = (DisplayName = "Is Included", ToolTip = "Whether actors assigned to this DataLayer are included by default when used in a filter"))
	bool bIsIncludedInActorFilterDefault;
#endif

#if WITH_EDITOR
	// Used to compare state pre/post undo
	TObjectPtr<const UDataLayerAsset> CachedDataLayerAsset;
#endif
};