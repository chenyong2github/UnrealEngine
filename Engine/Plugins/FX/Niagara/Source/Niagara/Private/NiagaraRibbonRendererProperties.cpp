// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraRendererRibbons.h"
#include "NiagaraConstants.h"
#include "NiagaraBoundsCalculatorHelper.h"
#include "NiagaraCustomVersion.h"
#include "Modules/ModuleManager.h"
#if WITH_EDITOR
#include "Widgets/Images/SImage.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/SWidget.h"
#include "AssetThumbnail.h"
#include "Widgets/Text/STextBlock.h"
#endif

#define LOCTEXT_NAMESPACE "UNiagaraRibbonRendererProperties"

TArray<TWeakObjectPtr<UNiagaraRibbonRendererProperties>> UNiagaraRibbonRendererProperties::RibbonRendererPropertiesToDeferredInit;


FNiagaraRibbonShapeCustomVertex::FNiagaraRibbonShapeCustomVertex()
	: Position(0.0f)
	, Normal(0.0f)
	, TextureV(0.0f)
{
}

FNiagaraRibbonUVSettings::FNiagaraRibbonUVSettings()
	: DistributionMode(ENiagaraRibbonUVDistributionMode::ScaledUsingRibbonSegmentLength)
	, LeadingEdgeMode(ENiagaraRibbonUVEdgeMode::Locked)
	, TrailingEdgeMode(ENiagaraRibbonUVEdgeMode::Locked)
	, TilingLength(100.0f)
	, Offset(FVector2D(0.0f, 0.0f))
	, Scale(FVector2D(1.0f, 1.0f))
	, bEnablePerParticleUOverride(false)
	, bEnablePerParticleVRangeOverride(false)
{
}

UNiagaraRibbonRendererProperties::UNiagaraRibbonRendererProperties()
	: Material(nullptr)
	, MaterialUserParamBinding(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()))
	, FacingMode(ENiagaraRibbonFacingMode::Screen)
#if WITH_EDITORONLY_DATA
	, UV0TilingDistance_DEPRECATED(0.0f)
	, UV0Scale_DEPRECATED(FVector2D(1.0f, 1.0f))
	, UV0AgeOffsetMode_DEPRECATED(ENiagaraRibbonAgeOffsetMode::Scale)
	, UV1TilingDistance_DEPRECATED(0.0f)
	, UV1Scale_DEPRECATED(FVector2D(1.0f, 1.0f))
	, UV1AgeOffsetMode_DEPRECATED(ENiagaraRibbonAgeOffsetMode::Scale)
#endif
	, Shape(ENiagaraRibbonShapeMode::Plane)
	, bEnableAccurateGeometry(false)
	, WidthSegmentationCount(1)
	, MultiPlaneCount(2)
	, TubeSubdivisions(3)
	, CurveTension(0.f)
	, TessellationMode(ENiagaraRibbonTessellationMode::Automatic)
	, TessellationFactor(16)
	, bUseConstantFactor(false)
	, TessellationAngle(15)
	, bScreenSpaceTessellation(true)
{
	AttributeBindings.Reserve(19);
	AttributeBindings.Add(&PositionBinding);
	AttributeBindings.Add(&ColorBinding);
	AttributeBindings.Add(&VelocityBinding);
	AttributeBindings.Add(&NormalizedAgeBinding);
	AttributeBindings.Add(&RibbonTwistBinding);
	AttributeBindings.Add(&RibbonWidthBinding);
	AttributeBindings.Add(&RibbonFacingBinding);
	AttributeBindings.Add(&RibbonIdBinding);
	AttributeBindings.Add(&RibbonLinkOrderBinding);
	AttributeBindings.Add(&MaterialRandomBinding);
	AttributeBindings.Add(&DynamicMaterialBinding);
	AttributeBindings.Add(&DynamicMaterial1Binding);
	AttributeBindings.Add(&DynamicMaterial2Binding);
	AttributeBindings.Add(&DynamicMaterial3Binding);
	AttributeBindings.Add(&RibbonUVDistance);
	AttributeBindings.Add(&U0OverrideBinding);
	AttributeBindings.Add(&V0RangeOverrideBinding);
	AttributeBindings.Add(&U1OverrideBinding);
	AttributeBindings.Add(&V1RangeOverrideBinding);
}

