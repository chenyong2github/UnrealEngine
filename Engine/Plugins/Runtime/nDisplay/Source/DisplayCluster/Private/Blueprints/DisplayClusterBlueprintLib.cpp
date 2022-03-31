// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/DisplayClusterBlueprintLib.h"
#include "Blueprints/DisplayClusterBlueprintAPIImpl.h"
#include "UObject/Package.h"

#include "DisplayClusterLightCardActor.h"
#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationTypes.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#define LOCTEXT_NAMESPACE "UDisplayClusterBlueprintLib"

UDisplayClusterBlueprintLib::UDisplayClusterBlueprintLib(class FObjectInitializer const & ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UDisplayClusterBlueprintLib::GetAPI(TScriptInterface<IDisplayClusterBlueprintAPI>& OutAPI)
{
	static UDisplayClusterBlueprintAPIImpl* Obj = NewObject<UDisplayClusterBlueprintAPIImpl>(GetTransientPackage(), NAME_None, RF_MarkAsRootSet);
	OutAPI = Obj;
}

ADisplayClusterLightCardActor* UDisplayClusterBlueprintLib::CreateLightCard(ADisplayClusterRootActor* RootActor)
{
	if (!RootActor)
	{
		return nullptr;
	}

	// Create the light card
#if WITH_EDITOR
	FScopedTransaction Transaction(LOCTEXT("CreateLightCard", "Create Light Card"));
#endif

	const FVector SpawnLocation = RootActor->GetDefaultCamera()->GetComponentLocation();
	FRotator SpawnRotation = RootActor->GetDefaultCamera()->GetComponentRotation();
	SpawnRotation.Yaw -= 180.f;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.bNoFail = true;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParameters.Name = TEXT("LightCard");
	SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	SpawnParameters.OverrideLevel = RootActor->GetLevel();

	ADisplayClusterLightCardActor* NewActor = CastChecked<ADisplayClusterLightCardActor>(
		RootActor->GetWorld()->SpawnActor(ADisplayClusterLightCardActor::StaticClass(),
			&SpawnLocation, &SpawnRotation, MoveTemp(SpawnParameters)));

#if WITH_EDITOR
	NewActor->SetActorLabel(NewActor->GetName());
#endif

	FAttachmentTransformRules AttachmentRules(EAttachmentRule::KeepWorld, false);
	NewActor->AttachToActor(RootActor, AttachmentRules);

	// Add it to the root actor
	UDisplayClusterConfigurationData* ConfigData = RootActor->GetConfigData();
	ConfigData->Modify();
	FDisplayClusterConfigurationICVFX_VisibilityList& RootActorLightCards = ConfigData->StageSettings.Lightcard.ShowOnlyList;

	RootActorLightCards.Actors.Add(NewActor);

	return NewActor;
}

#undef LOCTEXT_NAMESPACE