// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPropertyView.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "SRCProtocolShared.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RemoteControlProtocolWidgets"

int32 SPropertyView::DesiredWidth = 400.f;

void SPropertyView::Construct(const FArguments& InArgs)
{
	bRefreshObjectToDisplay = false;
	Object = InArgs._Object;
	RootPropertyName = InArgs._RootPropertyName;
	NameVisibility = InArgs._NameVisibility;
	DisplayNameOverride = InArgs._DisplayName;
	Struct = InArgs._Struct;
	Spacing = InArgs._Spacing;
	bColumnPadding = InArgs._ColumnPadding;
	bResizableColumn = InArgs._ResizableColumn;

	if (InArgs._ColumnSizeData.IsValid())
	{
		ColumnSizeData = InArgs._ColumnSizeData;
	}
	else
	{
		ColumnWidth = 0.7f;
		ColumnSizeData = MakeShared<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData>();
		ColumnSizeData->LeftColumnWidth = TAttribute<float>(this, &SPropertyView::OnGetLeftColumnWidth);
		ColumnSizeData->RightColumnWidth = TAttribute<float>(this, &SPropertyView::OnGetRightColumnWidth);
		ColumnSizeData->OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SPropertyView::OnSetColumnWidth);
	}

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	const FPropertyRowGeneratorArgs Args;
	Generator = PropertyEditorModule.CreatePropertyRowGenerator(Args);

	if(Object)
	{
		TArray<UObject*> Objects;
		Objects.Add(Object);
		Generator->SetObjects(Objects);
	}
	else if(Struct.IsValid())
	{
		Generator->SetStructure(Struct);
	}

	OnPropertyChangedHandle = Generator->OnFinishedChangingProperties().AddSP(this, &SPropertyView::OnPropertyChanged);

	if (GEditor)
	{
		OnObjectReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddSP(this, &SPropertyView::OnObjectReplaced);
		OnObjectTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddSP(this, &SPropertyView::OnObjectTransacted);
	}

	Construct();
}

void SPropertyView::Construct()
{
	Property.Reset();
	
	if (Object || Struct.IsValid())
	{
		GridPanel = SNew(SGridPanel).FillColumn(0.0f, 1.0f);
		TArray<TSharedRef<IDetailTreeNode>> RootNodes = Generator->GetRootTreeNodes();
		
		if(RootNodes.Num() > 0)
		{
			TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
			RootNodes[0]->GetChildren(ChildNodes);
			TSharedRef<IDetailTreeNode>* FoundNode = ChildNodes.FindByPredicate([&](const TSharedRef<IDetailTreeNode>& InNode)
			{
				if(const TSharedPtr<IPropertyHandle> PropertyHandle = InNode->CreatePropertyHandle())
				{
					return PropertyHandle->GetProperty()->GetFName() == RootPropertyName;
				}
				return false;
			});

			if(FoundNode)
			{
				Property = FoundNode->Get().CreatePropertyHandle();
				RootNodes.Empty();
				RootNodes.Add(*FoundNode);
			}
		}

		int32 Index = 0;
		AddWidgets(RootNodes, Index, 0.0f);

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(1.f, 5.0f)
			[
				GridPanel.ToSharedRef()
			]
		];
	}
	else
	{
		const FText ErrorText = LOCTEXT("InvalidObject", "Error: Not a valid Object");
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(5.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				  .HAlign(HAlign_Left)
				  .VAlign(VAlign_Center)
				  .FillWidth(1.0f)
				[
					SNew(STextBlock)
                    .Font(IDetailLayoutBuilder::GetDetailFontBold())
                    .Text(ErrorText)
                    .Margin(FMargin(5.0f, 5.0f, 0.0f, 0.0f))
                    .ColorAndOpacity(FLinearColor(1, 0, 0, 1))
				]
			]
		];
	}
}

SPropertyView::~SPropertyView()
{
	Generator->OnFinishedChangingProperties().Remove(OnPropertyChangedHandle);

	if (GEditor)
	{
		FCoreUObjectDelegates::OnObjectsReplaced.Remove(OnObjectReplacedHandle);
		FCoreUObjectDelegates::OnObjectTransacted.Remove(OnObjectTransactedHandle);
	}
}

