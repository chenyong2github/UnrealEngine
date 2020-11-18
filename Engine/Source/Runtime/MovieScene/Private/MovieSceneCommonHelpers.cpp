// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneCommonHelpers.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraComponent.h"
#include "KeyParams.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneSequence.h"
#include "MovieSceneSpawnable.h"
#include "Sections/MovieSceneSubSection.h"
#include "Algo/Sort.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "MovieSceneTrack.h"

UMovieSceneSection* MovieSceneHelpers::FindSectionAtTime( TArrayView<UMovieSceneSection* const> Sections, FFrameNumber Time )
{
	for( int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex )
	{
		UMovieSceneSection* Section = Sections[SectionIndex];

		//@todo sequencer: There can be multiple sections overlapping in time. Returning instantly does not account for that.
		if( Section->IsTimeWithinSection( Time ) && Section->IsActive() )
		{
			return Section;
		}
	}

	return nullptr;
}

UMovieSceneSection* MovieSceneHelpers::FindNearestSectionAtTime( TArrayView<UMovieSceneSection* const> Sections, FFrameNumber Time )
{
	TArray<UMovieSceneSection*> OverlappingSections, NonOverlappingSections;
	for (UMovieSceneSection* Section : Sections)
	{
		if (Section->GetRange().Contains(Time))
		{
			OverlappingSections.Add(Section);
		}
		else
		{
			NonOverlappingSections.Add(Section);
		}
	}

	if (OverlappingSections.Num())
	{
		Algo::Sort(OverlappingSections, SortOverlappingSections);
		return OverlappingSections[0];
	}

	if (NonOverlappingSections.Num())
	{
		Algo::SortBy(NonOverlappingSections, [](const UMovieSceneSection* A) { return A->GetRange().GetUpperBound(); }, SortUpperBounds);

		const int32 PreviousIndex = Algo::UpperBoundBy(NonOverlappingSections, TRangeBound<FFrameNumber>(Time), [](const UMovieSceneSection* A){ return A->GetRange().GetUpperBound(); }, SortUpperBounds)-1;
		if (NonOverlappingSections.IsValidIndex(PreviousIndex))
		{
			return NonOverlappingSections[PreviousIndex];
		}
		else
		{
			Algo::SortBy(NonOverlappingSections, [](const UMovieSceneSection* A) { return A->GetRange().GetLowerBound(); }, SortLowerBounds);
			return NonOverlappingSections[0];
		}
	}

	return nullptr;
}

bool MovieSceneHelpers::SortOverlappingSections(const UMovieSceneSection* A, const UMovieSceneSection* B)
{
	return A->GetRowIndex() == B->GetRowIndex()
		? A->GetOverlapPriority() < B->GetOverlapPriority()
		: A->GetRowIndex() < B->GetRowIndex();
}

void MovieSceneHelpers::SortConsecutiveSections(TArray<UMovieSceneSection*>& Sections)
{
	Sections.Sort([](const UMovieSceneSection& A, const UMovieSceneSection& B)
		{
			TRangeBound<FFrameNumber> LowerBoundA = A.GetRange().GetLowerBound();
			return TRangeBound<FFrameNumber>::MinLower(LowerBoundA, B.GetRange().GetLowerBound()) == LowerBoundA;
		}
	);
}

