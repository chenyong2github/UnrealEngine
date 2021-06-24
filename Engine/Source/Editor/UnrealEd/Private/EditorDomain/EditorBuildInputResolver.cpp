// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorBuildInputResolver.h"

#include "Serialization/BulkDataRegistry.h"

namespace UE::DerivedData
{

FEditorBuildInputResolver& FEditorBuildInputResolver::Get()
{
	static FEditorBuildInputResolver Singleton;
	return Singleton;
}

FRequest FEditorBuildInputResolver::ResolveKey(const FBuildKey& Key, FOnBuildKeyResolved&& OnResolved)
{
	// Not yet implemented
	OnResolved({ Key, {}, EStatus::Error });
	return FRequest();
}

FRequest FEditorBuildInputResolver::ResolveInputMeta(const FBuildDefinition& Definition, EPriority Priority,
	FOnBuildInputMetaResolved&& OnResolved)
{
	EStatus Status = EStatus::Ok;
	TArray<FString> InputKeys;
	TArray<FBuildInputMetaByKey> Inputs;

	Definition.IterateInputBuilds([&Status, &Definition](FStringView Key, const FBuildPayloadKey& PayloadKey)
		{
			UE_LOG(LogCore, Error, TEXT("FEditorBuildInputResolver::ResolveInputMeta: resolving InputBuilds is not yet implemented. Context=%.*s, Key=%.*s"),
				Definition.GetName().Len(), Definition.GetName().GetData(), Key.Len(), Key.GetData());
			Status = EStatus::Error;
		});

	/** Visits every input bulk data in order by key. */
	Definition.IterateInputBulkData([&Status, &InputKeys, &Inputs, &Definition](FStringView Key, const FGuid& BulkDataId)
		{
			FBuildInputMetaByKey& Input = Inputs.Emplace_GetRef();
			// TODO: MakeAsync: Create an IRequest that has an array of TFutures and calls .Then to populate the Inputs
			TFuture<UE::BulkDataRegistry::FMetaData> Future = IBulkDataRegistry::Get().GetMeta(BulkDataId);
			const UE::BulkDataRegistry::FMetaData& Result = Future.Get();
			if (Result.bValid)
			{
				Input.Key = InputKeys.Emplace_GetRef(Key);
				Input.RawHash = Result.RawHash;
				Input.RawSize = Result.RawSize;
			}
			else
			{
				Inputs.Pop();
				UE_LOG(LogCore, Error, TEXT("Failed to ResolveInputMetaData. Context=%.*s, Key=%.*s"),
					Definition.GetName().Len(), Definition.GetName().GetData(), Key.Len(), Key.GetData())
				Status = EStatus::Error;
			}
		});

	Definition.IterateInputFiles([&Status, &Definition](FStringView Key, FStringView Path)
		{
			UE_LOG(LogCore, Error, TEXT("FEditorBuildInputResolver::ResolveInputMeta: resolving InputFiles is not yet implemented. Context=%.*s, Key=%.*s"),
				Definition.GetName().Len(), Definition.GetName().GetData(), Key.Len(), Key.GetData());
			Status = EStatus::Error;
		});

	Definition.IterateInputHashes([&Status, &Definition](FStringView Key, const FIoHash& RawHash)
		{
			UE_LOG(LogCore, Error, TEXT("FEditorBuildInputResolver::ResolveInputMeta: resolving InputHashes is not yet implemented. Context=%.*s, Key=%.*s"),
				Definition.GetName().Len(), Definition.GetName().GetData(), Key.Len(), Key.GetData());
			Status = EStatus::Error;
		});

	OnResolved({ Inputs, Status });
	return FRequest();
}

FRequest FEditorBuildInputResolver::ResolveInputData(const FBuildDefinition& Definition, EPriority Priority,
	FOnBuildInputDataResolved&& OnResolved, FBuildInputFilter&& Filter)
{
	EStatus Status = EStatus::Ok;
	TArray<FString> InputKeys;
	TArray<FBuildInputDataByKey> Inputs;

	Definition.IterateInputBuilds([&Status, &Definition](FStringView Key, const FBuildPayloadKey& PayloadKey)
		{
			UE_LOG(LogCore, Error, TEXT("FEditorBuildInputResolver::ResolveInputData resolving InputBuilds is not yet implemented. Context=%.*s, Key=%.*s"),
				Definition.GetName().Len(), Definition.GetName().GetData(), Key.Len(), Key.GetData());
			Status = EStatus::Error;
		});

	/** Visits every input bulk data in order by key. */
	Definition.IterateInputBulkData([&Filter, &Status, &InputKeys, &Inputs, &Definition](FStringView Key, const FGuid& BulkDataId)
		{
			if (!Filter || Filter(Key))
			{
				// TODO: MakeAsync: Create an IRequest that has an array of TFutures and calls .Then to populate the Inputs
				TFuture<UE::BulkDataRegistry::FData> Future = IBulkDataRegistry::Get().GetData(BulkDataId);
				const UE::BulkDataRegistry::FData& Result = Future.Get();
				if (Result.bValid)
				{
					InputKeys.Emplace(Key);
					Inputs.Add({ InputKeys.Last(), Result.Buffer });
				}
				else
				{
					UE_LOG(LogCore, Error, TEXT("Failed to ResolveInputData. Context=%.*s, Key=%.*s"),
						Definition.GetName().Len(), Definition.GetName().GetData(), Key.Len(), Key.GetData())
					Status = EStatus::Error;
				}
			}
		});

	Definition.IterateInputFiles([&Status, &Definition](FStringView Key, FStringView Path)
		{
			UE_LOG(LogCore, Error, TEXT("FEditorBuildInputResolver::ResolveInputData: resolving InputFiles is not yet implemented. Context=%.*s, Key=%.*s"),
				Definition.GetName().Len(), Definition.GetName().GetData(), Key.Len(), Key.GetData());
			Status = EStatus::Error;
		});

	Definition.IterateInputHashes([&Status, &Definition](FStringView Key, const FIoHash& RawHash)
		{
			UE_LOG(LogCore, Error, TEXT("FEditorBuildInputResolver::ResolveInputData: resolving InputHashes is not yet implemented. Context=%.*s, Key=%.*s"),
				Definition.GetName().Len(), Definition.GetName().GetData(), Key.Len(), Key.GetData());
			Status = EStatus::Error;
		});

	OnResolved({ Inputs, Status });
	return FRequest();
}

FRequest FEditorBuildInputResolver::ResolveInputData(const FBuildAction& Action, EPriority Priority,
	FOnBuildInputDataResolved&& OnResolved, FBuildInputFilter&& Filter)
{
	// Not yet implemented
	OnResolved({ {}, EStatus::Error });
	return FRequest();
}

} // namespace UE::DerivedData
