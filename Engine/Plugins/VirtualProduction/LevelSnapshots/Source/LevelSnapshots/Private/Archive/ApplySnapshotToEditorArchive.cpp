// Copyright Epic Games, Inc. All Rights Reserved.

#include "Archive/ApplySnapshotToEditorArchive.h"

#include "Archive/ClassDefaults/ApplyClassDefaulDataArchive.h"
#include "Data/RestorationEvents/ApplySnapshotPropertiesScope.h"
#include "Data/Util/WorldData/ClassDataUtil.h"
#include "Data/Util/Property/PropertyUtil.h"
#include "Data/Util/WorldData/SnapshotObjectUtil.h"
#include "Data/WorldSnapshotData.h"
#include "LevelSnapshotsLog.h"
#include "Selection/PropertySelection.h"

#include "Engine/World.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "Util/Property/WorldReferenceCheckingUtil.h"

namespace UE::LevelSnapshots::Private::Internal
{
	using FShouldSerialize = TFunctionRef<bool(const FArchiveSerializedPropertyChain*, const FProperty*)>;
	class FCopyProperties : public FObjectWriter
	{
		using Super = FObjectWriter;

		FShouldSerialize PropertiesToSerialize;
		UObject* SnapshotObject;

		bool ContainsWorldObjectProperty(const FProperty* InProperty) const
		{
			return UE::LevelSnapshots::Private::ContainsSubobjectOrSatisfiesPredicate(SnapshotObject, GetSerializedPropertyChain(), InProperty, [](UObject* ContainedUObject)
			{
				return ContainedUObject && ContainedUObject->IsInA(UWorld::StaticClass());
			});
		}
				
	public:

		mutable TSet<const FTextProperty*> TextProperties;

		FCopyProperties(UObject* SnapshotObject, TArray<uint8>& SaveLocation, FShouldSerialize PropertiesToSerialize)
			: Super(SaveLocation)
			, PropertiesToSerialize(PropertiesToSerialize)
			, SnapshotObject(SnapshotObject)
		{
			ArNoDelta = true;
		}

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
		{
			// Do not copy object reference properties that have a world as outer: They will not be valid when copied from snapshot to editor world.
			// Hence, we only allow object references to external assets, e.g. UMaterials or UDataAssets
			if (ContainsWorldObjectProperty(InProperty))
			{
				return true;
			}

			const bool bIsPropertyAllowed = PropertiesToSerialize(GetSerializedPropertyChain(), InProperty);
			if (bIsPropertyAllowed)
			{
				if (const FTextProperty* TextProperty = CastField<FTextProperty>(InProperty))
				{
					TextProperties.Add(TextProperty);
					return false;
				}
			}
			return !bIsPropertyAllowed;
		}		
	};

	class FSerializeTextProperties : public FApplyClassDefaulDataArchive
	{
		TSet<const FTextProperty*>& TextProperties;
	public:
		
		FSerializeTextProperties(TSet<const FTextProperty*>& InTextProperties, FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InSerializedObject)
			:
			FApplyClassDefaulDataArchive(InObjectData, InSharedData, InSerializedObject, ESerialisationMode::RestoringChangedDefaults),
			TextProperties(InTextProperties)
		{}

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
		{
			if (const FTextProperty* TextProperty = CastField<FTextProperty>(InProperty))
			{
				return !TextProperties.Contains(TextProperty);
			}
			return true;
		}
	};
	
	static TSet<const FTextProperty*> CopyPastePropertiesDifferentInCDO(FShouldSerialize ShouldSerialize, UObject* InOriginalObject, UObject* InDeserializedVersion)
	{
		TArray<uint8> CopiedPropertyData;
		FCopyProperties CopySimpleProperties(InDeserializedVersion, CopiedPropertyData, ShouldSerialize);
		InDeserializedVersion->Serialize(CopySimpleProperties);
		
		FObjectReader PasteSimpleProperties(InOriginalObject, CopiedPropertyData);
		return CopySimpleProperties.TextProperties;
	}

	static TSet<const FTextProperty*> CopyPastePropertiesDifferentInCDO(const FPropertySelection& PropertiesLeftToSerialise, UObject* InOriginalObject, UObject* InDeserializedVersion)
	{
		if (PropertiesLeftToSerialise.IsEmpty())
		{
			return {};
		}

		return CopyPastePropertiesDifferentInCDO(
			[&PropertiesLeftToSerialise](const FArchiveSerializedPropertyChain* Chain, const FProperty* Property)
			{
				return PropertiesLeftToSerialise.ShouldSerializeProperty(Chain, Property);
			},
			InOriginalObject,
			InDeserializedVersion
			);
	}