void MovieSceneHelpers::FixupConsecutiveSections(TArray<UMovieSceneSection*>& Sections, UMovieSceneSection& Section, bool bDelete)
{
	// Find the previous section and extend it to take the place of the section being deleted
	int32 SectionIndex = INDEX_NONE;

	TRange<FFrameNumber> SectionRange = Section.GetRange();

	if (SectionRange.HasLowerBound() && SectionRange.HasUpperBound() && SectionRange.GetLowerBoundValue() >= SectionRange.GetUpperBoundValue())
	{
		return;
	}

	if (Sections.Find(&Section, SectionIndex))
	{
		int32 PrevSectionIndex = SectionIndex - 1;
		if( Sections.IsValidIndex( PrevSectionIndex ) )
		{
			// Extend the previous section
			if (bDelete)
			{
				TRangeBound<FFrameNumber> NewEndFrame = SectionRange.GetUpperBound();

				if (!Sections[PrevSectionIndex]->HasStartFrame() || NewEndFrame.GetValue() > Sections[PrevSectionIndex]->GetInclusiveStartFrame())
				{
					Sections[PrevSectionIndex]->SetEndFrame(NewEndFrame);
				}
			}
			else
			{
				TRangeBound<FFrameNumber> NewEndFrame = TRangeBound<FFrameNumber>::FlipInclusion(SectionRange.GetLowerBound());

				if (!Sections[PrevSectionIndex]->HasStartFrame() || NewEndFrame.GetValue() > Sections[PrevSectionIndex]->GetInclusiveStartFrame())
				{
					Sections[PrevSectionIndex]->SetEndFrame(NewEndFrame);
				}
			}
		}

		if( !bDelete )
		{
			int32 NextSectionIndex = SectionIndex + 1;
			if(Sections.IsValidIndex(NextSectionIndex))
			{
				// Shift the next CameraCut's start time so that it starts when the new CameraCut ends
				TRangeBound<FFrameNumber> NewStartFrame = TRangeBound<FFrameNumber>::FlipInclusion(SectionRange.GetUpperBound());

				if (!Sections[NextSectionIndex]->HasEndFrame() || NewStartFrame.GetValue() < Sections[NextSectionIndex]->GetExclusiveEndFrame())
				{
					Sections[NextSectionIndex]->SetStartFrame(NewStartFrame);
				}
			}
		}
	}

	SortConsecutiveSections(Sections);
}

void MovieSceneHelpers::FixupConsecutiveBlendingSections(TArray<UMovieSceneSection*>& Sections, UMovieSceneSection& Section, bool bDelete)
{
	int32 SectionIndex = INDEX_NONE;

	TRange<FFrameNumber> SectionRange = Section.GetRange();

	if (SectionRange.HasLowerBound() && SectionRange.HasUpperBound() && SectionRange.GetLowerBoundValue() >= SectionRange.GetUpperBoundValue())
	{
		return;
	}

	if (Sections.Find(&Section, SectionIndex))
	{
		// Find the previous section and extend it to take the place of the section being deleted
		int32 PrevSectionIndex = SectionIndex - 1;
		if (Sections.IsValidIndex(PrevSectionIndex))
		{
			UMovieSceneSection* PrevSection = Sections[PrevSectionIndex];
			if (PrevSection->GetRowIndex() == Section.GetRowIndex())
			{
				PrevSection->Modify();

				if (bDelete)
				{
					TRangeBound<FFrameNumber> NewEndFrame = SectionRange.GetUpperBound();
					
					if (!PrevSection->HasStartFrame() || NewEndFrame.GetValue() > PrevSection->GetInclusiveStartFrame())
					{
						// The current section was deleted... extend the previous section to fill the gap.
						PrevSection->SetEndFrame(NewEndFrame);
					}
				}
				else
				{
					// If we made a gap: adjust the previous section's end time so that it ends wherever the current section's ease-in ends.
					// If we created an overlap: calls to UMovieSceneTrack::UpdateEasing have already set the easing curves correctly based on overlaps.
					const FFrameNumber GapOrOverlap = SectionRange.GetLowerBoundValue() - PrevSection->GetRange().GetUpperBoundValue();
					if (GapOrOverlap > 0)
					{
						TRangeBound<FFrameNumber> NewEndFrame = TRangeBound<FFrameNumber>::Exclusive(SectionRange.GetLowerBoundValue() + Section.Easing.GetEaseInDuration());

						if (!PrevSection->HasStartFrame() || NewEndFrame.GetValue() > PrevSection->GetInclusiveStartFrame())
						{
							// It's a gap!
							PrevSection->SetEndFrame(NewEndFrame);
						}
					}
				}
			}
		}
		else
		{
			if (!bDelete)
			{
				// The given section is the first section. Let's clear its auto ease-in since there's no overlap anymore with a previous section.
				Section.Easing.AutoEaseInDuration = 0;
			}
		}

		// Find the next section and adjust its start time to match the moved/resized section's new end time.
		if (!bDelete)
		{
			int32 NextSectionIndex = SectionIndex + 1;
			if (Sections.IsValidIndex(NextSectionIndex))
			{
				UMovieSceneSection* NextSection = Sections[NextSectionIndex];
				if (NextSection->GetRowIndex() == Section.GetRowIndex())
				{
					NextSection->Modify();

					// If we made a gap: adjust the next section's start time so that it lines up with the current section's end.
					// If we created an overlap: adjust the next section's ease-in so it ends where the current section ends.
					const FFrameNumber GapOrOverlap = NextSection->GetRange().GetLowerBoundValue() - SectionRange.GetUpperBoundValue();
					if (GapOrOverlap > 0)
					{
						TRangeBound<FFrameNumber> NewStartFrame = TRangeBound<FFrameNumber>::Inclusive(SectionRange.GetUpperBoundValue() - NextSection->Easing.GetEaseInDuration());

						if (!NextSection->HasEndFrame() || NewStartFrame.GetValue() < NextSection->GetExclusiveEndFrame())
						{
							// It's a gap!
							NextSection->SetStartFrame(NewStartFrame);
						}
					}
				}
			}
			else
			{
				// The given section is the last section. Let's clear its auto ease-out since there's no overlap anymore with a next section.
				Section.Easing.AutoEaseOutDuration = 0;
			}
		}
	}

	SortConsecutiveSections(Sections);
}