FNiagaraRenderer* UNiagaraRibbonRendererProperties::CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter, const UNiagaraComponent* InComponent)
{
	FNiagaraRenderer* NewRenderer = new FNiagaraRendererRibbons(FeatureLevel, this, Emitter);
	NewRenderer->Initialize(this, Emitter, InComponent);
	return NewRenderer;
}

#if WITH_EDITORONLY_DATA
void UpgradeUVSettings(FNiagaraRibbonUVSettings& UVSettings, float TilingDistance, const FVector2D& Offset, const FVector2D& Scale)
{
	if (TilingDistance == 0)
	{
		UVSettings.LeadingEdgeMode = ENiagaraRibbonUVEdgeMode::SmoothTransition;
		UVSettings.TrailingEdgeMode = ENiagaraRibbonUVEdgeMode::SmoothTransition;
		UVSettings.DistributionMode = ENiagaraRibbonUVDistributionMode::ScaledUniformly;
	}
	else
	{
		UVSettings.LeadingEdgeMode = ENiagaraRibbonUVEdgeMode::Locked;
		UVSettings.TrailingEdgeMode = ENiagaraRibbonUVEdgeMode::Locked;
		UVSettings.DistributionMode = ENiagaraRibbonUVDistributionMode::TiledOverRibbonLength;
		UVSettings.TilingLength = TilingDistance;
	}
	UVSettings.Offset = Offset;
	UVSettings.Scale = Scale;
}
#endif

void UNiagaraRibbonRendererProperties::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA

	if (MaterialUserParamBinding.Parameter.GetType().GetClass() != UMaterialInterface::StaticClass())
	{
		FNiagaraTypeDefinition MaterialDef(UMaterialInterface::StaticClass());
		MaterialUserParamBinding.Parameter.SetType(MaterialDef);
	}

	const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);
	if (NiagaraVer < FNiagaraCustomVersion::RibbonRendererUVRefactor)
	{
		UpgradeUVSettings(UV0Settings, UV0TilingDistance_DEPRECATED, UV0Offset_DEPRECATED, UV0Scale_DEPRECATED);
		UpgradeUVSettings(UV1Settings, UV1TilingDistance_DEPRECATED, UV1Offset_DEPRECATED, UV1Scale_DEPRECATED);
	}

#endif

	PostLoadBindings(ENiagaraRendererSourceDataMode::Particles);

	if ( Material )
	{
		Material->ConditionalPostLoad();
	}
}

FNiagaraBoundsCalculator* UNiagaraRibbonRendererProperties::CreateBoundsCalculator()
{
	return new FNiagaraBoundsCalculatorHelper<false, false, true>();
}

