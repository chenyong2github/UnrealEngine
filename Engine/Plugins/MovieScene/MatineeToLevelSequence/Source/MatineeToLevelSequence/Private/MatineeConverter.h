// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MatineeToLevelSequenceModule.h"
#include "MatineeToLevelSequenceLog.h"
#include "MatineeUtils.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"
#include "Components/LightComponentBase.h"
#include "Tracks/MovieSceneMaterialTrack.h"
#include "MatineeImportTools.h"
#include "Factories/Factory.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Actor.h"

class UMovieSceneSequence;
class UMovieSceneTrack;

namespace UE
{
namespace MovieScene
{
	/** Find the factory that can create instances of the given class */
	static UFactory* FindFactoryForClass(UClass* InClass)
	{
		for (TObjectIterator<UClass> It ; It ; ++It)
		{
			UClass* CurrentClass = *It;
			if (CurrentClass->IsChildOf(UFactory::StaticClass()) && !(CurrentClass->HasAnyClassFlags(CLASS_Abstract)))
			{
				UFactory* Factory = Cast<UFactory>(CurrentClass->GetDefaultObject());
				if (Factory->CanCreateNew() && Factory->ImportPriority >= 0 && Factory->SupportedClass == InClass)
				{
					return Factory;
				}
			}
		}
		return nullptr;
	}
}
}

class FMatineeConverter
{
public:
 	FDelegateHandle RegisterTrackConverterForMatineeClass(TSubclassOf<UInterpTrack> InterpTrackClass, IMatineeToLevelSequenceModule::FOnConvertMatineeTrack OnConvertMatineeTrack);
	void UnregisterTrackConverterForMatineeClass(FDelegateHandle RemoveDelegate);

	/** Convert an interp group */
	void ConvertInterpGroup(UInterpGroup* Group, AActor* GroupActor, UMovieSceneSequence* NewSequence, UMovieScene* NewMovieScene, int32& NumWarnings) const;
	void ConvertInterpGroup(UInterpGroup* Group, FGuid ObjectBindingGuid, AActor* GroupActor, UMovieSceneSequence* NewSequence, UMovieScene* NewMovieScene, int32& NumWarnings) const;
	

private:

	/** Find or add a folder for the given actor **/
	static void FindOrAddFolder(UMovieScene* MovieScene, TWeakObjectPtr<AActor> Actor, FGuid Guid);

	/** Find or add a folder for the given actor **/
	static UMovieSceneFolder* FindOrAddFolder(UMovieScene* MovieScene, FName FolderName);

	/** Add master track to a folder **/
	static void AddMasterTrackToFolder(UMovieScene* MovieScene, UMovieSceneTrack* MovieSceneTrack, FName FolderName);

	/** Add property to possessable node **/
	template <typename T>
	static T* AddPropertyTrack(FName InPropertyName, AActor* InActor, const FGuid& PossessableGuid, UMovieSceneSequence* NewSequence, UMovieScene* NewMovieScene, int32& NumWarnings, TMap<UObject*, FGuid>& BoundObjectsToGuids);

private:

	FGuid FindComponentGuid(AActor* GroupActor, UMovieSceneSequence* NewSequence, UMovieScene* NewMovieScene, FGuid PossessableGuid) const;

	template<typename T>
	void CopyMaterialsToComponents(int32 NumMaterials, FGuid ComponentGuid, UMovieScene* NewMovieScene, T* MatineeMaterialParamTrack) const;

private:
	TMap<TSubclassOf<UInterpTrack>, IMatineeToLevelSequenceModule::FOnConvertMatineeTrack > ExtendedInterpConverters;
};

