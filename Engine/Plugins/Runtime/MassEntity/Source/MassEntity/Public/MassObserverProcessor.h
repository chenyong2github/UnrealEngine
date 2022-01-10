// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassObserverProcessor.generated.h"


class UMassEntitySubsystem;

UCLASS(abstract)
class MASSENTITY_API UMassObserverProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassObserverProcessor();

protected:
	virtual void PostInitProperties() override;

	/** Override to register this processor with the observer registry */
	virtual void Register() PURE_VIRTUAL(UMassObserverProcessor::Register, );

protected:
	/** Set in class' constructor and determines for which Fragment type this given UMassObserverProcessor will be used 
	 *  as the default initializer/deinitializer (as returned by the MassInitializersRegistry). If set to null will
	 *  require the user to manually add this UMassObserverProcessor as initializer/deinitializer */
	UPROPERTY()
	UScriptStruct* FragmentType;
};

UCLASS(abstract)
class MASSENTITY_API UMassFragmentInitializer : public UMassObserverProcessor
{
	GENERATED_BODY()

protected:
	/** Adds this FragmentInitializer to the MassInitializersRegistry to be used to initialize fragments of FragmentType */
	virtual void Register() override;
};

UCLASS(abstract)
class MASSENTITY_API UMassFragmentDeinitializer : public UMassObserverProcessor
{
	GENERATED_BODY()

protected:
	/** Adds this FragmentDestructor to the MassInitializersRegistry to be used to deinitialize fragments of FragmentType */
	virtual void Register() override;
};