void UNiagaraRibbonRendererProperties::GetUsedMaterials(const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const
{
	UMaterialInterface* UserParamMaterial = nullptr;
	if (InEmitter != nullptr && MaterialUserParamBinding.Parameter.IsValid() && InEmitter->FindBinding(MaterialUserParamBinding, UserParamMaterial))
	{
		OutMaterials.Add(UserParamMaterial);
	}
	else
	{
		OutMaterials.Add(Material);
	}
}

bool UNiagaraRibbonRendererProperties::PopulateRequiredBindings(FNiagaraParameterStore& InParameterStore)
{
	bool bAnyAdded = false;

	for (const FNiagaraVariableAttributeBinding* Binding : AttributeBindings)
	{
		if (Binding && Binding->CanBindToHostParameterMap())
		{
			InParameterStore.AddParameter(Binding->GetParamMapBindableVariable(), false);
			bAnyAdded = true;
		}
	}

	for (FNiagaraMaterialAttributeBinding& MaterialParamBinding : MaterialParameterBindings)
	{
		InParameterStore.AddParameter(MaterialParamBinding.GetParamMapBindableVariable(), false);
		bAnyAdded = true;
	}

	return bAnyAdded;
}

void UNiagaraRibbonRendererProperties::UpdateSourceModeDerivates(ENiagaraRendererSourceDataMode InSourceMode, bool bFromPropertyEdit)
{
	UNiagaraEmitter* SrcEmitter = GetTypedOuter<UNiagaraEmitter>();
	if (SrcEmitter)
	{
		for (FNiagaraMaterialAttributeBinding& MaterialParamBinding : MaterialParameterBindings)
		{
			MaterialParamBinding.CacheValues(SrcEmitter);
		}
	}

	Super::UpdateSourceModeDerivates(InSourceMode, bFromPropertyEdit);
}

void UNiagaraRibbonRendererProperties::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		// We can end up hitting PostInitProperties before the Niagara Module has initialized bindings this needs, mark this object for deferred init and early out.
		if (FModuleManager::Get().IsModuleLoaded("Niagara") == false)
		{
			RibbonRendererPropertiesToDeferredInit.Add(this);
			return;
		}
		InitBindings();
	}
}

/** The bindings depend on variables that are created during the NiagaraModule startup. However, the CDO's are build prior to this being initialized, so we defer setting these values until later.*/
void UNiagaraRibbonRendererProperties::InitCDOPropertiesAfterModuleStartup()
{
	UNiagaraRibbonRendererProperties* CDO = CastChecked<UNiagaraRibbonRendererProperties>(UNiagaraRibbonRendererProperties::StaticClass()->GetDefaultObject());
	CDO->InitBindings();

	for (TWeakObjectPtr<UNiagaraRibbonRendererProperties>& WeakRibbonRendererProperties : RibbonRendererPropertiesToDeferredInit)
	{
		if (WeakRibbonRendererProperties.Get())
		{
			WeakRibbonRendererProperties->InitBindings();
		}
	}
}

void UNiagaraRibbonRendererProperties::InitBindings()
{
	if (!PositionBinding.IsValid())
	{
		PositionBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_POSITION);
		ColorBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_COLOR);
		VelocityBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_VELOCITY);
		DynamicMaterialBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM);
		DynamicMaterial1Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_1);
		DynamicMaterial2Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_2);
		DynamicMaterial3Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_3);
		NormalizedAgeBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
		RibbonTwistBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONTWIST);
		RibbonWidthBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONWIDTH);
		RibbonFacingBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONFACING);
		RibbonIdBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONID);
		RibbonLinkOrderBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONLINKORDER);
		MaterialRandomBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_MATERIAL_RANDOM);
		RibbonUVDistance = FNiagaraConstants::GetAttributeDefaultBinding(RIBBONUVDISTANCE);
		U0OverrideBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONU0OVERRIDE);
		V0RangeOverrideBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONV0RANGEOVERRIDE);
		U1OverrideBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONU1OVERRIDE);
		V1RangeOverrideBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_RIBBONV1RANGEOVERRIDE);
	}
}

