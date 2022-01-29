// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/PersistentMeshSelectionManager.h"
#include "Selection/PersistentMeshSelection.h"

#include "InteractiveToolsContext.h"
#include "InteractiveToolManager.h"
#include "ContextObjectStore.h"
#include "Drawing/PreviewGeometryActor.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UPersistentMeshSelectionManager"

void UPersistentMeshSelectionManager::Initialize(TObjectPtr<UInteractiveToolsContext> ToolsContext)
{
	ParentContext = ToolsContext;
}

void UPersistentMeshSelectionManager::Shutdown()
{
	if (SelectionDisplay != nullptr)
	{
		SelectionDisplay->Disconnect();
		SelectionDisplay = nullptr;
	}
}

bool UPersistentMeshSelectionManager::HasActiveSelection()
{
	return (ActiveSelection != nullptr);
}

UPersistentMeshSelection* UPersistentMeshSelectionManager::GetActiveSelection()
{
	return ActiveSelection;
}

void UPersistentMeshSelectionManager::SetNewActiveSelection(UPersistentMeshSelection* Selection)
{
	TUniquePtr<FPersistentMeshSelectionChange> SelectionChange = MakeUnique<FPersistentMeshSelectionChange>();
	if (ActiveSelection != nullptr)
	{
		SelectionChange->From = ActiveSelection->GetSelection();
	}
	if (Selection != nullptr)
	{
		SelectionChange->To = Selection->GetSelection();
	}
	ParentContext->ToolManager->GetContextTransactionsAPI()->AppendChange(this, MoveTemp(SelectionChange),
		LOCTEXT("SelectionChange", "Selection Change"));

	ActiveSelection = Selection;
	OnSelectionModified();
}


void UPersistentMeshSelectionManager::SetNewActiveSelectionInternal(UPersistentMeshSelection* Selection)
{
	ActiveSelection = Selection;
	OnSelectionModified();
}


void UPersistentMeshSelectionManager::ClearActiveSelection()
{
	if (ActiveSelection == nullptr)
	{
		return;
	}

	SetNewActiveSelection(nullptr);
}




void UPersistentMeshSelectionManager::OnSelectionModified()
{
	if (ActiveSelection != nullptr && SelectionDisplay == nullptr)
	{
		SelectionDisplay = NewObject<UPreviewGeometry>(ParentContext);
		SelectionDisplay->CreateInWorld(ActiveSelection->GetTargetComponent()->GetWorld(), FTransform::Identity);
	}
	if (SelectionDisplay == nullptr)
	{
		return;
	}

	const FGenericMeshSelection* SelectionData = (ActiveSelection != nullptr) ? &ActiveSelection->GetSelection() : nullptr;

	bool bShowLines = (SelectionData != nullptr) && (SelectionData->HasRenderableLines());

	if (bShowLines)
	{
		const FColor ROIBorderColor(240, 15, 240);
		const float ROIBorderThickness = 8.0f;
		//const float ROIBorderDepthBias = 0.1f * (float)(WorldBounds.DiagonalLength() * 0.01);
		const float ROIBorderDepthBias = 0.01f;

		FTransform3d Transform(ActiveSelection->GetTargetComponent()->GetComponentToWorld());

		const TArray<UE::Geometry::FSegment3d>& Lines = SelectionData->RenderEdges;
		SelectionDisplay->CreateOrUpdateLineSet(TEXT("SelectionEdges"),  Lines.Num(),
			[&](int32 Index, TArray<FRenderableLine>& LinesOut) 
			{
				const UE::Geometry::FSegment3d& Segment = Lines[Index];
				FVector3d A = Transform.TransformPosition(Segment.StartPoint());
				FVector3d B = Transform.TransformPosition(Segment.EndPoint());
				LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, ROIBorderColor, ROIBorderThickness, ROIBorderDepthBias));
			}, 1);

		SelectionDisplay->SetLineSetVisibility(TEXT("SelectionEdges"), true);
	}
	else
	{
		SelectionDisplay->SetLineSetVisibility(TEXT("SelectionEdges"), false);
	}

}




bool UE::Geometry::RegisterPersistentMeshSelectionManager(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		UPersistentMeshSelectionManager* Found = ToolsContext->ContextObjectStore->FindContext<UPersistentMeshSelectionManager>();
		if (Found == nullptr)
		{
			UPersistentMeshSelectionManager* SelectionManager = NewObject<UPersistentMeshSelectionManager>(ToolsContext->ToolManager);
			if (ensure(SelectionManager))
			{
				SelectionManager->Initialize(ToolsContext);
				ToolsContext->ContextObjectStore->AddContextObject(SelectionManager);
				return true;
			}
			else
			{
				return false;
			}
		}
		return true;
	}
	return false;
}



bool UE::Geometry::DeregisterPersistentMeshSelectionManager(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		UPersistentMeshSelectionManager* Found = ToolsContext->ContextObjectStore->FindContext<UPersistentMeshSelectionManager>();
		if (Found != nullptr)
		{
			Found->Shutdown();
			ToolsContext->ContextObjectStore->RemoveContextObject(Found);
		}
		return true;
	}
	return false;
}


UPersistentMeshSelectionManager* UE::Geometry::FindPersistentMeshSelectionManager(UInteractiveToolManager* ToolManager)
{
	if (ensure(ToolManager))
	{
		UPersistentMeshSelectionManager* Found = ToolManager->GetContextObjectStore()->FindContext<UPersistentMeshSelectionManager>();
		if (Found != nullptr)
		{
			return Found;
		}
	}
	return nullptr;
}




void FPersistentMeshSelectionChange::Apply(UObject* Object)
{
	UPersistentMeshSelectionManager* SelectionManager = Cast<UPersistentMeshSelectionManager>(Object);
	if (SelectionManager)
	{
		UPersistentMeshSelection* NewSelection = NewObject<UPersistentMeshSelection>(SelectionManager);
		NewSelection->SetSelection(To);
		SelectionManager->SetNewActiveSelectionInternal(NewSelection);
	}
}

void FPersistentMeshSelectionChange::Revert(UObject* Object)
{
	UPersistentMeshSelectionManager* SelectionManager = Cast<UPersistentMeshSelectionManager>(Object);
	if (SelectionManager)
	{
		UPersistentMeshSelection* NewSelection = NewObject<UPersistentMeshSelection>(SelectionManager);
		NewSelection->SetSelection(From);
		SelectionManager->SetNewActiveSelectionInternal(NewSelection);
	}
}

bool FPersistentMeshSelectionChange::HasExpired(UObject* Object) const
{
	return false;
}

FString FPersistentMeshSelectionChange::ToString() const
{
	return TEXT("PersistentMeshSelectionChange");
}

#undef LOCTEXT_NAMESPACE