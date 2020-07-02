// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorStaticMeshLibrary.h"

#include "EditorScriptingUtils.h"

#include "ActorEditorUtils.h"
#include "AssetRegistryModule.h"
#include "Components/MeshComponent.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "EngineUtils.h"
#include "Engine/Brush.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "IContentBrowserSingleton.h"
#include "IMeshMergeUtilities.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "LevelEditorViewport.h"
#include "Engine/MapBuildDataRegistry.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "MeshMergeModule.h"
#include "PhysicsEngine/BodySetup.h"
#include "ScopedTransaction.h"
#include "Async/ParallelFor.h"
#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Async/Async.h"
#include "Misc/ScopedSlowTask.h"

#include "UnrealEdGlobals.h"
#include "UnrealEd/Private/GeomFitUtils.h"
#include "UnrealEd/Private/ConvexDecompTool.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "EditorStaticMeshLibrary"

/**
 *
 * Editor Scripting | Dataprep
 *
 **/

FStaticMeshReductionOptions UEditorStaticMeshLibrary::ConvertReductionOptions(const FEditorScriptingMeshReductionOptions_Deprecated& ReductionOptions)
{
	FStaticMeshReductionOptions MeshReductionOptions;

	MeshReductionOptions.bAutoComputeLODScreenSize = ReductionOptions.bAutoComputeLODScreenSize;

	for (int32 i = 0; i < ReductionOptions.ReductionSettings.Num(); i++)
	{
		FStaticMeshReductionSettings ReductionSetting;

		ReductionSetting.PercentTriangles = ReductionOptions.ReductionSettings[i].PercentTriangles;
		ReductionSetting.ScreenSize = ReductionOptions.ReductionSettings[i].ScreenSize;

		MeshReductionOptions.ReductionSettings.Add(ReductionSetting);
	}

	return MeshReductionOptions;
}

// Converts the deprecated EScriptingCollisionShapeType_Deprecated to the new EScriptCollisionShapeType
EScriptCollisionShapeType UEditorStaticMeshLibrary::ConvertCollisionShape(const EScriptingCollisionShapeType_Deprecated& CollisionShape)
{
	switch (CollisionShape)
	{
	case EScriptingCollisionShapeType_Deprecated::Box:
	{
		return EScriptCollisionShapeType::Box;
	}
	case EScriptingCollisionShapeType_Deprecated::Sphere:
	{
		return EScriptCollisionShapeType::Sphere;
	}
	case EScriptingCollisionShapeType_Deprecated::Capsule:
	{
		return EScriptCollisionShapeType::Capsule;
	}
	case EScriptingCollisionShapeType_Deprecated::NDOP10_X:
	{
		return EScriptCollisionShapeType::NDOP10_X;
	}
	case EScriptingCollisionShapeType_Deprecated::NDOP10_Y:
	{
		return EScriptCollisionShapeType::NDOP10_Y;
	}
	case EScriptingCollisionShapeType_Deprecated::NDOP10_Z:
	{
		return EScriptCollisionShapeType::NDOP10_Z;
	}
	case EScriptingCollisionShapeType_Deprecated::NDOP18:
	{
		return EScriptCollisionShapeType::NDOP18;
	}
	case EScriptingCollisionShapeType_Deprecated::NDOP26:
	{
		return EScriptCollisionShapeType::NDOP26;
	}
	default:
		return EScriptCollisionShapeType::Box;
	}
}

int32 UEditorStaticMeshLibrary::SetLodsWithNotification(UStaticMesh* StaticMesh, const FEditorScriptingMeshReductionOptions_Deprecated& ReductionOptions, bool bApplyChanges)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->SetLodsWithNotification(StaticMesh, ConvertReductionOptions(ReductionOptions), bApplyChanges) : -1;
}

