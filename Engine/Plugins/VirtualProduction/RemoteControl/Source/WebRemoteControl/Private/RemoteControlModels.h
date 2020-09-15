// Copyright Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "CoreMinimal.h"
#include "RemoteControlPreset.h"
#include "RemoteControlField.h"
#include "Algo/Transform.h"
#include "AssetData.h"
#include "GameFramework/Actor.h"
#include "RemoteControlModels.generated.h"

USTRUCT()
struct FRCPropertyDescription
{
	GENERATED_BODY()
	
	FRCPropertyDescription() = default;
	
	FRCPropertyDescription(const FProperty* Property)
		: Name(Property->GetName())
		, Type(Property->GetCPPType())
	{
		checkSlow(Property);

		#if WITH_EDITOR
		Description = Property->GetMetaData("ToolTip");
		#endif
	}

	UPROPERTY()
	FString Name;
	
	UPROPERTY()
	FString Description;
	
	UPROPERTY()
	FString Type;
};

USTRUCT()
struct FRCFunctionDescription
{
	GENERATED_BODY()

	FRCFunctionDescription() = default;

	FRCFunctionDescription(const UFunction* Function)
		: Name(Function->GetName())
	{
		checkSlow(Function);
#if WITH_EDITOR
		Description = Function->GetMetaData("ToolTip");
#endif
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			if (It && It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
			{
				Arguments.Emplace(*It);
			}
		}
	}
	
	UPROPERTY()
	FString Name;
	
	UPROPERTY()
	FString Description;
	
	UPROPERTY()
	TArray<FRCPropertyDescription> Arguments;
};

USTRUCT()
struct FRCExposedPropertyDescription
{
	GENERATED_BODY()

	FRCExposedPropertyDescription() = default;
	
	FRCExposedPropertyDescription(const FRemoteControlProperty& RCProperty, const FProperty* InUnderlyingProperty)
		: DisplayName(RCProperty.Label)
	{
		checkSlow(InUnderlyingProperty);
		UnderlyingProperty = InUnderlyingProperty;
	}
	
	UPROPERTY()
	FName DisplayName;
	
	UPROPERTY()
	FRCPropertyDescription UnderlyingProperty;
};


USTRUCT()
struct FRCExposedFunctionDescription
{
	GENERATED_BODY()
	
	FRCExposedFunctionDescription() = default;

	FRCExposedFunctionDescription(const FRemoteControlFunction& Function)
		: DisplayName(Function.Label)
		, UnderlyingFunction(Function.Function)
	{
	}
	
	UPROPERTY()
	FName DisplayName;
	
	UPROPERTY()
	FRCFunctionDescription UnderlyingFunction;
};

USTRUCT()
struct FRemoteControlTargetDescription
{
	GENERATED_BODY()

	FRemoteControlTargetDescription() = default;
	
	FRemoteControlTargetDescription(const FRemoteControlTarget& Target)
		: Name(Target.Alias)
	{
		BoundObjects = Target.ResolveBoundObjects();
		for (const FRemoteControlProperty& Property : Target.ExposedProperties)
		{
			if (TOptional<FExposedProperty> ExposedProperty = Target.ResolveExposedProperty(Property.Id))
			{
				ExposedProperties.Emplace(Property, ExposedProperty->Property);
			}
		}
		Algo::TransformIf(Target.ExposedFunctions, ExposedFunctions, [] (const FRemoteControlFunction& Function) {return !!Function.Function;},  [] (const FRemoteControlFunction& Function) { return Function; });
	}

	FRemoteControlTargetDescription(URemoteControlPreset* Preset, FName FieldLabel)
	{
		Name = Preset->GetOwnerAlias(Preset->GetFieldId(FieldLabel));
		BoundObjects = Preset->ResolvedBoundObjects(FieldLabel);
		if (TOptional<FRemoteControlFunction> RemoteControlFunction = Preset->GetFunction(FieldLabel))
		{
			FRCExposedFunctionDescription FunctionDescription(RemoteControlFunction.GetValue());
			ExposedFunctions.Emplace(MoveTemp(FunctionDescription));
		}
		else if (TOptional<FExposedProperty> ExposedProperty = Preset->ResolveExposedProperty(FieldLabel))
		{
			if (TOptional<FRemoteControlProperty> RemoteControlProperty = Preset->GetProperty(FieldLabel))
			{
				FRCExposedPropertyDescription PropertyDescription(RemoteControlProperty.GetValue(), ExposedProperty->Property);
				ExposedProperties.Emplace(MoveTemp(PropertyDescription));
			}
		}
	}
	
	UPROPERTY()
	FName Name;
	
	UPROPERTY()
	TArray<UObject*> BoundObjects;
	
