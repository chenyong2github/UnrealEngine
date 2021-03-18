// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRemoteControlProtocolWidgetExtension.h"
#include "RemoteControlProtocolBinding.h"

#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SCheckBox.h"
#include "Serialization/BufferArchive.h"

#include "IStructureDetailsView.h"
#include "IRemoteControlModule.h"
#include "RemoteControlPreset.h"

#include "IRemoteControlProtocolModule.h"
#include "IRemoteControlProtocol.h"



#define LOCTEXT_NAMESPACE "RemoteControlProtocolWidgets"

#pragma optimize("", off)

class SRangeValueBase : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRangeValueBase)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SRemoteControlProtocolWidgetExtension>& InWidgetExtension, const FGuid& InBindingId, const FGuid& InRangeID)
	{
		WidgetExtensionPtr = InWidgetExtension;
		BindingId = InBindingId;
		RangeID = InRangeID;
	}

protected:
	TWeakPtr<SRemoteControlProtocolWidgetExtension> WidgetExtensionPtr;
	FGuid BindingId;
	FGuid RangeID;
};

template <typename NumericType>
class SRangeValue : public SRangeValueBase
{
public:
	SLATE_BEGIN_ARGS(SRangeValue<NumericType>)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SRemoteControlProtocolWidgetExtension>& InWidgetExtension, const FGuid& InBindingId, const FGuid& InRangeID)
	{
		SRangeValueBase::Construct(SRangeValueBase::FArguments(), InWidgetExtension, InBindingId, InRangeID);

		ChildSlot
			[
				SNew(SNumericEntryBox<NumericType>)
				.Value(this, &SRangeValue<NumericType>::OnGetValue)
				.OnValueChanged(this, &SRangeValue<NumericType>::OnValueChanged)
				.OnValueCommitted(this, &SRangeValue<NumericType>::OnValueCommitted)
			];
	}

	TOptional<NumericType> OnGetValue() const
	{
		TSharedPtr<SRemoteControlProtocolWidgetExtension> WidgetExtension = WidgetExtensionPtr.Pin();

		NumericType Range = 0;


		if (TSharedPtr<FRemoteControlProperty> Property = WidgetExtension->GetPresetPtr()->GetExposedEntity<FRemoteControlProperty>(WidgetExtension->GetPropertyId()).Pin())
		{
			if (FRemoteControlProtocolBinding* ProtocolBinding = Property->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId))
			{
				if (FRemoteControlProtocolMapping* InRangesData = ProtocolBinding->FindMapping(RangeID))
				{
					Range = InRangesData->GetRangeValue<NumericType>();
				}
			}
		}


		return TOptional<NumericType>(Range);
	}

	void OnValueChanged(NumericType NewValue)
	{
		OnValueChangedInternal(NewValue);
	}

	void OnValueCommitted(NumericType NewValue, ETextCommit::Type CommitInfo)
	{
		OnValueChangedInternal(NewValue);
	}

private:
	void OnValueChangedInternal(NumericType NewValue)
	{
		TSharedPtr<SRemoteControlProtocolWidgetExtension> WidgetExtension = WidgetExtensionPtr.Pin();

		TSharedPtr<FRemoteControlProperty> RemoteControlProperty = WidgetExtension->GetPresetPtr()->GetExposedEntity<FRemoteControlProperty>(WidgetExtension->GetPropertyId()).Pin();
		check(RemoteControlProperty.IsValid());

		FRemoteControlProtocolBinding* InBinding = RemoteControlProperty->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId);

		
		{
			InBinding->SetRangeToMapping(RangeID, NewValue);

			IRemoteControlProtocolModule::Get().BindToProtocol(InBinding->GetProtocolName(), InBinding->GetRemoteControlProtocolEntityPtr());
		}
	}
};

template <typename NumericType>
class SMappingValue : public SRangeValueBase
{
public:

