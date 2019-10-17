// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Parameterization/DataprepParameterization.h"

#include "DataPrepAsset.h"
#include "DataprepParameterizationArchive.h"

#include "CoreGlobals.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Math/UnrealMathUtility.h"
#include "Parameterization/DataprepParameterizationUtils.h"
#include "Serialization/ObjectReader.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Object.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace DataprepParameterization
{
	void* GetAddressOf(const UArrayProperty& Property, void* BaseAddress, int32 ContainerIndex)
	{
		void* AddressOfArray = Property.ContainerPtrToValuePtr<void*>( BaseAddress, 0 );
		if ( ContainerIndex == INDEX_NONE )
		{
			// The index none is used when we want to get the container itself
			return AddressOfArray;
		}

		FScriptArrayHelper ArrayHelper( &Property, AddressOfArray );
		if ( ArrayHelper.IsValidIndex(ContainerIndex) )
		{
			return ArrayHelper.GetRawPtr( ContainerIndex );
		}

		return nullptr;
	}

	void* GetAddressOf(const USetProperty& Property, void* BaseAddress, int32 ContainerIndex)
	{
		void* AddressOfSet = Property.ContainerPtrToValuePtr<void*>( BaseAddress, 0 ); 
		if ( ContainerIndex == INDEX_NONE )
		{
			// The index none is used when we want to get the container itself
			return AddressOfSet;
		}

		FScriptSetHelper SetHelper(&Property, Property.ContainerPtrToValuePtr<void*>( BaseAddress, 0 ) );
		int32 RealIndex = SetHelper.FindInternalIndex( ContainerIndex );
		if ( SetHelper.IsValidIndex( RealIndex ) )
		{
			return SetHelper.GetElementPtr( RealIndex );
		}

		return nullptr;
	}

	void* GetAddressOf(const UMapProperty& Property, void* BaseAddress, int32 ContainerIndex)
	{
		void* AddressOfMap = Property.ContainerPtrToValuePtr<void*>( BaseAddress, 0 );
		if ( ContainerIndex == INDEX_NONE )
		{
			// The index none is used when we want to get the container itself
			return AddressOfMap;
		}

		FScriptMapHelper MapHelper(&Property, Property.ContainerPtrToValuePtr<void*>( BaseAddress, 0 ) );
		int32 RealIndex = MapHelper.FindInternalIndex( ContainerIndex );
		if ( MapHelper.IsValidIndex( RealIndex ) )
		{
			return MapHelper.GetPairPtr( RealIndex );
		}

		return nullptr;
	}

	void* GetAddressOf(const UProperty& Property, void* BaseAddress, int32 ContainerIndex)
	{
		UClass* Class = Property.GetClass();
		if ( Class == UArrayProperty::StaticClass() )
		{
			return GetAddressOf( static_cast<const UArrayProperty&>( Property ), BaseAddress, ContainerIndex );
		}
		else if ( Class == USetProperty::StaticClass() )
		{
			return GetAddressOf( static_cast<const USetProperty&>( Property ), BaseAddress, ContainerIndex );
		}
		else if ( Class == UMapProperty::StaticClass() )
		{
			return GetAddressOf( static_cast<const UMapProperty&>( Property ), BaseAddress, ContainerIndex );
		}

		return Property.ContainerPtrToValuePtr<void*>(BaseAddress, FMath::Max( ContainerIndex, 0 ) );
	}

	/**
	 * Get the outer on which we should look when searching for a child property
	 * @return Return a nullptr if the current property is not supported
	 */
	UObject* GetOuterForPropertyFinding(UProperty* Property)
	{
		if ( !Property )
		{
			return nullptr;
		}

		UClass* PropertyClass = Property->GetClass();
		while ( PropertyClass )
		{
			if ( PropertyClass == UStructProperty::StaticClass() )
			{
				UStructProperty* StructProperty = static_cast<UStructProperty*>(Property);
				UScriptStruct* ScriptStruct = StructProperty->Struct;
				/**
				 * We don't want to support struct that exist for the reinstancing
				 * That heuristic might not be good for hot reloaded c++ struct
+				 */
				if (ScriptStruct
					&& ScriptStruct->GetName().StartsWith( TEXT("STRUCT_REINST_") ) )
				{
					return nullptr;
				}
				return ScriptStruct;
			}
			else if (PropertyClass == UObjectPropertyBase::StaticClass())
			{
				UObjectPropertyBase* ObjectProperty = static_cast<UObjectPropertyBase*>(Property);
				UClass* Class = ObjectProperty->PropertyClass;
				// We reject properties that points to a class where there is newer version that exists
				if ( Class
					&& bool(Class->ClassFlags & (CLASS_NewerVersionExists | CLASS_Transient) ) )
				{
					return nullptr;
				}
				return Class;
			}

			PropertyClass = PropertyClass->GetSuperClass();
		}

		return Property;
	};


	/**
	 * Walk a binding using only the cached properties on the object
	 * We use a outer and class/struct heuristic to validate that properties are still valid
	 */
	int32 GetDeepestLevelOfValidCache(const FDataprepParameterizationBinding& Binding, void*& OutPropertyValueAddress)
	{
		UObject* CurrentOuter = nullptr;

		if ( UObject* ObjectBinded = Binding.ObjectBinded )
		{ 
			// We use the current outer has a heuristic to validate that the property is still valid
			CurrentOuter = ObjectBinded->GetClass();
			void* CurrentPropertyValueAddresss = ObjectBinded;
			void* LastValidValueAddress = ObjectBinded;

			const TArray<FDataprepPropertyLink>& PropertyChain = Binding.PropertyChain;

			for ( int32 Level = 0; Level < PropertyChain.Num();  Level++ )
			{
				bool bAbortCacheValidation = true;

				UProperty* CurentProperty = PropertyChain[Level].CachedProperty.Get();
				if ( CurentProperty )
				{
					CurrentPropertyValueAddresss = GetAddressOf( *CurentProperty, CurrentPropertyValueAddresss, PropertyChain[Level].ContainerIndex );

					// We look if the outer of the property is the right one as a heuristic for the validity of the current property
					bool bPropertySeamsValid = false;
					if ( UObject* CurrentPropertyOuter = CurentProperty->GetOuter() )
					{
						if ( CurrentPropertyOuter == CurrentOuter )
						{
							bPropertySeamsValid = true;
						}
						else if ( UStruct* CurrentOuterAsStruct = Cast<UStruct>( CurrentOuter ) )
						{
							// Walk the struct hierarchy in case we have a sub struct of the expected struct
							UObject* PropertyOuter = CurentProperty->GetOuter();
							CurrentOuterAsStruct = CurrentOuterAsStruct->GetSuperStruct();
							while ( !bPropertySeamsValid && CurrentOuterAsStruct )
							{
								bPropertySeamsValid = CurrentOuterAsStruct == PropertyOuter;
								CurrentOuterAsStruct = CurrentOuterAsStruct->GetSuperStruct();
							}
						}
					}

					// The get outer does also the validation for the non supported properties
					CurrentOuter = GetOuterForPropertyFinding( CurentProperty );

					bAbortCacheValidation = !( CurrentPropertyValueAddresss && bPropertySeamsValid && CurrentOuter );
				}
				else
				{
					CurrentPropertyValueAddresss = nullptr;
				}

				if ( bAbortCacheValidation )
				{
					Level--;
					OutPropertyValueAddress = LastValidValueAddress;
					return Level;
				}

				LastValidValueAddress = CurrentPropertyValueAddresss;
			}

			// The cached properties were all valid
			OutPropertyValueAddress = LastValidValueAddress;
			return  PropertyChain.Num() - 1;

		}

		OutPropertyValueAddress = nullptr;
		return INDEX_NONE;
	}


	/**
	 * Try to get the property from a binding
	 * @param OutPropertyValueAddress The address of where we can find the value of the property
	 * @return Return nullptr if the binding is invalid
	 */
	UProperty* GetPropertyFromBinding(FDataprepParameterizationBinding& Binding, void*& OutPropertyValueAddress)
	{
		if ( !Binding.ObjectBinded || !Binding. ValueType )
		{
			return nullptr;
		}

		// Get the last valid level from the cache
		int32 LevelIndex = GetDeepestLevelOfValidCache( Binding, OutPropertyValueAddress );
		

		UObject* CurrentOuter = Binding.ObjectBinded->GetClass();
		if ( LevelIndex != INDEX_NONE )
		{
			CurrentOuter = GetOuterForPropertyFinding( Binding.PropertyChain[LevelIndex].CachedProperty.Get() );
		}

		// We start updating the binding from the first invalid level this is always the one after 
		LevelIndex++;

		// Todo (what happen if the bottom property changed its type)

		// Find missing or new properties and update the cache of the property link
		UProperty* PropetyAtCurrentLevel = nullptr;
		TArray<FDataprepPropertyLink>& PropertyChain = Binding.PropertyChain;
		while ( LevelIndex < PropertyChain.Num() && CurrentOuter )
		{
			FDataprepPropertyLink& PropertyLink = Binding.PropertyChain[LevelIndex];
			PropetyAtCurrentLevel = FindObjectFast<UProperty>(CurrentOuter, PropertyLink.PropertyName);
			if (!PropetyAtCurrentLevel)
			{
				if ( UStruct* OuterAsStruct = Cast<UStruct>( CurrentOuter ) )
				{
					OuterAsStruct = OuterAsStruct->GetSuperStruct();
					while ( !PropetyAtCurrentLevel && OuterAsStruct )
					{
						PropetyAtCurrentLevel = FindObjectFast<UProperty>(OuterAsStruct, PropertyLink.PropertyName);
						OuterAsStruct = OuterAsStruct->GetSuperStruct();
					}
				}
			}

			PropertyLink.CachedProperty = PropetyAtCurrentLevel;
			CurrentOuter = GetOuterForPropertyFinding( PropetyAtCurrentLevel);
			LevelIndex++;
		}

		// If the current outer is null it's because the last property we checked is not supported
		if ( LevelIndex == PropertyChain.Num() && CurrentOuter )
		{
			PropetyAtCurrentLevel = PropertyChain.Last().CachedProperty.Get();
			if ( PropetyAtCurrentLevel->GetClass() == Binding.ValueType )
			{
				return PropetyAtCurrentLevel;
			}
		}

		return nullptr;
	}

	/**
	 * Try to get the property from a binding
	 * Return nullptr if the binding is invalid
	 */
	UProperty* GetPropertyFromBinding(FDataprepParameterizationBinding& Binding)
	{
		void* DummyPointer;
		return GetPropertyFromBinding( Binding, DummyPointer );
	}

	void CopyCompleteValue(UProperty& DestinationProperty, void* DestinationAddress, UProperty& SourceProperty, void* SourceAddress)
	{
		UClass* ProperyClass = DestinationProperty.GetClass();
		// We only support copying value of properties when they are from the same class
		check( ProperyClass == SourceProperty.GetClass() );

		// Bool properties are special because each property can have their own mask and offset from there base address (probably to support bitfields)
		if ( ProperyClass == UBoolProperty::StaticClass())
		{
			const bool bSourceValue = static_cast<UBoolProperty&>( SourceProperty ).GetPropertyValue( SourceAddress );
			static_cast<UBoolProperty&>( DestinationProperty ).SetPropertyValue( DestinationAddress, bSourceValue );
		}
		else
		{
			DestinationProperty.CopyCompleteValue( DestinationAddress, SourceAddress );
		}
	}
};


