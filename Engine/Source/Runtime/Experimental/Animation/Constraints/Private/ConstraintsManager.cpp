// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConstraintsManager.h"

#include "ConstraintsActor.h"

#include "Algo/Copy.h"
#include "Algo/StableSort.h"

#include "Engine/World.h"
#include "Engine/Level.h"

/** 
 * FConstraintTickFunction
 **/

FConstraintTickFunction::FConstraintTickFunction()
{
	TickGroup = TG_PrePhysics;
	bCanEverTick = true;
	bStartWithTickEnabled = true;
	bHighPriority = true;
}

FConstraintTickFunction::~FConstraintTickFunction()
{}

void FConstraintTickFunction::ExecuteTick(
	float DeltaTime,
	ELevelTick TickType,
	ENamedThreads::Type CurrentThread,
	const FGraphEventRef& MyCompletionGraphEvent)
{
	EvaluateFunctions();
}

void FConstraintTickFunction::RegisterFunction(ConstraintFunction InConstraint)
{
	ConstraintFunctions.Add(InConstraint);
}

void FConstraintTickFunction::EvaluateFunctions() const
{
	for (const ConstraintFunction& Function: ConstraintFunctions)
	{
		Function();
	}	
}

FString FConstraintTickFunction::DiagnosticMessage()
{
	if(!Constraint.IsValid())
	{
		return FString::Printf(TEXT("FConstraintTickFunction::Tick[%p]"), this);	
	}

#if WITH_EDITOR
	return FString::Printf(TEXT("FConstraintTickFunction::Tick[%p] (%s)"), this, *Constraint->GetLabel().ToString());
#else
	return FString::Printf(TEXT("FConstraintTickFunction::Tick[%p] (%s)"), this, *Constraint->GetName());
#endif
}

/** 
 * UTickableConstraint
 **/

#if WITH_EDITOR
FName UTickableConstraint::GetLabel() const
{
	return GetFName();
}
#endif

/** 
 * UConstraintsManager
 **/

UConstraintsManager::UConstraintsManager()
	: Level(nullptr)
{}

UConstraintsManager::~UConstraintsManager()
{}

void UConstraintsManager::Init(ULevel* InLevel)
{
	Level = InLevel;
}

UConstraintsManager* UConstraintsManager::Get(UWorld* InWorld)
{
	// look for ConstraintsActor and return its manager
	if (UConstraintsManager* Manager = Find(InWorld))
	{
		return Manager;
	}

	// create a new ConstraintsActor
	AConstraintsActor* ConstraintsActor = InWorld->SpawnActor<AConstraintsActor>();
#if WITH_EDITOR
	ConstraintsActor->SetActorLabel(TEXT("Constraints Manager"));
#endif // WITH_EDITOR
	ConstraintsActor->ConstraintsManager = NewObject<UConstraintsManager>(ConstraintsActor);
	
	ULevel* Level = InWorld->GetCurrentLevel();
	ConstraintsActor->ConstraintsManager->Init(Level);
	ConstraintsActor->ConstraintsManager->Level = Level;

	return ConstraintsActor->ConstraintsManager;
}

UConstraintsManager* UConstraintsManager::Find(const UWorld* InWorld)
{
	// should we work with the persistent level?
	const ULevel* Level = InWorld->GetCurrentLevel();

	// look for first ConstraintsActor
	auto FindFirstConstraintsActor = [](const ULevel* Level)
	{
		const int32 Index = Level->Actors.IndexOfByPredicate([](const AActor* Actor)
		{
			return IsValid(Actor) && Actor->IsA<AConstraintsActor>();
		} );

		return Index != INDEX_NONE ? Cast<AConstraintsActor>(Level->Actors[Index]) : nullptr;
	};

	const AConstraintsActor* ConstraintsActor = FindFirstConstraintsActor(Level);
	return ConstraintsActor ? ConstraintsActor->ConstraintsManager : nullptr;
}

