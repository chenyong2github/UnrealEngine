// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelStreamingPersistenceManager.h"
#include "LevelStreamingPersistenceModule.h"
#include "LevelStreamingPersistenceSettings.h"
#include "Serialization/ArchiveUObject.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Streaming/LevelStreamingDelegates.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "PropertyBag.h"
#include "PropertyPathHelpers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogLevelStreamingPersistenceManager, All, All);

/*
 * Achives used by ULevelStreamingPersistenceManager
 */

class FPersistentPropertiesArchive : public FObjectAndNameAsStringProxyArchive
{
public:
	FPersistentPropertiesArchive(FArchive& InArchive, const FLevelStreamingPersistentPropertyArray& InCustomPropertyList)
		: FObjectAndNameAsStringProxyArchive(InArchive, /*bLoadIfFindFails*/false)
	{
		check(InArchive.IsPersistent());
		check(InArchive.IsFilterEditorOnly());
		check(InArchive.ShouldSkipBulkData());
		SetIsLoading(InArchive.IsLoading());
		SetIsSaving(InArchive.IsSaving());
		SetIsPersistent(true);
		SetFilterEditorOnly(true);
		ArShouldSkipBulkData = true;
		ArUseCustomPropertyList = !InCustomPropertyList.IsEmpty();
		ArCustomPropertyList = ArUseCustomPropertyList ? &InCustomPropertyList[0] : nullptr;
	}

	virtual FArchive& operator<<(FLazyObjectPtr& Value) override { return FArchiveUObject::SerializeLazyObjectPtr(*this, Value); }
};

class FPersistentPropertiesWriter : public FMemoryWriter
{
public:
	FPersistentPropertiesWriter(TArray<uint8, TSizedDefaultAllocator<32>>& InBytes)
		: FMemoryWriter(InBytes, true)
	{
		SetFilterEditorOnly(true);
		ArShouldSkipBulkData = true;
		SetIsTextFormat(false);
		SetWantBinaryPropertySerialization(true);
	}
};

class FPersistentPropertiesReader : public FMemoryReader
{
public:
	FPersistentPropertiesReader(const TArray<uint8>& InBytes)
		: FMemoryReader(InBytes, true)
	{
		SetFilterEditorOnly(true);
		ArShouldSkipBulkData = true;
		SetIsTextFormat(false);
		SetWantBinaryPropertySerialization(true);
	}
};

/*
 * ULevelStreamingPersistenceManager implementation
 */

ULevelStreamingPersistenceManager::ULevelStreamingPersistenceManager(const FObjectInitializer& ObjectInitializer)
	: PersistenceModule(&ILevelStreamingPersistenceModule::Get())
{
}

bool ULevelStreamingPersistenceManager::DoesSupportWorldType(const EWorldType::Type InWorldType) const
{
	return InWorldType == EWorldType::PIE || InWorldType == EWorldType::Game;
}

bool ULevelStreamingPersistenceManager::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!UWorldSubsystem::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	UWorld* World = Cast<UWorld>(Outer);
	if (!World || !World->IsGameWorld() || (World->GetNetMode() == NM_Client))
	{
		return false;
	}

	UWorldPartition* WorldPartition = World->GetWorldPartition();
	// @todo_ow: Make UWorldPartition::IsStreamingEnabled not dependant of WorldPartition's World member
	if (WorldPartition && (!WorldPartition->bEnableStreaming || !World->GetWorldSettings()->SupportsWorldPartitionStreaming()))
	{
		return false;
	}

	return true;
}

void ULevelStreamingPersistenceManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FLevelStreamingDelegates::OnLevelBeginMakingInvisible.AddUObject(this, &ULevelStreamingPersistenceManager::OnLevelBeginMakingInvisible);
	FLevelStreamingDelegates::OnLevelBeginMakingVisible.AddUObject(this, &ULevelStreamingPersistenceManager::OnLevelBeginMakingVisible);
}

void ULevelStreamingPersistenceManager::PostInitialize()
{
	PersistentPropertiesInfo.Initialize();
}

void ULevelStreamingPersistenceManager::Deinitialize()
{
	FLevelStreamingDelegates::OnLevelBeginMakingInvisible.RemoveAll(this);
	FLevelStreamingDelegates::OnLevelBeginMakingVisible.RemoveAll(this);
}

bool ULevelStreamingPersistenceManager::TrySetPropertyValueFromString(const FString& InObjectPathName, const FName InPropertyName, const FString& InPropertyValue)
{
	const FString ObjectPathName = GetResolvedObjectPathName(InObjectPathName);
	FLevelStreamingPersistentObjectPropertyBag* PropertyBag = GetPropertyBag(ObjectPathName);
	if (PropertyBag && PropertyBag->SetPropertyValueFromString(InPropertyName, InPropertyValue))
	{
		// If object is loaded, copy property value to object
		auto ObjectPropertyPair = GetObjectPropertyPair(ObjectPathName, InPropertyName);
		CopyPropertyBagValueToObject(PropertyBag, ObjectPropertyPair.Key, ObjectPropertyPair.Value);
		return true;
	}
	return false;
}