bool FDataprepParameterizationBinding::operator==(const FDataprepParameterizationBinding& Other) const
{
	// Not a accurate but ok for now
	return ObjectBinded == Other.ObjectBinded && GetTypeHash(PropertyChain) == GetTypeHash(Other.PropertyChain) && ValueType == Other.ValueType;
}

uint32 GetTypeHash(const FDataprepParameterizationBinding& Binding)
{
	uint32 Hash = GetTypeHash( Binding.ObjectBinded );
	Hash = HashCombine( Hash, GetTypeHash( Binding.PropertyChain ) );
	return HashCombine( Hash, GetTypeHash( Binding.ValueType ) );
}

uint32 GetTypeHash(const TArray<FDataprepPropertyLink>& PropertyLinks)
{
	uint32 Hash = GetTypeHash( PropertyLinks.Num() );
	for ( const FDataprepPropertyLink& PropertyLink : PropertyLinks )
	{
		Hash = HashCombine( Hash, GetTypeHash( PropertyLink ) );
	}

	return Hash;
}

UDataprepParameterization::UDataprepParameterization()
{
	OnObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddUObject( this, &UDataprepParameterization::OnObjectModified );
}

UDataprepParameterization::~UDataprepParameterization()
{
	FCoreUObjectDelegates::OnObjectModified.Remove( OnObjectModifiedHandle );
}

