// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementLassoSelectTool.h"

#include "AssetPlacementEdMode.h"
#include "AssetPlacementSettings.h"
#include "Editor.h"
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
#include "Modes/PlacementModeSubsystem.h"
#include "Tools/AssetEditorContextInterface.h"
#include "EditorModeManager.h"
#include "ContextObjectStore.h"

constexpr TCHAR UPlacementModeLassoSelectTool::ToolName[];

namespace PlacementModeLassoToolInternal
{
	FTypedElementSelectionOptions SelectionOptions {};
}

UPlacementBrushToolBase* UPlacementModeLassoSelectToolBuilder::FactoryToolInstance(UObject* Outer) const
{
	return NewObject<UPlacementModeLassoSelectTool>(Outer);
}

void UPlacementModeLassoSelectTool::OnBeginDrag(const FRay& Ray)
{
	Super::OnBeginDrag(Ray);

	ElementsFromDrag.Reset();
	GetToolManager()->BeginUndoTransaction(NSLOCTEXT("AssetPlacementEdMode", "BrushSelect", "Select Elements"));
}

void UPlacementModeLassoSelectTool::OnEndDrag(const FRay& Ray)
{
	if (IAssetEditorContextInterface* AssetEditorContext = GetToolManager()->GetContextObjectStore()->FindContext<IAssetEditorContextInterface>())
	{
		UTypedElementSelectionSet* SelectionSet = AssetEditorContext->GetMutableSelectionSet();
		if (SelectionSet && ElementsFromDrag)
		{
			bool bSelectElements = !bCtrlToggle;

			if (!FoliageElementUtil::FoliageInstanceElementsEnabled())
			{
				ElementsFromDrag->RemoveAll<UTypedElementObjectInterface>([this, bSelectElements](const TTypedElement<UTypedElementObjectInterface>& ObjectInterface)
				{
					// Since the foliage static mesh instances do not currently operate with element handles, we have to drill in manually here.
					if (AInstancedFoliageActor* FoliageActor = ObjectInterface.GetObjectAs<AInstancedFoliageActor>())
					{
						FoliageActor->ForEachFoliageInfo([this, bSelectElements](UFoliageType* FoliageType, FFoliageInfo& FoliageInfo)
						{
							FTypedElementHandle SourceObjectHandle = UEngineElementsLibrary::AcquireEditorObjectElementHandle(FoliageType->GetSource());
							if (GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->DoesCurrentPaletteSupportElement(SourceObjectHandle))
							{
								TArray<int32> Instances;
								FSphere SphereToCheck(LastBrushStamp.WorldPosition, LastBrushStamp.Radius);
								FoliageInfo.GetInstancesInsideSphere(SphereToCheck, Instances);
								FoliageInfo.SelectInstances(bSelectElements, Instances);
							}
							return true; // continue iteration
						});
						return true; // Foliage - remove from the normal element select
					}
					return false; // Not foliage - will be processed via the normal element select
				});
			}

			if (bSelectElements)
			{
				SelectionSet->SelectElements(ElementsFromDrag.ToSharedRef(), PlacementModeLassoToolInternal::SelectionOptions);
			}
			else
			{
				SelectionSet->DeselectElements(ElementsFromDrag.ToSharedRef(), PlacementModeLassoToolInternal::SelectionOptions);
			}
		}
	}

	GetToolManager()->EndUndoTransaction();
	ElementsFromDrag.Reset();

	Super::OnEndDrag(Ray);
}

void UPlacementModeLassoSelectTool::OnTick(float DeltaTime)
{
	if (!bInBrushStroke)
	{
		return;
	}

	FTypedElementListRef HitElements = GetElementsInBrushRadius(LastDeviceInputRay);
	if (ElementsFromDrag)
	{
		ElementsFromDrag->Append(HitElements);
	}
	else
	{
		ElementsFromDrag = HitElements;
	}
}