void UNiagaraRibbonRendererProperties::CacheFromCompiledData(const FNiagaraDataSetCompiledData* CompiledData)
{
	// Initialize accessors
	bSortKeyDataSetAccessorIsAge = false;
	SortKeyDataSetAccessor.Init(CompiledData, RibbonLinkOrderBinding.GetDataSetBindableVariable().GetName());
	if (!SortKeyDataSetAccessor.IsValid())
	{
		bSortKeyDataSetAccessorIsAge = true;
		SortKeyDataSetAccessor.Init(CompiledData, NormalizedAgeBinding.GetDataSetBindableVariable().GetName());
	}	
	
	NormalizedAgeAccessor.Init(CompiledData, NormalizedAgeBinding.GetDataSetBindableVariable().GetName());

	PositionDataSetAccessor.Init(CompiledData, PositionBinding.GetDataSetBindableVariable().GetName());
	SizeDataSetAccessor.Init(CompiledData, RibbonWidthBinding.GetDataSetBindableVariable().GetName());
	TwistDataSetAccessor.Init(CompiledData, RibbonTwistBinding.GetDataSetBindableVariable().GetName());
	FacingDataSetAccessor.Init(CompiledData, RibbonFacingBinding.GetDataSetBindableVariable().GetName());

	MaterialParam0DataSetAccessor.Init(CompiledData, DynamicMaterialBinding.GetDataSetBindableVariable().GetName());
	MaterialParam1DataSetAccessor.Init(CompiledData, DynamicMaterial1Binding.GetDataSetBindableVariable().GetName());
	MaterialParam2DataSetAccessor.Init(CompiledData, DynamicMaterial2Binding.GetDataSetBindableVariable().GetName());
	MaterialParam3DataSetAccessor.Init(CompiledData, DynamicMaterial3Binding.GetDataSetBindableVariable().GetName());

	FNiagaraDataSetAccessor<float> RibbonUVDistanceAccessor;
	RibbonUVDistanceAccessor.Init(CompiledData, RibbonUVDistance.GetDataSetBindableVariable().GetName());
	DistanceFromStartIsBound = RibbonUVDistanceAccessor.IsValid();

	FNiagaraDataSetAccessor<float> U0OverrideDataSetAccessor;
	U0OverrideDataSetAccessor.Init(CompiledData, U0OverrideBinding.GetDataSetBindableVariable().GetName());
	U0OverrideIsBound = U0OverrideDataSetAccessor.IsValid();
	FNiagaraDataSetAccessor<float> U1OverrideDataSetAccessor;
	U1OverrideDataSetAccessor.Init(CompiledData, U1OverrideBinding.GetDataSetBindableVariable().GetName());
	U1OverrideIsBound = U1OverrideDataSetAccessor.IsValid();

	if (RibbonIdBinding.GetDataSetBindableVariable().GetType() == FNiagaraTypeDefinition::GetIDDef())
	{
		RibbonFullIDDataSetAccessor.Init(CompiledData, RibbonIdBinding.GetDataSetBindableVariable().GetName());
	}
	else
	{
		RibbonIdDataSetAccessor.Init(CompiledData, RibbonIdBinding.GetDataSetBindableVariable().GetName());
	}

	const bool bShouldDoFacing = FacingMode == ENiagaraRibbonFacingMode::Custom || FacingMode == ENiagaraRibbonFacingMode::CustomSideVector;

	// Initialize layout
	RendererLayout.Initialize(ENiagaraRibbonVFLayout::Num);
	RendererLayout.SetVariableFromBinding(CompiledData, PositionBinding, ENiagaraRibbonVFLayout::Position);
	RendererLayout.SetVariableFromBinding(CompiledData, VelocityBinding, ENiagaraRibbonVFLayout::Velocity);
	RendererLayout.SetVariableFromBinding(CompiledData, ColorBinding, ENiagaraRibbonVFLayout::Color);
	RendererLayout.SetVariableFromBinding(CompiledData, RibbonWidthBinding, ENiagaraRibbonVFLayout::Width);
	RendererLayout.SetVariableFromBinding(CompiledData, RibbonTwistBinding, ENiagaraRibbonVFLayout::Twist);
	if (bShouldDoFacing)
	{
		RendererLayout.SetVariableFromBinding(CompiledData, RibbonFacingBinding, ENiagaraRibbonVFLayout::Facing);
	}
	RendererLayout.SetVariableFromBinding(CompiledData, NormalizedAgeBinding, ENiagaraRibbonVFLayout::NormalizedAge);
	RendererLayout.SetVariableFromBinding(CompiledData, MaterialRandomBinding, ENiagaraRibbonVFLayout::MaterialRandom);
	RendererLayout.SetVariableFromBinding(CompiledData, RibbonUVDistance, ENiagaraRibbonVFLayout::DistanceFromStart);
	RendererLayout.SetVariableFromBinding(CompiledData, U0OverrideBinding, ENiagaraRibbonVFLayout::U0Override);
	RendererLayout.SetVariableFromBinding(CompiledData, V0RangeOverrideBinding, ENiagaraRibbonVFLayout::V0RangeOverride);
	RendererLayout.SetVariableFromBinding(CompiledData, U1OverrideBinding, ENiagaraRibbonVFLayout::U1Override);
	RendererLayout.SetVariableFromBinding(CompiledData, V1RangeOverrideBinding, ENiagaraRibbonVFLayout::V1RangeOverride);
	MaterialParamValidMask  = RendererLayout.SetVariableFromBinding(CompiledData, DynamicMaterialBinding, ENiagaraRibbonVFLayout::MaterialParam0) ? 1 : 0;
	MaterialParamValidMask |= RendererLayout.SetVariableFromBinding(CompiledData, DynamicMaterial1Binding, ENiagaraRibbonVFLayout::MaterialParam1) ? 2 : 0;
	MaterialParamValidMask |= RendererLayout.SetVariableFromBinding(CompiledData, DynamicMaterial2Binding, ENiagaraRibbonVFLayout::MaterialParam2) ? 4 : 0;
	MaterialParamValidMask |= RendererLayout.SetVariableFromBinding(CompiledData, DynamicMaterial3Binding, ENiagaraRibbonVFLayout::MaterialParam3) ? 8 : 0;
	RendererLayout.Finalize();
}


