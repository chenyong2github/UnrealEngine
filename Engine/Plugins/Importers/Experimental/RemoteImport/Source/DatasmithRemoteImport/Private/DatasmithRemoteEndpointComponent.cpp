// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRemoteEndpointComponent.h"
#include "DatasmithAdapter.h"
#include "DatasmithRemoteImportLog.h"
#include "RemoteImportLibrary.h"


void UDatasmithRemoteEndpointComponent::BeginPlay()
{
	Super::BeginPlay();
	FRemoteImportAnchor Anchor;
	Anchor.Name = Name.IsEmpty() ? GetName() : Name;
	Anchor.Description = TEXT("UDatasmithRemoteEndpointComponent");
	Anchor.OnImportFileDelegate.BindUObject(this, &UDatasmithRemoteEndpointComponent::OnImportFile);

	RegisteredAnchorName = URemoteImportLibrary::RegisterAnchor(Anchor);
}

void UDatasmithRemoteEndpointComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	URemoteImportLibrary::UnregisterAnchor(RegisteredAnchorName);
}

void UDatasmithRemoteEndpointComponent::OnImportFile(const FString& FilePath)
{
	UE_LOG(LogDatasmithRemoteImport, Display, TEXT("Translate %s..."), *FilePath);
	DatasmithAdapter::FTranslateResult Content = DatasmithAdapter::Translate(FilePath);
	UE_LOG(LogDatasmithRemoteImport, Display, TEXT("Import %s..."), *FilePath);
	bool ImportResult = DatasmithAdapter::Import(Content, GetWorld(), GetComponentTransform());
	UE_LOG(LogDatasmithRemoteImport, Display, TEXT("Import done for %s"), *FilePath);
}