	SLATE_BEGIN_ARGS(SMappingValue<NumericType>)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SRemoteControlProtocolWidgetExtension>& InWidgetExtension, const FGuid& InBindingId, const FGuid& InRangeID)
	{
		SRangeValueBase::Construct(SRangeValueBase::FArguments(), InWidgetExtension, InBindingId, InRangeID);

		ChildSlot
			[
				SNew(SNumericEntryBox<NumericType>)
				.Value(this, &SMappingValue<NumericType>::OnGetValue)
				.OnValueChanged(this, &SMappingValue<NumericType>::OnValueChanged)
				.OnValueCommitted(this, &SMappingValue<NumericType>::OnValueCommitted)
			];
	}

	TOptional<NumericType> OnGetValue() const
	{
		TSharedPtr<SRemoteControlProtocolWidgetExtension> WidgetExtension = WidgetExtensionPtr.Pin();

		if (TSharedPtr<FRemoteControlProperty> Property = WidgetExtension->GetPresetPtr()->GetExposedEntity<FRemoteControlProperty>(WidgetExtension->GetPropertyId()).Pin())
		{
			if (FRemoteControlProtocolBinding* ProtocolBinding = Property->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId))
			{
				if (FRemoteControlProtocolMapping* InRangesData = ProtocolBinding->FindMapping(RangeID))
				{
					return InRangesData->GetMappingValueAsPrimitive<NumericType>();
				}
			}
		}

		return 0;
	}

	void OnValueChanged(NumericType NewValue)
	{
		OnValueChangedInternal(NewValue);
	}

	void OnValueCommitted(NumericType NewValue, ETextCommit::Type CommitInfo)
	{
		OnValueChangedInternal(NewValue);
	}

private:
	void OnValueChangedInternal(NumericType NewValue)
	{
		TSharedPtr<SRemoteControlProtocolWidgetExtension> WidgetExtension = WidgetExtensionPtr.Pin();

		TSharedPtr<FRemoteControlProperty> RemoteControlProperty = WidgetExtension->GetPresetPtr()->GetExposedEntity<FRemoteControlProperty>(WidgetExtension->GetPropertyId()).Pin();
		check(RemoteControlProperty.IsValid());

		FRemoteControlProtocolBinding* InBinding = RemoteControlProperty->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId);
		
		{
			InBinding->SetPropertyDataToMapping(RangeID, (uint8*)&NewValue);

			IRemoteControlProtocolModule::Get().BindToProtocol(InBinding->GetProtocolName(), InBinding->GetRemoteControlProtocolEntityPtr());
		}
	}
};