#if WITH_EDITORONLY_DATA

bool UNiagaraRibbonRendererProperties::IsSupportedVariableForBinding(const FNiagaraVariableBase& InSourceForBinding, const FName& InTargetBindingName) const
{
	if (InSourceForBinding.IsInNameSpace(FNiagaraConstants::ParticleAttributeNamespace) ||
		InSourceForBinding.IsInNameSpace(FNiagaraConstants::UserNamespace) ||
		InSourceForBinding.IsInNameSpace(FNiagaraConstants::SystemNamespace) ||
		InSourceForBinding.IsInNameSpace(FNiagaraConstants::EmitterNamespace))
	{
		return true;
	}
	return false;
}

void UNiagaraRibbonRendererProperties::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraRibbonRendererProperties, TessellationAngle))
	{
		if (TessellationAngle > 0.f && TessellationAngle < 1.f)
		{
			TessellationAngle = 1.f;
		}
	}
}

const TArray<FNiagaraVariable>& UNiagaraRibbonRendererProperties::GetOptionalAttributes()
{
	static TArray<FNiagaraVariable> Attrs;

	if (Attrs.Num() == 0)
	{
		Attrs.Add(SYS_PARAM_PARTICLES_POSITION);
		Attrs.Add(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
		Attrs.Add(SYS_PARAM_PARTICLES_COLOR);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONID);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONTWIST);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONWIDTH);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONFACING);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONLINKORDER);
		Attrs.Add(RIBBONUVDISTANCE);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONU0OVERRIDE);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONV0RANGEOVERRIDE);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONU1OVERRIDE);
		Attrs.Add(SYS_PARAM_PARTICLES_RIBBONV1RANGEOVERRIDE);
	}

	return Attrs;
}


void UNiagaraRibbonRendererProperties::GetRendererWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const
{
	TSharedRef<SWidget> ThumbnailWidget = SNullWidget::NullWidget;
	int32 ThumbnailSize = 32;
	TArray<UMaterialInterface*> Materials;
	GetUsedMaterials(InEmitter, Materials);
	for (UMaterialInterface* PreviewedMaterial : Materials)
	{
		TSharedPtr<FAssetThumbnail> AssetThumbnail = MakeShareable(new FAssetThumbnail(PreviewedMaterial, ThumbnailSize, ThumbnailSize, InThumbnailPool));
		if (AssetThumbnail)
		{
			ThumbnailWidget = AssetThumbnail->MakeThumbnailWidget();
		}
		OutWidgets.Add(ThumbnailWidget);
	}

	if (Materials.Num() == 0)
	{
		TSharedRef<SWidget> SpriteWidget = SNew(SImage)
			.Image(FSlateIconFinder::FindIconBrushForClass(GetClass()));
		OutWidgets.Add(SpriteWidget);
	}
}

