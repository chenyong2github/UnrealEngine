// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/ProtocolBindingViewModel.h"

#include "Editor.h"
#include "IRemoteControlProtocolModule.h"
#include "ProtocolCommandChange.h"
#include "ProtocolEntityViewModel.h"
#include "ProtocolRangeViewModel.h"
#include "RemoteControlPreset.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "ProtocolBindingViewModel"

// Used by FChange (for Undo/Redo). Keep independent of ViewModel (can be out of scope when called).
namespace Commands
{
	struct FAddRemoveRangeArgs
	{
		FGuid EntityId;
		FGuid BindingId;
		FGuid RangeId;	
	};
	
	static FGuid AddRangeMappingInternal(URemoteControlPreset* InPreset, const FAddRemoveRangeArgs& InArgs)
	{
		if(TSharedPtr<FRemoteControlProperty> RCProperty = InPreset->GetExposedEntity<FRemoteControlProperty>(InArgs.EntityId).Pin())
		{
			FRemoteControlProtocolBinding* ProtocolBinding = RCProperty->ProtocolBinding.FindByHash(GetTypeHash(InArgs.BindingId), InArgs.BindingId);
			check(ProtocolBinding);

			const TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> EntityPtr = ProtocolBinding->GetRemoteControlProtocolEntityPtr();
			const FRemoteControlProtocolMapping RangesData(RCProperty->GetProperty(), (*EntityPtr)->GetRangePropertySize());
			ProtocolBinding->AddMapping(RangesData);

			return RangesData.GetId();
		}
		
		return FGuid();
	}

	static bool RemoveRangeMappingInternal(URemoteControlPreset* InPreset, const FAddRemoveRangeArgs& InArgs)
	{
		if(TSharedPtr<FRemoteControlProperty> RCProperty = InPreset->GetExposedEntity<FRemoteControlProperty>(InArgs.EntityId).Pin())
		{
			FRemoteControlProtocolBinding* ProtocolBinding = RCProperty->ProtocolBinding.FindByHash(GetTypeHash(InArgs.BindingId), InArgs.BindingId);
			check(ProtocolBinding);

			const int32 NumRemoved = ProtocolBinding->RemoveMapping(InArgs.RangeId);
			return NumRemoved > 0;
		}
		
		return false;
	}
}

TSharedRef<FProtocolBindingViewModel> FProtocolBindingViewModel::Create(const TSharedRef<FProtocolEntityViewModel>& InParentViewModel, const TSharedRef<FRemoteControlProtocolBinding>& InBinding)
{
	TSharedRef<FProtocolBindingViewModel> ViewModel = MakeShared<FProtocolBindingViewModel>(InParentViewModel, InBinding);
	ViewModel->Initialize();

	return ViewModel;
}

FProtocolBindingViewModel::FProtocolBindingViewModel(const TSharedRef<FProtocolEntityViewModel>& InParentViewModel, const TSharedRef<FRemoteControlProtocolBinding>& InBinding)
	: Preset(InParentViewModel->Preset),
	ParentViewModel(InParentViewModel),
	Property(InParentViewModel->Property),
	PropertyId(InParentViewModel->PropertyId),
	BindingId(InBinding->GetId())
{
	GEditor->RegisterForUndo(this);
}

FProtocolBindingViewModel::~FProtocolBindingViewModel()
{
	ParentViewModel.Reset();
	Ranges.Reset();
	GEditor->UnregisterForUndo(this);
}

void FProtocolBindingViewModel::Initialize()
{
	FRemoteControlProtocolBinding* Binding = GetBinding();
	// May be stale as a result of an undo deleting it.
	if(Binding)
	{
		Ranges.Empty(Ranges.Num());
		GetBinding()->ForEachMapping([&](FRemoteControlProtocolMapping& InMapping)
        { 
            Ranges.Emplace(FProtocolRangeViewModel::Create(SharedThis(this), InMapping.GetId()));
        });
	}
}

