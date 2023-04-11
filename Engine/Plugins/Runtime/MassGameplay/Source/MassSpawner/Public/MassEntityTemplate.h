// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessingTypes.h"
#include "MassEntityManager.h"
#include "MassCommonTypes.h"
#include "MassTranslator.h"
#include "Templates/SharedPointer.h"
#include "MassEntityTemplate.generated.h"


class UMassEntityTraitBase;
struct FMassEntityView;
struct FMassEntityTemplateIDFactory;
struct FMassEntityTemplate;


//ID of the template an entity is using
USTRUCT()
struct MASSSPAWNER_API FMassEntityTemplateID
{
	GENERATED_BODY()

	FMassEntityTemplateID()
		: bIsSet(false)
	{}

private:
	friend FMassEntityTemplateIDFactory;
	// use FMassEntityTemplateIDFactory to access this constructor flavor
	explicit FMassEntityTemplateID(uint32 InHash)
		: Hash(InHash), bIsSet(true)
	{}

public:
	uint32 GetHash() const 
	{
		checkSlow(bIsSet);
		return Hash; 
	}
	
	void Invalidate(uint32 InHash)
	{
		// the exact value we set here doesn't really matter, but just to keep the possible states consistent we set it 
		// to the default value;
		Hash = 0;
		bIsSet = false;
	}

	bool operator==(const FMassEntityTemplateID& Other) const
	{
		return (Hash == Other.Hash) && IsValid() == Other.IsValid();
	}

	friend uint32 GetTypeHash(const FMassEntityTemplateID& TemplateID)
	{
		return HashCombine(TemplateID.Hash, uint32(TemplateID.bIsSet));
	}

	bool IsValid() const { return bIsSet; }

	FString ToString() const;

protected:
	UPROPERTY()
	uint32 Hash = 0;

	UPROPERTY()
	uint8 bIsSet : 1;
};


/** 
 * Serves as data used to define and build finalized FMassEntityTemplate instances. Describes composition and initial
 * values of fragments for entities created with this data, and lets users modify and extend the data. Once finalized as 
 * FMassEntityTemplate the data will become immutable. 
 */
USTRUCT()
struct MASSSPAWNER_API FMassEntityTemplateData
{
	GENERATED_BODY()

	typedef TFunction<void(UObject& /*Owner*/, FMassEntityView& /*EntityView*/, const EMassTranslationDirection /*CurrentDirection*/)> FObjectFragmentInitializerFunction;

	FMassEntityTemplateData() = default;
	explicit FMassEntityTemplateData(const FMassEntityTemplate& InFinalizedTemplate);

	bool IsEmpty() const { return Composition.IsEmpty(); }

	TConstArrayView<FObjectFragmentInitializerFunction> GetObjectFragmentInitializers() const { return ObjectInitializers; }
	const FString& GetTemplateName() const { return TemplateName; }
	const FMassArchetypeCompositionDescriptor& GetCompositionDescriptor() const { return Composition; }
	const FMassArchetypeSharedFragmentValues& GetSharedFragmentValues() const { return SharedFragmentValues; }
	TConstArrayView<FInstancedStruct> GetInitialFragmentValues() const { return InitialFragmentValues; }

	TArray<FMassEntityTemplateData::FObjectFragmentInitializerFunction>& GetMutableObjectFragmentInitializers() { return ObjectInitializers; }

	void SetTemplateName(const FString& Name) { TemplateName = Name; }
	
	template<typename T>
	void AddFragment()
	{
		static_assert(TIsDerivedFrom<T, FMassFragment>::IsDerived, "Given struct doesn't represent a valid fragment type. Make sure to inherit from FMassFragment or one of its child-types.");
		Composition.Fragments.Add<T>();
	}

	void AddFragment(const UScriptStruct& FragmentType)
	{
		checkf(FragmentType.IsChildOf(FMassFragment::StaticStruct()), TEXT("Given struct doesn't represent a valid fragment type. Make sure to inherit from FMassFragment or one of its child-types."));
		Composition.Fragments.Add(FragmentType);
	}

	// @todo this function is doing nothing if a given fragment's initial value has already been created. This seems inconsistent with the other AddFragment functions (especially AddFragment_GetRef).
	void AddFragment(FConstStructView Fragment)
	{
		const UScriptStruct* FragmentType = Fragment.GetScriptStruct();
		checkf(FragmentType && FragmentType->IsChildOf(FMassFragment::StaticStruct()), TEXT("Given struct doesn't represent a valid fragment type. Make sure to inherit from FMassFragment or one of its child-types."));
		if (!Composition.Fragments.Contains(*FragmentType))
		{
			Composition.Fragments.Add(*FragmentType);
			InitialFragmentValues.Emplace(Fragment);
		}
		else if (!InitialFragmentValues.ContainsByPredicate(FStructTypeEqualOperator(FragmentType)))
		{
			InitialFragmentValues.Emplace(Fragment);
		}
	}