bool ULevelStreamingPersistenceManager::GetPropertyValueAsString(const FString& InObjectPathName, const FName InPropertyName, FString& OutPropertyValue)
{
	const FString ObjectPathName = GetResolvedObjectPathName(InObjectPathName);
	if (FLevelStreamingPersistentObjectPropertyBag* PropertyBag = GetPropertyBag(ObjectPathName))
	{
		return PropertyBag->GetPropertyValueAsString(InPropertyName, OutPropertyValue);
	}
	return false;
}

void ULevelStreamingPersistenceManager::OnLevelBeginMakingInvisible(UWorld* World, const ULevelStreaming* InStreamingLevel, ULevel* InLoadedLevel)
{
	if (IsValid(InLoadedLevel) && (World == GetWorld()))
	{
		SaveLevelPersistentPropertyValues(InLoadedLevel);
	}
}

void ULevelStreamingPersistenceManager::OnLevelBeginMakingVisible(UWorld* World, const ULevelStreaming* InStreamingLevel, ULevel* InLoadedLevel)
{
	if (IsValid(InLoadedLevel) && (World == GetWorld()))
	{
		RestoreLevelPersistentPropertyValues(InLoadedLevel);
	}
}

bool ULevelStreamingPersistenceManager::SaveLevelPersistentPropertyValues(const ULevel* InLevel)
{
	if (!IsEnabled())
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(ULevelStreamingPersistenceManager::SaveLevelPersistentPropertyValues);

	const FString LevelPathName = InLevel->GetPathName();
	FLevelStreamingPersistentPropertyValues& LevelProperties = LevelsPropertyValues.FindOrAdd(LevelPathName);

	auto SavePrivateProperties = [this, &LevelProperties](const UObject* Object)
	{
		int32 SavedCount = 0;
		check(IsValid(Object));
		const UClass* ObjectClass = Object->GetClass();
		if (PersistentPropertiesInfo.HasProperties(FLevelStreamingPersistentPropertiesInfo::PropertyType_Private, ObjectClass))
		{
			// Don't handle new objects (not part of original snapshot)
			if (FLevelStreamingPersistentObjectPrivateProperties* ObjectPrivatePropertyValues = LevelProperties.ObjectsPrivatePropertyValues.Find(Object->GetPathName()))
			{
				ObjectPrivatePropertyValues->PayloadData.Reset();
				ObjectPrivatePropertyValues->PersistentProperties.Reset();
				if (DiffWithSnapshot(Object, ObjectPrivatePropertyValues->Snapshot, ObjectPrivatePropertyValues->PersistentProperties))
				{
					FLevelStreamingPersistentPropertyArray CustomPropertyList;
					ObjectPrivatePropertyValues->BuildSerializedPropertyList(CustomPropertyList);
					FPersistentPropertiesWriter WriterAr(ObjectPrivatePropertyValues->PayloadData);
					FPersistentPropertiesArchive Ar(WriterAr, CustomPropertyList);
					Object->SerializeScriptProperties(Ar);
				}
			}
		}
		return SavedCount;
	};

	auto SavePublicProperties = [this, &LevelProperties](const UObject* Object)
	{
		int32 SavedCount = 0;
		check(IsValid(Object));
		const UClass* ObjectClass = Object->GetClass();
		// Save public properties
		if (PersistentPropertiesInfo.HasProperties(FLevelStreamingPersistentPropertiesInfo::PropertyType_Public, ObjectClass))
		{
			TArray<FProperty*, TInlineAllocator<32>> SerializedObjectProperties;
			PersistentPropertiesInfo.ForEachProperty(FLevelStreamingPersistentPropertiesInfo::PropertyType_Public, ObjectClass, [this, Object, &SerializedObjectProperties](FProperty* ObjectProperty)
			{
				if (!PersistenceModule->ShouldPersistProperty(Object, ObjectProperty))
				{
					return;
				}
				SerializedObjectProperties.Add(ObjectProperty);
			});
			if (!SerializedObjectProperties.IsEmpty())
			{
				FLevelStreamingPersistentObjectPublicProperties& ObjectPublicPropertyValues = LevelProperties.ObjectsPublicPropertyValues.FindOrAdd(Object->GetPathName());
				if (ObjectPublicPropertyValues.PropertyBag.Initialize([this, ObjectClass]() { return PersistentPropertiesInfo.GetPropertyBagFromClass(FLevelStreamingPersistentPropertiesInfo::PropertyType_Public, ObjectClass); }))
				{
					ObjectPublicPropertyValues.PersistentProperties.Reset();
					for (FProperty* ObjectProperty : SerializedObjectProperties)
					{
						if (const FProperty* BagProperty = ObjectPublicPropertyValues.PropertyBag.CopyPropertyValueFromObject(Object, ObjectProperty))
						{
							ObjectPublicPropertyValues.PersistentProperties.Add(BagProperty);
							++SavedCount;
						}
					}
				}
			}
		}
		return SavedCount;
	};

	int32 SavedCount = 0;
	ForEachObjectWithOuter(InLevel, [this, &SavePrivateProperties, &SavePublicProperties, &SavedCount](UObject* Object)
	{	
		SavedCount += SavePrivateProperties(Object);
		SavedCount += SavePublicProperties(Object);
	}, true, RF_NoFlags, EInternalObjectFlags::Garbage);
	return SavedCount > 0;
}