FGuid FProtocolBindingViewModel::AddRangeMapping()
{
	check(IsValid());

	FScopedTransaction Transaction(LOCTEXT("AddRangeMapping", "Add Range Mapping"));

	TSharedPtr<FProtocolRangeViewModel>& NewRangeViewModel = AddRangeMappingInternal();

	using FCommandChange = TRemoteControlProtocolCommandChange<Commands::FAddRemoveRangeArgs>;
	using FOnChange = FCommandChange::FOnUndoRedoDelegate;
	if(GUndo)
	{
		GUndo->StoreUndo(Preset.Get(),
            MakeUnique<FCommandChange>(
                Preset.Get(),
                Commands::FAddRemoveRangeArgs{ParentViewModel.Pin()->GetId(), GetId(), NewRangeViewModel->GetId()},
                FOnChange::CreateLambda([](URemoteControlPreset* InPreset, const Commands::FAddRemoveRangeArgs& InChangeArgs){ Commands::AddRangeMappingInternal(InPreset, InChangeArgs); }),
                FOnChange::CreateLambda([](URemoteControlPreset* InPreset, const Commands::FAddRemoveRangeArgs& InChangeArgs){ Commands::RemoveRangeMappingInternal(InPreset, InChangeArgs); })
            )
        );
	}

	OnRangeMappingAddedDelegate.Broadcast(NewRangeViewModel.ToSharedRef());	

	return NewRangeViewModel->GetId();
}

// Can call elsewhere without triggering a transaction or event
TSharedPtr<FProtocolRangeViewModel>& FProtocolBindingViewModel::AddRangeMappingInternal()
{
	FGuid RangeId = Commands::AddRangeMappingInternal(Preset.Get(), {ParentViewModel.Pin()->GetId(), GetId(), FGuid()});
	TSharedPtr<FProtocolRangeViewModel>& NewRangeViewModel = Ranges.Add_GetRef(MakeShared<FProtocolRangeViewModel>(SharedThis(this), RangeId));
	return NewRangeViewModel;
}

void FProtocolBindingViewModel::PostUndo(bool bSuccess)
{
	// Rebuild range ViewModels
	Initialize();
	OnChangedDelegate.Broadcast();
}

void FProtocolBindingViewModel::RemoveRangeMapping(const FGuid& InId)
{
	check(IsValid());

	FScopedTransaction Transaction(LOCTEXT("RemoveRangeMapping", "Remove Range Mapping"));
	check(GUndo != nullptr);

	using FCommandChange = TRemoteControlProtocolCommandChange<Commands::FAddRemoveRangeArgs>;
	using FOnChange = FCommandChange::FOnUndoRedoDelegate;
	if(GUndo)
	{
		GUndo->StoreUndo(Preset.Get(),
            MakeUnique<FCommandChange>(
                Preset.Get(),
                Commands::FAddRemoveRangeArgs{ParentViewModel.Pin()->GetId(), GetId(), InId},
                FOnChange::CreateLambda([](URemoteControlPreset* InPreset, const Commands::FAddRemoveRangeArgs& InChangeArgs){ Commands::RemoveRangeMappingInternal(InPreset, InChangeArgs); }),
                FOnChange::CreateLambda([](URemoteControlPreset* InPreset, const Commands::FAddRemoveRangeArgs& InChangeArgs){ Commands::AddRangeMappingInternal(InPreset, InChangeArgs); })
            )
        );
	}

	if(Commands::RemoveRangeMappingInternal(Preset.Get(), Commands::FAddRemoveRangeArgs{ParentViewModel.Pin()->GetId(), GetId(), InId}))
	{
		const int32 NumItemsRemoved = Ranges.RemoveAll([&](const TSharedPtr<FProtocolRangeViewModel>& InRange)
        {
            return InRange->GetId() == InId;
        });

		if(NumItemsRemoved <= 0)
			return;

		OnRangeMappingRemovedDelegate.Broadcast(InId);
	}
}

