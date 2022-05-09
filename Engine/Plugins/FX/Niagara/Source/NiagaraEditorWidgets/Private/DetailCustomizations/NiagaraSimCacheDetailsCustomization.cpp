// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSimCacheDetailsCustomization.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraSimCache.h"

#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailCustomization.h"

#define LOCTEXT_NAMESPACE "NiagaraSimCacheDetailsCustomization"

namespace NiagaraSimCacheDetailsCustomizationLocal
{
	static const FName NAME_Instance("Instance");

	struct FSimCacheBufferReader
	{
		struct FComponentInfo
		{
			FName			Name = NAME_None;
			uint32			ComponentOffset = INDEX_NONE;
			bool			bIsFloat = false;
			bool			bIsHalf = false;
			bool			bIsInt32 = false;
			bool			bShowAsBool = false;
			UEnum*			Enum = nullptr;
		};

		explicit FSimCacheBufferReader(UNiagaraSimCache* SimCache, uint32 InFrameIndex = 0, uint32 InEmitterIndex = INDEX_NONE)
			: WeakSimCache(SimCache)
			, FrameIndex(InFrameIndex)
			, EmitterIndex(InEmitterIndex)
		{
			UpdateLayout();
		}

		void UpdateLayout()
		{
			ComponentInfos.Empty();
			FoundFloats = 0;
			FoundHalfs = 0;
			FoundInt32s = 0;

			if ( const FNiagaraSimCacheDataBuffersLayout* SimCacheLayout = GetSimCacheBufferLayout() )
			{
				for (const FNiagaraSimCacheVariable& Variable : SimCacheLayout->Variables)
				{
					const FNiagaraTypeDefinition& TypeDef = Variable.Variable.GetType();
					if ( TypeDef.IsEnum() )
					{
						FComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
						ComponentInfo.Name = Variable.Variable.GetName();
						ComponentInfo.ComponentOffset = FoundInt32s++;
						ComponentInfo.bIsInt32 = true;
						ComponentInfo.Enum = TypeDef.GetEnum();
					}
					else
					{
						Build(Variable.Variable.GetName(), TypeDef.GetScriptStruct());
					}
				}

				if ((FoundFloats != SimCacheLayout->FloatCount) ||
					(FoundHalfs != SimCacheLayout->HalfCount) ||
					(FoundInt32s != SimCacheLayout->Int32Count) )
				{
					UE_LOG(LogNiagaraEditor, Warning, TEXT("SimCache Layout doesn't appear to match iterating the variables"));
					ComponentInfos.Empty();
				}
			}
		}

		TConstArrayView<FComponentInfo> GetComponentInfos() const
		{
			return MakeArrayView(ComponentInfos);
		}

		int32 GetNumInstances() const
		{
			const FNiagaraSimCacheDataBuffers* DataBuffer = GetSimCacheDataBuffers();
			return DataBuffer ? DataBuffer->NumInstances : 0;
		}

		int32 GetNumFrames() const
		{
			UNiagaraSimCache* SimCache = WeakSimCache.Get();
			return SimCache ? SimCache->CacheFrames.Num() : 0;
		}

		int32 GetFrameIndex() const
		{
			return FrameIndex;
		}

		void SetFrameIndex(int32 InFrameIndex)
		{
			FrameIndex = InFrameIndex;
		}

		int32 GetEmitterIndex() const
		{
			return EmitterIndex;
		}

		void SetEmitterIndex(int32 InEmitterIndex)
		{
			EmitterIndex = InEmitterIndex;
			UpdateLayout();
		}

		const FNiagaraSimCacheDataBuffersLayout* GetSimCacheBufferLayout() const
		{
			if (UNiagaraSimCache* SimCache = WeakSimCache.Get())
			{
				if (SimCache->CacheFrames.IsValidIndex(FrameIndex))
				{
					if (EmitterIndex == INDEX_NONE)
					{
						return &SimCache->CacheLayout.SystemLayout;
					}
					else if (SimCache->CacheLayout.EmitterLayouts.IsValidIndex(EmitterIndex))
					{
						return &SimCache->CacheLayout.EmitterLayouts[EmitterIndex];
					}
				}
			}
			return nullptr;
		}