void MovieSceneHelpers::GetDescendantMovieScenes(UMovieSceneSequence* InSequence, TArray<UMovieScene*> & InMovieScenes)
{
	UMovieScene* InMovieScene = InSequence->GetMovieScene();
	if (InMovieScene == nullptr || InMovieScenes.Contains(InMovieScene))
	{
		return;
	}

	InMovieScenes.Add(InMovieScene);

	for (auto Section : InMovieScene->GetAllSections())
	{
		UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section);
		if (SubSection != nullptr)
		{
			UMovieSceneSequence* SubSequence = SubSection->GetSequence();
			if (SubSequence != nullptr)
			{
				GetDescendantMovieScenes(SubSequence, InMovieScenes);
			}
		}
	}
}


USceneComponent* MovieSceneHelpers::SceneComponentFromRuntimeObject(UObject* Object)
{
	AActor* Actor = Cast<AActor>(Object);

	USceneComponent* SceneComponent = nullptr;
	if (Actor && Actor->GetRootComponent())
	{
		// If there is an actor, modify its root component
		SceneComponent = Actor->GetRootComponent();
	}
	else
	{
		// No actor was found.  Attempt to get the object as a component in the case that we are editing them directly.
		SceneComponent = Cast<USceneComponent>(Object);
	}
	return SceneComponent;
}

UCameraComponent* MovieSceneHelpers::CameraComponentFromActor(const AActor* InActor)
{
	TArray<UCameraComponent*> CameraComponents;
	InActor->GetComponents<UCameraComponent>(CameraComponents);

	// If there's a camera component that's active, return that one
	for (UCameraComponent* CameraComponent : CameraComponents)
	{
		if (CameraComponent->IsActive())
		{
			return CameraComponent;
		}
	}

	// Otherwise, return the first camera component
	for (UCameraComponent* CameraComponent : CameraComponents)
	{
		return CameraComponent;
	}

	return nullptr;
}

UCameraComponent* MovieSceneHelpers::CameraComponentFromRuntimeObject(UObject* RuntimeObject)
{
	if (RuntimeObject)
	{
		// find camera we want to control
		UCameraComponent* const CameraComponent = dynamic_cast<UCameraComponent*>(RuntimeObject);
		if (CameraComponent)
		{
			return CameraComponent;
		}

		// see if it's an actor that has a camera component
		AActor* const Actor = dynamic_cast<AActor*>(RuntimeObject);
		if (Actor)
		{
			return CameraComponentFromActor(Actor);
		}
	}

	return nullptr;
}

