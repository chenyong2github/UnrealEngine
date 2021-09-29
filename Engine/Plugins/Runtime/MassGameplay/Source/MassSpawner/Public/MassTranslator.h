// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassCommonTypes.h"
#include "LWComponentTypes.h"
#include "MassTranslator.generated.h"


class UPipeEntitySubsystem;

UENUM()
enum class EMassTranslationDirection : uint8
{
	None = 0,
	InitializationOnly = 1,
	ActorToMass = 1 << 1,
	MassToActor = 1 << 2,
	BothWays = ActorToMass | MassToActor
};
ENUM_CLASS_FLAGS(EMassTranslationDirection);

/** 
 *  A class that's responsible for translation between UObjects and Mass. A translator knows how to initialize 
 *  fragments related to the UClass that the given translator cares about. It can also be used at runtime to 
 *  copy values from UObjects to fragments and back.
 */
UCLASS(abstract)
class MASSSPAWNER_API UMassTranslator : public UPipeProcessor
{
	GENERATED_BODY()

protected:
	UMassTranslator();
public:
	/** Fetches the FComponentTag-derived types required by this Translator. And entity needs these tags to be 
	 *  processed by this Translator instance. 
	 * 
	 *  @todo might want this function on the PipeProcessor level. TBD
	 * 
	 *  @param OutTagTypes tag types will be appended to this array. No uniqueness checks are performed. */
	void AppendRequiredTags(FLWTagBitSet& InOutTags) const { InOutTags += RequiredTags; }

protected:
	void AddRequiredTagsToQuery(FLWComponentQuery& EntityQuery);

protected:
	/** These are the tag fragments expected by this translator that let other code (like entity traits) hint to 
	 *  the system which translators they'd want their entity to be processed by. */
	FLWTagBitSet RequiredTags;
};

UCLASS(abstract)
class MASSSPAWNER_API UMassFragmentBuilder : public UPipeProcessor
{
	GENERATED_BODY()

public:
	UMassFragmentBuilder();

	/** Override to register this fragment builder to the registry */
	virtual void Register() PURE_VIRTUAL(UMassFragmentBuilder::Register, );

protected:
	virtual void PostInitProperties() override;

protected:
	/** Set in class' constructor and determines for which Fragment type this given UMassFragmentBuilder will be used 
	 *  as the default initializer/deinitializer (as returned by the UMassTranslatorRegistry). If set to null will
	 *  require the user to manually add this UMassFragmentBuilder as initializer/deinitializer 
	 *  (@see FMassEntityTemplateBuildContext.AddInitializer and FMassEntityTemplateBuildContext.AddDeinitializer) */
	UPROPERTY()
	UScriptStruct* FragmentType;
};

UCLASS(abstract)
class MASSSPAWNER_API UMassFragmentInitializer : public UMassFragmentBuilder
{
	GENERATED_BODY()

public:
	/** adds this FragmentInitializer to the MassTranslatorRegistry to be used to initialize fragments of FragmentType */
	virtual void Register();
};

UCLASS(abstract)
class MASSSPAWNER_API UMassFragmentDestructor : public UMassFragmentBuilder
{
	GENERATED_BODY()

public:
	/** adds this FragmentDestructor to the MassTranslatorRegistry to be used to initialize fragments of FragmentType */
	virtual void Register();
};