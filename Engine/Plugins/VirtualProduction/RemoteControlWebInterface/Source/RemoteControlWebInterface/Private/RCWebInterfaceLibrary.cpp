// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCWebInterfaceLibrary.h"

#include "Engine/World.h"
#include "IRemoteControlModule.h"
#include "Kismet/GameplayStatics.h"
#include "RCWebInterfacePrivate.h"
#include "RemoteControlBinding.h"
#include "RemoteControlPreset.h"

#define LOCTEXT_NAMESPACE "RemoteControlWebInterface"


TMap<FString, AActor*> URCWebInterfaceBlueprintLibrary::FindMatchingActorsToRebind(const FString& PresetId, const TArray<FString>& PropertyIds)
{
	TSet<AActor*> MatchesIntersection;
	if (URemoteControlPreset* RCPreset = URCWebInterfaceBlueprintLibrary::GetPreset(PresetId))
	{
		for (const FString& PropertyId : PropertyIds)
		{
			const FGuid PropertyGuid(PropertyId);
			TWeakPtr<FRemoteControlProperty> ExposedEntity = RCPreset->GetExposedEntity<FRemoteControlProperty>(PropertyGuid);

			TSet<AActor*> MatchingActorsForProperty;
			if (TSharedPtr<FRemoteControlProperty> RemoteControlProperty = ExposedEntity.Pin())
			{
				TArray<UObject*> BoundObjects = RemoteControlProperty->GetBoundObjects();
				if (BoundObjects.Num() >= 1)
				{
					UObject* OwnerObject = BoundObjects[0];
					if (AActor* Actor = Cast<AActor>(OwnerObject))
					{
						TArray<AActor*> MatchingActors;
						UGameplayStatics::GetAllActorsOfClass(Actor->GetWorld(), Actor->GetClass(), MatchingActors);
						MatchingActorsForProperty.Append(MatchingActors);
					}
					else if (UActorComponent* ActorComponent = Cast<UActorComponent>(OwnerObject))
					{
						AActor* OwnerActor = ActorComponent->GetOwner();

						TArray<AActor*> MatchingActors;
						UGameplayStatics::GetAllActorsOfClass(OwnerActor->GetWorld(), OwnerActor->GetClass(), MatchingActors);
						for (AActor* MatchingActor : MatchingActors)
						{
							if (UActorComponent* MatchingComponent = MatchingActor->GetComponentByClass(ActorComponent->GetClass()))
							{
								MatchingActorsForProperty.Add(MatchingActor);
							}
						}
					}
				}
				else if (UClass* SupportedBindingClass = RemoteControlProperty->GetSupportedBindingClass())
				{
					constexpr bool bAllowPIE = false;
					UWorld* World = RCPreset->GetWorld(bAllowPIE);
					if (SupportedBindingClass->IsChildOf(AActor::StaticClass()))
					{
						TArray<AActor*> MatchingActors;
						UGameplayStatics::GetAllActorsOfClass(World, SupportedBindingClass, MatchingActors);
						MatchingActorsForProperty.Append(MatchingActors);
					}
					else if (SupportedBindingClass->IsChildOf(UActorComponent::StaticClass()))
					{
						TArray<AActor*> MatchingActors;
						UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), MatchingActors);
						for (AActor* MatchingActor : MatchingActors)
						{
							if (UActorComponent* MatchingComponent = MatchingActor->GetComponentByClass(SupportedBindingClass))
							{
								MatchingActorsForProperty.Add(MatchingActor);
							}
						}
					}
				}
			}
		
			if (MatchesIntersection.Num() == 0)
			{
				MatchesIntersection = MatchingActorsForProperty;
			}
			else
			{
				for (auto Iterator = MatchesIntersection.CreateIterator(); Iterator; ++Iterator)
				{
					if (!MatchingActorsForProperty.Contains(*Iterator))
					{
						Iterator.RemoveCurrent();
					}
				}
			}
		}
	}

	TMap<FString, AActor*> Matches;
	Matches.Reserve(MatchesIntersection.Num());
	for (AActor* MatchingActor : MatchesIntersection)
	{
		Matches.Add(GetActorNameOrLabel(MatchingActor), MatchingActor);
	}

	return Matches;
}

FString URCWebInterfaceBlueprintLibrary::GetOwnerActorLabel(const FString& PresetId, const TArray<FString>& PropertyIds)
{
	TSet<FString> Actors;
	if (URemoteControlPreset* RCPreset = URCWebInterfaceBlueprintLibrary::GetPreset(PresetId))
	{
		for (const FString& PropertyId : PropertyIds)
		{
			const FGuid PropertyGuid(PropertyId);
			TWeakPtr<FRemoteControlProperty> ExposedEntity = RCPreset->GetExposedEntity<FRemoteControlProperty>(PropertyGuid);
			if (TSharedPtr<FRemoteControlProperty> RemoteControlProperty = ExposedEntity.Pin())
			{
				TArray<UObject*> BoundObjects = RemoteControlProperty->GetBoundObjects();
				if (BoundObjects.Num() >= 1)
				{
					UObject* OwnerObject = BoundObjects[0];
					if (const AActor* Actor = Cast<AActor>(OwnerObject))
					{
						Actors.Add(GetActorNameOrLabel(Actor));
					}
					else if (const UActorComponent* ActorComponent = Cast<UActorComponent>(OwnerObject))
					{
						AActor* OwnerActor = ActorComponent->GetOwner();
						Actors.Add(GetActorNameOrLabel(OwnerActor));
					}
				}
			}
		}
	}

	if (Actors.Num() == 1)
	{
		return Actors.Array()[0];
	}

	if (Actors.Num() > 1)
	{
		UE_LOG(LogRemoteControlWebInterface, Warning, TEXT("Propreties have different owners"));
	}

	return FString();
}

void URCWebInterfaceBlueprintLibrary::RebindProperties(const FString& PresetId, const TArray<FString>& PropertyIds, AActor* NewOwner)
{
	if (URemoteControlPreset* RCPreset = URCWebInterfaceBlueprintLibrary::GetPreset(PresetId))
	{
		for (const FString& PropertyId : PropertyIds)
		{
			const FGuid PropertyGuid(PropertyId);
			TWeakPtr<FRemoteControlProperty> ExposedEntity = RCPreset->GetExposedEntity<FRemoteControlProperty>(PropertyGuid);
			if (TSharedPtr<FRemoteControlProperty> RemoteControlProperty = ExposedEntity.Pin())
			{
				RemoteControlProperty->BindObject(NewOwner);
			}
		}
	}
}

URemoteControlPreset* URCWebInterfaceBlueprintLibrary::GetPreset(const FString& PresetId)
{
	FGuid Id;
	if (FGuid::ParseExact(PresetId, EGuidFormats::Digits, Id))
	{
		if (URemoteControlPreset* ResolvedPreset = IRemoteControlModule::Get().ResolvePreset(Id))
		{
			return ResolvedPreset;
		}
	}

	return IRemoteControlModule::Get().ResolvePreset(*PresetId);
}

FString URCWebInterfaceBlueprintLibrary::GetActorNameOrLabel(const AActor* Actor)
{
#if WITH_EDITOR
	return Actor->GetActorLabel();
#else
	return Actor->GetName();
#endif
}

#undef LOCTEXT_NAMESPACE