void UDataprepParameterization::PostInitProperties()
{
	Super::PostInitProperties();

	if ( !HasAnyFlags( RF_ClassDefaultObject | RF_NeedLoad ) )
	{
		GenerateClass();
	}
}

void UDataprepParameterization::PostLoad()
{
	if ( !HasAnyFlags( RF_ClassDefaultObject | RF_NeedLoad ) )
	{
		SetFlags( RF_Public );
		LoadParameterization();
	}
	Super::PostLoad();
}

void UDataprepParameterization::Serialize(FArchive& Ar)
{
	if ( Ar.IsSaving() && !HasAnyFlags( RF_ClassDefaultObject ) )
	{
		// Todo track when the object is changed to avoid rewriting to array each time
		ParameterizationStorage.Empty(CustomContainerClass->GetMinAlignment());
		FDataprepParameterizationWriter Writer(DefaultParameterisation, ParameterizationStorage);
	}
	
	Super::Serialize( Ar );
}

void UDataprepParameterization::PostEditUndo()
{
	// This implementation work on the assumption that all the objects in the transaction were serialized before the calls to post edit undo
	PrepareCustomClassForNewClassGeneration();
	UClass* OldClass = CustomContainerClass;
	CustomContainerClass = nullptr;

	LoadParameterization();

	DoReinstancing( OldClass, false );

	OnTellInstancesToReloadTheirSerializedData.Broadcast();

	if ( UDataprepAsset* DataprepAsset = Cast<UDataprepAsset>( GetOuter() ) )
	{
		DataprepAsset->OnParameterizationWasModified.Broadcast();
	}
}