	UPROPERTY()
	TArray<FRCExposedPropertyDescription> ExposedProperties;
	
	UPROPERTY()
	TArray<FRCExposedFunctionDescription> ExposedFunctions;
};

USTRUCT()
struct FRCPresetDescription
{
	GENERATED_BODY()
	
	FRCPresetDescription() = default;

	FRCPresetDescription(const URemoteControlPreset* Preset)
	{
		checkSlow(Preset)

		Name = Preset->GetName();
		Path = Preset->GetPathName();

		for (const TPair<FName, FRemoteControlTarget>& SectionTuple : Preset->GetRemoteControlTargets())
		{
			ExposedObjects.Add(FRemoteControlTargetDescription{SectionTuple.Value});
		}
	}

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Path;
	
	UPROPERTY()
	TArray<FRemoteControlTargetDescription> ExposedObjects;
};

USTRUCT()
struct FRCShortPresetDescription
{
	GENERATED_BODY()

	FRCShortPresetDescription() = default;

	FRCShortPresetDescription(const URemoteControlPreset* Preset)

	{
		checkSlow(Preset);
		Name = Preset->GetName();
		Path =Preset->GetPathName();
	}

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Path;
};

USTRUCT() 
struct FRCAssetDescription
{
	GENERATED_BODY()

	FRCAssetDescription() = default;

	FRCAssetDescription(const FAssetData& InAsset)
	 : Name(InAsset.AssetName)
	 , Path(InAsset.GetFullName())
	{
	}

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FString Path;
};

USTRUCT()
struct FRCActorDescription
{
	GENERATED_BODY()

	FRCActorDescription() = default;

	FRCActorDescription(const AActor* InActor)
	{
		checkSlow(InActor);
#if WITH_EDITOR
		Name = InActor->GetActorLabel();
#else
		Name = InActor->GetName();
#endif
		Path = InActor->GetPathName();
	}

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Path;
};

USTRUCT()
struct FRCObjectDescription
{
	GENERATED_BODY()

	FRCObjectDescription() = default;

	FRCObjectDescription(const UObject* InObject)
	{
		checkSlow(InObject);
		Name = InObject->GetName();
		Path = InObject->GetPathName();
	}

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Path;
};

USTRUCT()
struct FRCPresetLayoutGroupDescription
{
	GENERATED_BODY()
	
	FRCPresetLayoutGroupDescription() = default;

	FRCPresetLayoutGroupDescription(const URemoteControlPreset* Preset,	const FRemoteControlPresetGroup& Group)
	{
		Fields.Reserve(Group.GetFields().Num());
		for (const FGuid& FieldId : Group.GetFields())
		{
			if (TOptional<FRemoteControlField> Field = Preset->GetField(FieldId))
			{
				Fields.Add(Field->Label);
			}
		}
	}

	UPROPERTY()
	TArray<FName> Fields;
};

USTRUCT()
struct FRCPresetLayoutDescription
{
	GENERATED_BODY()

	FRCPresetLayoutDescription() = default;

	FRCPresetLayoutDescription(const URemoteControlPreset* Preset, const FRemoteControlPresetLayout& Layout)
	{
		Algo::Transform(Layout.GetGroups(), Groups, [Preset](const FRemoteControlPresetGroup& Group) { return FRCPresetLayoutGroupDescription{Preset, Group}; });
	}

	UPROPERTY()
	TArray<FRCPresetLayoutGroupDescription> Groups;
};

USTRUCT()
struct FRCPresetFieldRenamed
{
	GENERATED_BODY()

	FRCPresetFieldRenamed() = default;

	FRCPresetFieldRenamed(const TTuple<FName, FName>& RenamedField)
		: OldFieldLabel(RenamedField.Key)
		, NewFieldLabel(RenamedField.Value)
	{
	}

	UPROPERTY()
	FName OldFieldLabel;

	UPROPERTY()
	FName NewFieldLabel;
};

USTRUCT()
struct FRCPresetFieldsRenamedEvent
{
	GENERATED_BODY()

	FRCPresetFieldsRenamedEvent() = default;

	FRCPresetFieldsRenamedEvent(FName InPresetName, TArray<TTuple<FName, FName>> InRenamedFields)
		: ResponseType(TEXT("FieldsRenamed"))
		, PresetName(InPresetName)
	{
		Algo::Transform(InRenamedFields, RenamedFields, [] (const TTuple<FName, FName>& InRenamedField) { return FRCPresetFieldRenamed{InRenamedField}; } );
	}

	UPROPERTY()
	FString ResponseType;

	UPROPERTY()
	FName PresetName;

	UPROPERTY()
	TArray<FRCPresetFieldRenamed> RenamedFields;
};