		const FNiagaraSimCacheDataBuffers* GetSimCacheDataBuffers() const
		{
			if ( UNiagaraSimCache* SimCache = WeakSimCache.Get() )
			{
				if ( SimCache->CacheFrames.IsValidIndex(FrameIndex) )
				{
					if ( EmitterIndex == INDEX_NONE )
					{
						return &SimCache->CacheFrames[FrameIndex].SystemData.SystemDataBuffers;
					}
					else if (SimCache->CacheFrames[FrameIndex].EmitterData.IsValidIndex(EmitterIndex))
					{
						return &SimCache->CacheFrames[FrameIndex].EmitterData[EmitterIndex].ParticleDataBuffers;
					}
				}
			}
			return nullptr;
		}

		FText GetComponentText(FName ComponentName, int32 InstanceIndex) const
		{
			const FComponentInfo* ComponentInfo = ComponentInfos.FindByPredicate([ComponentName](const FComponentInfo& Info) { return Info.Name == ComponentName; });
			const FNiagaraSimCacheDataBuffers* SimCacheDataBuffers = GetSimCacheDataBuffers();
			const FNiagaraSimCacheDataBuffersLayout* SimCacheBufferLayout = GetSimCacheBufferLayout();
			if ( ComponentInfo && SimCacheDataBuffers)
			{
				const int32 NumInstances = SimCacheDataBuffers->NumInstances;
				if (InstanceIndex >= 0 && InstanceIndex < NumInstances)
				{
					if ( ComponentInfo->bIsFloat )
					{
						const float* Value = reinterpret_cast<const float*>(SimCacheDataBuffers->FloatData.GetData()) + (ComponentInfo->ComponentOffset * NumInstances) + InstanceIndex;
						return FText::AsNumber(*Value);
					}
					else if ( ComponentInfo->bIsHalf )
					{
						const FFloat16* Value = reinterpret_cast<const FFloat16*>(SimCacheDataBuffers->HalfData.GetData()) + (ComponentInfo->ComponentOffset * NumInstances) + InstanceIndex;
						return FText::AsNumber(Value->GetFloat());
					}
					else if ( ComponentInfo->bIsInt32 )
					{
						const int32* Value = reinterpret_cast<const int32*>(SimCacheDataBuffers->Int32Data.GetData()) + (ComponentInfo->ComponentOffset * NumInstances) + InstanceIndex;
						if ( ComponentInfo->bShowAsBool )
						{
							return *Value == 0 ? LOCTEXT("False", "False") : LOCTEXT("True", "True");
						}
						else if ( ComponentInfo->Enum != nullptr )
						{
							return ComponentInfo->Enum->GetDisplayNameTextByValue(*Value);
						}
						else
						{
							return FText::AsNumber(*Value);
						}
					}
				}
			}
			return LOCTEXT("Error", "Error");
		}

	protected:
		TWeakObjectPtr<UNiagaraSimCache>	WeakSimCache;
		uint32								FrameIndex = 0;
		uint32								EmitterIndex = INDEX_NONE;

		TArray<FComponentInfo>				ComponentInfos;
		uint32								FoundFloats = 0;
		uint32								FoundHalfs = 0;
		uint32								FoundInt32s = 0;

		void Build(FName Name, UScriptStruct* Struct)
		{
			int32 NumProperties = 0;
			for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
			{
				++NumProperties;
			}

			for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
			{
				FProperty* Property = *PropertyIt;
				const FName PropertyName = NumProperties > 1 ? FName(*FString::Printf(TEXT("%s.%s"), *Name.ToString(), *Property->GetName())) : Name;
				if (Property->IsA(FFloatProperty::StaticClass()))
				{
					FComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
					ComponentInfo.Name = PropertyName;
					ComponentInfo.ComponentOffset = FoundFloats++;
					ComponentInfo.bIsFloat = true;
				}
				else if (Property->IsA(FUInt16Property::StaticClass()))
				{
					FComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
					ComponentInfo.Name = PropertyName;
					ComponentInfo.ComponentOffset = FoundHalfs++;
					ComponentInfo.bIsHalf = true;
				}
				else if (Property->IsA(FIntProperty::StaticClass()))
				{
					FComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
					ComponentInfo.Name = PropertyName;
					ComponentInfo.ComponentOffset = FoundInt32s++;
					ComponentInfo.bIsInt32 = true;
					ComponentInfo.bShowAsBool = (NumProperties == 1) && (Struct == FNiagaraTypeDefinition::GetBoolStruct());
				}
				else if ( Property->IsA(FBoolProperty::StaticClass()))
				{
					FComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
					ComponentInfo.Name = PropertyName;
					ComponentInfo.ComponentOffset = FoundInt32s++;
					ComponentInfo.bIsInt32 = true;
					ComponentInfo.bShowAsBool = true;
				}
				else if (Property->IsA(FEnumProperty::StaticClass()))
				{
					FComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
					ComponentInfo.Name = PropertyName;
					ComponentInfo.ComponentOffset = FoundInt32s++;
					ComponentInfo.bIsInt32 = true;
					ComponentInfo.Enum = CastFieldChecked<FEnumProperty>(Property)->GetEnum();
				}
				else if ( Property->IsA(FStructProperty::StaticClass()) )
				{
					FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(Property);
					Build(PropertyName, FNiagaraTypeHelper::FindNiagaraFriendlyTopLevelStruct(StructProperty->Struct, ENiagaraStructConversion::Simulation));
				}
				else
				{
					// Fail
				}
			}
		}
	};