TSharedRef<ITableRow> SRemoteControlProtocolWidgetExtension::FBindingItem::MakeRangeItem(TSharedPtr<FGuid> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<SHorizontalBox> MappingValueBox = SNew(SHorizontalBox);

	TSharedPtr<SRemoteControlProtocolWidgetExtension> WidgetExtension = WidgetExtensionPtr.Pin();
	check(WidgetExtension.IsValid());

	TSharedPtr<FRemoteControlProperty> RemoteControlProperty = WidgetExtension->GetPresetPtr()->GetExposedEntity<FRemoteControlProperty>(WidgetExtension->GetPropertyId()).Pin();
	check(RemoteControlProperty.IsValid());
	
	FRemoteControlProtocolBinding* InBinding = RemoteControlProperty->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId);
	{
		const FGuid RangeId = *InItem;
		//const FRemoteControlProtocolEntity* RemoteControlProtocolEntity = InBinding->GetRemoteControlProtocolEntityPtr();
		TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> RemoteControlProtocolEntityPtr = InBinding->GetRemoteControlProtocolEntityPtr();

		const FName& MappingPropertyName = (*RemoteControlProtocolEntityPtr)->GetRangePropertyName();

		FRemoteControlProtocolMapping* RangeData = InBinding->FindMapping(RangeId);
		// 1. Range value
		{
			TSharedRef<SWidget> MappingRangesWidget = [this, WidgetExtension, RangeId, &MappingPropertyName]() -> TSharedRef<SWidget>
			{
				if (MappingPropertyName == NAME_ByteProperty)
				{
					return SNew(SRangeValue<uint8>, WidgetExtension.ToSharedRef(), BindingId, RangeId);
				}
				else if (MappingPropertyName == NAME_FloatProperty)
				{
					return SNew(SRangeValue<float>, WidgetExtension.ToSharedRef(), BindingId, RangeId);
				}

				return SNew(STextBlock).Text(FText::FromString("ERROR"));
			}();

			MappingValueBox->AddSlot()
				.AutoWidth()
				.Padding(0.f, 0, 5.f, 0)
				.VAlign(VAlign_Center)
				[
					MappingRangesWidget
				];
		}

		// 2. Mapping Value
		{
			// 2.1 Mapping simple Numeric values
			if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(WidgetExtension->GetProperty()))
			{
				TSharedRef<SWidget> CheckboxWidget = SNew(SCheckBox)
					.OnCheckStateChanged_Lambda([this, WidgetExtension, RangeId](const ECheckBoxState InNewState)
					{
						TSharedPtr<FRemoteControlProperty> RemoteControlProperty = WidgetExtension->GetPresetPtr()->GetExposedEntity<FRemoteControlProperty>(WidgetExtension->GetPropertyId()).Pin();
						check(RemoteControlProperty.IsValid());

						FRemoteControlProtocolBinding* InBinding = RemoteControlProperty->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId);
						
						{
							bool Result = false;
							if (InNewState == ECheckBoxState::Checked)
							{
								Result = true;
							}

							InBinding->SetPropertyDataToMapping(RangeId, (uint8*)&Result);

							IRemoteControlProtocolModule::Get().BindToProtocol(InBinding->GetProtocolName(), InBinding->GetRemoteControlProtocolEntityPtr());
						}
					})
					.IsChecked_Lambda([this, WidgetExtension, RangeId]()
					{
						ECheckBoxState ReturnState = ECheckBoxState::Undetermined;

						bool bMappingValue = false;

						if (TSharedPtr<FRemoteControlProperty> Property = WidgetExtension->GetPresetPtr()->GetExposedEntity<FRemoteControlProperty>(WidgetExtension->GetPropertyId()).Pin())
						{
							if (FRemoteControlProtocolBinding* ProtocolBinding = Property->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId))
							{
								if (FRemoteControlProtocolMapping* InRangesData = ProtocolBinding->FindMapping(RangeId))
								{
									bMappingValue = InRangesData->GetMappingValueAsPrimitive<bool>();
								}
							}
						}

						if (bMappingValue == true)
						{
							ReturnState = ECheckBoxState::Checked;
						}
						else
						{
							ReturnState = ECheckBoxState::Unchecked;
						}

						return ReturnState;
					})
					.Padding(FMargin(4.0f, 0.0f));

				MappingValueBox->AddSlot()
					.AutoWidth()
					[
						CheckboxWidget
					];
			}
			else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(WidgetExtension->GetProperty()))
			{
				TSharedRef<SWidget> MappingValueWidget = [this, WidgetExtension, &RangeId]()->TSharedRef<SWidget>
				{
					if (const FByteProperty* ByteProperty = CastField<FByteProperty>(WidgetExtension->GetProperty()))
					{
						return SNew(SMappingValue<uint8>, WidgetExtension.ToSharedRef(), BindingId, RangeId);
					}

					else if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(WidgetExtension->GetProperty()))
					{
						return SNew(SMappingValue<double>, WidgetExtension.ToSharedRef(), BindingId, RangeId);
					}

					else if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(WidgetExtension->GetProperty()))
					{
						return SNew(SMappingValue<float>, WidgetExtension.ToSharedRef(), BindingId, RangeId);
					}

					else if (const FIntProperty* IntProperty = CastField<FIntProperty>(WidgetExtension->GetProperty()))
					{
						return SNew(SMappingValue<int>, WidgetExtension.ToSharedRef(), BindingId, RangeId);
					}

					else if (const FUInt32Property* UInt32Property = CastField<FUInt32Property>(WidgetExtension->GetProperty()))
					{
						return SNew(SMappingValue<uint32>, WidgetExtension.ToSharedRef(), BindingId, RangeId);
					}

					else if (const FInt16Property* Int16Property = CastField<FInt16Property>(WidgetExtension->GetProperty()))
					{
						return SNew(SMappingValue<int16>, WidgetExtension.ToSharedRef(), BindingId, RangeId);
					}

					else if (const FUInt16Property* FInt16Property = CastField<FUInt16Property>(WidgetExtension->GetProperty()))
					{
						return SNew(SMappingValue<uint16>, WidgetExtension.ToSharedRef(), BindingId, RangeId);
					}

					else if (const FInt64Property* Int64Property = CastField<FInt64Property>(WidgetExtension->GetProperty()))
					{
						return SNew(SMappingValue<int64>, WidgetExtension.ToSharedRef(), BindingId, RangeId);
					}

					else if (const FUInt64Property* FInt64Property = CastField<FUInt64Property>(WidgetExtension->GetProperty()))
					{
						return SNew(SMappingValue<uint64>, WidgetExtension.ToSharedRef(), BindingId, RangeId);
					}

					else if (const FInt8Property* Int8Property = CastField<FInt8Property>(WidgetExtension->GetProperty()))
					{
						return SNew(SMappingValue<int8>, WidgetExtension.ToSharedRef(), BindingId, RangeId);
					}

					return SNew(SBox)
						[
							SNew(STextBlock).Text(FText::FromString("Not Supported"))
						];
				}();

				MappingValueBox->AddSlot()
					.AutoWidth()
					[
						MappingValueWidget
					];
			}
			// 2.2 Mapping more complex structs
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(WidgetExtension->GetProperty()))
			{
				UScriptStruct* LocalScriptStruct = StructProperty->Struct;
				TSharedPtr<FStructOnScope> StructOnScope = RangeData->GetMappingPropertyAsStructOnScope();
				MappingValueBox->AddSlot()
					.Padding(0.f, 2.f)
					.AutoWidth()
					[
						CreateStructureDetailView(StructOnScope, [this, WidgetExtension](const FPropertyChangedEvent& PropertyChangedEvent)
				{
					TSharedPtr<FRemoteControlProperty> RemoteControlProperty = WidgetExtension->GetPresetPtr()->GetExposedEntity<FRemoteControlProperty>(WidgetExtension->GetPropertyId()).Pin();
					check(RemoteControlProperty.IsValid());
					FRemoteControlProtocolBinding* InBinding = RemoteControlProperty->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId);
							
					IRemoteControlProtocolModule::Get().BindToProtocol(InBinding->GetProtocolName(), InBinding->GetRemoteControlProtocolEntityPtr());
				})
					];
			}
			else
			{
				MappingValueBox->AddSlot()
					.AutoWidth()
					[
						SNew(SBox)
						[
							SNew(STextBlock).Text(FText::FromString("Not Supported"))
						]
					];
			 }
		}

		// 3. Remove button
		{
			MappingValueBox->AddSlot()
				.AutoWidth()
				.Padding(0.f, 0, 5.f, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.OnClicked(FOnClicked::CreateSP(this, &SRemoteControlProtocolWidgetExtension::FBindingItem::OnRemoveRangeMapping, RangeId))
				[
					SNew(STextBlock)
					.Text(FText::FromString("Remove"))
				]
				];
		}
	};

	return SNew(STableRow<TSharedPtr<FGuid>>, OwnerTable)
		[
			MappingValueBox
		];
}