float MovieSceneHelpers::GetSoundDuration(USoundBase* Sound)
{
	return Sound ? Sound->GetDuration() : 0.0f;
}


float MovieSceneHelpers::CalculateWeightForBlending(UMovieSceneSection* SectionToKey, FFrameNumber Time)
{
	float Weight = 1.0f;
	UMovieSceneTrack* Track = SectionToKey->GetTypedOuter<UMovieSceneTrack>();
	FOptionalMovieSceneBlendType BlendType = SectionToKey->GetBlendType();
	if (Track && BlendType.IsValid() && (BlendType.Get() == EMovieSceneBlendType::Additive || BlendType.Get() == EMovieSceneBlendType::Absolute))
	{
		//if additive weight is just the inverse of any weight on it
		if (BlendType.Get() == EMovieSceneBlendType::Additive)
		{
			float TotalWeightValue = SectionToKey->GetTotalWeightValue(Time);
			Weight = !FMath::IsNearlyZero(TotalWeightValue) ? 1.0f / TotalWeightValue : 0.0f;
		}
		else
		{

			const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
			TArray<UMovieSceneSection*, TInlineAllocator<4>> OverlappingSections;
			for (UMovieSceneSection* Section : Sections)
			{
				if (Section->GetRange().Contains(Time))
				{
					OverlappingSections.Add(Section);
				}
			}
			//if absolute need to calculate weight based upon other sections weights (+ implicit absolute weights)
			int TotalNumOfAbsoluteSections = 1;
			for (UMovieSceneSection* Section : OverlappingSections)
			{
				FOptionalMovieSceneBlendType NewBlendType = Section->GetBlendType();

				if (Section != SectionToKey && NewBlendType.IsValid() && NewBlendType.Get() == EMovieSceneBlendType::Absolute)
				{
					++TotalNumOfAbsoluteSections;
				}
			}
			float TotalWeightValue = SectionToKey->GetTotalWeightValue(Time);
			Weight = !FMath::IsNearlyZero(TotalWeightValue) ? float(TotalNumOfAbsoluteSections) / TotalWeightValue : 0.0f;
		}
	}
	return Weight;
}

FString MovieSceneHelpers::MakeUniqueSpawnableName(UMovieScene* MovieScene, const FString& InName)
{
	FString NewName = InName;

	auto DuplName = [&](FMovieSceneSpawnable& InSpawnable)
	{
		return InSpawnable.GetName() == NewName;
	};

	int32 Index = 2;
	FString UniqueString;
	while (MovieScene->FindSpawnable(DuplName))
	{
		NewName.RemoveFromEnd(UniqueString);
		UniqueString = FString::Printf(TEXT(" (%d)"), Index++);
		NewName += UniqueString;
	}
	return NewName;
}

FTrackInstancePropertyBindings::FTrackInstancePropertyBindings( FName InPropertyName, const FString& InPropertyPath )
	: PropertyPath( InPropertyPath )
	, PropertyName( InPropertyName )
{
	static const FString Set(TEXT("Set"));
	const FString FunctionString = Set + PropertyName.ToString();

	FunctionName = FName(*FunctionString);
}

struct FPropertyAndIndex
{
	FPropertyAndIndex() : Property(nullptr), ArrayIndex(INDEX_NONE) {}

	FProperty* Property;
	int32 ArrayIndex;
};

FPropertyAndIndex FindPropertyAndArrayIndex(UStruct* InStruct, const FString& PropertyName)
{
	FPropertyAndIndex PropertyAndIndex;

	// Calculate the array index if possible
	int32 ArrayIndex = -1;
	if (PropertyName.Len() > 0 && PropertyName.GetCharArray()[PropertyName.Len() - 1] == ']')
	{
		int32 OpenIndex = 0;
		if (PropertyName.FindLastChar('[', OpenIndex))
		{
			FString TruncatedPropertyName(OpenIndex, *PropertyName);
			PropertyAndIndex.Property = FindFProperty<FProperty>(InStruct, *TruncatedPropertyName);

			const int32 NumberLength = PropertyName.Len() - OpenIndex - 2;
			if (NumberLength > 0 && NumberLength <= 10)
			{
				TCHAR NumberBuffer[11];
				FMemory::Memcpy(NumberBuffer, &PropertyName[OpenIndex + 1], sizeof(TCHAR) * NumberLength);
				LexFromString(PropertyAndIndex.ArrayIndex, NumberBuffer);
			}

			return PropertyAndIndex;
		}
	}

	PropertyAndIndex.Property = FindFProperty<FProperty>(InStruct, *PropertyName);
	return PropertyAndIndex;
}