void UDataprepParameterization::OnObjectModified(UObject* Object)
{
	if ( Object == DefaultParameterisation )
	{
		Modify();
	}
}

UObject* UDataprepParameterization::GetDefaultObject()
{
	return DefaultParameterisation;
}

bool UDataprepParameterization::BindObjectProperty(UObject* InObject, const TArray<FDataprepPropertyLink>& InPropertyChain, FName ParameterName)
{
	if ( InObject && InPropertyChain.Num() > 0 && !ParameterName.IsNone() )
	{
		FDataprepParameterizationBinding Binding( InObject, InPropertyChain );
		void* AddressOfTheValueFromBinding;

		// We expect the chain to have a valid chain of cached property before inserting the binding
		if ( DataprepParameterization::GetDeepestLevelOfValidCache( Binding, AddressOfTheValueFromBinding) == InPropertyChain.Num() - 1 )
		{
			if ( UProperty** PropertyPtr = NameToParameterizationProperty.Find( ParameterName ) )
			{
				UProperty* Property = *PropertyPtr;


				// Ensure that the properties type match
				if ( Property->GetClass() == Binding.ValueType && !BindingsFromPipeline.Contains( Binding ) )
				{
					Modify();

					// Valid bind the add binding to the name
					BindingsFromPipeline.Add( Binding, ParameterName );
					NameUsage.FindOrAdd( ParameterName ).Add( Binding );
				}
			}
			else
			{
				Modify();
				OnCustomClassAboutToBeUpdated.Broadcast();

				BindingsFromPipeline.Add( MoveTemp(Binding) , ParameterName );
				NameUsage.FindOrAdd( ParameterName ).Add( FDataprepParameterizationBinding(InObject, InPropertyChain) );
				UProperty* SourceProperty = InPropertyChain.Last().CachedProperty.Get();

				// The validation we did with GetDeepestLevelOfValidCache ensure us that the property ptr is valid
				UProperty* NewProperty = AddPropertyToClass( ParameterName, *SourceProperty );
				UpdateClass();

				// Update the value of the new parameter to match the source
				UObject* Parameterization = CustomContainerClass->GetDefaultObject();
				UProperty* ParameterizationProperty = FindObjectFast<UProperty>( CustomContainerClass, ParameterName );
				check( ParameterizationProperty );
				void* ParameterizationValueAddress = DataprepParameterization::GetAddressOf( *ParameterizationProperty, Parameterization, INDEX_NONE );
				DataprepParameterization::CopyCompleteValue( *ParameterizationProperty, ParameterizationValueAddress, *SourceProperty, AddressOfTheValueFromBinding );
			}
		}
	}

	return false;
}