FReply SRemoteControlProtocolWidgetExtension::FBindingItem::OnRemoveRangeMapping(const FGuid InRangeId)
{
	TSharedPtr<SRemoteControlProtocolWidgetExtension> WidgetExtension = WidgetExtensionPtr.Pin();
	check(WidgetExtension.IsValid());

	TSharedPtr<FRemoteControlProperty> RemoteControlProperty = WidgetExtension->GetPresetPtr()->GetExposedEntity<FRemoteControlProperty>(WidgetExtension->GetPropertyId()).Pin();
	check(RemoteControlProperty.IsValid());
	FRemoteControlProtocolBinding* InBinding = RemoteControlProperty->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId);
	
	{
		if (!InBinding)
		{
			return FReply::Handled();
		}

		const FGuid RangeId = InRangeId;

		InBinding->RemoveMapping(RangeId);

		// Update protocols
		IRemoteControlProtocolModule::Get().BindToProtocol(InBinding->GetProtocolName(), InBinding->GetRemoteControlProtocolEntityPtr());

		int32 Index = RangesList.IndexOfByPredicate([this, &RangeId](const TSharedPtr<FGuid>& InRangeID) { return RangeId == *InRangeID; });
		if (Index != INDEX_NONE)
		{
			RangesList.RemoveAt(Index);
			RangesListView->RequestListRefresh();
		}
	}

	return FReply::Handled();
}