FTrackInstancePropertyBindings::FPropertyAddress FTrackInstancePropertyBindings::FindPropertyRecursive( void* BasePointer, UStruct* InStruct, TArray<FString>& InPropertyNames, uint32 Index )
{
	FPropertyAndIndex PropertyAndIndex = FindPropertyAndArrayIndex(InStruct, *InPropertyNames[Index]);
	
	FTrackInstancePropertyBindings::FPropertyAddress NewAddress;

	if (PropertyAndIndex.ArrayIndex != INDEX_NONE)
	{
		if (PropertyAndIndex.Property->IsA(FArrayProperty::StaticClass()))
		{
			FArrayProperty* ArrayProp = CastFieldChecked<FArrayProperty>(PropertyAndIndex.Property);

			FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(BasePointer));
			if (ArrayHelper.IsValidIndex(PropertyAndIndex.ArrayIndex))
			{
				FStructProperty* InnerStructProp = CastField<FStructProperty>(ArrayProp->Inner);
				if (InnerStructProp && InPropertyNames.IsValidIndex(Index + 1))
				{
					return FindPropertyRecursive(ArrayHelper.GetRawPtr(PropertyAndIndex.ArrayIndex), InnerStructProp->Struct, InPropertyNames, Index + 1);
				}
				else
				{
					NewAddress.Property = ArrayProp->Inner;
					NewAddress.Address = ArrayHelper.GetRawPtr(PropertyAndIndex.ArrayIndex);
				}
			}
		}
		else
		{
			UE_LOG(LogMovieScene, Error, TEXT("Mismatch in property evaluation. %s is not of type: %s"), *PropertyAndIndex.Property->GetName(), *FArrayProperty::StaticClass()->GetName());
		}
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(PropertyAndIndex.Property))
	{
		NewAddress.Property = StructProp;
		NewAddress.Address = BasePointer;

		if( InPropertyNames.IsValidIndex(Index+1) )
		{
			void* StructContainer = StructProp->ContainerPtrToValuePtr<void>(BasePointer);
			return FindPropertyRecursive( StructContainer, StructProp->Struct, InPropertyNames, Index+1 );
		}
		else
		{
			check( StructProp->GetName() == InPropertyNames[Index] );
		}
	}
	else if(PropertyAndIndex.Property)
	{
		NewAddress.Property = PropertyAndIndex.Property;
		NewAddress.Address = BasePointer;
	}

	return NewAddress;

}


FTrackInstancePropertyBindings::FPropertyAddress FTrackInstancePropertyBindings::FindProperty( const UObject& InObject, const FString& InPropertyPath )
{
	TArray<FString> PropertyNames;

	InPropertyPath.ParseIntoArray(PropertyNames, TEXT("."), true);

	if(IsValid(&InObject) && PropertyNames.Num() > 0)
	{
		return FindPropertyRecursive( (void*)&InObject, InObject.GetClass(), PropertyNames, 0 );
	}
	else
	{
		return FTrackInstancePropertyBindings::FPropertyAddress();
	}
}

