// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraComponentRendererProperties.h"
#include "NiagaraRenderer.h"
#include "NiagaraConstants.h"
#include "NiagaraRendererComponents.h"
#include "Modules/ModuleManager.h"
#if WITH_EDITOR
#include "Widgets/Images/SImage.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/SWidget.h"
#include "Styling/SlateBrush.h"
#include "AssetThumbnail.h"
#include "Widgets/Text/STextBlock.h"
#endif

static float GNiagaraComponentRenderComponentCountWarning = 50;
static FAutoConsoleVariableRef CVarNiagaraComponentRenderComponentCountWarning(
	TEXT("fx.Niagara.ComponentRenderComponentCountWarning"),
	GNiagaraComponentRenderComponentCountWarning,
	TEXT("The max number of allowed components before a ui warning is shown in the component renderer."),
	ECVF_Default
	);

#define LOCTEXT_NAMESPACE "UNiagaraComponentRendererProperties"

bool UNiagaraComponentRendererProperties::IsConvertible(const FNiagaraTypeDefinition& SourceType, const FNiagaraTypeDefinition& TargetType)
{
	if (SourceType == TargetType)
	{
		return true;
	}

	if ((SourceType == FNiagaraTypeDefinition::GetColorDef() && TargetType.GetStruct() == GetFColorDef().GetStruct()) ||
		(SourceType == FNiagaraTypeDefinition::GetVec3Def() && TargetType.GetStruct() == GetFColorDef().GetStruct()) ||
		(SourceType == FNiagaraTypeDefinition::GetVec3Def() && TargetType.GetStruct() == GetFRotatorDef().GetStruct()) ||
		(SourceType == FNiagaraTypeDefinition::GetVec4Def() && TargetType.GetStruct() == GetFColorDef().GetStruct()) ||
		(SourceType == FNiagaraTypeDefinition::GetQuatDef() && TargetType.GetStruct() == GetFRotatorDef().GetStruct()))
	{
		return true;
	}
	return false;
}

NIAGARA_API FNiagaraTypeDefinition UNiagaraComponentRendererProperties::ToNiagaraType(FProperty* Property)
{
	const FFieldClass* FieldClass = Property->GetClass();
	if (FieldClass->IsChildOf(FBoolProperty::StaticClass()))
	{
		return FNiagaraTypeDefinition::GetBoolDef();
	}
	if (FieldClass->IsChildOf(FIntProperty::StaticClass()))
	{
		return FNiagaraTypeDefinition::GetIntDef();
	}
	if (FieldClass->IsChildOf(FFloatProperty::StaticClass()))
	{
		return FNiagaraTypeDefinition::GetFloatDef();
	}
	if (FieldClass->IsChildOf(FStructProperty::StaticClass()))
	{
		FStructProperty* StructProperty = (FStructProperty*)Property;
		if (StructProperty->Struct)
		{
			return FNiagaraTypeDefinition(StructProperty->Struct);
		}
	}

	// we currently don't support reading arbitrary enum or object data from the simulation data
	/*
	if (FieldClass->IsChildOf(FObjectProperty::StaticClass()))
	{
		return FNiagaraTypeDefinition::GetUObjectDef();
	}
	if (FieldClass->IsChildOf(FEnumProperty::StaticClass()))
	{
		FEnumProperty* EnumProperty = (FEnumProperty*)PropertyHandle->GetProperty();
		if (UEnum* EnumDef = EnumProperty->GetEnum())
		{
			return FNiagaraTypeDefinition(EnumDef);
		}
	}
	 */

	return FNiagaraTypeDefinition();
}

FNiagaraTypeDefinition UNiagaraComponentRendererProperties::GetFColorDef()
{
	static UPackage* CoreUObjectPkg = FindObjectChecked<UPackage>(nullptr, TEXT("/Script/CoreUObject"));
	static UScriptStruct* ColorStruct = FindObjectChecked<UScriptStruct>(CoreUObjectPkg, TEXT("Color"));
	return FNiagaraTypeDefinition(ColorStruct);
}