void SRemoteControlProtocolWidgetExtension::Construct(const FArguments& InArgs)
{
	PresetPtr = InArgs._Preset;
	PropertyLabel = InArgs._PropertyLabel;

	ExposedProperty = PresetPtr.Get()->ResolveExposedProperty(PropertyLabel);
	Property = ExposedProperty->Property;

	// TODO. refactor this
	TOptional<FRemoteControlProperty> RemoteControlProperty = PresetPtr.Get()->GetProperty(PropertyLabel);
	PropertyId = RemoteControlProperty->GetId();

	IRemoteControlProtocolModule& RemoteControlProtocolModule = IRemoteControlProtocolModule::Get();

	if (RemoteControlProtocolModule.GetProtocols().Num() == 0)
	{
		ChildSlot
			[
				SNullWidget::NullWidget
			];
		return;
	}

	ChildSlot
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.Padding(0.f, 5.f)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.Padding(0.f, 0.f, 5.f, 0.f)
				.AutoWidth()
				[
					SAssignNew(ProtocolSelectionButton, SComboButton)
					.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
					.ForegroundColor(FSlateColor::UseForeground())
					.ButtonContent()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
						.Text_Lambda([this]() -> FText
						{
							return FText::FromName(*SelectedProtocol);
						})
					]
					.MenuContent()
					[
						SAssignNew(ProtocolListView, SListView<TSharedPtr<FName>>)
						.ListItemsSource(&UnselectedProtocols)
						.OnGenerateRow(this, &SRemoteControlProtocolWidgetExtension::MakeProtocolSelectionWidget)
						.OnSelectionChanged(this, &SRemoteControlProtocolWidgetExtension::OnProtocolSelectionChanged)
					]
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				[
					SNew(SButton)
					.OnClicked(FOnClicked::CreateSP(this, &SRemoteControlProtocolWidgetExtension::OnAddProtocol))
					.Text(LOCTEXT("AddProtocol", "Add Protocol"))
				]
			]

			+ SVerticalBox::Slot()
			.VAlign(VAlign_Fill)
			.AutoHeight()
			[
				SAssignNew(BindingListView, SListView<TSharedPtr<FBindingItem>>)
				.ListItemsSource(&BindingList)
				.OnGenerateRow(this, &SRemoteControlProtocolWidgetExtension::MakeBindingListItem)
			]
		];

	RefreshUnselectedProtocolsList();
	RefreshProtocolBinding();
}

bool SRemoteControlProtocolWidgetExtension::DeleteMappingFromListByID(const FGuid& InID)
{
	int32 Index = BindingList.IndexOfByPredicate([this, &InID](const TSharedPtr<FBindingItem>& MappingItem) { return InID == MappingItem->GetBindingId(); });
	if (Index != INDEX_NONE)
	{
		BindingList.RemoveAt(Index);
		return true;
	}

	return false;
}

TSharedRef<SWidget> SRemoteControlProtocolWidgetExtension::CreateStructureDetailView(TSharedPtr<FStructOnScope> StructData, const OnFinishedChangingPropertiesCallback& OnFinishedChangingProperties)
{
	FStructureDetailsViewArgs StructureViewArgs;
	FDetailsViewArgs ViewArgs;

	// create struct to display
	StructureViewArgs.bShowObjects = true;
	StructureViewArgs.bShowAssets = true;
	StructureViewArgs.bShowClasses = true;
	StructureViewArgs.bShowInterfaces = true;

	ViewArgs.bAllowSearch = false;
	ViewArgs.bHideSelectionTip = false;
	ViewArgs.bShowActorLabel = false;

	// Create a property view
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	TSharedPtr<IStructureDetailsView> StructureDetailsView = EditModule.CreateStructureDetailView(ViewArgs, StructureViewArgs, StructData, LOCTEXT("Struct", "Struct View"));
	StructureDetailsView->GetOnFinishedChangingPropertiesDelegate().AddLambda(OnFinishedChangingProperties);

	return StructureDetailsView->GetWidget().ToSharedRef();
}