bool ULevelStreamingPersistenceManager::RestoreLevelPersistentPropertyValues(ULevel* InLevel) const
{
	if (!IsEnabled())
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(ULevelStreamingPersistenceManager::RestoreLevelPersistentPropertyValues);

	auto SavePrivatePropertiesSnapshot = [this](UObject* Object, FLevelStreamingPersistentPropertyValues& LevelProperties)
	{
		int BuiltSnapshotCount = 0;
		const UClass* ObjectClass = Object->GetClass();

		if (PersistentPropertiesInfo.HasProperties(FLevelStreamingPersistentPropertiesInfo::PropertyType_Private, ObjectClass))
		{
			FLevelStreamingPersistentObjectPrivateProperties& ObjectPrivatePropertyValues = LevelProperties.ObjectsPrivatePropertyValues.FindOrAdd(Object->GetPathName());
			if (!ObjectPrivatePropertyValues.Snapshot.IsValid())
			{
				if (BuildSnapshot(Object, ObjectPrivatePropertyValues.Snapshot))
				{
					++BuiltSnapshotCount;
				}
			}
		}
		return BuiltSnapshotCount;
	};

	int32 RestoredCount = 0;
	int32 SavedCount = 0;

	const FString LevelPathName = InLevel->GetPathName();
	FLevelStreamingPersistentPropertyValues* LevelProperties = LevelsPropertyValues.Find(LevelPathName);
	{
		// The first time, create a snapshot of persistent properties
		if (!LevelProperties)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ULevelStreamingPersistenceManager::SavePrivatePropertiesSnapshot);

			FLevelStreamingPersistentPropertyValues& NewLevelProperties = LevelsPropertyValues.FindOrAdd(LevelPathName);
			ForEachObjectWithOuter(InLevel, [this, &NewLevelProperties, &SavedCount, &SavePrivatePropertiesSnapshot](UObject* Object)
			{
				SavedCount += SavePrivatePropertiesSnapshot(Object, NewLevelProperties);
			}, true, RF_NoFlags, EInternalObjectFlags::Garbage);
		}
		// Else, restore persistent properties
		else
		{
			for (const auto& [ObjectPath, ObjectPrivatePropertyValues] : LevelProperties->ObjectsPrivatePropertyValues)
			{
				if (UObject* Object = FindObject<UObject>(nullptr, *ObjectPath))
				{
					RestoredCount += RestorePrivateProperties(Object, ObjectPrivatePropertyValues);
				}
			}
			for (const auto& [ObjectPath, ObjectPublicPropertyValues] : LevelProperties->ObjectsPublicPropertyValues)
			{
				if (UObject* Object = FindObject<UObject>(nullptr, *ObjectPath))
				{
					RestoredCount += RestorePublicProperties(Object, ObjectPublicPropertyValues);
				}
			}
		}
	}
	
	return (RestoredCount > 0) || (SavedCount > 0);
}

int32 ULevelStreamingPersistenceManager::RestorePrivateProperties(UObject* Object, const FLevelStreamingPersistentObjectPrivateProperties& PersistentObjectProperties) const
{
	int32 RestoredCount = 0;
	check(IsValid(Object));

	// Restore private properties
	if (!PersistentObjectProperties.PayloadData.IsEmpty())
	{
		FLevelStreamingPersistentPropertyArray CustomPropertyList;
		PersistentObjectProperties.BuildSerializedPropertyList(CustomPropertyList);
		FPersistentPropertiesReader ReaderAr(PersistentObjectProperties.PayloadData);
		FPersistentPropertiesArchive Ar(ReaderAr, CustomPropertyList);
		Object->SerializeScriptProperties(Ar);
		PersistentObjectProperties.ForEachPersistentProperty([this, Object](const FProperty* ObjectProperty)
		{
			PersistenceModule->PostRestorePersistedProperty(Object, ObjectProperty);
		});
		++RestoredCount;
	}
	return RestoredCount;
};

int32 ULevelStreamingPersistenceManager::RestorePublicProperties(UObject* Object, const FLevelStreamingPersistentObjectPublicProperties& ObjectsPublicPropertyValues) const
{
	int RestoredCount = 0;
	check(IsValid(Object));
	const UClass* ObjectClass = Object->GetClass();
	// Restore public properties
	if (ObjectsPublicPropertyValues.PropertyBag.IsValid())
	{
		ObjectsPublicPropertyValues.ForEachPersistentProperty([this, Object, ObjectClass, &ObjectsPublicPropertyValues, &RestoredCount](const FProperty* BagProperty)
		{
			FProperty* ObjectProperty = ObjectClass->FindPropertyByName(BagProperty->GetFName());
			if (CopyPropertyBagValueToObject(&ObjectsPublicPropertyValues.PropertyBag, Object, ObjectProperty))
			{
				++RestoredCount;
			}
		});
	}
	return RestoredCount;
};