void SPropertyView::SetProperty(UObject* InObject, const FName InPropertyName)
{
	UObject* NewObjectToDisplay = InObject;
	if (Object != NewObjectToDisplay)
	{
		Object = NewObjectToDisplay;
		RootPropertyName = InPropertyName;
		Struct.Reset();
		Refresh();
	}
}

void SPropertyView::SetStruct(UObject* InObject, TSharedPtr<FStructOnScope>& InStruct)
{
	UObject* NewObjectToDisplay = InObject;
	if(Object != NewObjectToDisplay)
	{
		Object = NewObjectToDisplay;
		Struct = InStruct;
		Property = nullptr;
		Refresh();
	}
}

bool SPropertyView::CustomPrepass(float LayoutScaleMultiplier)
{
	if(bRefreshObjectToDisplay)
	{
		if(Object)
		{
			TArray<UObject*> Objects;
			Objects.Add(Object);
			Generator->SetObjects(Objects);
		}
		else if(Struct.IsValid())
		{
			Generator->SetStructure(Struct);
		}
		Construct();
		bRefreshObjectToDisplay = false;
	}
	
	return true;
}

void SPropertyView::AddReferencedObjects(FReferenceCollector& InCollector)
{
	InCollector.AddReferencedObject(Object);
	if(Property.IsValid())
	{
		if(FProperty* Prp = Property->GetProperty())
		{
			Prp->AddReferencedObjects(InCollector);
		}		
	}
}

void SPropertyView::Refresh()
{
	// forces CustomPrepass to be called, recreating widgets ie. for array items. Without this array items won't add/remove.
	Invalidate(EInvalidateWidgetReason::Prepass);
	bRefreshObjectToDisplay = true;
}

