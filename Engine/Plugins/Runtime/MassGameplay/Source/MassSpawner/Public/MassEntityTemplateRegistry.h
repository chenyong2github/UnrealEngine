// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MassCommonTypes.h"
#include "MassEntityTemplate.h"
#include "MassTranslatorRegistry.h"
#include "MassTranslator.h"
#include "MassEntityTemplateRegistry.generated.h"


class UWorld;
class UEntitySubsystem;
class UMassTranslator;

enum class EFragmentInitialization : uint8
{
	DefaultInitializer,
	NoInitializer
};

struct FMassEntityTemplateBuildContext
{
	explicit FMassEntityTemplateBuildContext(FMassEntityTemplate& InTemplate)
		: Template(InTemplate)
	{}

	//----------------------------------------------------------------------//
	// Fragments 
	//----------------------------------------------------------------------//
	template<typename T>
	T& AddFragment_GetRef()
	{
		return Template.GetMutableFragmentCollection().Add_GetRef<T>();
	}

	template<typename T>
	T& AddFragmentWithDefaultInitializer_GetRef()
	{
		AddDefaultInitializer<T>();
		return AddFragment_GetRef<T>();
	}

	template<typename T>
	T& AddFragmentWithInitializer_GetRef(const UPipeProcessor& Initializer)
	{
		AddInitializer(Initializer);
		return AddFragment_GetRef<T>();
	}

	template<typename T>
	void AddFragment()
	{
		static_assert(TIsDerivedFrom<T, FLWComponentData>::IsDerived, "Given struct doesn't represent a valid fragment type. Make sure to inherit from FLWComponentData or one of its child-types.");
		Template.GetMutableFragmentCollection().Add(T::StaticStruct());
	}

	template<typename T>
	void AddFragmentWithDefaultInitializer()
	{
		static_assert(TIsDerivedFrom<T, FLWComponentData>::IsDerived, "Given struct doesn't represent a valid fragment type. Make sure to inherit from FLWComponentData or one of its child-types.");
		AddFragment<T>();
		AddDefaultInitializer<T>();
	}

	template<typename T>
	void AddFragmentWithInitializer(const UPipeProcessor& Initializer)
	{
		static_assert(TIsDerivedFrom<T, FLWComponentData>::IsDerived, "Given struct doesn't represent a valid fragment type. Make sure to inherit from FLWComponentData or one of its child-types.");
		AddFragment<T>();
		AddInitializer(Initializer);
	}

	void AddFragment(const FStructView InFragment)
	{ 
		Template.GetMutableFragmentCollection().Add(InFragment);
	}

	void AddFragmentWithDefaultInitializer(FStructView InFragment)
	{
		AddFragment(InFragment);
		if (InFragment.GetScriptStruct())
		{
			AddDefaultInitializer(*InFragment.GetScriptStruct());
		}
	}

	void AddFragmentWithInitializer(FStructView InFragment, const UPipeProcessor& Initializer)
	{
		AddFragment(InFragment);
		AddInitializer(Initializer);
	}

	template<typename T>
	void AddTag()
	{
		Template.AddTag<T>();
	}

	void AddTag(const UScriptStruct& TagType)
	{
		Template.AddTag(TagType);
	}

	template<typename T>
	void AddChunkFragment()
	{
		Template.AddChunkFragment<T>();
	}

	template<typename T>
	T& AddChunkFragment_GetRef()
	{
		return Template.AddChunkFragment_GetRef<T>();
	}

	//----------------------------------------------------------------------//
	// Initializers
	//----------------------------------------------------------------------//
	void AddInitializer(const UPipeProcessor& Initializer) { Handlers.Initializers.AddUnique(&Initializer); }
	
	void AddDefaultInitializer(const UScriptStruct& FragmentType)
	{
		const UMassTranslatorRegistry& Registry = UMassTranslatorRegistry::Get();
		if (const UMassFragmentInitializer* Initializer = Registry.GetFragmentInitializer(FragmentType))
		{
			Handlers.Initializers.AddUnique(Initializer);
		}
	}

	template<typename T>
	void AddDefaultInitializer()
	{
		const UMassTranslatorRegistry& Registry = UMassTranslatorRegistry::Get();
		if (const UMassFragmentInitializer* Initializer = Registry.GetFragmentInitializer(*T::StaticStruct()))
		{
			Handlers.Initializers.AddUnique(Initializer);
		}
	}

	//----------------------------------------------------------------------//
	// Deinitializers
	//----------------------------------------------------------------------//
	void AddDeinitializer(const UPipeProcessor& Deinitializer) { Handlers.Deinitializers.AddUnique(&Deinitializer); }

	void AddDefaultDeinitializer(const UScriptStruct& FragmentType)
	{
		const UMassTranslatorRegistry& Registry = UMassTranslatorRegistry::Get();
		if (const UMassFragmentDestructor* Deinitializer = Registry.GetFragmentDestructor(FragmentType))
		{
			Handlers.Deinitializers.AddUnique(Deinitializer);
		}
	}