FLevelStreamingPersistentObjectPropertyBag* ULevelStreamingPersistenceManager::GetPropertyBag(const FString& InObjectPathName)
{
	FString LevelPathName;
	FString ObjectShortPathName;
	if (!SplitObjectPath(InObjectPathName, LevelPathName, ObjectShortPathName))
	{
		return nullptr;
	}
	FLevelStreamingPersistentPropertyValues* LevelProperties = LevelsPropertyValues.Find(LevelPathName);
	FLevelStreamingPersistentObjectPublicProperties* ObjectPublicPropertyValues = LevelProperties ? LevelProperties->ObjectsPublicPropertyValues.Find(InObjectPathName) : nullptr;
	return ObjectPublicPropertyValues ? &ObjectPublicPropertyValues->PropertyBag : nullptr;
}

const FLevelStreamingPersistentObjectPropertyBag* ULevelStreamingPersistenceManager::GetPropertyBag(const FString& InObjectPathName) const
{
	return const_cast<ULevelStreamingPersistenceManager*>(this)->GetPropertyBag(InObjectPathName);
}

bool ULevelStreamingPersistenceManager::CopyPropertyBagValueToObject(const FLevelStreamingPersistentObjectPropertyBag* InPropertyBag, UObject* InObject, FProperty* InObjectProperty) const
{
	if (InPropertyBag->CopyPropertyValueToObject(InObject, InObjectProperty))
	{
		PersistenceModule->PostRestorePersistedProperty(InObject, InObjectProperty);
		return true;
	}
	return false;
}

TPair<UObject*, FProperty*> ULevelStreamingPersistenceManager::GetObjectPropertyPair(const FString& InObjectPathName, const FName InPropertyName) const
{
	UObject* Object = FindObject<UObject>(nullptr, *InObjectPathName);
	FProperty* ObjectProperty = ::IsValid(Object) ? Object->GetClass()->FindPropertyByName(InPropertyName) : nullptr;
	return TPair<UObject*, FProperty*>(Object, ObjectProperty);
}

const FString ULevelStreamingPersistenceManager::GetResolvedObjectPathName(const FString& InObjectPathName) const
{
	if (UWorldPartition* WorldPartition = GetWorld()->GetWorldPartition())
	{
		const FSoftObjectPath Path(InObjectPathName);
		const FSoftObjectPath WorldPath(Path.GetAssetPath(), FString());
		const UWorld* World = Cast<UWorld>(WorldPath.ResolveObject());
		if (World && (World == GetWorld()))
		{
			FSoftObjectPath ResolvedPath;
			if (FWorldPartitionHelpers::ConvertEditorPathToRuntimePath(FSoftObjectPath(InObjectPathName), ResolvedPath))
			{
				return ResolvedPath.ToString();
			}
		}
	}
	return InObjectPathName;
}

bool ULevelStreamingPersistenceManager::SplitObjectPath(const FString& InObjectPathName, FString& OutLevelPathName, FString& OutShortObjectPathName) const
{
	FSoftObjectPath ObjectPath(InObjectPathName);
	if (ObjectPath.GetSubPathString().StartsWith(TEXT("PersistentLevel.")))
	{
		TStringBuilder<FName::StringBufferSize> Builder;
		Builder << ObjectPath.GetAssetPath() << SUBOBJECT_DELIMITER_CHAR << TEXT("PersistentLevel");
		OutLevelPathName = Builder.ToString();
		OutShortObjectPathName = ObjectPath.GetSubPathString().RightChop(16);
		return true;
	}
	return false;
}


bool ULevelStreamingPersistenceManager::BuildSnapshot(const UObject* InObject, FLevelStreamingPersistentObjectPropertyBag& OutSnapshot) const
{
	int SavedCount = 0;
	const UClass* ObjectClass = InObject->GetClass();
	if (PersistentPropertiesInfo.HasProperties(FLevelStreamingPersistentPropertiesInfo::PropertyType_Private, ObjectClass))
	{
		if (OutSnapshot.Initialize([this, ObjectClass]() { return PersistentPropertiesInfo.GetPropertyBagFromClass(FLevelStreamingPersistentPropertiesInfo::PropertyType_Private, ObjectClass); }))
		{
			PersistentPropertiesInfo.ForEachProperty(FLevelStreamingPersistentPropertiesInfo::PropertyType_Private, ObjectClass, [this, InObject, &SavedCount, &OutSnapshot](FProperty* ObjectProperty)
			{
				if (OutSnapshot.CopyPropertyValueFromObject(InObject, ObjectProperty))
				{
					++SavedCount;
				}
			});
		}
		check(OutSnapshot.IsValid());
	}
	return SavedCount > 0;
}

