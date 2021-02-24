// Copyright Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "CoreMinimal.h"
#include "Algo/Transform.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/ARFilter.h"
#include "GameFramework/Actor.h"
#include "RemoteControlActor.h"
#include "RemoteControlField.h"
#include "RemoteControlPreset.h"

#include "RemoteControlModels.generated.h"

namespace RemoteControlModels
{
	// Names used to sanitize property/function metadata
	static FName Name_UIMin("UIMin");
	static FName Name_UIMax("UIMax");
	static FName Name_ClampMin("ClampMin");
	static FName Name_ClampMax("ClampMax");
	static FName Name_ToolTip("ToolTip");
	static FName Name_EnumValues("EnumValues");

	// Names used to sanitize asset metadata
	static FName NAME_FiBData("FiBData");
	static FName NAME_ClassFlags("ClassFlags");
	static FName NAME_AssetImportData("AssetImportData");

	static TMap<FName, FString> SanitizeMetadata(const TMap<FName, FString>& InMetadata)
	{
		TMap<FName, FString> OutMetadata;
		OutMetadata.Reserve(InMetadata.Num());
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

	static TMap<FName, FString> SanitizeAssetMetadata(const FAssetDataTagMap& InAssetMetadata)
	{
		TMap<FName, FString> OutMetadata;
		OutMetadata.Reserve(InAssetMetadata.Num());
		auto IsValid = [] (const TTuple<FName, FString>& InTuple)
		{
			return InTuple.Key != NAME_FiBData
				&& InTuple.Key != NAME_ClassFlags
				&& InTuple.Key != NAME_AssetImportData;
		};

		Algo::TransformIf(InAssetMetadata, OutMetadata, IsValid, [](const TTuple<FName, FString>& InTuple) { return InTuple; });
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
		UEnum* Enum = nullptr;

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(ValueProperty))
		{
			Enum = EnumProperty->GetEnum();
		}

		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(ValueProperty))
		{
			Enum = ByteProperty->Enum;
		}

		if (Enum)
		{
			const int32 EnumCount = Enum->NumEnums() - 1; //Don't list the _MAX entry
			TStringBuilder<256> Builder;
			for (int32 Index = 0; Index < EnumCount; ++Index)
			{
				FString Text = Enum->GetDisplayNameTextByIndex(Index).ToString();
				if (Text.IsEmpty())
				{
					Text = Enum->GetNameStringByIndex(Index);
				}

				if (Index > 0)
				{
					Builder.Append(TEXT(", "));
				}
				Builder.Append(Text);
			}

			if (Builder.Len() > 0)
			{
				Metadata.FindOrAdd(RemoteControlModels::Name_EnumValues) = Builder.ToString();
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
		: DisplayName(RCProperty.GetLabel())
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
		: DisplayName(Function.GetLabel())
		, UnderlyingFunction(Function.Function)
	{
	}
	
	UPROPERTY()
	FName DisplayName;
	
	UPROPERTY()
	FRCFunctionDescription UnderlyingFunction;
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

		Class = InActor->GetClass()->GetName();
	}

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Path;

	UPROPERTY()
	FString Class;
};

USTRUCT()
struct FRCExposedActorDescription
{
	GENERATED_BODY()

	FRCExposedActorDescription() = default;

	FRCExposedActorDescription(const FRemoteControlActor& InExposedActor)
		: DisplayName(InExposedActor.GetLabel())
	{
		if (AActor* Actor = Cast<AActor>(InExposedActor.Path.ResolveObject()))
		{
			UnderlyingActor = FRCActorDescription{ Actor };
		}
	}

	UPROPERTY()
	FName DisplayName;

	UPROPERTY()
	FRCActorDescription UnderlyingActor;
};

USTRUCT()
struct FRCPresetLayoutGroupDescription
{
	GENERATED_BODY()

	FRCPresetLayoutGroupDescription() = default;

	FRCPresetLayoutGroupDescription(const URemoteControlPreset* Preset, const FRemoteControlPresetGroup& Group)
		: Name(Group.Name)
	{
		checkSlow(Preset);
		for (const FGuid& FieldId : Group.GetFields())
		{
			AddExposedField(Preset, FieldId);
		}
	}

	FRCPresetLayoutGroupDescription(const URemoteControlPreset* Preset, const FRemoteControlPresetGroup& Group, const TArray<FName>& FieldLabels)
		: Name(Group.Name)
	{
		checkSlow(Preset);
		for (FName FieldLabel : FieldLabels)
		{
			AddExposedField(Preset, Preset->GetFieldId(FieldLabel));
		}
	}

public:
	UPROPERTY()
	FName Name;

	UPROPERTY()
	TArray<FRCExposedPropertyDescription> ExposedProperties;

	UPROPERTY()
	TArray<FRCExposedFunctionDescription> ExposedFunctions;

	UPROPERTY()
	TArray<FRCExposedActorDescription> ExposedActors;

private:
	/** Add an exposed field to this group description. */
	void AddExposedField(const URemoteControlPreset* Preset, const FGuid& FieldId)
	{
		if (TOptional<FRemoteControlField> Field = Preset->GetField(FieldId))
		{
			if (Field->FieldType == EExposedFieldType::Property)
			{
				TOptional<FRemoteControlProperty> RCProperty = Preset->GetProperty(Field->GetLabel());
				TOptional<FExposedProperty> UnderlyingProperty = Preset->ResolveExposedProperty(Field->GetLabel());
				if (RCProperty && UnderlyingProperty && UnderlyingProperty->Property)
				{
					ExposedProperties.Add(FRCExposedPropertyDescription{ MoveTemp(*RCProperty), UnderlyingProperty->Property });
				}
			}
			else if (Field->FieldType == EExposedFieldType::Function)
			{
				TOptional<FRemoteControlFunction> RCFunction = Preset->GetFunction(Field->GetLabel());
				if (RCFunction && RCFunction->Function)
				{
					ExposedFunctions.Add(FRCExposedFunctionDescription{ MoveTemp(*RCFunction) });
				}
			}
		}
		else if (TSharedPtr<const FRemoteControlActor> Actor = Preset->GetExposedEntity<FRemoteControlActor>(FieldId).Pin())
		{
			if (Actor->Path.ResolveObject())
			{
				FRCExposedActorDescription ActorDescription{*Actor};
				ExposedActors.Add(MoveTemp(ActorDescription));
			}
		}
	}
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

		Algo::Transform(Preset->Layout.GetGroups(), Groups, [Preset](const FRemoteControlPresetGroup& Group) { return FRCPresetLayoutGroupDescription{ Preset, Group }; });
	}

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Path;

	UPROPERTY()
	TArray<FRCPresetLayoutGroupDescription> Groups;
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
		Metadata = RemoteControlModels::SanitizeAssetMetadata(InAsset.TagsAndValues.CopyMap());
	}

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FName Class;

	UPROPERTY()
	FName Path;

	UPROPERTY()
	TMap<FName, FString> Metadata;
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