int32 UEditorStaticMeshLibrary::SetLodFromStaticMesh(UStaticMesh* DestinationStaticMesh, int32 DestinationLodIndex, UStaticMesh* SourceStaticMesh, int32 SourceLodIndex, bool bReuseExistingMaterialSlots)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->SetLodFromStaticMesh(DestinationStaticMesh, DestinationLodIndex, SourceStaticMesh, SourceLodIndex, bReuseExistingMaterialSlots) : -1;
}

int32 UEditorStaticMeshLibrary::GetLodCount(UStaticMesh* StaticMesh)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->GetLodCount(StaticMesh) : -1;
}

bool UEditorStaticMeshLibrary::RemoveLods(UStaticMesh* StaticMesh)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->RemoveLods(StaticMesh) : false;
}

TArray<float> UEditorStaticMeshLibrary::GetLodScreenSizes(UStaticMesh* StaticMesh)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();
	TArray<float> ScreenSizes;

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->GetLodScreenSizes(StaticMesh) : ScreenSizes;
}

int32 UEditorStaticMeshLibrary::AddSimpleCollisionsWithNotification(UStaticMesh* StaticMesh, const EScriptingCollisionShapeType_Deprecated ShapeType, bool bApplyChanges)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->AddSimpleCollisionsWithNotification(StaticMesh, ConvertCollisionShape(ShapeType), bApplyChanges) : INDEX_NONE;
}

int32 UEditorStaticMeshLibrary::GetSimpleCollisionCount(UStaticMesh* StaticMesh)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->GetSimpleCollisionCount(StaticMesh) : -1;
}

TEnumAsByte<ECollisionTraceFlag> UEditorStaticMeshLibrary::GetCollisionComplexity(UStaticMesh* StaticMesh)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	if (StaticMeshEditorSubsystem)
	{
		return StaticMeshEditorSubsystem->GetCollisionComplexity(StaticMesh);
	}
	return ECollisionTraceFlag::CTF_UseDefault;
}

int32 UEditorStaticMeshLibrary::GetConvexCollisionCount(UStaticMesh* StaticMesh)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->GetConvexCollisionCount(StaticMesh) : -1;
}

bool UEditorStaticMeshLibrary::BulkSetConvexDecompositionCollisionsWithNotification(const TArray<UStaticMesh*>& InStaticMeshes, int32 HullCount, int32 MaxHullVerts, int32 HullPrecision, bool bApplyChanges)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->BulkSetConvexDecompositionCollisionsWithNotification(InStaticMeshes, HullCount, MaxHullVerts, HullPrecision, bApplyChanges) : false;
}

bool UEditorStaticMeshLibrary::SetConvexDecompositionCollisionsWithNotification(UStaticMesh* StaticMesh, int32 HullCount, int32 MaxHullVerts, int32 HullPrecision, bool bApplyChanges)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->SetConvexDecompositionCollisionsWithNotification(StaticMesh, HullCount, MaxHullVerts, HullPrecision, bApplyChanges) : false;
}

bool UEditorStaticMeshLibrary::RemoveCollisionsWithNotification(UStaticMesh* StaticMesh, bool bApplyChanges)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->RemoveCollisionsWithNotification(StaticMesh, bApplyChanges) : false;
}

void UEditorStaticMeshLibrary::EnableSectionCollision(UStaticMesh* StaticMesh, bool bCollisionEnabled, int32 LODIndex, int32 SectionIndex)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	if (StaticMeshEditorSubsystem)
	{
		return StaticMeshEditorSubsystem->EnableSectionCollision(StaticMesh, bCollisionEnabled, LODIndex, SectionIndex);
	}
}

bool UEditorStaticMeshLibrary::IsSectionCollisionEnabled(UStaticMesh* StaticMesh, int32 LODIndex, int32 SectionIndex)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->IsSectionCollisionEnabled(StaticMesh, LODIndex, SectionIndex) : false;
}