void FTrackInstancePropertyBindings::CallFunctionForEnum( UObject& InRuntimeObject, int64 PropertyValue )
{
	FPropertyAndFunction PropAndFunction = FindOrAdd(InRuntimeObject);
	if (UFunction* SetterFunction = PropAndFunction.SetterFunction.Get())
	{
		InvokeSetterFunction(&InRuntimeObject, SetterFunction, PropertyValue);
	}
	else if (FProperty* Property = PropAndFunction.PropertyAddress.GetProperty())
	{
		if (Property->IsA(FEnumProperty::StaticClass()))
		{
			if (FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(Property))
			{
				FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
				void* ValueAddr = EnumProperty->ContainerPtrToValuePtr<void>(PropAndFunction.PropertyAddress.Address);
				UnderlyingProperty->SetIntPropertyValue(ValueAddr, PropertyValue);
			}
		}
		else
		{
			UE_LOG(LogMovieScene, Error, TEXT("Mismatch in property evaluation. %s is not of type: %s"), *Property->GetName(), *FEnumProperty::StaticClass()->GetName());
		}
	}

	if (UFunction* NotifyFunction = PropAndFunction.NotifyFunction.Get())
	{
		InRuntimeObject.ProcessEvent(NotifyFunction, nullptr);
	}
}

void FTrackInstancePropertyBindings::CacheBinding(const UObject& Object)
{
	FPropertyAndFunction PropAndFunction;
	{
		PropAndFunction.PropertyAddress = FindProperty(Object, PropertyPath);

		UFunction* SetterFunction = Object.FindFunction(FunctionName);
		if (SetterFunction && SetterFunction->NumParms >= 1)
		{
			PropAndFunction.SetterFunction = SetterFunction;
		}
		
		UFunction* NotifyFunction = NotifyFunctionName != NAME_None ? Object.FindFunction(NotifyFunctionName) : nullptr;
		if (NotifyFunction && NotifyFunction->NumParms == 0 && NotifyFunction->ReturnValueOffset == MAX_uint16)
		{
			PropAndFunction.NotifyFunction = NotifyFunction;
		}
	}

	RuntimeObjectToFunctionMap.Add(FObjectKey(&Object), PropAndFunction);
}

FProperty* FTrackInstancePropertyBindings::GetProperty(const UObject& Object) const
{
	FPropertyAndFunction PropAndFunction = RuntimeObjectToFunctionMap.FindRef(&Object);
	if (FProperty* Property = PropAndFunction.PropertyAddress.GetProperty())
	{
		return Property;
	}

	return FindProperty(Object, PropertyPath).GetProperty();
}

int64 FTrackInstancePropertyBindings::GetCurrentValueForEnum(const UObject& Object)
{
	FPropertyAndFunction PropAndFunction = FindOrAdd(Object);

	if (FProperty* Property = PropAndFunction.PropertyAddress.GetProperty())
	{
		if (Property->IsA(FEnumProperty::StaticClass()))
		{
			if (FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(Property))
			{
				FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
				void* ValueAddr = EnumProperty->ContainerPtrToValuePtr<void>(PropAndFunction.PropertyAddress.Address);
				int64 Result = UnderlyingProperty->GetSignedIntPropertyValue(ValueAddr);
				return Result;
			}
		}
		else
		{
			UE_LOG(LogMovieScene, Error, TEXT("Mismatch in property evaluation. %s is not of type: %s"), *Property->GetName(), *FEnumProperty::StaticClass()->GetName());
		}
	}

	return 0;
}

template<> void FTrackInstancePropertyBindings::CallFunction<bool>(UObject& InRuntimeObject, TCallTraits<bool>::ParamType PropertyValue)
{
	FPropertyAndFunction PropAndFunction = FindOrAdd(InRuntimeObject);
	if (UFunction* SetterFunction = PropAndFunction.SetterFunction.Get())
	{
		InvokeSetterFunction(&InRuntimeObject, SetterFunction, PropertyValue);
	}
	else if (FProperty* Property = PropAndFunction.PropertyAddress.GetProperty())
	{
		if (Property->IsA(FBoolProperty::StaticClass()))
		{
			if (FBoolProperty* BoolProperty = CastFieldChecked<FBoolProperty>(Property))
			{
				uint8* ValuePtr = BoolProperty->ContainerPtrToValuePtr<uint8>(PropAndFunction.PropertyAddress.Address);
				BoolProperty->SetPropertyValue(ValuePtr, PropertyValue);
			}
		}
		else
		{
			UE_LOG(LogMovieScene, Error, TEXT("Mismatch in property evaluation. %s is not of type: %s"), *Property->GetName(), *FBoolProperty::StaticClass()->GetName());
		}
	}

	if (UFunction* NotifyFunction = PropAndFunction.NotifyFunction.Get())
	{
		InRuntimeObject.ProcessEvent(NotifyFunction, nullptr);
	}
}