template <typename T>
T* FMatineeConverter::AddPropertyTrack(FName InPropertyName, AActor* InActor, const FGuid& PossessableGuid, UMovieSceneSequence* NewSequence, UMovieScene* NewMovieScene, int32& NumWarnings, TMap<UObject*, FGuid>& BoundObjectsToGuids)
{
	T* PropertyTrack = nullptr;

	// Find the property that matinee uses
	void* PropContainer = NULL;
	FProperty* Property = NULL;
	UObject* PropObject = FMatineeUtils::FindObjectAndPropOffset(PropContainer, Property, InActor, InPropertyName );

	FGuid ObjectGuid = PossessableGuid;
	if (PropObject && Property)
	{
		// If the property object that owns this property isn't already bound, add a binding to the property object
		if (BoundObjectsToGuids.Contains(PropObject))
		{
			ObjectGuid = BoundObjectsToGuids[PropObject];
		}
		else
		{
			ObjectGuid = NewMovieScene->AddPossessable(PropObject->GetName(), PropObject->GetClass());
			NewSequence->BindPossessableObject(ObjectGuid, *PropObject, InActor);

			BoundObjectsToGuids.Add(PropObject, ObjectGuid);
		}

		FMovieScenePossessable* ChildPossessable = NewMovieScene->FindPossessable(ObjectGuid);

		if (ChildPossessable)
		{
			ChildPossessable->SetParent(PossessableGuid);
		}

		// cbb: String manipulations to get the property path in the right form for sequencer
		FString PropertyName = Property->GetFName().ToString();

		// Special case for Light components which have some deprecated names
		if (PropObject->GetClass()->IsChildOf(ULightComponentBase::StaticClass()))
		{
			TMap<FName, FName> PropertyNameRemappings;
			PropertyNameRemappings.Add(TEXT("Brightness"), TEXT("Intensity"));
			PropertyNameRemappings.Add(TEXT("Radius"), TEXT("AttenuationRadius"));

			FName* RemappedName = PropertyNameRemappings.Find(*PropertyName);
			if (RemappedName != nullptr)
			{
				PropertyName = RemappedName->ToString();
			}
		}

		TArray<FFieldVariant> PropertyArray;
		FFieldVariant Outer = Property->GetOwnerVariant();
		while (Outer.IsA(FProperty::StaticClass()) || Outer.IsA(UScriptStruct::StaticClass()))
		{
			PropertyArray.Insert(Outer, 0);
			Outer = Outer.GetOwnerVariant();
		}

		FString PropertyPath;
		for (auto PropertyIt : PropertyArray)
		{
			if (PropertyPath.Len())
			{
				PropertyPath = PropertyPath + TEXT(".");
			}
			PropertyPath = PropertyPath + PropertyIt.GetName();
		}
		if (PropertyPath.Len())
		{
			PropertyPath = PropertyPath + TEXT(".");
		}
		PropertyPath = PropertyPath + PropertyName;

		PropertyTrack = NewMovieScene->AddTrack<T>(ObjectGuid);	
		PropertyTrack->SetPropertyNameAndPath(*PropertyName, PropertyPath);
	}
	else
	{
		UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Can't find property '%s' for '%s'."), *InPropertyName.ToString(), *InActor->GetActorLabel());
		++NumWarnings;
	}

	return PropertyTrack;
}

template<typename T>
void FMatineeConverter::CopyMaterialsToComponents(int32 NumMaterials, FGuid ComponentGuid, UMovieScene* NewMovieScene, T* MatineeMaterialParamTrack) const
{
	// One matinee material track can change the same parameter for multiple materials, but sequencer binds them to individual tracks.
	for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
	{
		UMovieSceneComponentMaterialTrack* MaterialTrack = nullptr;

		TArray<UMovieSceneComponentMaterialTrack *> BoundTracks;

		// Find all tracks bound to the component we added.
		for (const FMovieSceneBinding& Binding : NewMovieScene->GetBindings())
		{
			if (Binding.GetObjectGuid() == ComponentGuid)
			{
				for (auto Track : Binding.GetTracks())
				{
					MaterialTrack = Cast<UMovieSceneComponentMaterialTrack>(Track);
					if (MaterialTrack)
					{
						BoundTracks.Add(MaterialTrack);
					}
				}
				break;
			}
		}

		// The material may have already been added to the component, so look first to see if there is a track with the current material index.
		UMovieSceneComponentMaterialTrack** FoundTrack = BoundTracks.FindByPredicate([MaterialIndex](UMovieSceneComponentMaterialTrack* Track) -> bool { return Track && Track->GetMaterialIndex() == MaterialIndex; });
		if (FoundTrack)
		{
			MaterialTrack = *FoundTrack;
		}

		if (MaterialTrack)
		{
			FMatineeImportTools::CopyInterpMaterialParamTrack(MatineeMaterialParamTrack, MaterialTrack);
		}
		else
		{
			MaterialTrack = NewMovieScene->AddTrack<UMovieSceneComponentMaterialTrack>(ComponentGuid);
			if (MaterialTrack)
			{
				MaterialTrack->SetMaterialIndex(MaterialIndex);
				FMatineeImportTools::CopyInterpMaterialParamTrack(MatineeMaterialParamTrack, MaterialTrack);
			}
		}
	}
}

