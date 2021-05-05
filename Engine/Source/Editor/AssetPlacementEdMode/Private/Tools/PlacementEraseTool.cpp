// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementEraseTool.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementCommonActions.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"
#include "Tools/AssetEditorContextInterface.h"
#include "Editor.h"
#include "InstancedFoliageActor.h"
#include "AssetPlacementEdMode.h"
#include "AssetPlacementSettings.h"
#include "Modes/PlacementModeSubsystem.h"
#include "Toolkits/IToolkitHost.h"
#include "UObject/GCObjectScopeGuard.h"
#include "EditorModeManager.h"
#include "ContextObjectStore.h"

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

	IAssetEditorContextInterface* AssetEditorContext = GetToolManager()->GetContextObjectStore()->FindContext<IAssetEditorContextInterface>();
	if (!AssetEditorContext)
	{
		return;
	}

	UTypedElementCommonActions* ElementCommonActions = AssetEditorContext->GetCommonActions();
	if (!ElementCommonActions)
	{
		return;
	}

	TGCObjectScopeGuard<UTypedElementList> ElementsToDelete(UTypedElementRegistry::GetInstance()->CreateElementList());

	TArray<FTypedElementHandle> HitElements = GetElementsInBrushRadius();
	for (const FTypedElementHandle& HitElement : HitElements)
	{
		if (TTypedElement<UTypedElementObjectInterface> ObjectInterface = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementObjectInterface>(HitElement))
		{
			// Since the foliage static mesh instances do not currently operate with element handles, we have to drill in manually here.
			if (AInstancedFoliageActor* FoliageActor = ObjectInterface.GetObjectAs<AInstancedFoliageActor>())
			{
				FoliageActor->ForEachFoliageInfo([this](UFoliageType* FoliageType, FFoliageInfo& FoliageInfo)
				{
					FTypedElementHandle SourceObjectHandle = UEngineElementsLibrary::AcquireEditorObjectElementHandle(FoliageType->GetSource());
					if (GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->DoesCurrentPaletteSupportElement(SourceObjectHandle))
					{
						TArray<int32> Instances;
						FSphere SphereToCheck(LastBrushStamp.WorldPosition, LastBrushStamp.Radius);
						FoliageInfo.GetInstancesInsideSphere(SphereToCheck, Instances);
						FoliageInfo.RemoveInstances(Instances, true);
					}
					return true; // continue iteration
				});
			}
			else
			{
				ElementsToDelete.Get()->Add(HitElement);
			}
		}
	}

	if (ElementsToDelete.Get()->HasElements())
	{
		UTypedElementSelectionSet* SelectionSet = AssetEditorContext->GetMutableSelectionSet();
		if (SelectionSet)
		{
			ElementCommonActions->DeleteElementsInList(ElementsToDelete.Get(), AssetEditorContext->GetEditingWorld(), SelectionSet, FTypedElementDeletionOptions());
		}
	}
}