	template<typename T>
	void AddDefaultDeinitializer()
	{
		const UMassTranslatorRegistry& Registry = UMassTranslatorRegistry::Get();
		if (const UMassFragmentDestructor* Deinitializer = Registry.GetFragmentDestructor(*T::StaticStruct()))
		{
			Handlers.Deinitializers.AddUnique(Deinitializer);
		}
	}

	//----------------------------------------------------------------------//
	// Translators
	//----------------------------------------------------------------------//
	template<typename T>
	void AddTranslator()
	{
		TArray<UScriptStruct*> TagTypes;
		GetDefault<T>()->AppendRequiredTags(Template.GetMutableTags());
	}

	//----------------------------------------------------------------------//
	// Template access
	//----------------------------------------------------------------------//
	FMassEntityTemplateID GetTemplateID() const { return Template.GetTemplateID(); }
	FMassUniqueFragmentCollection& GetMutableFragments() { return Template.GetMutableFragmentCollection(); }
	TArray<FMassEntityTemplate::FObjectFragmentInitializerFunction>& GetMutableObjectFragmentInitializers() { return Template.GetMutableObjectFragmentInitializers(); }

	FMassObjectHandlers Handlers;
protected:
	FMassEntityTemplate& Template;
};

/** @todo document 
 *  Note: the Object-Instance entity templates do not support having multiple instances of the same class that differ in
 *  composition (like with dynamically added components). The first registered instance will determine the composition 
 *  of the entity template.
 */
UCLASS()
class MASSSPAWNER_API UMassEntityTemplateRegistry : public UObject
{
	GENERATED_BODY()
public:
	// @todo consider TFunction instead
	DECLARE_DELEGATE_ThreeParams(FStructToTemplateBuilderDelegate, const UWorld* /*World*/, const FInstancedStruct& /*InStructInstance*/, FMassEntityTemplateBuildContext& /*BuildContext*/);
	DECLARE_DELEGATE_FourParams(FClassToTemplateBuilderDelegate, const UWorld* /*World*/, const UClass& /*InClass*/, const UObject* /*ObjectInstance*/, FMassEntityTemplateBuildContext& /*BuildContext*/);

	UMassEntityTemplateRegistry() = default;
	virtual void BeginDestroy() override;
	virtual UWorld* GetWorld() const override;

	static FStructToTemplateBuilderDelegate& FindOrAdd(const UScriptStruct& DataType);
	static FClassToTemplateBuilderDelegate& FindOrAdd(const UClass& Class);

	const FMassEntityTemplate* FindOrBuildStructTemplate(const FInstancedStruct& StructInstance);
	const FMassEntityTemplate* FindOrBuildInstanceTemplate(const UObject& Instance);
	const FMassEntityTemplate& FindInstanceTemplateChecked(const UObject& Instance) const;
	const FMassEntityTemplate* FindOrBuildClassTemplate(const UClass& Class);

	/** Removes all the cached template instances */
	void DebugReset();

	const FMassEntityTemplate* FindTemplateFromTemplateID(FMassEntityTemplateID TemplateID) const;
	FMassEntityTemplate* FindMutableTemplateFromTemplateID(FMassEntityTemplateID TemplateID);

	FMassEntityTemplate& CreateTemplate(const uint32 HashLookup, FMassEntityTemplateID TemplateID);
	void DestroyTemplate(const uint32 HashLookup, FMassEntityTemplateID TemplateID);
	void InitializeEntityTemplate(FMassEntityTemplate& OutTemplate) const;

protected:
	/** A function implementing common path for UClass and UObject template building 
	 *	@param Class type of class for which we need to build an entity template
	 *	@param Instance if given means we're building a template for a runtime UObject instance. It will be passed down to template builder
	 */
	const FMassEntityTemplate* BuildClassTemplate(const UClass& Class, const UObject* Instance = nullptr);

	/** @return true if a template has been built, false otherwise */
	bool BuildTemplateImpl(const FStructToTemplateBuilderDelegate& Builder, const FInstancedStruct& StructInstance, FMassEntityTemplate& OutTemplate);

	static FClassToTemplateBuilderDelegate* GetBuilderForClass(const UClass& Class);

protected:
	static TMap<const UScriptStruct*, FStructToTemplateBuilderDelegate> StructBasedBuilders;
	static TMap<const UClass*, FClassToTemplateBuilderDelegate> ClassBasedBuilders;
	
	/** 
	 *  Map from a hash to a FMassEntityTemplateID
	 *  For build from FInstancedStruct it will be the Combined hash of the UScriptStruct FName and FTemplateRegistryHelpers::CalcHash()
	 *  For build from UClass or UObject it will be the hash of the UClass FName
	 *  This hash will not be deterministic between server and clients
	 */
	TMap<uint32, FMassEntityTemplateID> LookupTemplateIDMap;

	UPROPERTY(Transient)
	TMap<FMassEntityTemplateID, FMassEntityTemplate> TemplateIDToTemplateMap;
};