FNiagaraTypeDefinition UNiagaraComponentRendererProperties::GetFRotatorDef()
{
	static UPackage* CoreUObjectPkg = FindObjectChecked<UPackage>(nullptr, TEXT("/Script/CoreUObject"));
	static UScriptStruct* RotatorStruct = FindObjectChecked<UScriptStruct>(CoreUObjectPkg, TEXT("Rotator"));
	return FNiagaraTypeDefinition(RotatorStruct);
}

TArray<TWeakObjectPtr<UNiagaraComponentRendererProperties>> UNiagaraComponentRendererProperties::ComponentRendererPropertiesToDeferredInit;

UNiagaraComponentRendererProperties::UNiagaraComponentRendererProperties()
	: ComponentCountLimit(15), bAssignComponentsOnParticleID(true)
#if WITH_EDITORONLY_DATA
	, bVisualizeComponents(true)
#endif
	, TemplateComponent(nullptr)
{
}

void UNiagaraComponentRendererProperties::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		// We can end up hitting PostInitProperties before the Niagara Module has initialized bindings this needs, mark this object for deferred init and early out.
		if (FModuleManager::Get().IsModuleLoaded("Niagara") == false)
		{
			ComponentRendererPropertiesToDeferredInit.Add(this);
			return;
		}
		else if (EnabledBinding.BoundVariable.GetName() == NAME_None)
		{
			EnabledBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_COMPONENTS_ENABLED);
		}
	}
}

void UNiagaraComponentRendererProperties::InitCDOPropertiesAfterModuleStartup()
{
	UNiagaraComponentRendererProperties* CDO = CastChecked<UNiagaraComponentRendererProperties>(UNiagaraComponentRendererProperties::StaticClass()->GetDefaultObject());
	CDO->EnabledBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_COMPONENTS_ENABLED);

	for (TWeakObjectPtr<UNiagaraComponentRendererProperties>& WeakComponentRendererProperties : ComponentRendererPropertiesToDeferredInit)
	{
		if (WeakComponentRendererProperties.Get())
		{
			if (WeakComponentRendererProperties->EnabledBinding.BoundVariable.GetName() == NAME_None)
			{
				WeakComponentRendererProperties->EnabledBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_COMPONENTS_ENABLED);
			}
		}
	}
}

FNiagaraRenderer* UNiagaraComponentRendererProperties::CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter)
{
	EmitterPtr = Emitter->GetCachedEmitter();

	FNiagaraRenderer* NewRenderer = new FNiagaraRendererComponents(FeatureLevel, this, Emitter);
	NewRenderer->Initialize(this, Emitter);
	return NewRenderer;
}

#if WITH_EDITORONLY_DATA

void UNiagaraComponentRendererProperties::PostEditChangeProperty(struct FPropertyChangedEvent& e)
{
	FName PropertyName = (e.Property != NULL) ? e.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraComponentRendererProperties, ComponentType))
	{
		PropertyBindings.Empty();
		if (TemplateComponent)
		{
			TemplateComponent->DestroyComponent();
		}
		if (ComponentType)
		{
			TemplateComponent = NewObject<USceneComponent>(this, ComponentType, NAME_None, RF_ArchetypeObject | RF_Public);
			TemplateComponent->SetVisibility(false);
			TemplateComponent->SetAutoActivate(false);
			TemplateComponent->SetComponentTickEnabled(false);
			
			// set some defaults on the component
			bool IsWorldSpace = EmitterPtr ? !EmitterPtr->bLocalSpace : true;
			TemplateComponent->SetAbsolute(IsWorldSpace, IsWorldSpace, IsWorldSpace);

			FNiagaraComponentPropertyBinding PositionBinding;
			PositionBinding.AttributeBinding.BoundVariable = SYS_PARAM_PARTICLES_POSITION;
			PositionBinding.AttributeBinding.DataSetVariable = FNiagaraConstants::GetAttributeAsDataSetKey(SYS_PARAM_PARTICLES_POSITION);
			PositionBinding.PropertyName = FName("RelativeLocation");
			PropertyBindings.Add(PositionBinding);

			FNiagaraComponentPropertyBinding ScaleBinding;
			ScaleBinding.AttributeBinding.BoundVariable = SYS_PARAM_PARTICLES_SCALE;
			ScaleBinding.AttributeBinding.DataSetVariable = FNiagaraConstants::GetAttributeAsDataSetKey(SYS_PARAM_PARTICLES_SCALE);
			ScaleBinding.PropertyName = FName("RelativeScale3D");
			PropertyBindings.Add(ScaleBinding);
		}
		else
		{
			TemplateComponent = nullptr;
		}
	}
	Super::PostEditChangeProperty(e);
}

