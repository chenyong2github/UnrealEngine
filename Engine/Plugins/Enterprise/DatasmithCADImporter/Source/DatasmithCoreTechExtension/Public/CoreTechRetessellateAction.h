// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CoreTechParametricSurfaceExtension.h"
#include "DatasmithCustomAction.h"
#include "DatasmithAdditionalData.h"
#include "DatasmithImportOptions.h"

#include "CoreTechRetessellateAction.generated.h"

class DATASMITHCORETECHEXTENSION_API FCoreTechRetessellate_Impl
{
public:
	static const FText Label;
	static const FText Tooltip;

	static bool CanApplyOnAssets(const TArray<FAssetData>& SelectedAssets);
	static void ApplyOnAssets(const TArray<FAssetData>& SelectedAssets);
	static bool ApplyOnOneAsset(UStaticMesh& StaticMesh, UCoreTechParametricSurfaceData& CoreTechData, const FDatasmithTessellationOptions& RetesselateOptions);
};


UCLASS()
class DATASMITHCORETECHEXTENSION_API UCoreTechRetessellateAction : public UDatasmithCustomActionBase
{
	GENERATED_BODY()

public:
	virtual const FText& GetLabel() override { return FCoreTechRetessellate_Impl::Label; }
	virtual const FText& GetTooltip() override { return FCoreTechRetessellate_Impl::Tooltip; }

	virtual bool CanApplyOnAssets(const TArray<FAssetData>& SelectedAssets) override
	{ return FCoreTechRetessellate_Impl::CanApplyOnAssets(SelectedAssets); }

	virtual void ApplyOnAssets(const TArray<FAssetData>& SelectedAssets) override
	{ return FCoreTechRetessellate_Impl::ApplyOnAssets(SelectedAssets); }

	virtual bool CanApplyOnActors(const TArray<AActor*>& SelectedActors) override;

	virtual void ApplyOnActors(const TArray<AActor*>& SelectedActors) override;
};


UCLASS(config = Editor, Transient)
class DATASMITHCORETECHEXTENSION_API UCoreTechRetessellateActionOptions : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "NotVisible", meta = (ShowOnlyInnerProperties))
	FDatasmithTessellationOptions Options;
};