	static void FixUpTextPropertiesDifferentInCDO(
		TSet<const FTextProperty*> TextProperties,
		FWorldSnapshotData& InSharedData,
		TOptional<FClassDataIndex> ClassIndex,
		FSnapshotDataCache& Cache,
		UObject* InOriginalObject)
	{
		const bool bAreTextPropertiesLeft = TextProperties.Num() > 0;
		if (!bAreTextPropertiesLeft)
		{
			return;
		}

		if (ClassIndex)
		{
			const FSubobjectArchetypeFallbackInfo FallbackInfo { InOriginalObject->GetOuter() };
			TOptional<TNonNullPtr<FClassSnapshotData>> ClassDefaults =
				InOriginalObject->IsA<AActor>()
					? TOptional<TNonNullPtr<FClassSnapshotData>>(&InSharedData.ClassData[*ClassIndex])
					: GetSubobjectArchetypeData(InSharedData, *ClassIndex, Cache, FallbackInfo);
			if (ClassDefaults)
			{
				FSerializeTextProperties SerializeTextProperties(TextProperties, *ClassDefaults.GetValue(), InSharedData, InOriginalObject);
				InOriginalObject->Serialize(SerializeTextProperties);
				return;
			}
		}
			
		UE_LOG(LogLevelSnapshots, Warning, TEXT("%d text properties have changed in class defaults since snapshot was taken but cannot be restored."), TextProperties.Num());
	}
}

void UE::LevelSnapshots::Private::FApplySnapshotToEditorArchive::ApplyToExistingEditorWorldObject(
	FObjectSnapshotData& InObjectData,
	FWorldSnapshotData& InSharedData,
	FSnapshotDataCache& Cache,
	UObject* InOriginalObject,
	UObject* InDeserializedVersion,
	const FPropertySelectionMap& InSelectionMapForResolvingSubobjects,
	TOptional<FClassDataIndex> ClassIndex)
{
	const FPropertySelection* Selection = InSelectionMapForResolvingSubobjects.GetObjectSelection(InOriginalObject).GetPropertySelection();
	if (!Selection || Selection->IsEmpty())
	{
		return;
	}

	UE_LOG(LogLevelSnapshots, Verbose, TEXT("Applying to existing object %s (class %s)"), *InOriginalObject->GetPathName(), *InOriginalObject->GetClass()->GetPathName());
	const FApplySnapshotPropertiesScope NotifySnapshotListeners({ InOriginalObject, InSelectionMapForResolvingSubobjects, Selection, true });
#if WITH_EDITOR
	InOriginalObject->Modify();
#endif

	FPropertySelection PropertiesLeftToSerialize = *Selection;
	auto TrackSerializedProperties = [&PropertiesLeftToSerialize](const FArchiveSerializedPropertyChain* Chain, const FProperty* Property) { PropertiesLeftToSerialize.RemoveProperty(Chain, Property); };
	
	// Step 1: Serialise  properties that were different from CDO at time of snapshotting and that are still different from CDO
	FApplySnapshotToEditorArchive ApplySavedData(InObjectData, InSharedData, InOriginalObject, InSelectionMapForResolvingSubobjects, Selection, TrackSerializedProperties, Cache);
	InOriginalObject->Serialize(ApplySavedData);
	
	// Step 2: Serialise any remaining properties that were not covered: properties that were equal to the CDO value when the snapshot was taken but now are different from the CDO.
	TSet<const FTextProperty*> TextProperties = Internal::CopyPastePropertiesDifferentInCDO(PropertiesLeftToSerialize, InOriginalObject, InDeserializedVersion);

	// Step 3: Serialize FText properties that have changed in CDO since snapshot was taken. 
	Internal::FixUpTextPropertiesDifferentInCDO(MoveTemp(TextProperties), InSharedData, ClassIndex, Cache, InOriginalObject);
}

void UE::LevelSnapshots::Private::FApplySnapshotToEditorArchive::ApplyToEditorWorldObjectRecreatedWithArchetype(
	FObjectSnapshotData& InObjectData,
	FWorldSnapshotData& InSharedData,
	FSnapshotDataCache& Cache,
	UObject* InOriginalObject,
	const FPropertySelectionMap& InSelectionMapForResolvingSubobjects)
{
	UE_LOG(LogLevelSnapshots, Verbose, TEXT("Applying to recreated object %s (class %s)"), *InOriginalObject->GetPathName(), *InOriginalObject->GetClass()->GetPathName());
	const FApplySnapshotPropertiesScope NotifySnapshotListeners({ InOriginalObject, InSelectionMapForResolvingSubobjects, {}, true });
	
	// Apply all properties that we saved into the target actor.
	// We assume that InOriginalObject was already created with the snapshot CDO as template: we do not need Step 2 from ApplyToExistingWorldObject.
	auto Dummy = [](auto,auto){};
	FApplySnapshotToEditorArchive ApplySavedData(InObjectData, InSharedData, InOriginalObject, InSelectionMapForResolvingSubobjects, {}, Dummy, Cache);
	InOriginalObject->Serialize(ApplySavedData);
}

