// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "UObject/Object.h"
#include "RHIDefinitions.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraMergeable.h"
#include "NiagaraGPUSortInfo.h"
#include "NiagaraPlatformSet.h"
#include "NiagaraRendererProperties.generated.h"

class FNiagaraRenderer;
class UMaterial;
class UMaterialInterface;
class FNiagaraEmitterInstance;
class SWidget;
class FAssetThumbnailPool;
struct FNiagaraDataSetCompiledData;
struct FSlateBrush;

#if WITH_EDITOR
// Helper class for GUI error handling
DECLARE_DELEGATE(FNiagaraRendererFeedbackFix);
class FNiagaraRendererFeedback
{
public:
	FNiagaraRendererFeedback(FText InDescriptionText, FText InSummaryText, FText InFixDescription = FText(), FNiagaraRendererFeedbackFix InFix = FNiagaraRendererFeedbackFix(), bool InDismissable = false)
		: DescriptionText(InDescriptionText)
		  , SummaryText(InSummaryText)
		  , FixDescription(InFixDescription)
		  , Fix(InFix)
		  , Dismissable(InDismissable)
	{}

	FNiagaraRendererFeedback(FText InSummaryText)
        : DescriptionText(FText())
          , SummaryText(InSummaryText)
          , FixDescription(FText())
          , Fix(FNiagaraRendererFeedbackFix())
		  , Dismissable(false)
	{}

	FNiagaraRendererFeedback()
	{}

	/** Returns true if the problem can be fixed automatically. */
	bool IsFixable() const
	{
		return Fix.IsBound();
	}

	/** Applies the fix if a delegate is bound for it.*/
	void TryFix() const
	{
		if (Fix.IsBound())
		{
			Fix.Execute();
		}
	}

	/** Full description text */
	FText GetDescriptionText() const
	{
		return DescriptionText;
	}

	/** Shortened error description text*/
	FText GetSummaryText() const
	{
		return SummaryText;
	}

	/** Full description text */
	FText GetFixDescriptionText() const
	{
		return FixDescription;
	}

	bool IsDismissable() const
	{
		return Dismissable;
	}

private:
	FText DescriptionText;
	FText SummaryText;
	FText FixDescription;
	FNiagaraRendererFeedbackFix Fix;
	bool Dismissable;
};
#endif

/** Mapping between a variable in the source dataset and the location we place it in the GPU buffer passed to the VF. */
struct FNiagaraRendererVariableInfo
{
	FNiagaraRendererVariableInfo() {}
	FNiagaraRendererVariableInfo(int32 InDataOffset, int32 InGPUBufferOffset, int32 InNumComponents, bool bInUpload, bool bInHalfType)
		: DatasetOffset(InDataOffset)
		, GPUBufferOffset(InGPUBufferOffset)
		, NumComponents(InNumComponents)
		, bUpload(bInUpload)
		, bHalfType(bInHalfType)
	{
	}

	FORCEINLINE int32 GetGPUOffset() const
	{
		int32 Offset = GPUBufferOffset;
		if (bHalfType)
		{
			Offset |= 1 << 31;
		}
		return Offset;
	}

	FORCEINLINE int32 GetEncodedDatasetOffset() const
	{
		return DatasetOffset | (bHalfType ? (1<<31) : 0);
	}

	int32 DatasetOffset = INDEX_NONE;
	int32 GPUBufferOffset = INDEX_NONE;
	int32 NumComponents = 0;
	bool bUpload = false;
	bool bHalfType = false;
};

/** Used for building renderer layouts for vertex factories */
struct FNiagaraRendererLayout
{
	void Initialize(int32 NumVariables);
	bool SetVariable(const FNiagaraDataSetCompiledData* CompiledData, const FNiagaraVariableBase& Variable, int32 VFVarOffset);
	bool SetVariableFromBinding(const FNiagaraDataSetCompiledData* CompiledData, const FNiagaraVariableAttributeBinding& VariableBinding, int32 VFVarOffset);
	void Finalize();