// we want InFunctionToTickBefore to tick first = InFunctionToTickBefore is a prerex of InFunctionToTickAfter
void UConstraintsManager::SetConstraintDependencies(
	FConstraintTickFunction* InFunctionToTickBefore,
	FConstraintTickFunction* InFunctionToTickAfter)
{
	// look for child tick function in in parent's prerequisites. 
	const TArray<FTickPrerequisite>& ParentPrerequisites = InFunctionToTickAfter->GetPrerequisites();
	const bool bIsChildAPrerexOfParent = ParentPrerequisites.ContainsByPredicate([InFunctionToTickBefore](const FTickPrerequisite& Prerex)
	{
		return Prerex.PrerequisiteTickFunction == InFunctionToTickBefore;
	});
	
	// child tick function is already a prerex -> parent already ticks after child
	if (bIsChildAPrerexOfParent)
	{
		return;
	}

	// look for parent tick function in in child's prerequisites
	const TArray<FTickPrerequisite>& ChildPrerequisites = InFunctionToTickBefore->GetPrerequisites();
	const bool bIsParentAPrerexOfChild = ChildPrerequisites.ContainsByPredicate([InFunctionToTickAfter](const FTickPrerequisite& Prerex)
	{
		return Prerex.PrerequisiteTickFunction == InFunctionToTickAfter;
	});
	
	// parent tick function is a prerex of the child tick function (child ticks after parent)
	// so remove it before setting new dependencies.
	if (bIsParentAPrerexOfChild)
	{
		InFunctionToTickBefore->RemovePrerequisite(this, *InFunctionToTickAfter);
	}
	
	// set dependency
	InFunctionToTickAfter->AddPrerequisite(this, *InFunctionToTickBefore);
}

void UConstraintsManager::Clear()
{
	Constraints.Empty();
}

void UConstraintsManager::Dump() const
{
	UE_LOG(LogTemp, Error, TEXT("nb consts = %d"), Constraints.Num());
	for (const TObjectPtr<UTickableConstraint>& Constraint: Constraints)
	{
		if (IsValid(Constraint))
		{
			UE_LOG(LogTemp, Warning, TEXT("\t%s (target hash = %u)"),
				*Constraint->GetName(), Constraint->GetTargetHash());
		}
	}
}

FConstraintsManagerController& FConstraintsManagerController::Get(UWorld* InWorld)
{
	static FConstraintsManagerController Singleton;
	Singleton.World = InWorld;
	return Singleton;
}

UConstraintsManager* FConstraintsManagerController::GetManager() const
{
	if (!World)
	{
		return nullptr;
	}
	
	// look for ConstraintsActor and return its manager
	if (UConstraintsManager* Manager = FindManager())
	{
		return Manager;
	}

	// create a new ConstraintsActor
	AConstraintsActor* ConstraintsActor = World->SpawnActor<AConstraintsActor>();
#if WITH_EDITOR
	ConstraintsActor->SetActorLabel(TEXT("Constraints Manager"));
#endif // WITH_EDITOR
	ConstraintsActor->ConstraintsManager = NewObject<UConstraintsManager>(ConstraintsActor);
	
	ULevel* Level = World->GetCurrentLevel();
	ConstraintsActor->ConstraintsManager->Level = Level;

	return ConstraintsActor->ConstraintsManager;
}

UConstraintsManager* FConstraintsManagerController::FindManager() const
{
	if (!World)
	{
		return nullptr;
	}
	
	// should we work with the persistent level?
	const ULevel* Level = World->GetCurrentLevel();

	// look for first ConstraintsActor
	auto FindFirstConstraintsActor = [](const ULevel* Level)
	{
		const int32 Index = Level->Actors.IndexOfByPredicate([](const AActor* Actor)
		{
			return IsValid(Actor) && Actor->IsA<AConstraintsActor>();
		} );

		return Index != INDEX_NONE ? Cast<AConstraintsActor>(Level->Actors[Index]) : nullptr;
	};

	const AConstraintsActor* ConstraintsActor = FindFirstConstraintsActor(Level);
	return ConstraintsActor ? ConstraintsActor->ConstraintsManager : nullptr;
}

void FConstraintsManagerController::DestroyManager() const
{
	if (!World)
	{
		return;
	}
	
	// should we work with the persistent level?
	const ULevel* Level = World->GetCurrentLevel();

	// note there should be only one...
	TArray<AActor*> ConstraintsActorsToRemove;
	Algo::CopyIf(Level->Actors, ConstraintsActorsToRemove, [](const AActor* Actor)
	{
		return IsValid(Actor) && Actor->IsA<AConstraintsActor>();
	} );

	for (AActor* ConstraintsActor: ConstraintsActorsToRemove)
	{
		World->EditorDestroyActor(ConstraintsActor, true);
	}
}

void FConstraintsManagerController::AddConstraint(UTickableConstraint* InConstraint) const
{
	if (!InConstraint)
	{
		return;
	}
	
	UConstraintsManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}

	// TODO handle transaction
	Manager->Constraints.Emplace(InConstraint);

	InConstraint->ConstraintTick.Constraint = InConstraint;
	InConstraint->ConstraintTick.RegisterFunction(InConstraint->GetFunction());
	InConstraint->ConstraintTick.RegisterTickFunction(World->GetCurrentLevel());
}

