// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGCreateTargetActor.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGManagedResource.h"
#include "PCGModule.h"
#include "PCGParamData.h"
#include "Helpers/PCGActorHelpers.h"
#include "Helpers/PCGHelpers.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGCreateTargetActor)

#define LOCTEXT_NAMESPACE "PCGCreateTargetActor"

#if WITH_EDITOR

FText UPCGCreateTargetActor::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Create Target Actor");
}

#endif // WITH_EDITOR

FPCGElementPtr UPCGCreateTargetActor::CreateElement() const
{
	return MakeShared<FPCGCreateTargetActorElement>();
}

TArray<FPCGPinProperties> UPCGCreateTargetActor::InputPinProperties() const
{
	return TArray<FPCGPinProperties>();
}

TArray<FPCGPinProperties> UPCGCreateTargetActor::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace_GetRef(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param, false);

	return PinProperties;
}

void UPCGCreateTargetActor::BeginDestroy()
{
#if WITH_EDITOR
	TeardownBlueprintEvent();
#endif

	Super::BeginDestroy();
}

#if WITH_EDITOR
void UPCGCreateTargetActor::SetupBlueprintEvent()
{
	if (UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(TemplateActorClass))
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintClass->ClassGeneratedBy))
		{
			Blueprint->OnChanged().AddUObject(this, &UPCGCreateTargetActor::OnBlueprintChanged);
		}
	}
}

void UPCGCreateTargetActor::TeardownBlueprintEvent()
{
	if (UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(TemplateActorClass))
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintClass->ClassGeneratedBy))
		{
			Blueprint->OnChanged().RemoveAll(this);
		}
	}
}

void UPCGCreateTargetActor::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGCreateTargetActor, TemplateActorClass))
	{
		TeardownBlueprintEvent();
	}
}

void UPCGCreateTargetActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property)
	{
		const FName& PropertyName = PropertyChangedEvent.Property->GetFName();

		if (PropertyName == GET_MEMBER_NAME_CHECKED(UPCGCreateTargetActor, TemplateActorClass))
		{
			SetupBlueprintEvent();
			RefreshTemplateActor();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UPCGCreateTargetActor::PreEditUndo()
{
	TeardownBlueprintEvent();

	Super::PreEditUndo();
}

void UPCGCreateTargetActor::PostEditUndo()
{
	Super::PostEditUndo();

	SetupBlueprintEvent();
	RefreshTemplateActor();
}

void UPCGCreateTargetActor::OnBlueprintChanged(UBlueprint* InBlueprint)
{
	RefreshTemplateActor();
	OnSettingsChangedDelegate.Broadcast(this, EPCGChangeType::Settings);
}

#endif // WITH_EDITOR

void UPCGCreateTargetActor::RefreshTemplateActor()
{
	if (TemplateActorClass)
	{
		AActor* NewTemplateActor = NewObject<AActor>(this, TemplateActorClass, NAME_None, RF_ArchetypeObject | RF_Transactional | RF_Public);

		if (TemplateActor)
		{
			UEngine::FCopyPropertiesForUnrelatedObjectsParams Options;
			Options.bNotifyObjectReplacement = true;
			UEngine::CopyPropertiesForUnrelatedObjects(TemplateActor, NewTemplateActor, Options);

			TemplateActor->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
		}

		TemplateActor = NewTemplateActor;
	}
	else
	{
		TemplateActor = nullptr;
	}
}

void UPCGCreateTargetActor::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	SetupBlueprintEvent();

	if (TemplateActorClass)
	{
		if (TemplateActor)
		{
			TemplateActor->ConditionalPostLoad();
		}

		RefreshTemplateActor();
	}
#endif // WITH_EDITOR
}

bool FPCGCreateTargetActorElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGCreateTargetActorElement::Execute);

	// Early out if the actor isn't going to be consumed by something else
	if (Context->Node && !Context->Node->IsOutputPinConnected(PCGPinConstants::DefaultOutputLabel))
	{
		return true;
	}

	const UPCGCreateTargetActor* Settings = Context->GetInputSettings<UPCGCreateTargetActor>();
	check(Settings);

	// Early out if the template actor isn't valid
	if (!Settings->TemplateActorClass || Settings->TemplateActorClass->HasAnyClassFlags(CLASS_Abstract))
	{
		const FText ClassName = Settings->TemplateActorClass ? FText::FromString(Settings->TemplateActorClass->GetFName().ToString()) : FText::FromName(NAME_None);
		PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("InvalidTemplateActorClass", "Invalid template actor class '{0}'"), ClassName));
		return true;
	}

	if (!ensure(Settings->TemplateActor && Settings->TemplateActor->IsA(Settings->TemplateActorClass)))
	{
		return true;
	}

	AActor* TargetActor = Settings->RootActor.Get() ? Settings->RootActor.Get() : Context->GetTargetActor(nullptr);
	if (!TargetActor)
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidTargetActor", "Invalid target actor"));
		return true;
	}

	const bool bHasAuthority = !Context->SourceComponent.IsValid() || (Context->SourceComponent->GetOwner() && Context->SourceComponent->GetOwner()->HasAuthority());
	const bool bSpawnedActorRequiresAuthority = Settings->TemplateActor->GetIsReplicated();

	if (!bHasAuthority && bSpawnedActorRequiresAuthority)
	{
		return true;
	}

	// Spawn actor
	AActor* TemplateActor = Settings->TemplateActor.Get();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = TargetActor;
	SpawnParams.Template = TemplateActor;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (PCGHelpers::IsRuntimeOrPIE())
	{
		SpawnParams.ObjectFlags |= RF_Transient;
	}

	FTransform Transform = TargetActor->GetTransform();
	AActor* GeneratedActor = UPCGActorHelpers::SpawnDefaultActor(TargetActor->GetWorld(), Settings->TemplateActorClass, Transform, SpawnParams, TargetActor);

	if (!GeneratedActor)
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("ActorSpawnFailed", "Failed to spawn actor"));
		return true;
	}

	GeneratedActor->Tags.Add(PCGHelpers::DefaultPCGActorTag);

	if (UPCGComponent* SourceComponent = Context->SourceComponent.Get())
	{
		UPCGManagedActors* ManagedActors = NewObject<UPCGManagedActors>(SourceComponent);
		ManagedActors->GeneratedActors.Add(GeneratedActor);
		SourceComponent->AddToManagedResources(ManagedActors);
	}

	// Create param data output with reference to actor
	UPCGParamData* ParamData = NewObject<UPCGParamData>();
	check(ParamData && ParamData->Metadata);
	ParamData->Metadata->CreateAttribute<FString>(NAME_None, FSoftObjectPath(GeneratedActor).ToString(), /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);

	// Add param data to output and we're done
	Context->OutputData.TaggedData.Emplace_GetRef().Data = ParamData;
	return true;
}

#undef LOCTEXT_NAMESPACE