TSharedRef<ITableRow> SRemoteControlProtocolWidgetExtension::MakeBindingListItem(TSharedPtr<FBindingItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (ensure(InItem.IsValid()))
	{
		SRemoteControlProtocolWidgetExtension::FBindingItem* MappingItem = InItem.Get();

		TSharedRef<SWidget> StructureDetailView = [this, &InItem]() -> TSharedRef<SWidget>
		{
			
			const FGuid BindingId = InItem->GetBindingId();

			TSharedPtr<FRemoteControlProperty> RemoteControlProperty = PresetPtr->GetExposedEntity<FRemoteControlProperty>(PropertyId).Pin();
			check(RemoteControlProperty.IsValid());
			FRemoteControlProtocolBinding* InBinding = RemoteControlProperty->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId);
			
			TSharedPtr<FStructOnScope> StructOnScope = InBinding->GetStructOnScope();


			if (StructOnScope)
			{
				return CreateStructureDetailView(StructOnScope, [this, BindingId](const FPropertyChangedEvent& PropertyChangedEvent)
				{
					TSharedPtr<FRemoteControlProperty> RemoteControlProperty = PresetPtr->GetExposedEntity<FRemoteControlProperty>(PropertyId).Pin();
					check(RemoteControlProperty.IsValid());
					FRemoteControlProtocolBinding* InBinding = RemoteControlProperty->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId);
		
					{
						IRemoteControlProtocolModule::Get().BindToProtocol(InBinding->GetProtocolName(), InBinding->GetRemoteControlProtocolEntityPtr());
					}
				});
			}


			return SNew(STextBlock).Text(LOCTEXT("MappingItemError", "Mapping Item Error"));
		}();

		return SNew(STableRow<TSharedPtr<FBindingItem>>, OwnerTable)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.Padding(0.f, 5.f)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					[
						StructureDetailView
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(10.f, 0.f)
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.f, 5.f)
						[
							SNew(SButton)
							.OnClicked(FOnClicked::CreateSP(this, &SRemoteControlProtocolWidgetExtension::OnAddRangeMapping, InItem.ToSharedRef()))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("AddMapping", "Add Mapping"))
							]
						]

						+ SVerticalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(0.f, 5.f)
						[
							SNew(SButton)
								.OnClicked(FOnClicked::CreateSP(this, &SRemoteControlProtocolWidgetExtension::OnRemoveMapping, InItem.ToSharedRef()))
								[
									SNew(STextBlock).Text(LOCTEXT("Remove", "Remove"))
								]
						]
					]

				]
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Fill)
				.AutoHeight()
				[
					SAssignNew(InItem->RangesListView, SListView<TSharedPtr<FGuid>>)
					.ListItemsSource(&InItem->RangesList)
					.OnGenerateRow(MappingItem, &SRemoteControlProtocolWidgetExtension::FBindingItem::MakeRangeItem)
				]
			];
	}
	else
	{
		return SNew(STableRow<TSharedPtr<FBindingItem>>, OwnerTable)
			[
				SNew(STextBlock).Text(FText::FromString("ERROR"))
			];
	}
}

FReply SRemoteControlProtocolWidgetExtension::OnAddRangeMapping(TSharedRef<FBindingItem> InItem)
{
	const FGuid& BindingId = InItem->GetBindingId();

	TSharedPtr<FRemoteControlProperty> RemoteControlProperty = PresetPtr->GetExposedEntity<FRemoteControlProperty>(PropertyId).Pin();
	check(RemoteControlProperty.IsValid());
	FRemoteControlProtocolBinding* InBinding = RemoteControlProperty->ProtocolBinding.FindByHash(GetTypeHash(BindingId), BindingId);
	
	TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> EntityPtr = InBinding->GetRemoteControlProtocolEntityPtr();
	
	FRemoteControlProtocolMapping RangesData(Property, (*EntityPtr)->GetRangePropertySize());
	const FGuid& RangeID = RangesData.GetId();

	
	{
		if (InBinding)
		{
			InBinding->AddMapping(RangesData);

			InItem->RangesList.Add(MakeShared<FGuid>(RangeID));
			InItem->RangesListView->RequestListRefresh();

			IRemoteControlProtocolModule::Get().BindToProtocol(InBinding->GetProtocolName(), InBinding->GetRemoteControlProtocolEntityPtr());
		}
	}

	return FReply::Handled();
}

FReply SRemoteControlProtocolWidgetExtension::OnRemoveMapping(TSharedRef<FBindingItem> InItem)
{
	const FGuid& BindingId = InItem->GetBindingId();
	
	if (TSharedPtr<FRemoteControlProperty> RemoteControlProperty = PresetPtr->GetExposedEntity<FRemoteControlProperty>(PropertyId).Pin())
	{
		RemoteControlProperty->ProtocolBinding.RemoveByHash(GetTypeHash(BindingId), BindingId);
	}

	//TODO. Remove from the protocol
	//IRemoteControlProtocolModule::Get().BindToProtocol(ProtocolBindingItem->GetProtocolName(), ProtocolBindingItem->GetBindingStructBuffer());

	if (DeleteMappingFromListByID(BindingId))
	{
		BindingListView->RequestListRefresh();
	}

	return FReply::Handled();
}

TSharedRef<ITableRow> SRemoteControlProtocolWidgetExtension::MakeProtocolSelectionWidget(TSharedPtr<FName> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	FText DisplayText;
	if (InItem.IsValid() && !InItem->IsNone())
	{
		DisplayText = FText::FromName(*InItem);
	}

	return SNew(STableRow<TSharedPtr<FText>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text(DisplayText)
		];
}

