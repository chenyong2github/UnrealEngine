// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnvQueryTest_InsideWaterBody.h"
#include "UObject/UObjectHash.h"
#include "GameFramework/Volume.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "WaterBodyActor.h"

UEnvQueryTest_InsideWaterBody::UEnvQueryTest_InsideWaterBody(const FObjectInitializer& ObjectInitializer) 
	: Super(ObjectInitializer)
{
	Cost = EEnvTestCost::High;
	ValidItemType = UEnvQueryItemType_VectorBase::StaticClass();
	SetWorkOnFloatValues(false);
}

void UEnvQueryTest_InsideWaterBody::RunTest(FEnvQueryInstance& QueryInstance) const
{	
	BoolValue.BindData(QueryInstance.Owner.Get(), QueryInstance.QueryID);
	bool bWantsInside = BoolValue.GetValue();

	TArray<UObject*> WaterBodies;
	GetObjectsOfClass(AWaterBody::StaticClass(), WaterBodies, true, RF_ClassDefaultObject, EInternalObjectFlags::PendingKill);

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FVector ItemLocation = GetItemLocation(QueryInstance, It.GetIndex());

		bool bInside = false;
		for (UObject* ObjectIter : WaterBodies)
		{
			const AWaterBody& WaterBody = *CastChecked<AWaterBody>(ObjectIter);
			EWaterBodyQueryFlags QueryFlags = EWaterBodyQueryFlags::ComputeImmersionDepth;
			if (bIncludeWaves)
			{
				QueryFlags |= EWaterBodyQueryFlags::IncludeWaves;

				if (bSimpleWaves)
				{
					QueryFlags |= EWaterBodyQueryFlags::SimpleWaves;
				}
			}
			
			if (bIgnoreExclusionVolumes)
			{
				QueryFlags |= EWaterBodyQueryFlags::IgnoreExclusionVolumes;
			}
			FWaterBodyQueryResult QueryResult = WaterBody.QueryWaterInfoClosestToWorldLocation(ItemLocation, QueryFlags);
			bInside = QueryResult.IsInWater();
		}

		It.SetScore(TestPurpose, FilterType, bInside, bWantsInside);
	}
}

FText UEnvQueryTest_InsideWaterBody::GetDescriptionDetails() const
{
	return DescribeBoolTestParams("inside water body");
}