bool ULevelStreamingPersistenceManager::DiffWithSnapshot(const UObject* InObject, const FLevelStreamingPersistentObjectPropertyBag& InSnapshot, TArray<const FProperty*>& OutChangedProperties) const
{
	if (ensure(InSnapshot.IsValid()))
	{
		const UClass* ObjectClass = InObject->GetClass();

		// Find changed properties
		PersistentPropertiesInfo.ForEachProperty(FLevelStreamingPersistentPropertiesInfo::PropertyType_Private, ObjectClass, [this, InObject, &InSnapshot, &OutChangedProperties](FProperty* ObjectProperty)
		{
			if (!PersistenceModule->ShouldPersistProperty(InObject, ObjectProperty))
			{
				return;
			}
			bool bIsIdentical = true;
			if (InSnapshot.ComparePropertyValueWithObject(InObject, ObjectProperty, bIsIdentical) && !bIsIdentical)
			{
				OutChangedProperties.Add(ObjectProperty);
			}
		});

		return OutChangedProperties.Num() > 0;
	}

	return false;
}

void ULevelStreamingPersistenceManager::DumpContent() const
{
	UE_LOG(LogLevelStreamingPersistenceManager, Log, TEXT("World Persistence Content for %s"), *GetWorld()->GetName());

	TMap<const UClass*, UObject*> TemporaryObjects;
	auto GetTemporaryObjectForClass = [&TemporaryObjects](const UClass* InClass)
	{
		UObject*& Object = TemporaryObjects.FindOrAdd(InClass);
		if (!Object)
		{
			Object = NewObject<UObject>(GetTransientPackage(), InClass);
		}
		return Object;
	};

	for (auto& [LevelPathName, LevelPropertyValues] : LevelsPropertyValues)
	{
		if (LevelPropertyValues.ObjectsPrivatePropertyValues.Num())
		{
			UE_LOG(LogLevelStreamingPersistenceManager, Log, TEXT("[+] Level Private Properties of %s"), *LevelPathName);
			for (const auto& [ObjectPathName, ObjectPropertyValues] : LevelPropertyValues.ObjectsPrivatePropertyValues)
			{
				if (!ObjectPropertyValues.PayloadData.IsEmpty())
				{
					if (const UClass* Class = PersistentPropertiesInfo.GetClassFromPropertyBag(FLevelStreamingPersistentPropertiesInfo::PropertyType_Private, ObjectPropertyValues.Snapshot.GetPropertyBagStruct()))
					{
						if (UObject* TempObject = GetTemporaryObjectForClass(Class))
						{
							FString ObjectLevelPathName;
							FString ObjectShortPathName;
							const bool bUseShortName = SplitObjectPath(ObjectPathName, ObjectLevelPathName, ObjectShortPathName);

							UE_LOG(LogLevelStreamingPersistenceManager, Log, TEXT("  [+] Object Private Properties of %s"), bUseShortName ? *ObjectShortPathName : *ObjectPathName);

							if (RestorePrivateProperties(TempObject, ObjectPropertyValues))
							{
								for (const FProperty* Property : ObjectPropertyValues.PersistentProperties)
								{
									FString PropertyValue;
									if (PropertyPathHelpers::GetPropertyValueAsString(TempObject, Property->GetName(), PropertyValue))
									{
										UE_LOG(LogLevelStreamingPersistenceManager, Log, TEXT("   - Property[%s] = %s"), *Property->GetName(), *PropertyValue);
									}
								}
							}
						}
					}
				}
			}
		}

		if (LevelPropertyValues.ObjectsPublicPropertyValues.Num())
		{
			UE_LOG(LogLevelStreamingPersistenceManager, Log, TEXT("[+] Level Public Properties of %s"), *LevelPathName);
			for (auto& [ObjectPathName, ObjectPublicPropertyValues] : LevelPropertyValues.ObjectsPublicPropertyValues)
			{
				if (ObjectPublicPropertyValues.PropertyBag.IsValid())
				{
					FString ObjectLevelPathName;
					FString ObjectShortPathName;
					const bool bUseShortName = SplitObjectPath(ObjectPathName, ObjectLevelPathName, ObjectShortPathName);

					UE_LOG(LogLevelStreamingPersistenceManager, Log, TEXT("  [+] Object Public Properties of %s"), bUseShortName ? *ObjectShortPathName : *ObjectPathName);
					ObjectPublicPropertyValues.PropertyBag.DumpContent([](const FProperty* Property, const FString& PropertyValue)
					{
						UE_LOG(LogLevelStreamingPersistenceManager, Log, TEXT("   - Property[%s] = %s"), *Property->GetName(), *PropertyValue);
					}, &ObjectPublicPropertyValues.PersistentProperties);
				}
			}
		}
	}
}

/*
 * FLevelStreamingPersistentObjectPrivateProperties implementation
 */

void FLevelStreamingPersistentObjectPrivateProperties::ForEachPersistentProperty(TFunctionRef<void(const FProperty*)> Func) const
{
	for (const FProperty* Property : PersistentProperties)
	{
		Func(Property);
	}
}

void FLevelStreamingPersistentObjectPrivateProperties::BuildSerializedPropertyList(FLevelStreamingPersistentPropertyArray& OutCustomPropertyList) const
{
	ForEachPersistentProperty([&OutCustomPropertyList](const FProperty* Property)
	{
		OutCustomPropertyList.Emplace(const_cast<FProperty*>(Property));
	});

	// Link changed properties
	if (!OutCustomPropertyList.IsEmpty())
	{
		for (int i = 0; i < OutCustomPropertyList.Num() - 1; ++i)
		{
			OutCustomPropertyList[i].PropertyListNext = &OutCustomPropertyList[i + 1];
		}
	}
}