FReply SRemoteControlProtocolWidgetExtension::OnAddProtocol()
{
	if (SelectedProtocol.IsValid())
	{
		UScriptStruct* ScriptStruct = IRemoteControlProtocolModule::Get().GetScriptStructByName(*SelectedProtocol);

		if (ScriptStruct)
		{
			TSharedPtr<IRemoteControlProtocol> ProtocolPtr = IRemoteControlProtocolModule::Get().GetProtocolByName(*SelectedProtocol);
			if (ProtocolPtr)
			{
				TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> RemoteControlProtocolEntityPtr = ProtocolPtr->CreateNewProtocolEntity(ExposedProperty->Property, PresetPtr.Get()->GetFName(), PropertyId);

				FRemoteControlProtocolBinding ProtocolBinding(*SelectedProtocol, PropertyId, RemoteControlProtocolEntityPtr);
				IRemoteControlProtocolModule::Get().BindToProtocol(ProtocolBinding.GetProtocolName(), ProtocolBinding.GetRemoteControlProtocolEntityPtr());

				const FGuid BindingId = ProtocolBinding.GetId();

				TSharedPtr<FRemoteControlProperty> RemoteControlProperty = PresetPtr->GetExposedEntity<FRemoteControlProperty>(PropertyId).Pin();
				check(RemoteControlProperty.IsValid());
				RemoteControlProperty->ProtocolBinding.Add(ProtocolBinding);

				BindingList.Add(MakeShared<FBindingItem>(SharedThis(this), BindingId));

				BindingListView->RequestListRefresh();
			}
		}
	}

	return FReply::Handled();
}

void SRemoteControlProtocolWidgetExtension::OnProtocolSelectionChanged(TSharedPtr<FName> NewSelection, ESelectInfo::Type /*SelectInfo*/)
{
	if (NewSelection.IsValid())
	{
		SelectedProtocol = MakeShared<FName>(*NewSelection);

		ProtocolSelectionButton->SetIsOpen(false);

		RefreshUnselectedProtocolsList();
	}
}

void SRemoteControlProtocolWidgetExtension::RefreshUnselectedProtocolsList()
{
	UnselectedProtocols.Empty();
	
	IRemoteControlProtocolModule& RemoteControlProtocolModule = IRemoteControlProtocolModule::Get();

	for (const TPair<FName, TSharedPtr<IRemoteControlProtocol>>& ProtocolPair : RemoteControlProtocolModule.GetProtocols())
	{
		if (!SelectedProtocol.IsValid())
		{
			SelectedProtocol = MakeShared<FName>(ProtocolPair.Key);
		}
		else if (*SelectedProtocol != ProtocolPair.Key)
		{
			UnselectedProtocols.Add(MakeShared<FName>(ProtocolPair.Key));
		}
	}

	if (ProtocolListView.IsValid())
	{
		ProtocolListView->RequestListRefresh();
	}
}

void SRemoteControlProtocolWidgetExtension::RefreshProtocolBinding()
{
	TSharedRef<SRemoteControlProtocolWidgetExtension> ThisRef = SharedThis(this);

	if (TSharedPtr<FRemoteControlProperty> RemoteControlProperty = PresetPtr->GetExposedEntity<FRemoteControlProperty>(PropertyId).Pin())
	{
		for (FRemoteControlProtocolBinding& InBinding : RemoteControlProperty->ProtocolBinding)
		{
			UE_LOG(LogTemp, Warning, TEXT("two times %s !!!"), *InBinding.GetProtocolName().ToString());

			TSharedPtr<FBindingItem> BindingItem = MakeShared<FBindingItem>(ThisRef, InBinding.GetId());

			InBinding.ForEachMapping([this, &BindingItem](FRemoteControlProtocolMapping& InRangeData)
				{
					BindingItem->RangesList.Add(MakeShared<FGuid>(InRangeData.GetId()));
				});

			BindingList.Add(BindingItem);

			IRemoteControlProtocolModule::Get().BindToProtocol(InBinding.GetProtocolName(), InBinding.GetRemoteControlProtocolEntityPtr());
		}
	}

	BindingListView->RequestListRefresh();
}

#undef LOCTEXT_NAMESPACE

#pragma optimize("", on)