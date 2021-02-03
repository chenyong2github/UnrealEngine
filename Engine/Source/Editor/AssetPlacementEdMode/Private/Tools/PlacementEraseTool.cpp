// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementEraseTool.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"
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
		if (TTypedElement<UTypedElementObjectInterface> ObjectInterface = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementObjectInterface>(HitElement))
		{
			AActor* Actor = ObjectInterface.GetObjectAs<AActor>();
			if (!Actor)
			{
				continue;
			}

			// Since the foliage static mesh instances do not currently operate with element handles, we have to drill in manually here.
			if (AInstancedFoliageActor* FoliageActor = Cast<AInstancedFoliageActor>(Actor))
			{
				FoliageActor->ForEachFoliageInfo([this](UFoliageType* FoliageType, FFoliageInfo& FoliageInfo)
				{
					FTypedElementHandle SourceObjectHandle = UEngineElementsLibrary::AcquireEditorObjectElementHandle(FoliageType->GetSource());
					if (UAssetPlacementEdMode::DoesPaletteSupportElement(SourceObjectHandle, PlacementSettings->PaletteItems))
					{
						TArray<int32> Instances;
						FSphere SphereToCheck(LastBrushStamp.WorldPosition, LastBrushStamp.Radius);
						FoliageInfo.GetInstancesInsideSphere(SphereToCheck, Instances);
						FoliageInfo.RemoveInstances(Instances, true);
					}
					return true; // continue iteraton
				});
			}
			else if (ActorSubsystem)
			{
				ActorSubsystem->DestroyActor(Actor);
			}
		}
	}
}