void UNiagaraComponentRendererProperties::GetRendererWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const
{
	TSharedRef<SWidget> LightWidget = SNew(SImage)
		.Image(FSlateIconFinder::FindIconBrushForClass(GetClass()));
	OutWidgets.Add(LightWidget);
}

void UNiagaraComponentRendererProperties::GetRendererTooltipWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const
{
	TSharedRef<SWidget> LightTooltip = SNew(STextBlock)
		.Text(LOCTEXT("ComponentRendererTooltip", "Component Renderer"));
	OutWidgets.Add(LightTooltip);
}

void UNiagaraComponentRendererProperties::GetRendererFeedback(const UNiagaraEmitter* InEmitter, TArray<FText>& OutErrors, TArray<FText>& OutWarnings, TArray<FText>& OutInfo) const
{
	Super::GetRendererFeedback(InEmitter, OutErrors, OutWarnings, OutInfo);

	OutInfo.Add(FText::FromString(TEXT("The component renderer is still a very experimental feature that offers great flexibility, \nbut is *not* optimized for performance or safety. \nWith great power comes great responsibility.")));

	if (InEmitter && TemplateComponent)
	{
		bool IsWorldSpace = !InEmitter->bLocalSpace;
		if (TemplateComponent->IsUsingAbsoluteLocation() != IsWorldSpace)
		{
			OutWarnings.Add(FText::FromString(TEXT("The component location is configured to use a different localspace setting than the emitter.")));
		}
		if (TemplateComponent->IsUsingAbsoluteRotation() != IsWorldSpace)
		{
			OutWarnings.Add(FText::FromString(TEXT("The component rotation is configured to use a different localspace setting than the emitter.")));
		}
		if (TemplateComponent->IsUsingAbsoluteScale() != IsWorldSpace)
		{
			OutWarnings.Add(FText::FromString(TEXT("The component scale is configured to use a different localspace setting than the emitter.")));
		}
	}

	if (ComponentCountLimit > GNiagaraComponentRenderComponentCountWarning)
	{
		OutWarnings.Add(FText::FromString(TEXT("Creating and updating many components each tick will have a serious impact on performance.")));
	}
}

const TArray<FNiagaraVariable>& UNiagaraComponentRendererProperties::GetBoundAttributes()
{
	CurrentBoundAttributes.Reset();

	CurrentBoundAttributes.Add(SYS_PARAM_PARTICLES_COMPONENTS_ENABLED);
	if (bAssignComponentsOnParticleID)
	{
		CurrentBoundAttributes.Add(SYS_PARAM_PARTICLES_UNIQUE_ID);
	}
	for (const FNiagaraComponentPropertyBinding& PropertyBinding : PropertyBindings)
	{
		if (PropertyBinding.AttributeBinding.BoundVariable.IsValid())
		{
			CurrentBoundAttributes.Add(PropertyBinding.AttributeBinding.BoundVariable);
		}
	}
	return CurrentBoundAttributes;
}

const TArray<FNiagaraVariable>& UNiagaraComponentRendererProperties::GetOptionalAttributes()
{
	static TArray<FNiagaraVariable> Attrs;
	if (Attrs.Num() == 0)
	{
		Attrs.Add(SYS_PARAM_PARTICLES_COMPONENTS_ENABLED);
	}
	return Attrs;
}

#endif // WITH_EDITORONLY_DATA

#undef LOCTEXT_NAMESPACE