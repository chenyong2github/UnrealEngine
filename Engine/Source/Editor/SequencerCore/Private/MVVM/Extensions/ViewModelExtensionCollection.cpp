// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVM/Extensions/ViewModelExtensionCollection.h"
#include "Containers/ArrayView.h"
#include "MVVM/SharedViewModelData.h"
#include "MVVM/ViewModels/ViewModel.h"
#include "MVVM/ViewModels/ViewModelIterators.h"

namespace UE
{
namespace Sequencer
{

FViewModelExtensionCollection::FViewModelExtensionCollection(FViewModelTypeID InExtensionType)
	: ExtensionType(InExtensionType)
	, bNeedsUpdate(true)
{}

FViewModelExtensionCollection::FViewModelExtensionCollection(FViewModelTypeID InExtensionType, TWeakPtr<FViewModel> InWeakModel, int32 InDesiredRecursionDepth)
	: WeakModel(InWeakModel)
	, ExtensionType(InExtensionType)
	, DesiredRecursionDepth(InDesiredRecursionDepth)
	, bNeedsUpdate(true)
{
	TSharedPtr<FViewModel> Model = InWeakModel.Pin();
	if (Model && Model->IsConstructed())
	{
		Initialize();
	}
}

FViewModelExtensionCollection::~FViewModelExtensionCollection()
{
	Destroy();
}

void FViewModelExtensionCollection::Initialize()
{
	if (!OnHierarchyUpdatedHandle.IsValid())
	{
		if (TSharedPtr<FViewModel> Model = WeakModel.Pin())
		{
			TSharedPtr<FSharedViewModelData> SharedData = Model->GetSharedData();
			OnHierarchyUpdatedHandle = SharedData->SubscribeToHierarchyChanged(Model)
				.AddRaw(this, &FViewModelExtensionCollection::OnHierarchyUpdated);
		}
	}

	bNeedsUpdate = true;
	OnExtensionsDirtied();
}

void FViewModelExtensionCollection::Reinitialize(TWeakPtr<FViewModel> InWeakModel, int32 InDesiredRecursionDepth)
{
	Destroy();

	DesiredRecursionDepth = InDesiredRecursionDepth;
	WeakModel = InWeakModel;

	Initialize();
}

void FViewModelExtensionCollection::FViewModelExtensionCollection::Update() const
{
	bNeedsUpdate = false;
	ExtensionContainer.Reset();

	if (TSharedPtr<FViewModel> Model = WeakModel.Pin())
	{
		FParentFirstChildIterator ChildIt = Model->GetDescendants();
		if (DesiredRecursionDepth != -1)
		{
			ChildIt.SetMaxDepth(DesiredRecursionDepth);
		}

		for (; ChildIt; ++ChildIt)
		{
			if (void* Extension = ChildIt->CastRaw(ExtensionType))
			{
				ExtensionContainer.Add(Extension);
			}
		}
	}
}

void FViewModelExtensionCollection::OnHierarchyUpdated()
{
	bNeedsUpdate = true;
	OnExtensionsDirtied();
}

void FViewModelExtensionCollection::Destroy()
{
	if (TSharedPtr<FViewModel> Model = WeakModel.Pin())
	{
		TSharedPtr<FSharedViewModelData> SharedData = Model->GetSharedData();
		if (SharedData)
		{
			SharedData->UnsubscribeFromHierarchyChanged(Model, OnHierarchyUpdatedHandle);
		}
	}

	OnHierarchyUpdatedHandle = FDelegateHandle();

	bNeedsUpdate = true;
	OnExtensionsDirtied();
}

} // namespace Sequencer
} // namespace UE

