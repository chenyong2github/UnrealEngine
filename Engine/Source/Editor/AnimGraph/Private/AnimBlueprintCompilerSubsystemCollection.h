// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SubsystemCollection.h"
#include "AnimBlueprintCompilerSubsystem.h"

class FAnimBlueprintCompilerContext;

/** Subsystem collection for the anim blueprint compiler */
class FAnimBlueprintCompilerSubsystemCollection : public FSubsystemCollection<UAnimBlueprintCompilerSubsystem>
{
private:
	friend class FAnimBlueprintCompilerContext;
	friend class UAnimBlueprintCompilerSubsystem;

	/** Register the compiler with this collection */
	void RegisterContext(FAnimBlueprintCompilerContext* InCompilerContext) { CompilerContext = InCompilerContext; }

	/** Get the first subsystem that implements the requested interface */
	template<typename InterfaceClass>
	InterfaceClass* FindSubsystemWithInterface(TSubclassOf<UInterface> InInterfaceClass) const
	{
		const TArray<UAnimBlueprintCompilerSubsystem*>& BaseArray = GetSubsystemArray<UAnimBlueprintCompilerSubsystem>(UAnimBlueprintCompilerSubsystem::StaticClass());
		for(UAnimBlueprintCompilerSubsystem* Subsystem : BaseArray)
		{
			if(Subsystem->GetClass()->ImplementsInterface(InInterfaceClass.Get()))
			{
				return Cast<InterfaceClass>(Subsystem);
			}
		}

		return nullptr;
	}

	/** The compiler this collection is registered to */
	FAnimBlueprintCompilerContext* CompilerContext;
};