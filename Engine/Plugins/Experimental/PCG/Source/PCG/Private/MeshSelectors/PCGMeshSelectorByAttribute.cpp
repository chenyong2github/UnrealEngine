// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshSelectors/PCGMeshSelectorByAttribute.h"

#include "Data/PCGPointData.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Data/PCGSpatialData.h"
#include "MeshSelectors/PCGMeshSelectorBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGMeshSelectorByAttribute)

void UPCGMeshSelectorByAttribute::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (bOverrideMaterials_DEPRECATED)
	{
		MaterialOverrideMode = EPCGMeshSelectorMaterialOverrideMode::StaticOverride;
		bOverrideMaterials_DEPRECATED = false;
	}
#endif
}

void UPCGMeshSelectorByAttribute::SelectInstances_Implementation(
	FPCGContext& Context, 
	const UPCGStaticMeshSpawnerSettings* Settings, 
	const UPCGPointData* InPointData,
	TArray<FPCGMeshInstanceList>& OutMeshInstances,
	UPCGPointData* OutPointData) const
{
	if (!InPointData)
	{
		PCGE_LOG_C(Error, &Context, "Missing input data");
		return;
	}

	if (!InPointData->Metadata)
	{
		PCGE_LOG_C(Error, &Context, "Unable to get metadata from input");
		return;
	}

	if (!InPointData->Metadata->HasAttribute(AttributeName))
	{
		PCGE_LOG_C(Error, &Context, "Attribute %s is not in the metadata", *AttributeName.ToString());
		return;
	}

	const FPCGMetadataAttributeBase* AttributeBase = InPointData->Metadata->GetConstAttribute(AttributeName);
	check(AttributeBase);

	if (AttributeBase->GetTypeId() != PCG::Private::MetadataTypes<FString>::Id)
	{
		PCGE_LOG_C(Error, &Context, "Attribute is not of valid type FString");
		return;
	}

	const FPCGMetadataAttribute<FString>* Attribute = static_cast<const FPCGMetadataAttribute<FString>*>(AttributeBase);

	FPCGMeshMaterialOverrideHelper MaterialOverrideHelper(Context, MaterialOverrideMode, MaterialOverrides, MaterialOverrideAttributes, InPointData->Metadata);

	if (!MaterialOverrideHelper.IsValid())
	{
		return;
	}

	TMap<PCGMetadataValueKey, TSoftObjectPtr<UStaticMesh>> ValueKeyToMesh;

	// ByAttribute takes in SoftObjectPaths per point in the metadata, so we can pass those directly into the outgoing pin if it exists
	if (OutPointData)
	{
		OutPointData->SetPoints(InPointData->GetPoints());
		OutPointData->Metadata->DeleteAttribute(Settings->OutAttributeName);
		OutPointData->Metadata->CopyAttribute(InPointData->Metadata, AttributeName, Settings->OutAttributeName);
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::Execute::SelectEntries);

	// Assign points to entries
	for (const FPCGPoint& Point : InPointData->GetPoints())
	{
		if (Point.Density <= 0.0f)
		{
			continue;
		}

		PCGMetadataValueKey ValueKey = Attribute->GetValueKey(Point.MetadataEntry);
		TSoftObjectPtr<UStaticMesh>* NewMesh = ValueKeyToMesh.Find(ValueKey);
		TSoftObjectPtr<UStaticMesh> Mesh = nullptr;

		// if this ValueKey has not been seen before, let's cache it for the future
		if (!NewMesh)
		{
			FString MeshSoftObjectPath = Attribute->GetValue(ValueKey);

			if (!MeshSoftObjectPath.IsEmpty() && MeshSoftObjectPath != TEXT("None"))
			{
				FSoftObjectPath MeshPath(MeshSoftObjectPath);
				Mesh = TSoftObjectPtr<UStaticMesh>(MeshPath);

				if (Mesh.IsNull())
				{
					PCGE_LOG_C(Error, &Context, "Invalid mesh path: %s.", *MeshSoftObjectPath);
				}
			}
			else
			{
				PCGE_LOG_C(Warning, &Context, "Trivially invalid mesh path used: %s", *MeshSoftObjectPath);
			}

			ValueKeyToMesh.Add(ValueKey, Mesh);
		}
		else
		{
			Mesh = *NewMesh;
		}

		if (Mesh.IsNull())
		{
			continue;
		}

		const bool bReverseTransform = (Point.Transform.GetDeterminant() < 0);
		const int32 Index = FindOrAddInstanceList(OutMeshInstances, Mesh, bOverrideCollisionProfile, CollisionProfile, MaterialOverrideHelper.OverridesMaterials(), MaterialOverrideHelper.GetMaterialOverrides(Point.MetadataEntry), CullStartDistance, CullEndDistance, WorldPositionOffsetDisableDistance, bReverseTransform);
		check(Index != INDEX_NONE);
		
		OutMeshInstances[Index].Instances.Emplace(Point);
	}
}
