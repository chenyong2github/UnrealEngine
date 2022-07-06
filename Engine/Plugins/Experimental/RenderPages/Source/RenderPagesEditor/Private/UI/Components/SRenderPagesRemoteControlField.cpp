// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRenderPagesRemoteControlField.h"
#include "UI/Utils/RenderPagesWidgetUtils.h"
#include "Algo/Transform.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "RemoteControlField.h"
#include "RemoteControlPreset.h"
#include "SRenderPagesRemoteControlTreeNode.h"
#include "UObject/Object.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "SRenderPagesExposedField"


namespace ExposedFieldUtils
{
	TSharedRef<SWidget> CreateNodeValueWidget(const FNodeWidgets& NodeWidgets)
	{
		TSharedRef<SHorizontalBox> FieldWidget = SNew(SHorizontalBox);

		if (NodeWidgets.ValueWidget)
		{
			FieldWidget->AddSlot()
				.Padding(FMargin(3.0f, 2.0f))
				.HAlign(HAlign_Right)
				.FillWidth(1.0f)
				[
					NodeWidgets.ValueWidget.ToSharedRef()
				];
		}
		else if (NodeWidgets.WholeRowWidget)
		{
			FieldWidget->AddSlot()
				.Padding(FMargin(3.0f, 2.0f))
				.FillWidth(1.0f)
				[
					NodeWidgets.WholeRowWidget.ToSharedRef()
				];
		}

		return FieldWidget;
	}
}

TSharedPtr<UE::RenderPages::Private::SRenderPagesRemoteControlTreeNode> UE::RenderPages::Private::SRenderPagesRemoteControlField::MakeInstance(const FRenderPagesRemoteControlGenerateWidgetArgs& Args)
{
	return SNew(SRenderPagesRemoteControlField, StaticCastSharedPtr<FRemoteControlField>(Args.Entity), Args.ColumnSizeData).Preset(Args.Preset);
}

void UE::RenderPages::Private::SRenderPagesRemoteControlField::Construct(const FArguments& InArgs, TWeakPtr<FRemoteControlField> InField, FRenderPagesRemoteControlColumnSizeData InColumnSizeData)
{
	FieldWeakPtr = MoveTemp(InField);
	ColumnSizeData = MoveTemp(InColumnSizeData);

	if (const TSharedPtr<FRemoteControlField> Field = FieldWeakPtr.Pin())
	{
		Initialize(Field->GetId(), InArgs._Preset.Get());

		CachedLabel = Field->GetLabel();
		EntityId = Field->GetId();

		if (Field->FieldType == EExposedFieldType::Property)
		{
			ConstructPropertyWidget();
		}
	}
}

void UE::RenderPages::Private::SRenderPagesRemoteControlField::GetNodeChildren(TArray<TSharedPtr<SRenderPagesRemoteControlTreeNode>>& OutChildren) const
{
	OutChildren.Append(ChildWidgets);
}

UE::RenderPages::Private::SRenderPagesRemoteControlTreeNode::ENodeType UE::RenderPages::Private::SRenderPagesRemoteControlField::GetRCType() const
{
	return ENodeType::Field;
}

FName UE::RenderPages::Private::SRenderPagesRemoteControlField::GetFieldLabel() const
{
	return CachedLabel;
}

EExposedFieldType UE::RenderPages::Private::SRenderPagesRemoteControlField::GetFieldType() const
{
	if (const TSharedPtr<FRemoteControlField> Field = FieldWeakPtr.Pin())
	{
		return Field->FieldType;
	}
	return EExposedFieldType::Invalid;
}

void UE::RenderPages::Private::SRenderPagesRemoteControlField::Refresh()
{
	if (const TSharedPtr<FRemoteControlField> Field = FieldWeakPtr.Pin())
	{
		CachedLabel = Field->GetLabel();

		if (Field->FieldType == EExposedFieldType::Property)
		{
			ConstructPropertyWidget();
		}
	}
}