void UE::LevelSnapshots::Private::FApplySnapshotToEditorArchive::ApplyToEditorWorldObjectRecreatedWithoutArchetype(
	FObjectSnapshotData& InObjectData,
	FWorldSnapshotData& InSharedData,
	FSnapshotDataCache& Cache,
	UObject* InOriginalObject,
	const FPropertySelectionMap& InSelectionMapForResolvingSubobjects,
	FClassDataIndex ClassIndex)
{
	FPropertySelection PropertiesThatWereSerialized;
	auto TrackSerializedProperties = [&PropertiesThatWereSerialized](const FArchiveSerializedPropertyChain* Chain, const FProperty* Property) { PropertiesThatWereSerialized.AddProperty({ Chain, Property}); };

	// 1. Serialize archetype first to handle the case were the archetype has changed properties since the snapshot was taken
	const FSubobjectArchetypeFallbackInfo ClassFallbackInfo{ InOriginalObject->GetOuter(), InOriginalObject->GetFName(), InOriginalObject->GetFlags() };
	SerializeClassDefaultsIntoSubobject(InOriginalObject, InSharedData, ClassIndex, Cache, ClassFallbackInfo);
	
	// Step 2: Apply the data that was different from CDO at time of snapshotting
	FApplySnapshotToEditorArchive ApplySavedData(InObjectData, InSharedData, InOriginalObject, InSelectionMapForResolvingSubobjects, {}, TrackSerializedProperties, Cache);
	InOriginalObject->Serialize(ApplySavedData);
}

bool UE::LevelSnapshots::Private::FApplySnapshotToEditorArchive::ShouldSkipProperty(const FProperty* InProperty) const
{
	SCOPED_SNAPSHOT_CORE_TRACE(ShouldSkipProperty);
	
	bool bShouldSkipProperty = Super::ShouldSkipProperty(InProperty);
	
	if (!bShouldSkipProperty && !ShouldSerializeAllProperties())
	{
		const bool bIsAllowed = SelectionSet.GetValue()->ShouldSerializeProperty(GetSerializedPropertyChain(), InProperty);  
		bShouldSkipProperty = !bIsAllowed;
	}
	
	return bShouldSkipProperty;
}

void UE::LevelSnapshots::Private::FApplySnapshotToEditorArchive::PushSerializedProperty(FProperty* InProperty, const bool bIsEditorOnlyProperty)
{
	// Do before call to super because super appends InProperty to property chain
	OnPropertySerialized(GetSerializedPropertyChain(), InProperty);
	
	Super::PushSerializedProperty(InProperty, bIsEditorOnlyProperty);
}

UObject* UE::LevelSnapshots::Private::FApplySnapshotToEditorArchive::ResolveObjectDependency(int32 ObjectIndex) const
{
	FString LocalizationNamespace;
#if USE_STABLE_LOCALIZATION_KEYS
	LocalizationNamespace = GetLocalizationNamespace();
#endif
	return ResolveObjectDependencyForEditorWorld(GetSharedData(), Cache, ObjectIndex, LocalizationNamespace, SelectionMapForResolvingSubobjects);
}

UE::LevelSnapshots::Private::FApplySnapshotToEditorArchive::FApplySnapshotToEditorArchive(
	FObjectSnapshotData& InObjectData,
	FWorldSnapshotData& InSharedData,
	UObject* InOriginalObject,
	const FPropertySelectionMap& InSelectionMapForResolvingSubobjects,
	TOptional<const FPropertySelection*> InSelectionSet,
	FOnSerializeProperty InOnPropertySerialized,
	FSnapshotDataCache& Cache)
        : Super(InObjectData, InSharedData, true, InOriginalObject)
		, SelectionMapForResolvingSubobjects(InSelectionMapForResolvingSubobjects)
		, SelectionSet(InSelectionSet)
		, OnPropertySerialized(InOnPropertySerialized)
		, Cache(Cache)
{}

bool UE::LevelSnapshots::Private::FApplySnapshotToEditorArchive::ShouldSerializeAllProperties() const
{
	return !SelectionSet.IsSet();
}