/*
 * FLevelStreamingPersistentObjectPublicProperties implementation
 */

void FLevelStreamingPersistentObjectPublicProperties::ForEachPersistentProperty(TFunctionRef<void(const FProperty*)> Func) const
{
	for (const FProperty* Property : PersistentProperties)
	{
		Func(Property);
	}
}

/*
 * FLevelStreamingPersistentPropertiesInfo implementation
 */

void FLevelStreamingPersistentPropertiesInfo::Initialize()
{
	auto FillClassesProperties = [this](const FString& InPropertyPath, bool bIsPublic)
	{
		TMap<TWeakObjectPtr<const UClass>, TSet<FProperty*>>& OutClassesProperties = ClassesProperties[bIsPublic ? PropertyType_Public : PropertyType_Private];
		if (FProperty* Property = TFieldPath<FProperty>(*InPropertyPath).Get())
		{
			if (const UClass* Class = Property->GetOwnerClass())
			{
				OutClassesProperties.FindOrAdd(Class).Add(Property);
			}
		}
	};

	auto CreateClassDefaultPropertyBag = [this](EPropertyType InAccessSpecifier)
	{
		TMap<TWeakObjectPtr<const UClass>, FInstancedPropertyBag>& ClassesDefaults = ObjectClassToPropertyBag[InAccessSpecifier];
		for (auto& [Class, ClassProperties] : ClassesProperties[InAccessSpecifier])
		{
			check(Class.IsValid());
			check(!ClassesDefaults.Contains(Class));

			TArray<FPropertyBagPropertyDesc> Descs;
			ForEachProperty(InAccessSpecifier, Class.Get(), [&Descs](FProperty* Property)
			{
				Descs.Emplace(Property->GetFName(), Property);
			});

			FInstancedPropertyBag& PropertyBag = ClassesDefaults.FindOrAdd(Class);
			PropertyBag.AddProperties(Descs);
			check(PropertyBag.GetPropertyBagStruct());
			PropertyBagToObjectClass[InAccessSpecifier].Add(PropertyBag.GetPropertyBagStruct(), Class);
		}
	};

	for (const FLevelStreamingPersistentProperty& PersistentProperty : GetDefault<ULevelStreamingPersistenceSettings>()->Properties)
	{
		FillClassesProperties(PersistentProperty.Path, PersistentProperty.bIsPublic);
	}
	CreateClassDefaultPropertyBag(PropertyType_Private);
	CreateClassDefaultPropertyBag(PropertyType_Public);
}

const UPropertyBag* FLevelStreamingPersistentPropertiesInfo::GetPropertyBagFromClass(EPropertyType InAccessSpecifier, const UClass* InClass) const
{
	if (ensure(InAccessSpecifier < PropertyType_Count))
	{
		const UClass* Class = InClass;
		while (Class)
		{
			if (const FInstancedPropertyBag* InstancedPropertyBag = ObjectClassToPropertyBag[InAccessSpecifier].Find(Class))
			{
				return InstancedPropertyBag->GetPropertyBagStruct();
			}
			Class = Class->GetSuperClass();
		}
	}
	return nullptr;
}

const UClass* FLevelStreamingPersistentPropertiesInfo::GetClassFromPropertyBag(EPropertyType InAccessSpecifier, const UPropertyBag* InPropertyBag) const
{
	if (ensure(InAccessSpecifier < PropertyType_Count))
	{
		if (const TWeakObjectPtr<const UClass>* FoundClass = PropertyBagToObjectClass[InAccessSpecifier].Find(InPropertyBag))
		{
			return FoundClass->Get();
		}
	}
	return nullptr;
}

bool FLevelStreamingPersistentPropertiesInfo::HasProperty(EPropertyType InAccessSpecifier, const FProperty* InProperty) const
{
	if (ensure(InAccessSpecifier < PropertyType_Count))
	{
		if (const UClass* Class = InProperty->GetOwnerClass())
		{
			if (const TSet<FProperty*>* ClassProperties = ClassesProperties[InAccessSpecifier].Find(Class))
			{
				return ClassProperties->Contains(InProperty);
			}
		}
	}
	return false;
}

void FLevelStreamingPersistentPropertiesInfo::ForEachProperty(EPropertyType InAccessSpecifier, const UClass* InClass, TFunctionRef<void(FProperty*)> Func) const
{
	if (ensure(InAccessSpecifier < PropertyType_Count))
	{
		const UClass* Class = InClass;
		while (Class)
		{
			if (const TSet<FProperty*>* ClassProperties = ClassesProperties[InAccessSpecifier].Find(Class))
			{
				for (FProperty* Property : *ClassProperties)
				{
					Func(Property);
				}
			}
			Class = Class->GetSuperClass();
		}
	}
}

bool FLevelStreamingPersistentPropertiesInfo::HasProperties(EPropertyType InAccessSpecifier, const UClass* InClass) const
{
	if (ensure(InAccessSpecifier < PropertyType_Count))
	{
		const UClass* Class = InClass;
		while (Class)
		{
			if (ClassesProperties[InAccessSpecifier].Contains(Class))
			{
				return true;
			}
			Class = Class->GetSuperClass();
		}
	}
	return false;
}

