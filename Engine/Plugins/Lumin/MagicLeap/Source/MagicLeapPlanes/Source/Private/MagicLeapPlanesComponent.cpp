// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapPlanesComponent.h"
#include "MagicLeapPlanesModule.h"
#include "Components/BoxComponent.h"

UMagicLeapPlanesComponent::UMagicLeapPlanesComponent()
: QueryFlags({ EMagicLeapPlaneQueryFlags::Vertical, EMagicLeapPlaneQueryFlags::Horizontal, EMagicLeapPlaneQueryFlags::Arbitrary, EMagicLeapPlaneQueryFlags::PreferInner })
, MaxResults(10)
, MinHolePerimeter(50.0f)
, MinPlaneArea(400.0f)
, QueryType(EMagicLeapPlaneQueryType::Bulk)
, SimilarityThreshold(1.0f)
, CurrentQueryType(QueryType)
{
	bAutoActivate = true;
	SearchVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("SearchVolume"));
	SearchVolume->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	SearchVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SearchVolume->SetCanEverAffectNavigation(false);
	SearchVolume->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
	SearchVolume->SetCollisionObjectType(ECollisionChannel::ECC_WorldDynamic);
	SearchVolume->SetGenerateOverlapEvents(false);
	// Recommended default box extents for meshing - 10m (5m radius)
	SearchVolume->SetBoxExtent(FVector(1000, 1000, 1000), false);
}

void UMagicLeapPlanesComponent::BeginPlay()
{
	Super::BeginPlay();
	GetMagicLeapPlanesModule().CreateTracker();

	// A handle is only selectively needed
	if (QueryType == EMagicLeapPlaneQueryType::Delta)
	{

		QueryHandle = GetMagicLeapPlanesModule().AddQuery(QueryType);

	}

	CurrentQueryType = QueryType;

}

void UMagicLeapPlanesComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetMagicLeapPlanesModule().DestroyTracker();
	Super::EndPlay(EndPlayReason);
}

bool UMagicLeapPlanesComponent::RequestPlanesAsync()
{
	
	if(QueryType != CurrentQueryType)
	{
		
		GetMagicLeapPlanesModule().RemoveQuery(QueryHandle);

		// A handle is only selectively needed
		if (QueryType == EMagicLeapPlaneQueryType::Delta)
		{

			QueryHandle = GetMagicLeapPlanesModule().AddQuery(QueryType);

		}

		CurrentQueryType = QueryType;
		
	}
		
	FMagicLeapPlanesQuery QueryParams;
	QueryParams.Flags = QueryFlags;
	QueryParams.MaxResults = MaxResults;
	QueryParams.MinHoleLength = MinHolePerimeter;
	QueryParams.MinPlaneArea = MinPlaneArea;
	QueryParams.SearchVolumePosition = SearchVolume->GetComponentLocation();
	QueryParams.SearchVolumeOrientation = SearchVolume->GetComponentQuat();
	QueryParams.SearchVolumeExtents = SearchVolume->GetScaledBoxExtent();
	QueryParams.SimilarityThreshold = SimilarityThreshold;

	if(QueryType == EMagicLeapPlaneQueryType::Bulk)
	{
		return GetMagicLeapPlanesModule().QueryBeginAsync(
			QueryParams,
			OnPlanesQueryResult);
	}
	
	return GetMagicLeapPlanesModule().PersistentQueryBeginAsync(
		QueryParams,
		QueryHandle,
		OnPersistentPlanesQueryResult);		
	
	
}