void UNiagaraRibbonRendererProperties::GetRendererTooltipWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const
{
	TArray<UMaterialInterface*> Materials;
	GetUsedMaterials(InEmitter, Materials);
	if (Materials.Num() > 0)
	{
		GetRendererWidgets(InEmitter, OutWidgets, InThumbnailPool);
	}
	else
	{
		TSharedRef<SWidget> RibbonTooltip = SNew(STextBlock)
			.Text(LOCTEXT("RibbonRendererNoMat", "Ribbon Renderer (No Material Set)"));
		OutWidgets.Add(RibbonTooltip);
	}
}


void UNiagaraRibbonRendererProperties::GetRendererFeedback(const UNiagaraEmitter* InEmitter, TArray<FText>& OutErrors, TArray<FText>& OutWarnings, TArray<FText>& OutInfo) const
{
	Super::GetRendererFeedback(InEmitter, OutErrors, OutWarnings, OutInfo);
}


bool UNiagaraRibbonRendererProperties::IsMaterialValidForRenderer(UMaterial* InMaterial, FText& InvalidMessage)
{
	if (InMaterial->bUsedWithNiagaraRibbons == false)
	{
		InvalidMessage = NSLOCTEXT("NiagaraRibbonRendererProperties", "InvalidMaterialMessage", "The material isn't marked as \"Used with Niagara ribbons\"");
		return false;
	}
	return true;
}

void UNiagaraRibbonRendererProperties::FixMaterial(UMaterial* InMaterial)
{
	InMaterial->Modify();
	InMaterial->bUsedWithNiagaraRibbons = true;
	InMaterial->ForceRecompileForRendering();
}

bool UNiagaraRibbonRendererProperties::CanEditChange(const FProperty* InProperty) const
{

	if (InProperty->HasMetaData(TEXT("Category")) && InProperty->GetMetaData(TEXT("Category")).Contains("Tessellation"))
	{
		FName PropertyName = InProperty->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraRibbonRendererProperties, CurveTension))
		{
			return TessellationMode != ENiagaraRibbonTessellationMode::Disabled;
		}
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraRibbonRendererProperties, TessellationFactor))
		{
			return TessellationMode == ENiagaraRibbonTessellationMode::Custom;
		}
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraRibbonRendererProperties, TessellationMode))
		{
			return Super::CanEditChange(InProperty);
		}
		return TessellationMode == ENiagaraRibbonTessellationMode::Custom;
	}
	return Super::CanEditChange(InProperty);
}

void UNiagaraRibbonRendererProperties::RenameVariable(const FNiagaraVariableBase& OldVariable, const FNiagaraVariableBase& NewVariable, const UNiagaraEmitter* InEmitter)
{
	Super::RenameVariable(OldVariable, NewVariable, InEmitter);

	// Handle renaming material bindings
	for (FNiagaraMaterialAttributeBinding& Binding : MaterialParameterBindings)
	{
		Binding.RenameVariableIfMatching(OldVariable, NewVariable, InEmitter, GetCurrentSourceMode());
	}
}

void UNiagaraRibbonRendererProperties::RemoveVariable(const FNiagaraVariableBase& OldVariable, const UNiagaraEmitter* InEmitter)
{
	Super::RemoveVariable(OldVariable, InEmitter);

	// Handle resetting material bindings to defaults
	for (FNiagaraMaterialAttributeBinding& Binding : MaterialParameterBindings)
	{
		if (Binding.Matches(OldVariable, InEmitter, GetCurrentSourceMode()))
		{
			Binding.NiagaraVariable = FNiagaraVariable();
			Binding.CacheValues(InEmitter);
		}
	}
}

#endif // WITH_EDITORONLY_DATA
#undef LOCTEXT_NAMESPACE