	TConstArrayView<FNiagaraRendererVariableInfo> GetVFVariables_RenderThread() const { check(IsInRenderingThread()); return MakeArrayView(VFVariables_RT); }
	int32 GetTotalFloatComponents_RenderThread() const { check(IsInRenderingThread()); return TotalFloatComponents_RT; }
	int32 GetTotalHalfComponents_RenderThread() const { check(IsInRenderingThread()); return TotalHalfComponents_RT; }

private:
	TArray<FNiagaraRendererVariableInfo> VFVariables_GT;
	int32 TotalFloatComponents_GT;
	int32 TotalHalfComponents_GT;

	TArray<FNiagaraRendererVariableInfo> VFVariables_RT;
	int32 TotalFloatComponents_RT;
	int32 TotalHalfComponents_RT;
};

/**
* Emitter properties base class
* Each EmitterRenderer derives from this with its own class, and returns it in GetProperties; a copy
* of those specific properties is stored on UNiagaraEmitter (on the System) for serialization
* and handed back to the System renderer on load.
*/
UCLASS(ABSTRACT)
class NIAGARA_API UNiagaraRendererProperties : public UNiagaraMergeable
{
	GENERATED_BODY()

public:
	UNiagaraRendererProperties()		
		: bIsEnabled(true)
		, bMotionBlurEnabled_DEPRECATED(true)
	{
	}

	//UObject Interface Begin
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if WITH_EDITORONLY_DATA
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//UObject Interface End
	
