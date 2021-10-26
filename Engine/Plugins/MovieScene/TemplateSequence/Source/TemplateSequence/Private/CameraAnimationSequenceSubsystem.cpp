// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraAnimationSequenceSubsystem.h"
#include "Engine/World.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"

#define LOCTEXT_NAMESPACE "CameraAnimationSequenceSubsystem"

UCameraAnimationSequenceSubsystem* UCameraAnimationSequenceSubsystem::GetCameraAnimationSequenceSubsystem(const UWorld* InWorld)
{
	if (InWorld)
	{
		return InWorld->GetSubsystem<UCameraAnimationSequenceSubsystem>();
	}

	return nullptr;
}

UCameraAnimationSequenceSubsystem::UCameraAnimationSequenceSubsystem()
{
}

UCameraAnimationSequenceSubsystem::~UCameraAnimationSequenceSubsystem()
{
}

void UCameraAnimationSequenceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Linker = NewObject<UMovieSceneEntitySystemLinker>(this, TEXT("CameraAnimationSequenceSubsystemLinker"));
	Runner.AttachToLinker(Linker);
}

void UCameraAnimationSequenceSubsystem::Deinitialize()
{
	// We check if the runner still has a valid pointer on the linker because the linker could
	// have been GC'ed just now, which would make DetachFromLinker complain.
	if (Runner.GetLinker())
	{
		Runner.DetachFromLinker();
	}
	Linker = nullptr;

	Super::Deinitialize();
}

#undef LOCTEXT_NAMESPACE
