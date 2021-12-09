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
class UMassEntitySubsystem;

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
		return Template.AddFragment_GetRef<T>();
	}

	template<typename T>
	T& AddFragmentWithDefaultInitializer_GetRef()
	{
		AddDefaultInitializer<T>();
		return AddFragment_GetRef<T>();
	}

	template<typename T>
	T& AddFragmentWithInitializer_GetRef(const UMassProcessor& Initializer)
	{
		AddInitializer(Initializer);
		return AddFragment_GetRef<T>();
	}

	template<typename T>
	void AddFragment()
	{
		Template.AddFragment<T>();
	}

	template<typename T>
	void AddFragmentWithDefaultInitializer()
	{
		AddFragment<T>();
		AddDefaultInitializer<T>();
	}

	template<typename T>
	void AddFragmentWithInitializer(const UMassProcessor& Initializer)
	{
		AddFragment<T>();
		AddInitializer(Initializer);
	}

	void AddFragment(FConstStructView InFragment)
	{ 
		Template.AddFragment(InFragment);
	}

	void AddFragmentWithDefaultInitializer(FConstStructView InFragment)
	{
		AddFragment(InFragment);
		if (InFragment.GetScriptStruct())
		{
			AddDefaultInitializer(*InFragment.GetScriptStruct());
		}
	}

	void AddFragmentWithInitializer(FConstStructView InFragment, const UMassProcessor& Initializer)
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

	void AddConstSharedFragment(const FConstSharedStruct& InSharedFragment)
	{
		Template.AddConstSharedFragment(InSharedFragment);
	}

	void AddSharedFragment(const FSharedStruct& InSharedFragment)
	{
		Template.AddSharedFragment(InSharedFragment);
	}

	template<typename T>
	bool HasFragment() const
	{
		return Template.HasFragment<T>();
	}
	
	bool HasFragment(const UScriptStruct& ScriptStruct) const
	{
		return Template.HasFragment(ScriptStruct);
	}

	template<typename T>
	bool HasTag() const
	{
		return Template.HasTag<T>();
	}

	template<typename T>
	bool HasChunkFragment() const
	{
		return Template.HasChunkFragment<T>();
	}

	template<typename T>
	bool HasSharedFragment() const
	{
		return Template.HasSharedFragment<T>();
	}

	//----------------------------------------------------------------------//
	// Initializers
	//----------------------------------------------------------------------//
	void AddInitializer(const UMassProcessor& Initializer) { Handlers.Initializers.AddUnique(&Initializer); }
	
	void AddDefaultInitializer(const UScriptStruct& FragmentType)
	{
		const UMassTranslatorRegistry& Registry = UMassTranslatorRegistry::Get();
		if (const UMassFragmentInitializer* Initializer = Registry.GetFragmentInitializer(FragmentType))
		{
			Handlers.DefaultInitializers.AddUnique(Initializer);
		}
	}

	template<typename T>
	void AddDefaultInitializer()
	{
		const UMassTranslatorRegistry& Registry = UMassTranslatorRegistry::Get();
		if (const UMassFragmentInitializer* Initializer = Registry.GetFragmentInitializer(*T::StaticStruct()))
		{
			Handlers.DefaultInitializers.AddUnique(Initializer);
		}
	}

	//----------------------------------------------------------------------//
	// Deinitializers
	//----------------------------------------------------------------------//
	void AddDeinitializer(const UMassProcessor& Deinitializer) { Handlers.Deinitializers.AddUnique(&Deinitializer); }

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
	TArray<FMassEntityTemplate::FObjectFragmentInitializerFunction>& GetMutableObjectFragmentInitializers() { return Template.GetMutableObjectFragmentInitializers(); }

	FMassObjectHandlers Handlers;
protected:
	FMassEntityTemplate& Template;
};

/** @todo document 
 */
UCLASS()
class MASSSPAWNER_API UMassEntityTemplateRegistry : public UObject
{
	GENERATED_BODY()
public:
	// @todo consider TFunction instead
	DECLARE_DELEGATE_ThreeParams(FStructToTemplateBuilderDelegate, const UWorld* /*World*/, const FInstancedStruct& /*InStructInstance*/, FMassEntityTemplateBuildContext& /*BuildContext*/);

	UMassEntityTemplateRegistry() = default;
	virtual void BeginDestroy() override;
	virtual UWorld* GetWorld() const override;

	static FStructToTemplateBuilderDelegate& FindOrAdd(const UScriptStruct& DataType);

	const FMassEntityTemplate* FindOrBuildStructTemplate(const FInstancedStruct& StructInstance);

	/** Removes all the cached template instances */
	void DebugReset();

	const FMassEntityTemplate* FindTemplateFromTemplateID(FMassEntityTemplateID TemplateID) const;
	FMassEntityTemplate* FindMutableTemplateFromTemplateID(FMassEntityTemplateID TemplateID);

	FMassEntityTemplate& CreateTemplate(const uint32 HashLookup, FMassEntityTemplateID TemplateID);
	void DestroyTemplate(const uint32 HashLookup, FMassEntityTemplateID TemplateID);
	void InitializeEntityTemplate(FMassEntityTemplate& OutTemplate) const;

protected:
	/** @return true if a template has been built, false otherwise */
	bool BuildTemplateImpl(const FStructToTemplateBuilderDelegate& Builder, const FInstancedStruct& StructInstance, FMassEntityTemplate& OutTemplate);

protected:
	static TMap<const UScriptStruct*, FStructToTemplateBuilderDelegate> StructBasedBuilders;

	/** 
	 *  Map from a hash to a FMassEntityTemplateID
	 *  For build from FInstancedStruct it will be the Combined hash of the UScriptStruct FName and FTemplateRegistryHelpers::CalcHash()
	 *  This hash will not be deterministic between server and clients
	 */
	TMap<uint32, FMassEntityTemplateID> LookupTemplateIDMap;

	UPROPERTY(Transient)
	TMap<FMassEntityTemplateID, FMassEntityTemplate> TemplateIDToTemplateMap;
};

