// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compatibility/TypedElementActorPackagePathToColumnProcessor.h"

#include "Elements/Columns/TypedElementPackageColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "MassActorSubsystem.h"
#include "UObject/Package.h"

void UTypedElementActorPackagePathFactory::RegisterQueries(ITypedElementDataStorageInterface& DataStorage) const
{
	using namespace TypedElementQueryBuilder;
	using DSI = ITypedElementDataStorageInterface;

	DataStorage.RegisterQuery(
		Select(
			TEXT("Sync actor package info to columns"),
			FObserver(FObserver::EEvent::Add, FMassActorFragment::StaticStruct())
				.ForceToGameThread(true),
			[](const FMassActorFragment& Actor, FTypedElementPackagePathColumn& Path, FTypedElementPackageLoadedPathColumn& LoadedPath)
			{
				if (const AActor* ActorInstance = Actor.Get(); ActorInstance != nullptr)
				{
					const UPackage* Target = ActorInstance->GetPackage();
					Target->GetPathName(nullptr, Path.Path);
					LoadedPath.LoadedPath = Target->GetLoadedPath();
				}
			}
		)
		.Compile()
	);
}
