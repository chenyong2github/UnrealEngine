// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementEraseTool.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Actor/ActorElementData.h"
#include "Editor.h"
#include "InstancedFoliageActor.h"
#include "AssetPlacementEdMode.h"
#include "AssetPlacementSettings.h"

constexpr TCHAR UPlacementModeEraseTool::ToolName[];

UPlacementBrushToolBase* UPlacementModeEraseToolBuilder::FactoryToolInstance(UObject* Outer) const
{
	return NewObject<UPlacementModeEraseTool>(Outer);
}

void UPlacementModeEraseTool::OnBeginDrag(const FRay& Ray)
{
	Super::OnBeginDrag(Ray);

	GetToolManager()->BeginUndoTransaction(NSLOCTEXT("AssetPlacementEdMode", "BrushErase", "Erase Painted Elements"));
}

void UPlacementModeEraseTool::OnEndDrag(const FRay& Ray)
{
	GetToolManager()->EndUndoTransaction();

	Super::OnEndDrag(Ray);
}

void UPlacementModeEraseTool::OnTick(float DeltaTime)
{
	if (!bInBrushStroke)
	{
		return;
	}

	UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	TArray<FTypedElementHandle> HitElements = GetElementsInBrushRadius();
	for (const FTypedElementHandle& HitElement : HitElements)
	{
		const FActorElementData* ActorData = HitElement.GetData<FActorElementData>();
		if (ActorData && ActorData->Actor)
		{
			// Since the foliage static mesh instances do not currently operate with element handles, we have to drill in manually here.
			if (AInstancedFoliageActor* FoliageActor = Cast<AInstancedFoliageActor>(ActorData->Actor))
			{
				for (auto& FoliageInfo : FoliageActor->FoliageInfos)
				{
					FTypedElementHandle SourceObjectHandle = UEngineElementsLibrary::AcquireEditorObjectElementHandle(FoliageInfo.Key->GetSource());
					if (UAssetPlacementEdMode::DoesPaletteSupportElement(SourceObjectHandle, PlacementSettings->PaletteItems))
					{
						TArray<int32> Instances;
						FSphere SphereToCheck(LastBrushStamp.WorldPosition, LastBrushStamp.Radius);
						FoliageInfo.Value->GetInstancesInsideSphere(SphereToCheck, Instances);
						FoliageInfo.Value->RemoveInstances(FoliageActor, Instances, true);
					}
				}
			}
			else if (ActorSubsystem)
			{
				ActorSubsystem->DestroyActor(ActorData->Actor);
			}
		}
	}
}