int32 FConstraintsManagerController::GetConstraintIndex(const FName& InConstraintName) const
{
	const UConstraintsManager* Manager = FindManager();
	if (!Manager)
	{
		return INDEX_NONE;
	}
	
	return Manager->Constraints.IndexOfByPredicate([InConstraintName](const TObjectPtr<UTickableConstraint>& Constraint)
	{
		return 	Constraint->GetFName() == InConstraintName;
	} );
}
	
void FConstraintsManagerController::RemoveConstraint(const FName& InConstraintName) const
{
	const int32 Index = GetConstraintIndex(InConstraintName);
	if (Index == INDEX_NONE)
	{
		return;	
	}
	
	RemoveConstraint(Index);
}

void FConstraintsManagerController::RemoveConstraint(const int32 InConstraintIndex) const
{
	UConstraintsManager* Manager = FindManager();
	if (!Manager)
	{
		return;
	}
	
	if (!Manager->Constraints.IsValidIndex(InConstraintIndex))
	{
		return;
	}

	// TODO handle transaction
	
	Manager->Constraints[InConstraintIndex]->ConstraintTick.UnRegisterTickFunction();
	Manager->Constraints.RemoveAt(InConstraintIndex);

	// destroy constraints actor if no constraints left
	if (Manager->Constraints.IsEmpty())
	{
		DestroyManager();
	}
}

TArray< TObjectPtr<UTickableConstraint> > FConstraintsManagerController::GetParentConstraints(
	const uint32 InTargetHash,
	const bool bSorted) const
{
		static const TArray< TObjectPtr<UTickableConstraint> > DummyArray;

	const UConstraintsManager* Manager = FindManager();
	if (!Manager)
	{
		return DummyArray;
	}
	
	if (InTargetHash == 0)
	{
		return DummyArray;
	}

	using ConstraintPtr = TObjectPtr<UTickableConstraint>;
	TArray< TObjectPtr<UTickableConstraint> > FilteredConstraints =
	Manager->Constraints.FilterByPredicate( [InTargetHash](const ConstraintPtr& Constraint)
	{
		return Constraint->GetTargetHash() == InTargetHash;
	} );
	
	if (bSorted)
	{
		// LHS ticks before RHS = LHS is a prerex of RHS 
		auto TicksBefore = [](const UTickableConstraint& LHS, const UTickableConstraint& RHS)
		{
			const TArray<FTickPrerequisite>& RHSPrerex = RHS.ConstraintTick.GetPrerequisites();
			const FConstraintTickFunction& LHSTickFunction = LHS.ConstraintTick;
			const bool bIsLHSAPrerexOfRHS = RHSPrerex.ContainsByPredicate([&LHSTickFunction](const FTickPrerequisite& Prerex)
			{
				return Prerex.PrerequisiteTickFunction == &LHSTickFunction;
			});
			return bIsLHSAPrerexOfRHS;
		};
		
		Algo::StableSort(FilteredConstraints, [TicksBefore](const ConstraintPtr& LHS, const ConstraintPtr& RHS)
		{
			return TicksBefore(*LHS, *RHS);
		});
	}
	
	return FilteredConstraints;
}

void FConstraintsManagerController::SetConstraintsDependencies(
	const FName& InNameToTickBefore,
	const FName& InNameToTickAfter) const
{
	UConstraintsManager* Manager = FindManager();
	if (!Manager)
	{
		return;
	}

	const int32 IndexBefore = GetConstraintIndex(InNameToTickBefore);
	const int32 IndexAfter = GetConstraintIndex(InNameToTickAfter);
	if (IndexBefore == INDEX_NONE || IndexAfter == INDEX_NONE || IndexAfter == IndexBefore)
	{
		return;
	}

	FConstraintTickFunction& FunctionToTickBefore = Manager->Constraints[IndexBefore]->ConstraintTick;
	FConstraintTickFunction& FunctionToTickAfter = Manager->Constraints[IndexAfter]->ConstraintTick;

	Manager->SetConstraintDependencies( &FunctionToTickBefore, &FunctionToTickAfter);
}

const TArray< TObjectPtr<UTickableConstraint> >& FConstraintsManagerController::GetConstrainsArray() const
{
	UConstraintsManager* Manager = FindManager();
	if (!Manager)
	{
		static const TArray< TObjectPtr<UTickableConstraint> > Empty;
		return Empty;
	}

	return Manager->Constraints;
}