/*
 * Level Streaming Persistence Manager Console command helper
 */
namespace LSPMConsoleCommandHelper
{
	template<typename PropertyType>
	static bool GetValueFromString(const FString& InPropertyValue, PropertyType& OutResult) { return false; }

	template<typename PropertyType>
	static FString GetValueToString(const PropertyType& InPropertyValue) { return FString(TEXT("<unknown>")); }

	template<typename PropertyType>
	static bool TrySetPropertyValueFromString(ULevelStreamingPersistenceManager* InLevelStreamingPersistenceManager, const FString& InObjectPath, const FName InPropertyName, const FString& InPropertyValue)
	{
		PropertyType ValueFromString;
		if (GetValueFromString(InPropertyValue, ValueFromString))
		{
			if (InLevelStreamingPersistenceManager->TrySetPropertyValue(InObjectPath, InPropertyName, ValueFromString))
			{
				PropertyType Result;
				if (InLevelStreamingPersistenceManager->GetPropertyValue(InObjectPath, InPropertyName, Result))
				{
					FString ResultToString = GetValueToString(Result);
					UE_LOG(LogLevelStreamingPersistenceManager, Log, TEXT("SetPropertyValue succeded : Property[%s] = %s for object %s"), *InPropertyName.ToString(), *ResultToString, *InObjectPath);
					return true;
				}
			}
		}
		return false;
	}

	template<typename PropertyType>
	static bool GetPropertyValueAsString(ULevelStreamingPersistenceManager* InLevelStreamingPersistenceManager, const FString& InObjectPath, const FName InPropertyName, FString& OutPropertyValue)
	{
		PropertyType Result;
		if (InLevelStreamingPersistenceManager->GetPropertyValue(InObjectPath, InPropertyName, Result))
		{
			OutPropertyValue = GetValueToString(Result);
			UE_LOG(LogLevelStreamingPersistenceManager, Log, TEXT("GetPropertyValue succeded : Property[%s] = %s for object %s"), *InPropertyName.ToString(), *OutPropertyValue, *InObjectPath);
			return true;
		}
		return false;
	}

	template<> bool GetValueFromString(const FString& InPropertyValue, bool& OutResult) { OutResult = InPropertyValue.ToBool(); return true; }
	template<> bool GetValueFromString(const FString& InPropertyValue, int32& OutResult) { LexFromString(OutResult, *InPropertyValue); return true; }
	template<> bool GetValueFromString(const FString& InPropertyValue, int64& OutResult) { LexFromString(OutResult, *InPropertyValue); return true; }
	template<> bool GetValueFromString(const FString& InPropertyValue, float& OutResult) { LexFromString(OutResult, *InPropertyValue); return true; }
	template<> bool GetValueFromString(const FString& InPropertyValue, double& OutResult) { LexFromString(OutResult, *InPropertyValue); return true; }
	template<> bool GetValueFromString(const FString& InPropertyValue, FName& OutResult) { OutResult = FName(InPropertyValue); return true; }
	template<> bool GetValueFromString(const FString& InPropertyValue, FString& OutResult) { OutResult = InPropertyValue; return true; }
	template<> bool GetValueFromString(const FString& InPropertyValue, FText& OutResult) { OutResult.FromString(InPropertyValue); return true; }
	template<> bool GetValueFromString(const FString& InPropertyValue, FVector& OutResult) { return OutResult.InitFromString(InPropertyValue); }
	template<> bool GetValueFromString(const FString& InPropertyValue, FRotator& OutResult) { return OutResult.InitFromString(InPropertyValue); }
	template<> bool GetValueFromString(const FString& InPropertyValue, FTransform& OutResult) { return OutResult.InitFromString(InPropertyValue); }
	template<> bool GetValueFromString(const FString& InPropertyValue, FColor& OutResult) { return OutResult.InitFromString(InPropertyValue); }
	template<> bool GetValueFromString(const FString& InPropertyValue, FLinearColor& OutResult) { return OutResult.InitFromString(InPropertyValue); }

	template<> FString GetValueToString(const bool& InPropertyValue) { return LexToString(InPropertyValue); }
	template<> FString GetValueToString(const int32& InPropertyValue) { return LexToString(InPropertyValue); }
	template<> FString GetValueToString(const int64& InPropertyValue) { return LexToString(InPropertyValue); }
	template<> FString GetValueToString(const float& InPropertyValue) { return LexToString(InPropertyValue); }
	template<> FString GetValueToString(const double& InPropertyValue) { return LexToString(InPropertyValue); }
	template<> FString GetValueToString(const FName& InPropertyValue) { return InPropertyValue.ToString(); }
	template<> FString GetValueToString(const FString& InPropertyValue) { return InPropertyValue; }
	template<> FString GetValueToString(const FText& InPropertyValue) { return InPropertyValue.ToString(); }
	template<> FString GetValueToString(const FVector& InPropertyValue) { return InPropertyValue.ToString(); }
	template<> FString GetValueToString(const FRotator& InPropertyValue) { return InPropertyValue.ToString(); }
	template<> FString GetValueToString(const FTransform& InPropertyValue) { return InPropertyValue.ToString(); }
	template<> FString GetValueToString(const FColor& InPropertyValue) { return InPropertyValue.ToString(); }
	template<> FString GetValueToString(const FLinearColor& InPropertyValue) { return InPropertyValue.ToString(); }

} // namespace LSPMConsoleCommandHelper

