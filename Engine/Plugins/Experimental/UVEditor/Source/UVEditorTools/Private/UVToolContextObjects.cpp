// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVToolContextObjects.h"

#include "DynamicMesh/MeshIndexUtil.h"
#include "Engine/World.h"
#include "InputRouter.h" // Need to define this and UWorld so weak pointers know they are UThings
#include "InteractiveToolManager.h"
#include "ToolTargets/UVEditorToolMeshInput.h"

using namespace UE::Geometry;

namespace UVToolContextObjectLocals
{

	/**
	 * A wrapper change that applies a given change to the unwrap canonical mesh of an input, uses that to update the
	 * other views, and issues an OnUndoRedo broadcast.
	 */
	class  FUVEditorMeshChange : public FToolCommandChange
	{
	public:
		FUVEditorMeshChange(UUVEditorToolMeshInput* UVToolInputObjectIn, TUniquePtr<FDynamicMeshChange> UnwrapCanonicalMeshChangeIn)
			: UVToolInputObject(UVToolInputObjectIn)
			, UnwrapCanonicalMeshChange(MoveTemp(UnwrapCanonicalMeshChangeIn))
		{
			ensure(UVToolInputObjectIn);
			ensure(UnwrapCanonicalMeshChange);
		};

		virtual void Apply(UObject* Object) override
		{
			UnwrapCanonicalMeshChange->Apply(UVToolInputObject->UnwrapCanonical.Get(), false);
			UVToolInputObject->UpdateFromCanonicalUnwrapUsingMeshChange(*UnwrapCanonicalMeshChange);
			UVToolInputObject->OnUndoRedo.Broadcast(false);
		}

		virtual void Revert(UObject* Object) override
		{
			UnwrapCanonicalMeshChange->Apply(UVToolInputObject->UnwrapCanonical.Get(), true);
			UVToolInputObject->UpdateFromCanonicalUnwrapUsingMeshChange(*UnwrapCanonicalMeshChange);
			UVToolInputObject->OnUndoRedo.Broadcast(true);
		}

		virtual bool HasExpired(UObject* Object) const override
		{
			return !(UVToolInputObject.IsValid() && UnwrapCanonicalMeshChange);
		}


		virtual FString ToString() const override
		{
			return TEXT("FUVEditorMeshChange");
		}

	protected:
		TWeakObjectPtr<UUVEditorToolMeshInput> UVToolInputObject;
		TUniquePtr<FDynamicMeshChange> UnwrapCanonicalMeshChange;
	};
}

void UUVToolEmitChangeAPI::EmitToolIndependentChange(UObject* TargetObject, TUniquePtr<FToolCommandChange> Change, const FText& Description)
{
	ToolManager->GetContextTransactionsAPI()->AppendChange(TargetObject, MoveTemp(Change), Description);
}

void UUVToolEmitChangeAPI::EmitToolIndependentUnwrapCanonicalChange(UUVEditorToolMeshInput* InputObject, 
	TUniquePtr<FDynamicMeshChange> UnwrapCanonicalMeshChange, const FText& Description)
{
	ToolManager->GetContextTransactionsAPI()->AppendChange(InputObject, 
		MakeUnique<UVToolContextObjectLocals::FUVEditorMeshChange>(InputObject, MoveTemp(UnwrapCanonicalMeshChange)), Description);
}

void UUVToolEmitChangeAPI::EmitToolDependentChange(UObject* TargetObject, TUniquePtr<FToolCommandChange> Change, const FText& Description)
{
	// This should wrap the change in the proper wrapper that will expire it when the tool changes
	ToolManager->EmitObjectChange(TargetObject, MoveTemp(Change), Description);
}

void UUVToolLivePreviewAPI::Initialize(UWorld* WorldIn, UInputRouter* RouterIn)
{
	World = WorldIn;
	InputRouter = RouterIn;
}

void UUVToolAABBTreeStorage::Set(FDynamicMesh3* MeshKey, TSharedPtr<FDynamicMeshAABBTree3> Tree)
{
	AABBTrees.Add(MeshKey, Tree);
}

TSharedPtr<FDynamicMeshAABBTree3> UUVToolAABBTreeStorage::Get(FDynamicMesh3* MeshKey)
{
	TSharedPtr<FDynamicMeshAABBTree3>* FoundPtr = AABBTrees.Find(MeshKey);
	return FoundPtr ? *FoundPtr : nullptr;
}

void UUVToolAABBTreeStorage::Remove(FDynamicMesh3* MeshKey)
{
	AABBTrees.Remove(MeshKey);
}

void UUVToolAABBTreeStorage::RemoveByPredicate(TUniqueFunction<
	bool(const TPair<FDynamicMesh3*, TSharedPtr<FDynamicMeshAABBTree3>>&)> Predicate)
{
	for (auto It = AABBTrees.CreateIterator(); It; ++It)
	{
		if (Predicate(*It))
		{
			It.RemoveCurrent();
		}
	}
}

void UUVToolAABBTreeStorage::Empty()
{
	AABBTrees.Empty();
}