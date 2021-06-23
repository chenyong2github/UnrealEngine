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

	ElementsFromDrag.Empty();
	GetToolManager()->BeginUndoTransaction(NSLOCTEXT("AssetPlacementEdMode", "BrushSelect", "Select Elements"));
}

void UPlacementModeLassoSelectTool::OnEndDrag(const FRay& Ray)
{
	if (IAssetEditorContextInterface* AssetEditorContext = GetToolManager()->GetContextObjectStore()->FindContext<IAssetEditorContextInterface>())
	{
		if (UTypedElementSelectionSet* SelectionSet = AssetEditorContext->GetMutableSelectionSet())
		{
			bool bSelectElements = !bCtrlToggle;
			for (const FTypedElementHandle& HitElement : ElementsFromDrag)
			{
				if (!FoliageElementUtil::FoliageInstanceElementsEnabled())
				{
					if (TTypedElement<UTypedElementObjectInterface> ObjectInterface = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementObjectInterface>(HitElement))
					{
						if (AInstancedFoliageActor* FoliageActor = ObjectInterface.GetObjectAs<AInstancedFoliageActor>())
						{
							FoliageActor->ForEachFoliageInfo([this, bSelectElements](UFoliageType* InFoliageType, FFoliageInfo& InFoliageInfo)
								{
									FTypedElementHandle SourceObjectHandle = UEngineElementsLibrary::AcquireEditorObjectElementHandle(InFoliageType->GetSource());
									if (GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->DoesCurrentPaletteSupportElement(SourceObjectHandle))
									{
										TArray<int32> Instances;
										FSphere SphereToCheck(LastBrushStamp.WorldPosition, LastBrushStamp.Radius);
										InFoliageInfo.GetInstancesInsideSphere(SphereToCheck, Instances);
										InFoliageInfo.SelectInstances(bSelectElements, Instances);
									}
									return true;
								});

							continue;
						}
					}
				}

				if (bSelectElements)
				{
					SelectionSet->SelectElement(HitElement, PlacementModeLassoToolInternal::SelectionOptions);
				}
				else
				{
					SelectionSet->DeselectElement(HitElement, PlacementModeLassoToolInternal::SelectionOptions);
				}
			}
		}
	}

	GetToolManager()->EndUndoTransaction();

	Super::OnEndDrag(Ray);
}

void UPlacementModeLassoSelectTool::OnTick(float DeltaTime)
{
	if (!bInBrushStroke)
	{
		return;
	}

	ElementsFromDrag.Append(GetElementsInBrushRadius(LastDeviceInputRay));
}