void SPropertyView::AddWidgets(const TArray<TSharedRef<IDetailTreeNode>>& InDetailTree, int32& InIndex, float InLeftPadding)
{
	// Check type and metadata for visibility/editability
	auto IsDisplayable = [](const TSharedPtr<IPropertyHandle>& InPropertyHandle)
	{
		if (InPropertyHandle.IsValid() && InPropertyHandle->IsValidHandle() && InPropertyHandle->IsEditable())
		{
			FProperty* PropertyToVerify = InPropertyHandle->GetProperty();

			if (PropertyToVerify)
			{
				if (const FFieldVariant Outer = PropertyToVerify->GetOwnerVariant())
				{
					// if the outer is a container property (array,set or map) it's editable even without the proper flags.
					if (Outer.IsA<FArrayProperty>() || Outer.IsA<FSetProperty>() || Outer.IsA<FMapProperty>())
					{
						return true;
					}
				}

				return PropertyToVerify && !PropertyToVerify->HasAnyPropertyFlags(CPF_DisableEditOnInstance) && PropertyToVerify->HasAnyPropertyFlags(CPF_Edit);
			}
		}

		// Ok to display DetailNode without property because at this stage the parent property was displayable
		return true;
	};

	// Check if array, map, set
	auto IsContainer = [](const TSharedPtr<IPropertyHandle>& InPropertyHandle)
	{
		if (InPropertyHandle.IsValid() && InPropertyHandle->IsValidHandle() && InPropertyHandle->IsEditable())
		{
			if (FProperty* PropertyToVerify = InPropertyHandle->GetProperty())
			{
				FFieldClass* PropertyClass = PropertyToVerify->GetClass();
				if (PropertyClass == FArrayProperty::StaticClass()
					|| PropertyClass == FSetProperty::StaticClass()
					|| PropertyClass == FMapProperty::StaticClass())
				{
					return !PropertyToVerify->HasAnyPropertyFlags(CPF_DisableEditOnInstance) && PropertyToVerify->HasAnyPropertyFlags(CPF_Edit);
				}
			}
		}

		return false;
	};

	for (const TSharedRef<IDetailTreeNode>& ChildNode : InDetailTree)
	{
		TSharedPtr<IPropertyHandle> PropertyHandle = ChildNode->CreatePropertyHandle();
		if (ChildNode->GetNodeType() == EDetailNodeType::Category)
		{
			if (InIndex > 0)
			{
				GridPanel->AddSlot(0, InIndex)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					  .VAlign(VAlign_Center)
					  .HAlign(HAlign_Left)
					[
						SNew(SSpacer)
						.Size(FVector2D(0.f, 10.f))
					]
				];
				InIndex++;
			}

			TArray<TSharedRef<IDetailTreeNode>> Children;
			ChildNode->GetChildren(Children);
			AddWidgets(Children, InIndex, InLeftPadding);
		}
		else if (IsContainer(PropertyHandle))
		{
			TSharedPtr<IDetailPropertyRow> DetailPropertyRow = ChildNode->GetRow();
			if (DetailPropertyRow.IsValid())
			{
				FDetailWidgetRow Row;
				TSharedPtr<SWidget> NameWidget;
				TSharedPtr<SWidget> ValueWidget;
				DetailPropertyRow->GetDefaultWidgets(NameWidget, ValueWidget, Row, true);

				CreateDefaultWidget(FPropertyWidgetCreationArgs(InIndex, NameWidget, ValueWidget, InLeftPadding));
				InIndex++;

				// @todo: this needs to update when items added/removed from container
				TArray<TSharedRef<IDetailTreeNode>> Children;
				ChildNode->GetChildren(Children);
				if (Children.Num() > 0)
				{
					// #ueent_todo: Find a way to add collapse/expand capability for property with children
					AddWidgets(Children, InIndex, InLeftPadding + 10.f);
				}
			}
		}
		else if (IsDisplayable(PropertyHandle))
		{
			TSharedPtr<SWidget> NameWidget;
			TSharedPtr<SWidget> ValueWidget;
			EHorizontalAlignment HAlign;
			EVerticalAlignment VAlign;
			TOptional<float> MinWidth;
			TOptional<float> MaxWidth;

			TSharedPtr<IDetailPropertyRow> DetailPropertyRow = ChildNode->GetRow();
			// Overrides the top-level property display name, if specified
			if(InIndex == 0 && DisplayNameOverride.IsSet())
			{
				DetailPropertyRow->DisplayName(DisplayNameOverride.GetValue());
			}
			
			if (DetailPropertyRow.IsValid())
			{
				FDetailWidgetRow Row;
				DetailPropertyRow->GetDefaultWidgets(NameWidget, ValueWidget, Row, true);
				HAlign = Row.ValueWidget.HorizontalAlignment;
				VAlign = Row.ValueWidget.VerticalAlignment;
				MinWidth = Row.ValueWidget.MinWidth;
				MaxWidth = Row.ValueWidget.MaxWidth;
			}
			else 
			{
				FNodeWidgets NodeWidgets = ChildNode->CreateNodeWidgets();

				NameWidget = NodeWidgets.NameWidget;
				ValueWidget = NodeWidgets.ValueWidget;
				HAlign = NodeWidgets.ValueWidgetLayoutData.HorizontalAlignment;
				VAlign = NodeWidgets.ValueWidgetLayoutData.VerticalAlignment;
				MinWidth = NodeWidgets.ValueWidgetLayoutData.MinWidth;
				MaxWidth = NodeWidgets.ValueWidgetLayoutData.MaxWidth;
			}

			if (NameWidget.IsValid() && ValueWidget.IsValid())
			{
				bool bDisplayChildren = true;

				// Do not display children if the property is a FVector or FVector2D
				// @todo: find way of detecting from customization if this only requires single row, rather than by checking type
				if (PropertyHandle.IsValid())
				{
					FVector DummyVec;
					FVector2D DummyVec2D;

					bDisplayChildren &= PropertyHandle->GetValue(DummyVec) == FPropertyAccess::Fail;
					bDisplayChildren &= PropertyHandle->GetValue(DummyVec2D) == FPropertyAccess::Fail;
				}

				InIndex++;

				TArray<TSharedRef<IDetailTreeNode>> Children;
				ChildNode->GetChildren(Children);
				if (bDisplayChildren && Children.Num() > 0)
				{
					// #ueent_todo: Find a way to add collapse/expand capability for property with children
					AddWidgets(Children, InIndex, InLeftPadding + 10.f);
				}
				else
				{
					// If root widget, single line (no children, or bDisplayChildren == false, disable name widget + column, and not forcibly shown
					if((InIndex <= 1 && NameVisibility != EPropertyNameVisibility::Show)
					// Or if forcibly hidden 
						|| NameVisibility == EPropertyNameVisibility::Hide)
					{
						NameWidget.Reset();
					}
					
					// Only creates row if no children or bDisplayChildren == false (single row property), so need to reverse index
					CreateDefaultWidget(FPropertyWidgetCreationArgs(InIndex - 1, NameWidget, ValueWidget, InLeftPadding, MinWidth, MaxWidth));
				}
			}
		}
	}
}

