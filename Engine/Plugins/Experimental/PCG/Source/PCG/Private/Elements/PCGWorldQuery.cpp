// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGWorldQuery.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGHelpers.h"

#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGWorldQuery)

FPCGElementPtr UPCGWorldQuerySettings::CreateElement() const
{
	return MakeShared<FPCGWorldVolumetricQueryElement>();
}

bool FPCGWorldVolumetricQueryElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGWorldVolumetricQueryElement::Execute);

	const UPCGWorldQuerySettings* Settings = Context->GetInputSettings<UPCGWorldQuerySettings>();
	check(Settings);

	FPCGWorldVolumetricQueryParams QueryParams = Settings->QueryParams;
	
	// TODO: Add params pin + Apply param data overrides

	check(Context->SourceComponent.IsValid());
	UWorld* World = Context->SourceComponent->GetWorld();
	// TODO: Not strictly required but will be useful until we change the usage of this
	FBox SourceBounds = PCGHelpers::GetActorBounds(Context->SourceComponent->GetOwner());

	UPCGWorldVolumetricData* Data = NewObject<UPCGWorldVolumetricData>();
	Data->Initialize(World, SourceBounds);
	Data->QueryParams = QueryParams;
	Data->OriginatingComponent = Context->SourceComponent;
	Data->TargetActor = Context->SourceComponent->GetOwner();

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = Data;

	// Pass-through settings & exclusions
	Context->OutputData.TaggedData.Append(Context->InputData.GetAllSettings());

	return true;
}

FPCGElementPtr UPCGWorldRayHitSettings::CreateElement() const
{
	return MakeShared<FPCGWorldRayHitQueryElement>();
}

bool FPCGWorldRayHitQueryElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGWorldRayHitQueryElement::Execute);

	const UPCGWorldRayHitSettings* Settings = Context->GetInputSettings<UPCGWorldRayHitSettings>();
	check(Settings);

	FPCGWorldRayHitQueryParams QueryParams = Settings->QueryParams;
	
	// TODO: Support params pin + Apply param data

	// Compute default parameters based on owner component - raycast down local Z axis
	if (!QueryParams.bOverrideDefaultParams)
	{
		AActor* Owner = Context->SourceComponent->GetOwner();
		const FTransform& Transform = Owner->GetTransform();

		const FBox LocalBounds = PCGHelpers::GetActorLocalBounds(Owner);
		const FVector RayOrigin = Transform.TransformPosition(FVector(0, 0, LocalBounds.Max.Z));
		const FVector RayEnd = Transform.TransformPosition(FVector(0, 0, LocalBounds.Min.Z));

		const FVector::FReal RayLength = (RayEnd - RayOrigin).Length();
		const FVector RayDirection = (RayLength > UE_SMALL_NUMBER ? (RayEnd - RayOrigin) / RayLength : FVector(0, 0, -1.0));

		QueryParams.RayOrigin = RayOrigin;
		QueryParams.RayDirection = RayDirection;
		QueryParams.RayLength = RayLength;
	}
	else // user provided ray parameters
	{
		const FVector::FReal RayDirectionLength = QueryParams.RayDirection.Length();
		if (RayDirectionLength > UE_SMALL_NUMBER)
		{
			QueryParams.RayDirection = QueryParams.RayDirection / RayDirectionLength;
			QueryParams.RayLength *= RayDirectionLength;
		}
		else
		{
			QueryParams.RayDirection = FVector(0, 0, -1.0);
		}
	}

	check(Context->SourceComponent.IsValid());
	UWorld* World = Context->SourceComponent->GetWorld();
	// TODO: Not strictly required but will be useful until we change the usage of this
	FBox SourceBounds = PCGHelpers::GetActorBounds(Context->SourceComponent->GetOwner());

	UPCGWorldRayHitData* Data = NewObject<UPCGWorldRayHitData>();
	Data->Initialize(World, SourceBounds);
	Data->QueryParams = QueryParams;
	Data->OriginatingComponent = Context->SourceComponent;
	Data->TargetActor = Context->SourceComponent->GetOwner();

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = Data;

	// Pass-through settings & exclusions
	Context->OutputData.TaggedData.Append(Context->InputData.GetAllSettings());

	return true;
}