void FProtocolBindingViewModel::AddDefaultRangeMappings()
{
	check(IsValid());

	static FName ClampMinKey = "ClampMin";
	static FName ClampMaxKey = "ClampMax";

	TSharedPtr<FProtocolRangeViewModel> MinItem = AddRangeMappingInternal();
	TSharedPtr<FProtocolRangeViewModel> MaxItem = AddRangeMappingInternal();

	FName RangePropertyTypeName = GetBinding()->GetRemoteControlProtocolEntityPtr()->Get()->GetRangePropertyName();
	FProperty* RangeProperty = GetProtocol()->GetRangeInputTemplateProperty();
	const uint8 RangePropertySize = GetBinding()->GetRemoteControlProtocolEntityPtr()->Get()->GetRangePropertySize();

	// Range (input)
	{
		// fixup typename according to typesize, ie. it can be a UInt32 but a typesize of 2 makes its a UInt16
		if(RangePropertyTypeName == NAME_UInt32Property && RangePropertySize > 0)
		{
			if(RangePropertySize == sizeof(uint8))
			{
				RangePropertyTypeName = NAME_ByteProperty;
			}
			else if(RangePropertySize == sizeof(uint16))
			{
				RangePropertyTypeName = NAME_UInt16Property; 
			}
			else if(RangePropertySize == sizeof(uint64))
			{
				RangePropertyTypeName = NAME_UInt64Property;
			}
		}

		if(const FNumericProperty* NumericProperty = CastField<FNumericProperty>(RangeProperty))
		{
			if(NumericProperty->IsInteger())
			{
				TOptional<int32> IntMin;
				if(NumericProperty->HasMetaData(ClampMinKey))
				{
					IntMin = NumericProperty->GetIntMetaData(ClampMinKey);
				}

				TOptional<int64> IntMax;
				if(NumericProperty->HasMetaData(ClampMaxKey))
				{
					IntMax = NumericProperty->GetIntMetaData(ClampMaxKey);
				}
				
				if(RangePropertyTypeName == NAME_ByteProperty)
				{
					MinItem->SetInputValue<uint8>(IntMin.Get(0));
					MaxItem->SetInputValue<uint8>(IntMax.Get(TNumericLimits<uint8>::Max()));
				}
				else if(RangePropertyTypeName == NAME_UInt16Property)
				{
					MinItem->SetInputValue<uint16>(IntMin.Get(0));
					MaxItem->SetInputValue<uint16>(IntMax.Get(TNumericLimits<uint16>::Max()));
				}
				else if(RangePropertyTypeName == NAME_UInt32Property)
				{
					MinItem->SetInputValue<uint32>(IntMin.Get(0));
					MaxItem->SetInputValue<uint32>(IntMax.Get(TNumericLimits<uint32>::Max()));
				}
				else if(RangePropertyTypeName == NAME_UInt64Property)
				{
					MinItem->SetInputValue<uint64>(IntMin.Get(0));
					MaxItem->SetInputValue<uint64>(IntMax.Get(TNumericLimits<uint64>::Max()));
				}
			}
			else if(NumericProperty->IsFloatingPoint())
			{
				TOptional<float> FloatMin;
				if(NumericProperty->HasMetaData(ClampMinKey))
				{
					FloatMin = NumericProperty->GetFloatMetaData(ClampMinKey);
				}

				TOptional<float> FloatMax;
				if(NumericProperty->HasMetaData(ClampMaxKey))
				{
					FloatMax = NumericProperty->GetFloatMetaData(ClampMaxKey);
				}

				if(RangePropertyTypeName == NAME_FloatProperty)
				{
					MinItem->SetInputValue<float>(FloatMin.Get(0.0f));
					MaxItem->SetInputValue<float>(FloatMax.Get(1.0f));
				}
			}
		}
	}

	const FProperty* MappingProperty = GetProperty().Get();
	FName MappingPropertyTypeName = MappingProperty->GetClass()->GetFName();
	if(const FStructProperty* StructProperty = CastField<FStructProperty>(MappingProperty))
	{
		MappingPropertyTypeName = StructProperty->Struct->GetFName();
	}
	// Mapping (output)
	{
		if(const FNumericProperty* NumericProperty = CastField<FNumericProperty>(MappingProperty))
		{
			if(NumericProperty->IsInteger())
			{
				TOptional<int32> IntMin;
				if(NumericProperty->HasMetaData(ClampMinKey))
				{
					IntMin = NumericProperty->GetIntMetaData(ClampMinKey);
				}

				TOptional<int64> IntMax;
				if(NumericProperty->HasMetaData(ClampMaxKey))
				{
					IntMax = NumericProperty->GetIntMetaData(ClampMaxKey);
				}
				
				if(RangePropertyTypeName == NAME_ByteProperty)
				{
					MinItem->SetOutputValue<uint8>(IntMin.Get(0));
					MaxItem->SetOutputValue<uint8>(IntMax.Get(TNumericLimits<uint8>::Max()));
				}
				else if(MappingPropertyTypeName == NAME_Int8Property)
				{
					MinItem->SetOutputValue<int8>(IntMin.Get(0));
					MaxItem->SetOutputValue<int8>(IntMax.Get(TNumericLimits<int8>::Max()));
				}
				else if(MappingPropertyTypeName == NAME_Int16Property)
				{
					MinItem->SetOutputValue<int16>(IntMin.Get(0));
					MaxItem->SetOutputValue<int16>(IntMax.Get(TNumericLimits<int16>::Max()));
				}
				else if(RangePropertyTypeName == NAME_UInt16Property)
				{
					MinItem->SetOutputValue<uint16>(IntMin.Get(0));
					MaxItem->SetOutputValue<uint16>(IntMax.Get(TNumericLimits<uint16>::Max()));
				}
				else if(MappingPropertyTypeName == NAME_Int32Property)
				{
					MinItem->SetOutputValue<int32>(IntMin.Get(0));
					MaxItem->SetOutputValue<int32>(IntMax.Get(TNumericLimits<int32>::Max()));
				}
				else if(RangePropertyTypeName == NAME_UInt32Property)
				{
					MinItem->SetOutputValue<uint32>(IntMin.Get(0));
					MaxItem->SetOutputValue<uint32>(IntMax.Get(TNumericLimits<uint32>::Max()));
				}
				else if(MappingPropertyTypeName == NAME_Int64Property)
				{
					MinItem->SetOutputValue<int64>(IntMin.Get(0));
					MaxItem->SetOutputValue<int64>(IntMax.Get(TNumericLimits<int64>::Max()));
				}
				else if(RangePropertyTypeName == NAME_UInt64Property)
				{
					MinItem->SetOutputValue<uint64>(IntMin.Get(0));
					MaxItem->SetOutputValue<uint64>(IntMax.Get(TNumericLimits<uint64>::Max()));
				}
			}
			else if(NumericProperty->IsFloatingPoint())
			{
				TOptional<float> FloatMin;
				if(NumericProperty->HasMetaData(ClampMinKey))
				{
					FloatMin = NumericProperty->GetFloatMetaData(ClampMinKey);
				}

				TOptional<float> FloatMax;
				if(NumericProperty->HasMetaData(ClampMaxKey))
				{
					FloatMax = NumericProperty->GetFloatMetaData(ClampMaxKey);
				}

				if(RangePropertyTypeName == NAME_FloatProperty)
				{
					MinItem->SetOutputValue<float>(FloatMin.Get(0.0f));
					MaxItem->SetOutputValue<float>(FloatMax.Get(1.0f)); // @todo: get current value of property for max unless it's 0.0
				}
				else if(MappingPropertyTypeName == NAME_DoubleProperty)
				{
					MinItem->SetOutputValue<double>(FloatMin.Get(0.0));
					MaxItem->SetOutputValue<double>(FloatMax.Get(TNumericLimits<float>::Max()));
				}
			}
		}
		else
		{
			if(MappingPropertyTypeName == NAME_BoolProperty)
			{
				MinItem->SetOutputValue<bool>(false);
				MaxItem->SetOutputValue<bool>(true);
			}
			else if(MappingPropertyTypeName == NAME_Vector)
			{
				MinItem->SetOutputValue<FVector>(FVector::ZeroVector);
				MaxItem->SetOutputValue<FVector>(FVector::OneVector);
			}
			else if(MappingPropertyTypeName == NAME_Rotator)
			{
				MinItem->SetOutputValue<FRotator>(FRotator::ZeroRotator);
				MaxItem->SetOutputValue<FRotator>({90.0f, 0.0f, 0.0f}); // @note: what should the default for a rotator be?
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("AddDefaultRangeMappings unhandled type: %s"), *MappingPropertyTypeName.ToString());
			}	
		}
	}
}

FRemoteControlProtocolBinding* FProtocolBindingViewModel::GetBinding() const
{
	check(IsValid());

	if (TSharedPtr<FRemoteControlProperty> RCProperty = Preset->GetExposedEntity<FRemoteControlProperty>(PropertyId).Pin())
	{
		return RCProperty->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId);
	}

	checkNoEntry();
	return nullptr;
}

TSharedPtr<IRemoteControlProtocol> FProtocolBindingViewModel::GetProtocol() const
{
	return IRemoteControlProtocolModule::Get().GetProtocolByName(GetBinding()->GetProtocolName());
}

FRemoteControlProtocolMapping* FProtocolBindingViewModel::GetRangesMapping(const FGuid& InRangeId) const
{
	check(IsValid());
	
	FRemoteControlProtocolMapping* Mapping = GetBinding()->FindMapping(InRangeId);
	check(InRangeId == Mapping->GetId());
	return Mapping;
}

void FProtocolBindingViewModel::Remove() const
{
	ParentViewModel.Pin()->RemoveBinding(GetId());
}

#undef LOCTEXT_NAMESPACE