bool UDataprepParameterization::IsObjectPropertyBinded(UObject* Object, const TArray<FDataprepPropertyLink>& PropertyChain) const
{
	FDataprepParameterizationBinding Binding( Object, PropertyChain );
	return BindingsFromPipeline.Contains( Binding );
}

void UDataprepParameterization::RemoveBindedObjectProperty(UObject* Object, const TArray<FDataprepPropertyLink>& PropertyChain)
{
	FDataprepParameterizationBinding Binding( Object, PropertyChain );
	uint32 Hash = GetTypeHash( Binding );
	FName* ParameterNamePtr = BindingsFromPipeline.FindByHash( Hash, Binding );
	if ( ParameterNamePtr )
	{
		Modify();
		OnCustomClassAboutToBeUpdated.Broadcast();
		BindingsFromPipeline.RemoveByHash( Hash, Binding );
		FName ParameterName = *ParameterNamePtr; 
		if ( TSet<FDataprepParameterizationBinding>* BindingAssociatedToName = NameUsage.Find( ParameterName ) )
		{
			BindingAssociatedToName->RemoveByHash( Hash, Binding );
			if ( BindingAssociatedToName->Num() == 0 )
			{
				NameToParameterizationProperty.Remove( ParameterName );
				UpdateClass();
			}
		}
	}
}

void UDataprepParameterization::GenerateClass()
{
	if ( !CustomContainerClass )
	{
		CustomContainerClass = NewObject<UClass>(GetOutermost(), FName(TEXT("Parameterization")), RF_Transient );
		CustomContainerClass->SetSuperStruct(UObject::StaticClass());

		// Make the properties appear in a alphabetically order (for that we must add the properties to the class in the reverse order)
		NameToParameterizationProperty.KeySort([](const FName& First, const FName& Second)
		{
			return !First.LexicalLess(Second);
		});

		for ( TPair<FName, UProperty*>& Pair : NameToParameterizationProperty )
		{
			UProperty* NewProperty = DuplicateObject<UProperty>(Pair.Value, CustomContainerClass, Pair.Key);
			NewProperty->SetFlags(RF_Transient);
			NewProperty->PropertyFlags = CPF_Edit;

			// Need to manually call Link to fix-up some data (such as the C++ property flags) that are only set during Link
			{
				FArchive Ar;
				NewProperty->LinkWithoutChangingOffset(Ar);
			}

			CustomContainerClass->AddCppProperty(NewProperty);

			Pair.Value = NewProperty;
		}

		CustomContainerClass->Bind();
		CustomContainerClass->StaticLink(true);
		CustomContainerClass->AssembleReferenceTokenStream(true);

		DefaultParameterisation = CustomContainerClass->GetDefaultObject(true);
	}
}