	class SSimCacheDataBufferRowWidget : public SMultiColumnTableRow<TSharedPtr<int32>>
	{
	public:
		SLATE_BEGIN_ARGS(SSimCacheDataBufferRowWidget) {}
			SLATE_ARGUMENT(TSharedPtr<int32>, RowIndexPtr)
			SLATE_ARGUMENT(TSharedPtr<FSimCacheBufferReader>, BufferReader)
		SLATE_END_ARGS()

		/** Construct function for this widget */
		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
		{
			RowIndexPtr = InArgs._RowIndexPtr;
			BufferReader = InArgs._BufferReader;

			SMultiColumnTableRow<TSharedPtr<int32>>::Construct(
				FSuperRowType::FArguments()
				.Style(FAppStyle::Get(), "DataTableEditor.CellListViewRow"),
				InOwnerTableView
			);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
		{
			const int32 InstanceIndex = *RowIndexPtr;
			if ( InColumnName == NAME_Instance )
			{
				return SNew(STextBlock)
					.Text(FText::AsNumber(InstanceIndex));
			}
			else
			{
				return SNew(STextBlock)
					.Text(BufferReader->GetComponentText(InColumnName, InstanceIndex));
			}
		}

		TSharedPtr<int32>						RowIndexPtr;
		TSharedPtr<FSimCacheBufferReader>		BufferReader;
	};

	class SSimCacheDataBufferWidget : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SSimCacheDataBufferWidget) {}
			SLATE_ARGUMENT(UNiagaraSimCache*, SimCache)
		SLATE_END_ARGS()

		using FBufferSelectionInfo = TPair<int32, FText>;

