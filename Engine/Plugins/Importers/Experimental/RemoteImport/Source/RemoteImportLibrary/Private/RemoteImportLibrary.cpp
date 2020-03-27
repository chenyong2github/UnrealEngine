// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteImportLibrary.h"
#include "RemoteImportLibraryLog.h"
#include "RemoteImportServer.h"


TArray<FRemoteImportAnchor> URemoteImportLibrary::Anchors;
TUniquePtr<FRemoteImportServer> URemoteImportLibrary::Instance;
FSimpleMulticastDelegate URemoteImportLibrary::OnAnchorListChange;


URemoteImportLibrary::URemoteImportLibrary()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FRemoteImportAnchor DefaultAnchor;
		DefaultAnchor.Name = TEXT("Default");
		RegisterAnchor(DefaultAnchor);
	}
}

bool URemoteImportLibrary::ImportSource(const FString& FilePath, const FString& DestinationName)
{
	FRemoteImportAnchor* Destination = FindAnchor(DestinationName);
	if (!Destination)
	{
		UE_LOG(LogRemoteImportLibrary, Warning, TEXT("ImportSource failed: destination [%s] is unknown."), *DestinationName);
		return false;
	}

	Destination->OnImportFileDelegate.ExecuteIfBound(FilePath);
	return Destination->OnImportFileDelegate.IsBound();
}


FString URemoteImportLibrary::RegisterAnchor(const FRemoteImportAnchor& Anchor, bool bAllowRename)
{
	FRemoteImportAnchor AnchorCopy = Anchor;
	FString InitialName = AnchorCopy.Name;
	int32 Suffix = 0;
	while (Anchors.FindByPredicate([&](const FRemoteImportAnchor& Registered){return Registered.Name == AnchorCopy.Name;}))
	{
		if (!bAllowRename)
		{
			return {};
		}
		AnchorCopy.Name = InitialName + TEXT("_") + FString::FromInt(Suffix++);
	}
	Anchors.Add(AnchorCopy);
	OnAnchorListChange.Broadcast();
	return AnchorCopy.Name;
}

void URemoteImportLibrary::UnregisterAnchor(const FString& AnchorHandle)
{
	if (Anchors.RemoveAll([&AnchorHandle](const FRemoteImportAnchor& Anchor){ return Anchor.Name == AnchorHandle; }))
	{
		OnAnchorListChange.Broadcast();
	}
}

TArray<FString> URemoteImportLibrary::ListAnchors()
{
	TArray<FString> Out;
	Out.Reserve(Anchors.Num());
	for (const FRemoteImportAnchor& Anchor : Anchors)
	{
		Out.Add(Anchor.Name);
	}
	return Out;
}


FRemoteImportAnchor* URemoteImportLibrary::FindAnchor(const FString& AnchorName)
{
	return Anchors.FindByPredicate([&AnchorName](const FRemoteImportAnchor& Anchor){ return Anchor.Name == AnchorName; });
}


void URemoteImportLibrary::StartRemoteImportServer()
{
	Instance = MakeUnique<FRemoteImportServer>();
	UE_LOG(LogRemoteImportLibrary, Display, TEXT("RemoteImportServer started"));
}


void URemoteImportLibrary::StopRemoteImportServer()
{
	Instance.Reset();
	UE_LOG(LogRemoteImportLibrary, Display, TEXT("RemoteImportServer stopped"));
}


bool URemoteImportLibrary::IsRemoteImportServerActive()
{
	return Instance.IsValid();
}
