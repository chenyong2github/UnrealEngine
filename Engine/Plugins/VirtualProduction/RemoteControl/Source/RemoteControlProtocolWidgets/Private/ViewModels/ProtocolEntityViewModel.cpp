// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProtocolEntityViewModel.h"

#include "Editor.h"
#include "IRemoteControlProtocol.h"
#include "IRemoteControlProtocolModule.h"
#include "ProtocolBindingViewModel.h"
#include "ProtocolCommandChange.h"
#include "RemoteControlPreset.h"
#include "ScopedTransaction.h"
#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Algo/FindSequence.h"
#include "Algo/Partition.h"

#define LOCTEXT_NAMESPACE "ProtocolEntityViewModel"

// Used by FChange (for Undo/Redo). Keep independent of ViewModel (can be out of scope when called).
namespace Commands
{
	struct FAddRemoveProtocolArgs
	{
		FGuid EntityId;
		FName ProtocolName;
		FGuid BindingId;
	};

	static TSharedPtr<FRemoteControlProtocolBinding> AddProtocolInternal(URemoteControlPreset* InPreset, const FAddRemoveProtocolArgs& InArgs)
	{
		const TSharedPtr<IRemoteControlProtocol> Protocol = IRemoteControlProtocolModule::Get().GetProtocolByName(InArgs.ProtocolName);
		if (Protocol.IsValid())
		{
			if (TSharedPtr<FRemoteControlProperty> RCProperty = InPreset->GetExposedEntity<FRemoteControlProperty>(InArgs.EntityId).Pin())
			{
				const FRemoteControlProtocolEntityPtr RemoteControlProtocolEntityPtr = Protocol->CreateNewProtocolEntity(RCProperty->GetProperty(), InPreset, InArgs.EntityId);

				FRemoteControlProtocolBinding ProtocolBinding(InArgs.ProtocolName, InArgs.EntityId, RemoteControlProtocolEntityPtr);
				Protocol->Bind(ProtocolBinding.GetRemoteControlProtocolEntityPtr());
				RCProperty->ProtocolBinding.Emplace(MoveTemp(ProtocolBinding));

				return MakeShared<FRemoteControlProtocolBinding>(ProtocolBinding);
			}
		}

		return nullptr;
	}

	static FName RemoveProtocolInternal(URemoteControlPreset* InPreset, const FAddRemoveProtocolArgs& InArgs)
	{
		if (TSharedPtr<FRemoteControlProperty> RCProperty = InPreset->GetExposedEntity<FRemoteControlProperty>(InArgs.EntityId).Pin())
		{
			TSet<FRemoteControlProtocolBinding>& Test = RCProperty->ProtocolBinding;
			FRemoteControlProtocolBinding* FoundElement = Test.FindByHash(GetTypeHash(InArgs.BindingId), InArgs.BindingId);
			if (FoundElement)
			{
				Test.RemoveByHash(GetTypeHash(InArgs.BindingId), InArgs.BindingId);
				return FoundElement->GetProtocolName();
			}
		}

		return NAME_None;
	}
}

TSharedRef<FProtocolEntityViewModel> FProtocolEntityViewModel::Create(URemoteControlPreset* InPreset, const FGuid& InEntityId)
{
	TSharedRef<FProtocolEntityViewModel> ViewModel = MakeShared<FProtocolEntityViewModel>(InPreset, InEntityId);
	ViewModel->Initialize();

	return ViewModel;
}

FProtocolEntityViewModel::FProtocolEntityViewModel(URemoteControlPreset* InPreset, const FGuid& InEntityId)
    : Preset(InPreset),
    PropertyId(InEntityId)
{
	check(Preset != nullptr);
	check(Preset->IsExposed(InEntityId));
	GEditor->RegisterForUndo(this);
}

FProtocolEntityViewModel::~FProtocolEntityViewModel()
{
	Bindings.Reset();
	GEditor->UnregisterForUndo(this);
}

void FProtocolEntityViewModel::Initialize()
{
	Bindings.Empty(Bindings.Num());
	if (TSharedPtr<FRemoteControlProperty> RCProperty = Preset->GetExposedEntity<FRemoteControlProperty>(PropertyId).Pin())
	{
		Property = RCProperty->GetProperty();
		for(FRemoteControlProtocolBinding& Binding : RCProperty->ProtocolBinding)
		{
			const TSharedPtr<IRemoteControlProtocol> Protocol = IRemoteControlProtocolModule::Get().GetProtocolByName(Binding.GetProtocolName());
			// Supporting plugin needs to be loaded/protocol available
			if (Protocol.IsValid())
			{
				Bindings.Emplace(FProtocolBindingViewModel::Create(SharedThis(this), MakeShared<FRemoteControlProtocolBinding>(Binding)));
				Protocol->Bind(Binding.GetRemoteControlProtocolEntityPtr());
			}
		}
	}
}

