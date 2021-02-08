// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementLassoSelectTool.h"

#include "AssetPlacementEdMode.h"
#include "AssetPlacementSettings.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"
#include "InteractiveToolManager.h"
#include "InstancedFoliageActor.h"
#include "ToolContextInterfaces.h"
#include "UObject/Object.h"
#include "BaseBehaviors/KeyAsModifierInputBehavior.h"
#include "Elements/Framework/TypedElementSelectionSet.h"

constexpr TCHAR UPlacementModeLassoSelectTool::ToolName[];

UPlacementBrushToolBase* UPlacementModeLassoSelectToolBuilder::FactoryToolInstance(UObject* Outer) const
{
	return NewObject<UPlacementModeLassoSelectTool>(Outer);
}

void UPlacementModeLassoSelectTool::OnBeginDrag(const FRay& Ray)
{
	Super::OnBeginDrag(Ray);

	GetToolManager()->BeginUndoTransaction(NSLOCTEXT("AssetPlacementEdMode", "BrushSelect", "Select Elements"));
}

void UPlacementModeLassoSelectTool::OnEndDrag(const FRay& Ray)
{
	GetToolManager()->EndUndoTransaction();

	Super::OnEndDrag(Ray);
}

void UPlacementModeLassoSelectTool::OnTick(float DeltaTime)
{
	if (!bInBrushStroke)
	{
		return;
	}

	bool bSelectElements = !bCtrlToggle;

	TArray<FTypedElementHandle> HitElements = GetElementsInBrushRadius();
	for (const FTypedElementHandle& HitElement : HitElements)
	{
		// Todo: Replace direct foliage instance selection with typed element selection
		if (TTypedElement<UTypedElementObjectInterface> ObjectInterface = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementObjectInterface>(HitElement))
		{
			if (AInstancedFoliageActor* FoliageActor = ObjectInterface.GetObjectAs<AInstancedFoliageActor>())
			{
				FoliageActor->ForEachFoliageInfo([this, bSelectElements](UFoliageType* InFoliageType, FFoliageInfo& InFoliageInfo)
				{
					FTypedElementHandle SourceObjectHandle = UEngineElementsLibrary::AcquireEditorObjectElementHandle(InFoliageType->GetSource());
					if (UAssetPlacementEdMode::DoesPaletteSupportElement(SourceObjectHandle, PlacementSettings->PaletteItems))
					{
						TArray<int32> Instances;
						FSphere SphereToCheck(LastBrushStamp.WorldPosition, LastBrushStamp.Radius);
						InFoliageInfo.GetInstancesInsideSphere(SphereToCheck, Instances);
						InFoliageInfo.SelectInstances(bSelectElements, Instances);
					}
					return true;
				});
			}
		}

		FToolBuilderState SelectionState;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentSelectionState(SelectionState);
		if (SelectionState.TypedElementSelectionSet.IsValid())
		{
			if (bSelectElements)
			{
				SelectionState.TypedElementSelectionSet->SelectElement(HitElement, FTypedElementSelectionOptions());
			}
			else
			{
				SelectionState.TypedElementSelectionSet->DeselectElement(HitElement, FTypedElementSelectionOptions());
			}
		}
	}
}