	template<typename T>
	T& AddFragment_GetRef()
	{
		static_assert(TIsDerivedFrom<T, FMassFragment>::IsDerived, "Given struct doesn't represent a valid fragment type. Make sure to inherit from FMassFragment or one of its child-types.");
		if (!Composition.Fragments.Contains<T>())
		{
			Composition.Fragments.Add<T>();
		}
		else if (FInstancedStruct* Fragment = InitialFragmentValues.FindByPredicate(FStructTypeEqualOperator(T::StaticStruct())))
		{
			return Fragment->template GetMutable<T>();
		}

		// Add a default initial fragment value
		return InitialFragmentValues.Emplace_GetRef(T::StaticStruct()).template GetMutable<T>();
	}

	template<typename T>
	T* GetMutableFragment()
	{
		static_assert(TIsDerivedFrom<T, FMassFragment>::IsDerived, "Given struct doesn't represent a valid fragment type. Make sure to inherit from FMassFragment or one of its child-types.");
		FInstancedStruct* Fragment = InitialFragmentValues.FindByPredicate(FStructTypeEqualOperator(T::StaticStruct()));
		return Fragment ? &Fragment->template GetMutable<T>() : (T*)nullptr;
	}

	template<typename T>
	void AddTag()
	{
		static_assert(TIsDerivedFrom<T, FMassTag>::IsDerived, "Given struct doesn't represent a valid mass tag type. Make sure to inherit from FMassTag or one of its child-types.");
		Composition.Tags.Add<T>();
	}
	
	void AddTag(const UScriptStruct& TagType)
	{
		checkf(TagType.IsChildOf(FMassTag::StaticStruct()), TEXT("Given struct doesn't represent a valid mass tag type. Make sure to inherit from FMassTag or one of its child-types."));
		Composition.Tags.Add(TagType);
	}

	const FMassTagBitSet& GetTags() const { return Composition.Tags; }
	FMassTagBitSet& GetMutableTags() { return Composition.Tags; }

	template<typename T>
	void AddChunkFragment()
	{
		static_assert(TIsDerivedFrom<T, FMassChunkFragment>::IsDerived, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FMassChunkFragment or one of its child-types.");
		Composition.ChunkFragments.Add<T>();
	}

	void AddConstSharedFragment(const FConstSharedStruct& SharedFragment)
	{
		const UScriptStruct* FragmentType = SharedFragment.GetScriptStruct();
		if(ensureMsgf(FragmentType && FragmentType->IsChildOf(FMassSharedFragment::StaticStruct()), TEXT("Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.")))
		{
			if (!Composition.SharedFragments.Contains(*FragmentType))
			{
				Composition.SharedFragments.Add(*FragmentType);
				SharedFragmentValues.AddConstSharedFragment(SharedFragment);
			}
#if DO_ENSURE
			else
			{
				const FConstSharedStruct* Struct = SharedFragmentValues.GetConstSharedFragments().FindByPredicate(FStructTypeEqualOperator(SharedFragment));
				ensureMsgf(Struct && *Struct == SharedFragment, TEXT("Adding 2 different const shared fragment of the same type is not allowed"));

			}
#endif // DO_ENSURE
		}
	}

	void AddSharedFragment(const FSharedStruct& SharedFragment)
	{
		const UScriptStruct* FragmentType = SharedFragment.GetScriptStruct();
		if(ensureMsgf(FragmentType && FragmentType->IsChildOf(FMassSharedFragment::StaticStruct()), TEXT("Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.")))
		{
			if (!Composition.SharedFragments.Contains(*FragmentType))
			{
				Composition.SharedFragments.Add(*FragmentType);
				SharedFragmentValues.AddSharedFragment(SharedFragment);
			}
	#if DO_ENSURE
			else
			{
				const FSharedStruct* Struct = SharedFragmentValues.GetSharedFragments().FindByPredicate(FStructTypeEqualOperator(SharedFragment));
				ensureMsgf(Struct && *Struct == SharedFragment, TEXT("Adding 2 different shared fragment of the same type is not allowed"));

			}
	#endif // DO_ENSURE
		}
	}

	template<typename T>
	bool HasFragment() const
	{
		return Composition.Fragments.Contains<T>();
	}
	
	bool HasFragment(const UScriptStruct& ScriptStruct) const
	{
		return Composition.Fragments.Contains(ScriptStruct);
	}

	template<typename T>
	bool HasTag() const
	{
		return Composition.Tags.Contains<T>();
	}

	template<typename T>
	bool HasChunkFragment() const
	{
		return Composition.ChunkFragments.Contains<T>();
	}

	template<typename T>
	bool HasSharedFragment() const
	{
		return Composition.SharedFragments.Contains<T>();
	}

	bool HasSharedFragment(const UScriptStruct& ScriptStruct) const
	{
		return Composition.SharedFragments.Contains(ScriptStruct);
	}

	friend uint32 GetTypeHash(const FMassEntityTemplateData& Template);

	void Sort()
	{
		SharedFragmentValues.Sort();
	}

protected:
	FMassArchetypeCompositionDescriptor Composition;
	FMassArchetypeSharedFragmentValues SharedFragmentValues;

	// Initial fragment values, this is not part of the archetype as it is the spawner job to set them.
	TArray<FInstancedStruct> InitialFragmentValues;

	// These functions will be called to initialize entity's UObject-based fragments
	TArray<FObjectFragmentInitializerFunction> ObjectInitializers;

	FString TemplateName;
};

/**
 * A finalized and const wrapper for FMassEntityTemplateData, associated with a Mass archetype and template ID. 
 * Designed to never be changed. If a change is needed a copy of the hosted FMassEntityTemplateData needs to be made and 
 * used to create another finalized FMassEntityTemplate (via FMassEntityTemplateManager).
 */
struct MASSSPAWNER_API FMassEntityTemplate final : public TSharedFromThis<FMassEntityTemplate> 
{
	friend TSharedFromThis<FMassEntityTemplate>;