void UE::RenderPages::Private::SRenderPagesRemoteControlField::RefreshValue()
{
	if (!Generator.IsValid() || (GetFieldType() != EExposedFieldType::Property))
	{
		Refresh();
		return;
	}

	if (const TSharedPtr<FRemoteControlField> Field = FieldWeakPtr.Pin())
	{
		TArray<UObject*> Objects = Field->GetBoundObjects();
		if (Objects.Num() <= 0)
		{
			Generator->SetObjects({});
			ChildSlot.AttachWidget(MakeFieldWidget(SNullWidget::NullWidget));
			return;
		}

		Generator->SetObjects({Objects[0]});

		if (TSharedPtr<IDetailTreeNode> Node = RenderPagesWidgetUtils::FindNode(Generator->GetRootTreeNodes(), Field->FieldPathInfo.ToPathPropertyString(), RenderPagesWidgetUtils::ERenderPagesFindNodeMethod::Path))
		{
			TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
			Node->GetChildren(ChildNodes);
			ChildWidgets.Reset(ChildNodes.Num());

			for (const TSharedRef<IDetailTreeNode>& ChildNode : ChildNodes)
			{
				ChildWidgets.Add(SNew(SRenderPagesRemoteControlFieldChildNode, ChildNode, ColumnSizeData));
			}

			//TODO:  still causes the value widgets (like the color wheel) to disconnect when this function is called,  maybe Node->CreateNodeWidgets() should be cached
			ChildSlot.AttachWidget(MakeFieldWidget(ExposedFieldUtils::CreateNodeValueWidget(Node->CreateNodeWidgets())));
		}
		else
		{
			ChildSlot.AttachWidget(MakeFieldWidget(SNullWidget::NullWidget));
		}
	}
}

void UE::RenderPages::Private::SRenderPagesRemoteControlField::GetBoundObjects(TSet<UObject*>& OutBoundObjects) const
{
	if (const TSharedPtr<FRemoteControlField> Field = FieldWeakPtr.Pin())
	{
		OutBoundObjects.Append(Field->GetBoundObjects());
	}
}

TSharedRef<SWidget> UE::RenderPages::Private::SRenderPagesRemoteControlField::ConstructWidget()
{
	if (const TSharedPtr<FRemoteControlField> Field = FieldWeakPtr.Pin())
	{
		// For the moment, just use the first object.
		TArray<UObject*> Objects = Field->GetBoundObjects();
		if ((GetFieldType() == EExposedFieldType::Property) && (Objects.Num() > 0))
		{
			FPropertyRowGeneratorArgs Args;
			Generator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(Args);
			Generator->SetObjects({Objects[0]});

			if (TSharedPtr<IDetailTreeNode> Node = RenderPagesWidgetUtils::FindNode(Generator->GetRootTreeNodes(), Field->FieldPathInfo.ToPathPropertyString(), RenderPagesWidgetUtils::ERenderPagesFindNodeMethod::Path))
			{
				TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
				Node->GetChildren(ChildNodes);
				ChildWidgets.Reset(ChildNodes.Num());

				for (const TSharedRef<IDetailTreeNode>& ChildNode : ChildNodes)
				{
					ChildWidgets.Add(SNew(SRenderPagesRemoteControlFieldChildNode, ChildNode, ColumnSizeData));
				}

				return MakeFieldWidget(ExposedFieldUtils::CreateNodeValueWidget(Node->CreateNodeWidgets()));
			}
		}
	}

	return MakeFieldWidget(SNullWidget::NullWidget);
}

TSharedRef<SWidget> UE::RenderPages::Private::SRenderPagesRemoteControlField::MakeFieldWidget(const TSharedRef<SWidget>& InWidget)
{
	return CreateEntityWidget(InWidget);
}

void UE::RenderPages::Private::SRenderPagesRemoteControlField::ConstructPropertyWidget()
{
	ChildSlot.AttachWidget(ConstructWidget());
}


void UE::RenderPages::Private::SRenderPagesRemoteControlFieldChildNode::Construct(const FArguments& InArgs, const TSharedRef<IDetailTreeNode>& InNode, FRenderPagesRemoteControlColumnSizeData InColumnSizeData)
{
	TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
	InNode->GetChildren(ChildNodes);

	Algo::Transform(ChildNodes, ChildrenNodes, [InColumnSizeData](const TSharedRef<IDetailTreeNode>& ChildNode) { return SNew(SRenderPagesRemoteControlFieldChildNode, ChildNode, InColumnSizeData); });

	ColumnSizeData = InColumnSizeData;

	FNodeWidgets Widgets = InNode->CreateNodeWidgets();
	FRenderPagesMakeNodeWidgetArgs Args;
	Args.NameWidget = Widgets.NameWidget;
	Args.ValueWidget = ExposedFieldUtils::CreateNodeValueWidget(InNode->CreateNodeWidgets());

	ChildSlot
	[
		MakeNodeWidget(Args)
	];
}


#undef LOCTEXT_NAMESPACE /*RemoteControlPanel*/
