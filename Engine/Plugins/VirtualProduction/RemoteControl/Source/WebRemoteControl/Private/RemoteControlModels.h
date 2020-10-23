// Copyright Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "CoreMinimal.h"
#include "Algo/Transform.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/ARFilter.h"
#include "GameFramework/Actor.h"
#include "RemoteControlField.h"
#include "RemoteControlPreset.h"

#include "RemoteControlModels.generated.h"

namespace RemoteControlModels
{
	static FName Name_UIMin = TEXT("UIMin");
	static FName Name_UIMax = TEXT("UIMax");
	static FName Name_ClampMin = TEXT("ClampMin");
	static FName Name_ClampMax = TEXT("ClampMax");
	static FName Name_ToolTip = TEXT("ToolTip");
	static FName Name_EnumValues = TEXT("EnumValues");

	static TMap<FName, FString> SanitizeMetadata(const TMap<FName, FString>& InMetadata)
	{
		TMap<FName, FString> OutMetadata;
		auto IsValid = [] (const TTuple<FName, FString>& InTuple)
			{
				return InTuple.Key == Name_UIMin
					|| InTuple.Key == Name_UIMax
					|| InTuple.Key == Name_ClampMin
					|| InTuple.Key == Name_ClampMax
					|| InTuple.Key == Name_ToolTip;
			};

		Algo::TransformIf(InMetadata, OutMetadata, IsValid, [](const TTuple<FName, FString>& InTuple) { return InTuple; });
		return OutMetadata;
	}
}

USTRUCT()
struct FRCPropertyDescription
{
	GENERATED_BODY()
	
	FRCPropertyDescription() = default;
	
	FRCPropertyDescription(const FProperty* Property)
	{
		checkSlow(Property);

		Name = Property->GetName();

		const FProperty* ValueProperty = Property;
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			ContainerType = Property->GetCPPType();
			ValueProperty = ArrayProperty->Inner;
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			ContainerType = Property->GetCPPType();
			ValueProperty = SetProperty->ElementProp;
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			ContainerType = Property->GetCPPType();
			KeyType = MapProperty->KeyProp->GetCPPType();
			ValueProperty = MapProperty->ValueProp;
		}
		else if (Property->ArrayDim > 1)
		{
			ContainerType = TEXT("CArray");
		}

		//Write the type name
		Type = ValueProperty->GetCPPType();


#if WITH_EDITOR
		Metadata = Property->GetMetaDataMap() ? RemoteControlModels::SanitizeMetadata(*Property->GetMetaDataMap()) : TMap<FName, FString>();
		Description = Property->GetMetaData("ToolTip");
#endif

		//Fill Enum choices metadata
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(ValueProperty))
		{
			if (const UEnum* Enum = EnumProperty->GetEnum())
			{
				const int32 EnumCount = Enum->NumEnums() - 1; //Don't list the _MAX entry
				FString EnumValues;
				for (int32 Index = 0; Index < EnumCount; ++Index)
				{
					if (Index > 0)
					{
						EnumValues = FString::Printf(TEXT("%s,%s"), *EnumValues, *Enum->GetNameStringByIndex(Index));
					}
					else
					{
						EnumValues = FString::Printf(TEXT("%s"), *Enum->GetNameStringByIndex(Index));
					}
				}

				if (EnumValues.Len() > 0)
				{
					Metadata.FindOrAdd(RemoteControlModels::Name_EnumValues) = MoveTemp(EnumValues);
				}
			}
		}
	}

	/** Name of the exposed property */
	UPROPERTY()
	FString Name;
	
	/** Description of the exposed property */
	UPROPERTY()
	FString Description;
	
	/** Type of the property value (If an array, this will be the content of the array) */
	UPROPERTY()
	FString Type;

	/** Type of the container (TMap, TArray, CArray, TSet) or empty if none */
	UPROPERTY()
	FString ContainerType;

	/** Key type if container is a map */
	UPROPERTY()
	FString KeyType;

	/** Metadata for this exposed property */
	UPROPERTY()
	TMap<FName, FString> Metadata;
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

	FRCShortPresetDescription(const TSoftObjectPtr<URemoteControlPreset>& Preset)
	{
		Name = Preset.GetAssetName();
		Path = Preset.GetLongPackageName() + TEXT(".") + Name;
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
		, Class(InAsset.AssetClass)
		, Path(InAsset.ObjectPath)
	{
	}

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FName Class;

	UPROPERTY()
	FName Path;
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
struct FRCAssetFilter
{
	GENERATED_BODY()

	FRCAssetFilter() = default;

	FARFilter ToARFilter() const
	{
		FARFilter Filter;
		Filter.PackageNames = PackageNames;
		Filter.ClassNames = ClassNames;
		Filter.RecursiveClassesExclusionSet = RecursiveClassesExclusionSet;
		Filter.bRecursiveClasses = RecursiveClasses;
		Filter.PackagePaths = PackagePaths;

		// Default to a recursive search at root if no filter is specified.
		if (Filter.PackageNames.Num() == 0
			&& Filter.ClassNames.Num() == 0
			&& Filter.PackagePaths.Num() == 0)
		{
			Filter.PackagePaths = { FName("/Game") };
			Filter.bRecursivePaths = true;
		}
		else
		{
			Filter.PackagePaths = PackagePaths;
			Filter.bRecursivePaths = RecursivePaths;
		}

		return Filter;
	}

	/** The filter component for package names */
	UPROPERTY()
	TArray<FName> PackageNames;

	/** The filter component for package paths */
	UPROPERTY()
	TArray<FName> PackagePaths;

	/** The filter component for class names. Instances of the specified classes, but not subclasses (by default), will be included. Derived classes will be included only if bRecursiveClasses is true. */
	UPROPERTY()
	TArray<FName> ClassNames;

	/** Only if bRecursiveClasses is true, the results will exclude classes (and subclasses) in this list */
	UPROPERTY()
	TSet<FName> RecursiveClassesExclusionSet;

	/** If true, subclasses of ClassNames will also be included and RecursiveClassesExclusionSet will be excluded. */
	UPROPERTY()
	bool RecursiveClasses = false;

	/** If true, PackagePath components will be recursive */
	UPROPERTY()
	bool RecursivePaths = false;
};