void UEditorStaticMeshLibrary::EnableSectionCastShadow(UStaticMesh* StaticMesh, bool bCastShadow, int32 LODIndex, int32 SectionIndex)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	if (StaticMeshEditorSubsystem)
	{
		return StaticMeshEditorSubsystem->EnableSectionCastShadow(StaticMesh, bCastShadow, LODIndex, SectionIndex);
	}
}

bool UEditorStaticMeshLibrary::HasVertexColors(UStaticMesh* StaticMesh)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->HasVertexColors(StaticMesh) : false;
}

bool UEditorStaticMeshLibrary::HasInstanceVertexColors(UStaticMeshComponent* StaticMeshComponent)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->HasInstanceVertexColors(StaticMeshComponent) : false;
}

bool UEditorStaticMeshLibrary::SetGenerateLightmapUVs(UStaticMesh* StaticMesh, bool bGenerateLightmapUVs)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->SetGenerateLightmapUVs(StaticMesh, bGenerateLightmapUVs) : false;
}

int32 UEditorStaticMeshLibrary::GetNumberVerts(UStaticMesh* StaticMesh, int32 LODIndex)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->GetNumberVerts(StaticMesh, LODIndex) : 0;
}

int32 UEditorStaticMeshLibrary::GetNumberMaterials(UStaticMesh* StaticMesh)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->GetNumberMaterials(StaticMesh) : 0;
}

void UEditorStaticMeshLibrary::SetAllowCPUAccess(UStaticMesh* StaticMesh, bool bAllowCPUAccess)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	if (StaticMeshEditorSubsystem)
	{
		return StaticMeshEditorSubsystem->SetAllowCPUAccess(StaticMesh, bAllowCPUAccess);
	}
}

int32 UEditorStaticMeshLibrary::GetNumUVChannels(UStaticMesh* StaticMesh, int32 LODIndex)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->GetNumUVChannels(StaticMesh, LODIndex) : 0;
}

bool UEditorStaticMeshLibrary::AddUVChannel(UStaticMesh* StaticMesh, int32 LODIndex)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->AddUVChannel(StaticMesh, LODIndex) : false;
}

bool UEditorStaticMeshLibrary::InsertUVChannel(UStaticMesh* StaticMesh, int32 LODIndex, int32 UVChannelIndex)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->InsertUVChannel(StaticMesh, LODIndex, UVChannelIndex) : false;
}

bool UEditorStaticMeshLibrary::RemoveUVChannel(UStaticMesh* StaticMesh, int32 LODIndex, int32 UVChannelIndex)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->RemoveUVChannel(StaticMesh, LODIndex, UVChannelIndex) : false;
}

bool UEditorStaticMeshLibrary::GeneratePlanarUVChannel(UStaticMesh* StaticMesh, int32 LODIndex, int32 UVChannelIndex, const FVector& Position, const FRotator& Orientation, const FVector2D& Tiling)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->GeneratePlanarUVChannel(StaticMesh, LODIndex, UVChannelIndex, Position, Orientation, Tiling) : false;
}

bool UEditorStaticMeshLibrary::GenerateCylindricalUVChannel(UStaticMesh* StaticMesh, int32 LODIndex, int32 UVChannelIndex, const FVector& Position, const FRotator& Orientation, const FVector2D& Tiling)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->GenerateCylindricalUVChannel(StaticMesh, LODIndex, UVChannelIndex, Position, Orientation, Tiling) : false;
}

bool UEditorStaticMeshLibrary::GenerateBoxUVChannel(UStaticMesh* StaticMesh, int32 LODIndex, int32 UVChannelIndex, const FVector& Position, const FRotator& Orientation, const FVector& Size)
{
	UStaticMeshEditorSubsystem* StaticMeshEditorSubsystem = GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();

	return StaticMeshEditorSubsystem ? StaticMeshEditorSubsystem->GenerateBoxUVChannel(StaticMesh, LODIndex, UVChannelIndex, Position, Orientation, Size) : false;
}

#undef LOCTEXT_NAMESPACE