TSharedRef<SWidget> SPropertyView::CreatePropertyWidget(const FPropertyWidgetCreationArgs& InCreationArgs)
{
	if (InCreationArgs.HasNameWidget() && InCreationArgs.bResizableColumn)
	{
		return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Top)
        .HAlign(HAlign_Fill)
        .Padding(0.0f, 0.0f, 0.0f, InCreationArgs.Spacing)
        [
            SNew(RemoteControlProtocolWidgetUtils::SCustomSplitter)
            .LeftWidget(InCreationArgs.NameWidget)
            .RightWidget(InCreationArgs.ValueWidget)
            .ColumnSizeData(InCreationArgs.ColumnSizeData)
        ];
	}
	else
	{
		TSharedRef<SHorizontalBox> NameValuePairWidget = SNew(SHorizontalBox);

		// Prepend name widget if present
		if(InCreationArgs.NameWidget.IsValid())
		{
			NameValuePairWidget
            ->AddSlot()
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
            .AutoWidth()
            [
                InCreationArgs.NameWidget.ToSharedRef()
            ];
		}

		NameValuePairWidget
        ->AddSlot()
        .AutoWidth()
        [
            SNew(SHorizontalBox)
            .Clipping(EWidgetClipping::OnDemand)
            + SHorizontalBox::Slot()
            [
                SNew(RemoteControlProtocolWidgetUtils::SConstrainedBox)
                .MinWidth(InCreationArgs.ValueMinWidth)
                .MaxWidth(InCreationArgs.ValueMaxWidth)
                [
                    InCreationArgs.ValueWidget.ToSharedRef()
                ]
            ]
        ];
		
		return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Top)
        .HAlign(HAlign_Fill)
        .Padding(0.0f, 0.0f, 0.0f, InCreationArgs.Spacing)
        [
            NameValuePairWidget
        ];
	}
}

void SPropertyView::CreateDefaultWidget(const FPropertyWidgetCreationArgs& InCreationArgs)
{
	TSharedPtr<SHorizontalBox> NameColumn = nullptr;
	if(InCreationArgs.NameWidget.IsValid())
	{
		NameColumn = SNew(SHorizontalBox)
                    .Clipping(EWidgetClipping::OnDemand);

		InCreationArgs.NameWidget->SetClipping(EWidgetClipping::OnDemand);

		// Add the name widget
		NameColumn->AddSlot()
          .VAlign(VAlign_Fill)
          .HAlign(HAlign_Left)
          .Padding(FMargin(InCreationArgs.LeftPadding, 0.f, 0.f, 0.f))
           [
                InCreationArgs.NameWidget.ToSharedRef()
           ];
	}

	GridPanel->AddSlot(0, InCreationArgs.Index)
    [
        CreatePropertyWidget(FPropertyWidgetCreationArgs(InCreationArgs, NameColumn, ColumnSizeData, Spacing, bResizableColumn))
    ];
}


void SPropertyView::OnPropertyChanged(const FPropertyChangedEvent& InEvent)
{
	if(Property.IsValid() && Property->GetProperty() == InEvent.Property)
	{
		Refresh();		
	}
}

void SPropertyView::OnObjectReplaced(const TMap<UObject*, UObject*>& InReplacementObjectMap)
{
	if (UObject* const* ObjectPtr = InReplacementObjectMap.Find(Object))
	{
		Object = *ObjectPtr;
		Refresh();
	}
}

void SPropertyView::OnObjectTransacted(UObject* InObject, const FTransactionObjectEvent& InTransactionObjectEvent)
{
	if(InObject == Object || (Object && Object->GetOuter() == InObject))
	{
		Refresh();
	}
}

#undef LOCTEXT_NAMESPACE