void UDataprepParameterization::UpdateClass()
{
	// Move away the old class
	if ( CustomContainerClass )
	{
		const FString OldClassName = MakeUniqueObjectName( CustomContainerClass->GetOuter(), CustomContainerClass->GetClass(), *FString::Printf(TEXT("%s_REINST"), *CustomContainerClass->GetName()) ).ToString();
		CustomContainerClass->ClassFlags |= CLASS_NewerVersionExists;
		CustomContainerClass->SetFlags( RF_NewerVersionExists );
		CustomContainerClass->ClearFlags( RF_Public | RF_Standalone );
		CustomContainerClass->Rename( *OldClassName, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty );
	}

	UClass* OldClass = CustomContainerClass;

	CustomContainerClass = nullptr;

	// Generate the new class
	GenerateClass();

	DoReinstancing( OldClass );
}

void UDataprepParameterization::LoadParameterization()
{
	if ( !CustomContainerClass )
	{ 
		CustomContainerClass = NewObject<UClass>( GetOutermost(), FName(TEXT("Parameterization")), RF_Transient );
		CustomContainerClass->SetSuperStruct( UObject::StaticClass() );

		TSet<FDataprepParameterizationBinding> BindingToRemove;
		NameUsage.Empty( NameUsage.Num() );
		NameToParameterizationProperty.Empty( NameToParameterizationProperty.Num() );

		for ( TPair<FDataprepParameterizationBinding, FName>& Binding : BindingsFromPipeline )
		{
			const FName BindingName = Binding.Value;
			NameUsage.FindOrAdd( BindingName ).Add( Binding.Key );

			UProperty* PropertyFromChain = DataprepParameterization::GetPropertyFromBinding( Binding.Key );
			UProperty** PropertyFromParameterizationClass = NameToParameterizationProperty.Find( BindingName );

			if ( PropertyFromChain && !PropertyFromParameterizationClass )
			{
				UProperty* NewProperty = AddPropertyToClass( BindingName, *PropertyFromChain );
			}
			else if ( !PropertyFromChain || PropertyFromChain->GetClass() != (*PropertyFromParameterizationClass)->GetClass() )
			{
				BindingToRemove.Add(Binding.Key);
			}
		}

		// Remove the invalid bindings
		for ( const FDataprepParameterizationBinding& InvalidBinding : BindingToRemove )
		{
			BindingsFromPipeline.Remove( InvalidBinding );
		}

		// Make the properties appear in a alphabetically order (for that we must add the properties to the class in the reverse order)
		NameToParameterizationProperty.KeySort( [](const FName& First, const FName& Second)
			{
				return !First.LexicalLess( Second );
			});

		for ( const TPair<FName, UProperty*>& Pair : NameToParameterizationProperty )
		{
			CustomContainerClass->AddCppProperty( Pair.Value );
		}

		CustomContainerClass->Bind();
		CustomContainerClass->StaticLink(true);
		CustomContainerClass->AssembleReferenceTokenStream(true);

		DefaultParameterisation = CustomContainerClass->GetDefaultObject(true);
		FDataprepParameterizationReader Reader( DefaultParameterisation, ParameterizationStorage );
	}
}

void UDataprepParameterization::PrepareCustomClassForNewClassGeneration()
{
	if ( CustomContainerClass )
	{
		const FString OldClassName = MakeUniqueObjectName( CustomContainerClass->GetOuter(), CustomContainerClass->GetClass(), *FString::Printf(TEXT("%s_REINST"), *CustomContainerClass->GetName()) ).ToString();
		CustomContainerClass->ClassFlags |= CLASS_NewerVersionExists;
		CustomContainerClass->SetFlags( RF_NewerVersionExists );
		CustomContainerClass->ClearFlags( RF_Public | RF_Standalone );
		CustomContainerClass->Rename( *OldClassName, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty);
	}
}