		void Construct(const FArguments& InArgs)
		{
			UNiagaraSimCache* SimCache = InArgs._SimCache;
			WeakSimCache = SimCache;

			BufferReader = MakeShared<FSimCacheBufferReader>(SimCache);

			HeaderRowWidget = SNew(SHeaderRow);

			UpdateColumns(false);
			UpdateRows(false);
			UpdateBufferSelectionList();

			TSharedRef<SScrollBar> HorizontalScrollBar =
				SNew(SScrollBar)
				.AlwaysShowScrollbar(true)
				.Thickness(12.0f)
				.Orientation(Orient_Horizontal);
			TSharedRef<SScrollBar> VerticalScrollBar =
				SNew(SScrollBar)
				.AlwaysShowScrollbar(true)
				.Thickness(12.0f)
				.Orientation(Orient_Vertical);

			// Widget
			ChildSlot
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					// Select cache buffer
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(10.0f)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Right)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CacheBufferSelection", "Cache Buffer Selection"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					[
						SNew(SComboBox<TSharedPtr<FBufferSelectionInfo>>)
						.OptionsSource(&BufferSelectionList)
						.OnGenerateWidget(this, &SSimCacheDataBufferWidget::BufferSelectionGenerateWidget)
						.OnSelectionChanged(this, &SSimCacheDataBufferWidget::BufferSelectionChanged)
						.InitiallySelectedItem(BufferSelectionList[0])
						[
							SNew(STextBlock)
							.Text(this, &SSimCacheDataBufferWidget::GetBufferSelectionText)
						]
					]
					// Select Cache Frame
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(10.0f)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Right)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("FrameIndex", "Frame Index"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					[
						SNew(SSpinBox<int32>)
						.MinValue(0)
						.MaxValue(this, &SSimCacheDataBufferWidget::GetNumFrames)
						.Value(this, &SSimCacheDataBufferWidget::GetFrameIndex)
						.OnValueChanged(this, &SSimCacheDataBufferWidget::SetFrameIndex)
					]
					// Component Filter
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(10.0f)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Right)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ComponentFilter", "Component Filter"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					[
						SNew(SEditableTextBox)
						.OnTextChanged(this, &SSimCacheDataBufferWidget::OnComponentFilterChange)
						.MinDesiredWidth(100)
					]
				]
				//// Main Spreadsheet View
				+SVerticalBox::Slot()
				//.FillHeight(1)
				//.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					[
						SNew(SScrollBox)
						.Orientation(Orient_Horizontal)
						.ExternalScrollbar(HorizontalScrollBar)
						+SScrollBox::Slot()
						[
							SAssignNew(ListViewWidget, SListView<TSharedPtr<int32>>)
							.ListItemsSource(&RowItems)
							.OnGenerateRow(this, &SSimCacheDataBufferWidget::MakeRowWidget)
							.Visibility(EVisibility::Visible)
							.SelectionMode(ESelectionMode::Single)
							.ExternalScrollbar(VerticalScrollBar)
							.HeaderRow(HeaderRowWidget)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						VerticalScrollBar
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					HorizontalScrollBar
				]
			];
		}

		TSharedRef<ITableRow> MakeRowWidget(const TSharedPtr<int32> RowIndexPtr, const TSharedRef<STableViewBase>& OwnerTable) const
		{
			return
				SNew(SSimCacheDataBufferRowWidget, OwnerTable)
				.RowIndexPtr(RowIndexPtr)
				.BufferReader(BufferReader);
		}

		void UpdateColumns(const bool bRefresh)
		{
			HeaderRowWidget->ClearColumns();

			HeaderRowWidget->AddColumn(
				SHeaderRow::Column(NAME_Instance)
				.DefaultLabel(FText::FromName(NAME_Instance))
				.HAlignHeader(EHorizontalAlignment::HAlign_Center)
				.VAlignHeader(EVerticalAlignment::VAlign_Fill)
				.HAlignCell(EHorizontalAlignment::HAlign_Center)
				.VAlignCell(EVerticalAlignment::VAlign_Fill)
				.SortMode(EColumnSortMode::None)
			);

			const bool bFilterActive = ComponentFilterArray.Num() > 0;
			for (const FSimCacheBufferReader::FComponentInfo& ComponentInfo : BufferReader->GetComponentInfos())
			{
				if (bFilterActive)
				{
					const FString ComponentInfoString = ComponentInfo.Name.ToString();
					const bool bPassedFilter = ComponentFilterArray.ContainsByPredicate(
						[ComponentInfoString=ComponentInfo.Name.ToString()](const FString& ComponentFilter)
						{
							return ComponentInfoString.Contains(ComponentFilter);
						}
					);
					if ( bPassedFilter == false)
					{
						continue;
					}
				}

				HeaderRowWidget->AddColumn(
					SHeaderRow::Column(ComponentInfo.Name)
					.DefaultLabel(FText::FromName(ComponentInfo.Name))
					.HAlignHeader(EHorizontalAlignment::HAlign_Center)
					.VAlignHeader(EVerticalAlignment::VAlign_Fill)
					.HAlignCell(EHorizontalAlignment::HAlign_Center)
					.VAlignCell(EVerticalAlignment::VAlign_Fill)
					.SortMode(EColumnSortMode::None)
				);
			}

			HeaderRowWidget->ResetColumnWidths();
			HeaderRowWidget->RefreshColumns();
			if (bRefresh && ListViewWidget)
			{
				ListViewWidget->RequestListRefresh();
			}
		}

		void UpdateRows(const bool bRefresh)
		{
			RowItems.Reset(BufferReader->GetNumInstances());
			for (int32 i = 0; i < BufferReader->GetNumInstances(); ++i)
			{
				RowItems.Emplace(MakeShared<int32>(i));
			}

			if (bRefresh && ListViewWidget )
			{
				ListViewWidget->RequestListRefresh();
			}
		}

		void UpdateBufferSelectionList()
		{
			BufferSelectionList.Empty();

			UNiagaraSimCache* SimCache = WeakSimCache.Get();
			if (SimCache && SimCache->IsCacheValid())
			{
				BufferSelectionList.Emplace(MakeShared<FBufferSelectionInfo>(INDEX_NONE, LOCTEXT("SystemInstance", "System Instance")));
				for ( int32 i=0; i < SimCache->CacheLayout.EmitterLayouts.Num(); ++i )
				{
					BufferSelectionList.Emplace(
						MakeShared<FBufferSelectionInfo>(i, FText::Format(LOCTEXT("EmitterFormat", "Emitter - {0}"), FText::FromName(SimCache->CacheLayout.EmitterLayouts[i].LayoutName)))
					);
				}
			}
			else
			{
				BufferSelectionList.Emplace(MakeShared<FBufferSelectionInfo>(INDEX_NONE, LOCTEXT("InvalidCache", "Invalid Cache")));
			}
		}

		TOptional<int32> GetNumFrames() const { return BufferReader->GetNumFrames(); }
		int32 GetFrameIndex() const { return BufferReader->GetFrameIndex(); }

		void SetFrameIndex(int32 Value)
		{
			BufferReader->SetFrameIndex(Value);
			UpdateRows(true);
		}

		TSharedRef<SWidget> BufferSelectionGenerateWidget(TSharedPtr<FBufferSelectionInfo> InItem)
		{
			return
				SNew(STextBlock)
				.Text(InItem->Value)
				.Font(IDetailLayoutBuilder::GetDetailFont());
		}

		void BufferSelectionChanged(TSharedPtr<FBufferSelectionInfo> NewSelection, ESelectInfo::Type SelectInfo)
		{
			BufferReader->SetEmitterIndex(NewSelection.IsValid() ? NewSelection->Key : INDEX_NONE);
			UpdateColumns(false);
			UpdateRows(true);
		}

		FText GetBufferSelectionText() const
		{
			const int32 EmitterIndex = BufferReader->GetEmitterIndex();
			for ( const TSharedPtr<FBufferSelectionInfo>& SelectionInfo : BufferSelectionList )
			{
				if ( SelectionInfo->Key == EmitterIndex )
				{
					return SelectionInfo->Value;
				}
			}
			return FText::GetEmpty();
		}

		void OnComponentFilterChange(const FText& InFilter)
		{
			ComponentFilterArray.Empty();
			InFilter.ToString().ParseIntoArray(ComponentFilterArray, TEXT(","));
			UpdateColumns(true);
		}

		TWeakObjectPtr<UNiagaraSimCache>			WeakSimCache;
		TArray<TSharedPtr<int32>>					RowItems;
		TSharedPtr<FSimCacheBufferReader>			BufferReader;

		TArray<TSharedPtr<FBufferSelectionInfo>>	BufferSelectionList;

		int32										FrameIndex = 0;
		
		TSharedPtr<SHeaderRow>						HeaderRowWidget;
		TSharedPtr<SListView<TSharedPtr<int32>>>	ListViewWidget;

		TArray<FString>								ComponentFilterArray;
	};
}

TSharedRef<IDetailCustomization> FNiagaraSimCacheDetailsCustomization::MakeInstance()
{
	return MakeShared<FNiagaraSimCacheDetailsCustomization>();
}

void FNiagaraSimCacheDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	static const FName NAME_SimCache("SimCache");

	TArray<TWeakObjectPtr<UObject>> ObjectsCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsCustomized);
	if (ObjectsCustomized.Num() != 1 || !ObjectsCustomized[0]->IsA<UNiagaraSimCache>())
	{
		return;
	}

	UNiagaraSimCache* SimcCache = CastChecked<UNiagaraSimCache>(ObjectsCustomized[0]);
	IDetailCategoryBuilder& DetailCategory = DetailBuilder.EditCategory(NAME_SimCache);

	DetailCategory.AddCustomRow(LOCTEXT("DataBuffers", "DataBuffers"))
	.WholeRowWidget
	[
		SNew(NiagaraSimCacheDetailsCustomizationLocal::SSimCacheDataBufferWidget)
		.SimCache(SimcCache)
	];
}

#undef LOCTEXT_NAMESPACE