bool ULevelStreamingPersistenceManager::bIsEnabled = true;
FAutoConsoleVariableRef ULevelStreamingPersistenceManager::EnableCommand(
	TEXT("s.LevelStreamingPersistence.Enabled"),
	ULevelStreamingPersistenceManager::bIsEnabled,
	TEXT("Turn on/off to enable/disable world persistent subsystem."),
	ECVF_Default);

#if !UE_BUILD_SHIPPING

FAutoConsoleCommand ULevelStreamingPersistenceManager::DumpContentCommand(
	TEXT("s.LevelStreamingPersistence.DumpContent"),
	TEXT("Dump the content of WorldPersistentSubsystems"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				if (ULevelStreamingPersistenceManager* LevelStreamingPersistenceManager = World->GetSubsystem<ULevelStreamingPersistenceManager>())
				{
					LevelStreamingPersistenceManager->DumpContent();
				}
			}
		}
	})
);

FAutoConsoleCommand ULevelStreamingPersistenceManager::SetPropertyValueCommand(
	TEXT("s.LevelStreamingPersistence.SetPropertyValue"),
	TEXT("Set the persistent property's value for a given object"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& InArgs)
	{
		if (InArgs.Num() < 3)
		{
			return;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				if (ULevelStreamingPersistenceManager* Manager = World->GetSubsystem<ULevelStreamingPersistenceManager>())
				{
					const FString& ObjectPath = InArgs[0];
					const FName PropertyName = FName(InArgs[1]);
					const FString& PropertyValue = InArgs[2];

					if (LSPMConsoleCommandHelper::TrySetPropertyValueFromString<bool>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::TrySetPropertyValueFromString<int32>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::TrySetPropertyValueFromString<int64>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::TrySetPropertyValueFromString<float>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::TrySetPropertyValueFromString<double>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::TrySetPropertyValueFromString<FName>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::TrySetPropertyValueFromString<FString>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::TrySetPropertyValueFromString<FText>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::TrySetPropertyValueFromString<FVector>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::TrySetPropertyValueFromString<FRotator>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::TrySetPropertyValueFromString<FTransform>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::TrySetPropertyValueFromString<FColor>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::TrySetPropertyValueFromString<FLinearColor>(Manager, ObjectPath, PropertyName, PropertyValue))
					{
						return;
					}
					else if (Manager->TrySetPropertyValueFromString(ObjectPath, PropertyName, PropertyValue))
					{
						FString Result;
						if (Manager->GetPropertyValueAsString(ObjectPath, PropertyName, Result))
						{
							UE_LOG(LogLevelStreamingPersistenceManager, Log, TEXT("GetPropertyValueAsString succeded : Property[%s] = %s for object %s"), *PropertyName.ToString(), *Result, *ObjectPath);
							return;
						}
					}
				}
			}
		}
	})
);

FAutoConsoleCommand ULevelStreamingPersistenceManager::GetPropertyValueCommand(
	TEXT("s.LevelStreamingPersistence.GetPropertyValue"),
	TEXT("Get the persistent property's value for a given object"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& InArgs)
	{
		if (InArgs.Num() < 2)
		{
			return;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				if (ULevelStreamingPersistenceManager* Manager = World->GetSubsystem<ULevelStreamingPersistenceManager>())
				{
					const FString& ObjectPath = InArgs[0];
					const FName PropertyName = FName(InArgs[1]);

					FString PropertyValue;
					if (LSPMConsoleCommandHelper::GetPropertyValueAsString<bool>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::GetPropertyValueAsString<int32>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::GetPropertyValueAsString<int64>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::GetPropertyValueAsString<float>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::GetPropertyValueAsString<double>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::GetPropertyValueAsString<FName>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::GetPropertyValueAsString<FString>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::GetPropertyValueAsString<FText>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::GetPropertyValueAsString<FVector>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::GetPropertyValueAsString<FRotator>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::GetPropertyValueAsString<FTransform>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::GetPropertyValueAsString<FColor>(Manager, ObjectPath, PropertyName, PropertyValue) ||
						LSPMConsoleCommandHelper::GetPropertyValueAsString<FLinearColor>(Manager, ObjectPath, PropertyName, PropertyValue))
					{
						return;
					}
					else
					{
						FString Result;
						if (Manager->GetPropertyValueAsString(ObjectPath, PropertyName, Result))
						{
							UE_LOG(LogLevelStreamingPersistenceManager, Log, TEXT("GetPropertyValueAsString succeded : Property[%s] = %s for object %s"), *PropertyName.ToString(), *Result, *ObjectPath);
							return;
						}
					}
				}
			}
		}
	})
);

#endif // !UE_BUILD_SHIPPING