void UDataprepParameterization::DoReinstancing(UClass* OldClass, bool bMigrateData)
{
	if ( OldClass && CustomContainerClass )
	{
		// For the CDO
		UObject* OldCDO = OldClass->GetDefaultObject();
		UObject* NewCDO = CustomContainerClass->GetDefaultObject();

		if ( bMigrateData )
		{
			GEngine->CopyPropertiesForUnrelatedObjects( OldClass->GetDefaultObject(), CustomContainerClass->GetDefaultObject() );
		}

		// For the instances
		TArray<UObject*> Objects;
		constexpr bool bIncludeDerivedClasses = false;
		GetObjectsOfClass( OldClass, Objects, bIncludeDerivedClasses );
		TMap<UObject*, UObject*> OldToNew;
		OldToNew.Reserve( Objects.Num() + 1 );
		for (UObject* OldObject : Objects)
		{
			if ( OldObject && OldObject->IsValidLowLevel() )
			{
				FName ObjectName = OldObject->GetFName();
				UObject* Outer = OldObject->GetOuter();
				OldObject->Rename( nullptr, GetTransientPackage(), REN_DoNotDirty | REN_DontCreateRedirectors );

				ObjectName = MakeUniqueObjectName( Outer, CustomContainerClass, ObjectName );
				UObject* Object = NewObject<UObject>( Outer, CustomContainerClass, ObjectName, OldObject->GetFlags() );
				
				if ( bMigrateData )
				{
					GEngine->CopyPropertiesForUnrelatedObjects( OldObject, Object );
				}

				OldToNew.Add( OldObject, Object );
			}
		}

		OldToNew.Add( OldCDO, NewCDO );

		/**
		 * Notify the tools 
		 * If we did the data migration the tools were already notify of the change by the copy properties for unrelated objects by GEngine::CopyPropertiesForUnrelatedObjects
		 */
		if ( !bMigrateData && GEngine)
		{
			GEngine->NotifyToolsOfObjectReplacement(OldToNew);
		}

		OnCustomClassWasUpdated.Broadcast( OldToNew );

		DefaultParameterisation = NewCDO;
	}
}

UProperty* UDataprepParameterization::AddPropertyToClass(FName ParameterisationPropertyName, UProperty& Property)
{
	if ( !NameToParameterizationProperty.Find( ParameterisationPropertyName ) )
	{
		UProperty* NewProperty = DuplicateObject<UProperty>( &Property, CustomContainerClass, ParameterisationPropertyName );
		NewProperty->SetFlags( RF_Transient );
		NewProperty->PropertyFlags = CPF_Edit | CPF_NonTransactional;

		// Need to manually call Link to fix-up some data (such as the C++ property flags) that are only set during Link
		{
			FArchive Ar;
			NewProperty->LinkWithoutChangingOffset( Ar );
		}

		NameToParameterizationProperty.Add( ParameterisationPropertyName, NewProperty );

		return NewProperty;
	}

	return nullptr;
}


UDataprepParameterizationInstance::UDataprepParameterizationInstance()
{
	OnObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddUObject(this, &UDataprepParameterizationInstance::OnObjectModified );
}

UDataprepParameterizationInstance::~UDataprepParameterizationInstance()
{
	FCoreUObjectDelegates::OnObjectModified.Remove( OnObjectModifiedHandle );
}

void UDataprepParameterizationInstance::PostLoad()
{
	if ( !HasAnyFlags( RF_ClassDefaultObject | RF_NeedLoad ) )
	{
		SetFlags( RF_Public );
		LoadParameterization();
		SetupCallbacksFromSourceParameterisation();
	}
	Super::PostLoad();
}

void UDataprepParameterizationInstance::Serialize(FArchive& Ar)
{
	if ( Ar.IsSaving() && !HasAnyFlags( RF_ClassDefaultObject ) )
	{
		check( SourceParameterization );
		// Todo track when the object is changed to avoid rewriting to array each time
		ParameterizationInstanceStorage.Empty( ParameterizationInstanceStorage.Num() );
		FDataprepParameterizationWriter Writer( ParameterizationInstance, ParameterizationInstanceStorage );
	}

	Super::Serialize( Ar );
}