// @note: This should closely match RemoteControlProtocolBinding.h
bool FProtocolEntityViewModel::CanAddBinding(const FName& InProtocolName, FText& OutMessage) const
{
	// If no protocols registered
	if (InProtocolName == NAME_None || !GetProperty().IsValid())
	{
		return false;
	}

	FFieldClass* PropertyClass = GetProperty()->GetClass();
	if (PropertyClass == FBoolProperty::StaticClass())
	{
		return FRemoteControlProtocolBinding::IsRangeTypeSupported<bool>();
	}
	
	if (PropertyClass->IsChildOf(FNumericProperty::StaticClass()))
	{
		return true;
	}
	
	if (PropertyClass->IsChildOf(FStructProperty::StaticClass()))
	{
		return true;
	}

	if (PropertyClass->IsChildOf(FArrayProperty::StaticClass())
		|| PropertyClass->IsChildOf(FSetProperty::StaticClass())
		|| PropertyClass->IsChildOf(FMapProperty::StaticClass()))
	{
		return true;
	}

	// Remaining should be strings, enums
	OutMessage = FText::Format(LOCTEXT("UnsupportedType", "Unsupported Type \"{0}\" for Protocol Binding"), PropertyClass->GetDisplayNameText());
	return false;
}

TSharedPtr<FProtocolBindingViewModel> FProtocolEntityViewModel::AddBinding(const FName& InProtocolName)
{
	check(IsValid());

	FScopedTransaction Transaction(LOCTEXT("AddProtocolBinding", "Add Protocol Binding"));

	const TSharedPtr<FRemoteControlProtocolBinding> ProtocolBinding = Commands::AddProtocolInternal(Preset.Get(), Commands::FAddRemoveProtocolArgs{GetId(), InProtocolName});
	if (ProtocolBinding.IsValid())
	{
		using FCommandChange = TRemoteControlProtocolCommandChange<Commands::FAddRemoveProtocolArgs>;
		using FOnChange = FCommandChange::FOnUndoRedoDelegate;
		if (GUndo)
		{
			GUndo->StoreUndo(Preset.Get(),
                MakeUnique<FCommandChange>(
                    Preset.Get(),
                    Commands::FAddRemoveProtocolArgs{GetId(), InProtocolName, ProtocolBinding->GetId()},
                    FOnChange::CreateLambda([](URemoteControlPreset* InPreset, const Commands::FAddRemoveProtocolArgs& InChangeArgs){ Commands::AddProtocolInternal(InPreset, InChangeArgs); }),
                    FOnChange::CreateLambda([](URemoteControlPreset* InPreset, const Commands::FAddRemoveProtocolArgs& InChangeArgs){ Commands::RemoveProtocolInternal(InPreset, InChangeArgs); })
                )
            );
		}

		TSharedPtr<FProtocolBindingViewModel>& NewBindingViewModel = Bindings.Add_GetRef(MakeShared<FProtocolBindingViewModel>(SharedThis(this), ProtocolBinding.ToSharedRef()));
		NewBindingViewModel->AddDefaultRangeMappings();
		
		OnBindingAddedDelegate.Broadcast(NewBindingViewModel.ToSharedRef());

		return NewBindingViewModel;
	}

	return nullptr;
}

void FProtocolEntityViewModel::RemoveBinding(const FGuid& InBindingId)
{
	check(IsValid());

	FScopedTransaction Transaction(LOCTEXT("RemoveProtocolBinding", "Remove Protocol Binding"));

	const FName RemovedProtocolName = Commands::RemoveProtocolInternal(Preset.Get(), Commands::FAddRemoveProtocolArgs{GetId(), NAME_None, InBindingId});
	if (RemovedProtocolName != NAME_None)
	{
		using FCommandChange = TRemoteControlProtocolCommandChange<Commands::FAddRemoveProtocolArgs>;
		using FOnChange = FCommandChange::FOnUndoRedoDelegate;
		if (GUndo)
		{
			GUndo->StoreUndo(Preset.Get(),
                MakeUnique<FCommandChange>(
                    Preset.Get(),
                    Commands::FAddRemoveProtocolArgs{GetId(), RemovedProtocolName, InBindingId},
                    FOnChange::CreateLambda([](URemoteControlPreset* InPreset, const Commands::FAddRemoveProtocolArgs& InChangeArgs){ Commands::RemoveProtocolInternal(InPreset, InChangeArgs); }),
                    FOnChange::CreateLambda([](URemoteControlPreset* InPreset, const Commands::FAddRemoveProtocolArgs& InChangeArgs){ Commands::AddProtocolInternal(InPreset, InChangeArgs); })
                )
            );
		}
	}

	const int32 NumItemsRemoved = Bindings.RemoveAll([&](const TSharedPtr<FProtocolBindingViewModel>& InBinding)
    {
        return InBinding->GetId() == InBindingId;
    });

	if (NumItemsRemoved <= 0)
	{
		return;
	}

	OnBindingRemovedDelegate.Broadcast(InBindingId);
}

TArray<TSharedPtr<FProtocolBindingViewModel>> FProtocolEntityViewModel::GetFilteredBindings(const TSet<FName>& InHiddenProtocolTypeNames)
{
	if (InHiddenProtocolTypeNames.Num() == 0)
	{
		return GetBindings();
	}

	TArray<TSharedPtr<FProtocolBindingViewModel>> FilteredBindings;
	for(const TSharedPtr<FProtocolBindingViewModel>& Binding : Bindings)
	{
		// make sure this bindings protocol name is NOT in the Hidden list, then add it to FilteredBindings
		if (Algo::NoneOf(InHiddenProtocolTypeNames, [&Binding](const FName& InHiddenTypeName)
		{
			return InHiddenTypeName == Binding->GetBinding()->GetProtocolName();
		}))
		{
			FilteredBindings.Add(Binding);
		}
	}
	
	return FilteredBindings;
}

void FProtocolEntityViewModel::PostUndo(bool bSuccess)
{
	// Rebuild range ViewModels
	Initialize();
	OnChangedDelegate.Broadcast();
}

#undef LOCTEXT_NAMESPACE