template<> bool FTrackInstancePropertyBindings::ResolvePropertyValue<bool>(const FPropertyAddress& Address, bool& OutValue)
{
	if (FProperty* Property = Address.GetProperty())
	{
		if (Property->IsA(FBoolProperty::StaticClass()))
		{
			if (FBoolProperty* BoolProperty = CastFieldChecked<FBoolProperty>(Property))
			{
				const uint8* ValuePtr = BoolProperty->ContainerPtrToValuePtr<uint8>(Address.Address);
				OutValue = BoolProperty->GetPropertyValue(ValuePtr);
				return true;
			}
		}
		else
		{
			UE_LOG(LogMovieScene, Error, TEXT("Mismatch in property evaluation. %s is not of type: %s"), *Property->GetName(), *FBoolProperty::StaticClass()->GetName());
		}
	}

	return false;
}

template<> void FTrackInstancePropertyBindings::SetCurrentValue<bool>(UObject& Object, TCallTraits<bool>::ParamType InValue)
{
	FPropertyAndFunction PropAndFunction = FindOrAdd(Object);
	if (FProperty* Property = PropAndFunction.PropertyAddress.GetProperty())
	{
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			uint8* ValuePtr = BoolProperty->ContainerPtrToValuePtr<uint8>(PropAndFunction.PropertyAddress.Address);
			BoolProperty->SetPropertyValue(ValuePtr, InValue);
		}
	}

	if (UFunction* NotifyFunction = PropAndFunction.NotifyFunction.Get())
	{
		Object.ProcessEvent(NotifyFunction, nullptr);
	}
}


template<> void FTrackInstancePropertyBindings::CallFunction<UObject*>(UObject& InRuntimeObject, UObject* PropertyValue)
{
	FPropertyAndFunction PropAndFunction = FindOrAdd(InRuntimeObject);
	if (UFunction* SetterFunction = PropAndFunction.SetterFunction.Get())
	{
		InvokeSetterFunction(&InRuntimeObject, SetterFunction, PropertyValue);
	}
	else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(PropAndFunction.PropertyAddress.GetProperty()))
	{
		uint8* ValuePtr = ObjectProperty->ContainerPtrToValuePtr<uint8>(PropAndFunction.PropertyAddress.Address);
		ObjectProperty->SetObjectPropertyValue(ValuePtr, PropertyValue);
	}

	if (UFunction* NotifyFunction = PropAndFunction.NotifyFunction.Get())
	{
		InRuntimeObject.ProcessEvent(NotifyFunction, nullptr);
	}
}

template<> bool FTrackInstancePropertyBindings::ResolvePropertyValue<UObject*>(const FPropertyAddress& Address, UObject*& OutValue)
{
	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Address.GetProperty()))
	{
		const uint8* ValuePtr = ObjectProperty->ContainerPtrToValuePtr<uint8>(Address.Address);
		OutValue = ObjectProperty->GetObjectPropertyValue(ValuePtr);
		return true;
	}

	return false;
}

template<> void FTrackInstancePropertyBindings::SetCurrentValue<UObject*>(UObject& InRuntimeObject, UObject* InValue)
{
	FPropertyAndFunction PropAndFunction = FindOrAdd(InRuntimeObject);
	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(PropAndFunction.PropertyAddress.GetProperty()))
	{
		uint8* ValuePtr = ObjectProperty->ContainerPtrToValuePtr<uint8>(PropAndFunction.PropertyAddress.Address);
		ObjectProperty->SetObjectPropertyValue(ValuePtr, InValue);
	}

	if (UFunction* NotifyFunction = PropAndFunction.NotifyFunction.Get())
	{
		InRuntimeObject.ProcessEvent(NotifyFunction, nullptr);
	}
}