	virtual FNiagaraRenderer* CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter, const UNiagaraComponent* InComponent) PURE_VIRTUAL ( UNiagaraRendererProperties::CreateEmitterRenderer, return nullptr;);
	virtual class FNiagaraBoundsCalculator* CreateBoundsCalculator() PURE_VIRTUAL(UNiagaraRendererProperties::CreateBoundsCalculator, return nullptr;);
	virtual void GetUsedMaterials(const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const PURE_VIRTUAL(UNiagaraRendererProperties::GetUsedMaterials,);

	virtual bool IsSimTargetSupported(ENiagaraSimTarget InSimTarget) const { return false; };

	const TArray<const FNiagaraVariableAttributeBinding*>& GetAttributeBindings() const { return AttributeBindings; }
	uint32 ComputeMaxUsedComponents(const FNiagaraDataSetCompiledData* CompiledDataSetData) const;

	virtual bool NeedsLoadForTargetPlatform(const class ITargetPlatform* TargetPlatform) const override;

	/** Method to add asset tags that are specific to this renderer. By default we add in how many instances of this class exist in the list.*/
	virtual void GetAssetTagsForContext(const UObject* InAsset, const TArray<const UNiagaraRendererProperties*>& InProperties, TMap<FName, uint32>& NumericKeys, TMap<FName, FString>& StringKeys) const;

	/** In the case that we need parameters bound in that aren't Particle variables, these should be set up here so that the data is appropriately populated after the simulation.*/
	virtual bool PopulateRequiredBindings(FNiagaraParameterStore& InParameterStore) { return false; }	

#if WITH_EDITORONLY_DATA

	virtual bool IsSupportedVariableForBinding(const FNiagaraVariableBase& InSourceForBinding, const FName& InTargetBindingName) const;

	/** Internal handling of any emitter variable renames. Note that this doesn't modify the renderer, the caller will need to do that if it is desired.*/
	virtual void RenameEmitter(const FName& InOldName, const UNiagaraEmitter* InRenamedEmitter);
	virtual void RenameVariable(const FNiagaraVariableBase& OldVariable, const FNiagaraVariableBase& NewVariable, const UNiagaraEmitter* InEmitter);
	virtual void RemoveVariable(const FNiagaraVariableBase& OldVariable, const UNiagaraEmitter* InEmitter);
	virtual bool IsMaterialValidForRenderer(UMaterial* Material, FText& InvalidMessage) { return true; }

	virtual void FixMaterial(UMaterial* Material) { }

	virtual TArray<FNiagaraVariable> GetBoundAttributes() const;

	virtual const TArray<FNiagaraVariable>& GetRequiredAttributes() { static TArray<FNiagaraVariable> Vars; return Vars; };
	virtual const TArray<FNiagaraVariable>& GetOptionalAttributes() { static TArray<FNiagaraVariable> Vars; return Vars; };

	virtual void GetAdditionalVariables(TArray<FNiagaraVariableBase>& OutArray) const { };

	UNiagaraRendererProperties* StaticDuplicateWithNewMergeId(UObject* InOuter) const
	{
		return CastChecked<UNiagaraRendererProperties>(Super::StaticDuplicateWithNewMergeIdInternal(InOuter));
	}

	virtual void GetRendererWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const PURE_VIRTUAL(UNiagaraRendererProperties::GetRendererWidgets, );
	virtual void GetRendererTooltipWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const PURE_VIRTUAL(UNiagaraRendererProperties::GetRendererTooltipWidgets, );
	virtual void GetRendererFeedback(const UNiagaraEmitter* InEmitter, TArray<FText>& OutErrors, TArray<FText>& OutWarnings, TArray<FText>& OutInfo) const {};
	virtual void GetRendererFeedback(UNiagaraEmitter* InEmitter, TArray<FNiagaraRendererFeedback>& OutErrors, TArray<FNiagaraRendererFeedback>& OutWarnings, TArray<FNiagaraRendererFeedback>& OutInfo) const;

	// The icon to display in the niagara stack widget under the renderer section
	virtual const FSlateBrush* GetStackIcon() const;

	// The text to display in the niagara stack widget under the renderer section
	virtual FText GetWidgetDisplayName() const;

#endif // WITH_EDITORONLY_DATA

	virtual ENiagaraRendererSourceDataMode GetCurrentSourceMode() const {	return ENiagaraRendererSourceDataMode::Particles;}

	virtual bool GetIsActive() const;
	virtual bool GetIsEnabled() const { return bIsEnabled; }
	virtual void SetIsEnabled(bool bInIsEnabled);

	virtual void CacheFromCompiledData(const FNiagaraDataSetCompiledData* CompiledData) {}

	virtual bool NeedsMIDsForMaterials() const { return false; }

	/** When true, the renderer will be opted in to get its PostSystemTick_GameThread overload called */
	virtual bool NeedsSystemPostTick() const { return false; }
	/** When true, the renderer will be opted in to get its OnSystemComplete_GameThread overload called */
	virtual bool NeedsSystemCompletion() const { return false; }

	bool NeedsPreciseMotionVectors() const;

	/** Platforms on which this renderer is enabled. */
	UPROPERTY(EditAnywhere, Category = "Scalability")
	FNiagaraPlatformSet Platforms;

	/** By default, emitters are drawn in the order that they are added to the system. This value will allow you to control the order in a more fine-grained manner.
	Materials of the same type (i.e. Transparent) will draw in order from lowest to highest within the system. The default value is 0.*/
	UPROPERTY(EditAnywhere, Category = "Sort Order")
	int32 SortOrderHint;

	/** Hint about how to generate motion (velocity) vectors for this renderer. */
	UPROPERTY(EditAnywhere, Category = "Motion Blur")
	ENiagaraRendererMotionVectorSetting MotionVectorSetting;

	UPROPERTY()
	bool bIsEnabled;

protected:
	UPROPERTY()
	bool bMotionBlurEnabled_DEPRECATED; // This has been rolled into MotionVectorSetting

	TArray<const FNiagaraVariableAttributeBinding*> AttributeBindings;

	virtual void PostLoadBindings(ENiagaraRendererSourceDataMode InSourceMode);
	virtual void UpdateSourceModeDerivates(ENiagaraRendererSourceDataMode InSourceMode, bool bFromPropertyEdit = false);

#if WITH_EDITORONLY_DATA
	/** returns the variable associated with the supplied binding if it should be bound given the current settings of the RendererProperties. */
	virtual FNiagaraVariable GetBoundAttribute(const FNiagaraVariableAttributeBinding* Binding) const;
#endif
};
