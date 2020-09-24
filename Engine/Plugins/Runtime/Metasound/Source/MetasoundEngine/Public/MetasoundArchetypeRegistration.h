// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontend.h"
#include "Misc/PackageName.h"

#if WITH_EDITOR
#include "AssetRegistryModule.h"
#endif // WITH_EDITOR

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"

namespace Metasound
{
	namespace Frontend
	{
		// Use this function to register a specific metasound archetype with the frontend.
		// Metasound Archetypes are lists of required inputs and outputs.
		// For example, a Metasound Source has a required audio output,
		// and a Metasound Source Effect has a required audio input and audio output.
		template <typename UClassType>
		bool RegisterArchetype()
		{
			static_assert(std::is_base_of<FMetasoundAssetBase, UClassType>::value, "Metasound Archetypes have to inherit from FMetasoundAssetBase (see: UMetasound, UMetasoundSource)");

			// Autogenerate our getter and cast lambdas here:
			FMetasoundArchetypeRegistryParams_Internal InternalParams =
			{
				GetDefault<UClassType>()->GetArchetype(),
				UClassType::StaticClass(),
				// SafeCast:
				[](UObject* InObject) -> FMetasoundAssetBase*
				{
					check(InObject);
					return static_cast<FMetasoundAssetBase*>(CastChecked<UClassType>(InObject));
				},
				// SafeConstCast:
				[](const UObject* InObject) -> const FMetasoundAssetBase*
				{
					check(InObject);
					return static_cast<const FMetasoundAssetBase*>(CastChecked<const UClassType>(InObject));
				},
				//Object Getter:
				[](const FMetasoundDocument& InDocument, const FString& InPath) -> UObject*
				{
					UPackage* PackageToSaveTo = nullptr;
					if (GIsEditor)
					{
						FText InvalidPathReason;
						bool const bValidPackageName = FPackageName::IsValidLongPackageName(InPath, false, &InvalidPathReason);

						if (!ensureAlwaysMsgf(bValidPackageName, TEXT("Tried to generate a Metasound UObject with an invalid package path/name Falling back to transient package, which means we won't be able to save this asset.")))
						{
							PackageToSaveTo = GetTransientPackage();
						}
						else
						{
							PackageToSaveTo = CreatePackage(*InPath);
						}
					}
					else
					{
						PackageToSaveTo = GetTransientPackage();
					}

					UClassType* NewMetasoundObject = NewObject<UClassType>(PackageToSaveTo, *InDocument.RootClass.Metadata.NodeName);
					NewMetasoundObject->SetDocument(InDocument);

#if WITH_EDITOR
					AsyncTask(ENamedThreads::GameThread, [NewMetasoundObject]()
					{
						FAssetRegistryModule::AssetCreated(NewMetasoundObject);
						NewMetasoundObject->MarkPackageDirty();
						// todo: how do you get the package for a uobject and save it? I forget
					});
#endif

					return NewMetasoundObject;
				}
			};

			return RegisterArchetype_Internal(MoveTemp(InternalParams));
		}
	}
}