	FMassEntityTemplate() = default;
	FMassEntityTemplate(const FMassEntityTemplateData& InData, FMassEntityManager& EntityManager, FMassEntityTemplateID InTemplateID);
	FMassEntityTemplate(FMassEntityTemplateData&& InData, FMassEntityManager& EntityManager, FMassEntityTemplateID InTemplateID);

	/** InArchetype is expected to be valid. The function will crash-check it. */
	void SetArchetype(const FMassArchetypeHandle& InArchetype);
	const FMassArchetypeHandle& GetArchetype() const { return Archetype; }

	bool IsValid() const { return Archetype.IsValid(); }

	void SetTemplateID(FMassEntityTemplateID InTemplateID) { TemplateID = InTemplateID; }
	FMassEntityTemplateID GetTemplateID() const { return TemplateID; }

	FString DebugGetDescription(FMassEntityManager* EntityManager = nullptr) const;
	FString DebugGetArchetypeDescription(FMassEntityManager& EntityManager) const;

	static TSharedRef<FMassEntityTemplate> MakeFinalTemplate(FMassEntityManager& EntityManager, FMassEntityTemplateData&& TempTemplateData, FMassEntityTemplateID InTemplateID);

	//-----------------------------------------------------------------------------
	// FMassEntityTemplateData getters
	//-----------------------------------------------------------------------------
	FORCEINLINE TConstArrayView<FMassEntityTemplateData::FObjectFragmentInitializerFunction> GetObjectFragmentInitializers() const { return TemplateData.GetObjectFragmentInitializers(); }
	FORCEINLINE const FString& GetTemplateName() const { return TemplateData.GetTemplateName(); }
	FORCEINLINE const FMassArchetypeCompositionDescriptor& GetCompositionDescriptor() const { return TemplateData.GetCompositionDescriptor(); }
	FORCEINLINE const FMassArchetypeSharedFragmentValues& GetSharedFragmentValues() const { return TemplateData.GetSharedFragmentValues(); }
	FORCEINLINE TConstArrayView<FInstancedStruct> GetInitialFragmentValues() const { return TemplateData.GetInitialFragmentValues(); }

	const FMassEntityTemplateData& GetTemplateData() const { return TemplateData; }

private:
	FMassEntityTemplateData TemplateData;
	FMassArchetypeHandle Archetype;
	FMassEntityTemplateID TemplateID;
};


struct FMassEntityTemplateIDFactory
{
	static FMassEntityTemplateID Make(const FMassEntityTemplateData& TemplateData);
	static FMassEntityTemplateID Make(TConstArrayView<UMassEntityTraitBase*> Traits);
};