void UDataprepParameterizationInstance::PostEditUndo()
{
	LoadParameterization();
}

void UDataprepParameterizationInstance::OnObjectModified(UObject* Object)
{
	if ( Object == ParameterizationInstance )
	{
		Modify();
	}
}

void UDataprepParameterizationInstance::ApplyParameterization(const TMap<UObject*, UObject*>& SourceToCopy)
{
	check( SourceParameterization );
	for ( TPair<FDataprepParameterizationBinding, FName>& BindingPair : SourceParameterization->BindingsFromPipeline )
	{
		FDataprepParameterizationBinding& Binding = BindingPair.Key;
		if ( UObject* Object = SourceToCopy.FindRef( Binding.ObjectBinded ) )
		{
			TGuardValue<UObject*> GuardObjectBinded( Binding.ObjectBinded, Object );

			void* DestinationAddress = nullptr;
			if ( UProperty* DestinationProperty = DataprepParameterization::GetPropertyFromBinding( Binding, DestinationAddress ) )
			{
				UProperty* ParameterizationProperty = FindObjectFast<UProperty>( SourceParameterization->CustomContainerClass, BindingPair.Value );
				void* ParameterizationAddress =  DataprepParameterization::GetAddressOf( *ParameterizationProperty, ParameterizationInstance, INDEX_NONE );
				DataprepParameterization::CopyCompleteValue( *DestinationProperty, DestinationAddress, *ParameterizationProperty, ParameterizationAddress );
				check( Object );
			}
		}
	}
}

void UDataprepParameterizationInstance::CustomClassAboutToBeUpdated()
{
	// The instance is about to be modified
	Modify();
}

void UDataprepParameterizationInstance::CustomClassWasUpdated(const TMap<UObject*, UObject*>& OldToNew)
{
	if ( UObject* NewInstance = OldToNew.FindRef( ParameterizationInstance ) )
	{
		ParameterizationInstance = NewInstance;
	}
}

void UDataprepParameterizationInstance::LoadParameterization()
{
	check( SourceParameterization );

	if ( !SourceParameterization->CustomContainerClass )
	{
		SourceParameterization->LoadParameterization();
	}

	if ( !ParameterizationInstance )
	{
		ParameterizationInstance = NewObject<UObject>( this, SourceParameterization->CustomContainerClass, FName(TEXT("Parameterization")), RF_Transient );
	}

	FDataprepParameterizationReader Reader( ParameterizationInstance, ParameterizationInstanceStorage );
}

void UDataprepParameterizationInstance::SetParameterizationSource(UDataprepParameterization& Parameterization)
{
	UndoSetupForCallbacksFromParameterization();

	SourceParameterization = &Parameterization;
	SetupCallbacksFromSourceParameterisation();

	// Reload the parameterization (this act as a sort of data migration process)
	LoadParameterization();
}

void UDataprepParameterizationInstance::SetupCallbacksFromSourceParameterisation()
{
	check( SourceParameterization );

	SourceParameterization->OnCustomClassAboutToBeUpdated.AddUObject( this, &UDataprepParameterizationInstance::CustomClassAboutToBeUpdated );
	SourceParameterization->OnCustomClassWasUpdated.AddUObject( this, &UDataprepParameterizationInstance::CustomClassWasUpdated );
	SourceParameterization->OnTellInstancesToReloadTheirSerializedData.AddUObject( this, &UDataprepParameterizationInstance::LoadParameterization );
}

void UDataprepParameterizationInstance::UndoSetupForCallbacksFromParameterization()
{
	if (SourceParameterization)
	{
		SourceParameterization->OnCustomClassAboutToBeUpdated.RemoveAll( this );
		SourceParameterization->OnCustomClassWasUpdated.RemoveAll( this );
		SourceParameterization->OnTellInstancesToReloadTheirSerializedData.RemoveAll( this );
	}
}

