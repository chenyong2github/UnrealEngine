// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RemoteControlField.h"
#include "UI/Components/SRenderPagesRemoteControlEntity.h"
#include "UI/Components/SRenderPagesRemoteControlTreeNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"


class IDetailTreeNode;
class URemoteControlPreset;
enum class EExposedFieldType : uint8;
struct FGuid;
struct FRemoteControlField;
struct FRenderPagesGenerateWidgetArgs;


namespace UE::RenderPages::Private
{
	/**
	 * The remote control field child node widget, copied over from the remote control plugin, and slightly modified and cleaned up for usage in the render pages plugin.
	 *
	 * Represents a child of an exposed field widget.
	 */
	struct SRenderPagesRemoteControlFieldChildNode : SRenderPagesRemoteControlTreeNode
	{
		SLATE_BEGIN_ARGS(SRenderPagesRemoteControlFieldChildNode) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<IDetailTreeNode>& InNode, FRenderPagesRemoteControlColumnSizeData InColumnSizeData);
		virtual void GetNodeChildren(TArray<TSharedPtr<SRenderPagesRemoteControlTreeNode>>& OutChildren) const override { return OutChildren.Append(ChildrenNodes); }
		virtual FGuid GetRCId() const override { return FGuid(); }
		virtual ENodeType GetRCType() const override { return ENodeType::FieldChild; }

		TArray<TSharedPtr<SRenderPagesRemoteControlFieldChildNode>> ChildrenNodes;
	};


	/**
	 * The remote control field widget, copied over from the remote control plugin, and slightly modified and cleaned up for usage in the render pages plugin.
	 *
	 * Widget that displays an exposed field.
	 */
	struct SRenderPagesRemoteControlField : SRenderPagesRemoteControlEntity
	{
		SLATE_BEGIN_ARGS(SRenderPagesRemoteControlField)
				: _Preset(nullptr) {}
			SLATE_ATTRIBUTE(URemoteControlPreset*, Preset)
		SLATE_END_ARGS()

		static TSharedPtr<SRenderPagesRemoteControlTreeNode> MakeInstance(const FRenderPagesRemoteControlGenerateWidgetArgs& Args);

		void Construct(const FArguments& InArgs, TWeakPtr<FRemoteControlField> Field, FRenderPagesRemoteControlColumnSizeData ColumnSizeData);

		//~ SRenderPagesTreeNode Interface 
		virtual void GetNodeChildren(TArray<TSharedPtr<SRenderPagesRemoteControlTreeNode>>& OutChildren) const override;
		virtual ENodeType GetRCType() const override;
		virtual void Refresh() override;
		virtual void RefreshValue() override;
		//~ End SRenderPagesTreeNode Interface

		/** Get a weak pointer to the underlying remote control field. */
		TWeakPtr<FRemoteControlField> GetRemoteControlField() const { return FieldWeakPtr; }

		/** Get this field's label. */
		FName GetFieldLabel() const;

		/** Get this field's type. */
		EExposedFieldType GetFieldType() const;

		/** Returns this widget's underlying objects. */
		void GetBoundObjects(TSet<UObject*>& OutBoundObjects) const;

	private:
		/** Construct a property widget. */
		TSharedRef<SWidget> ConstructWidget();
		
		/** Create the wrapper around the field value widget. */
		TSharedRef<SWidget> MakeFieldWidget(const TSharedRef<SWidget>& InWidget);
		
		/** Construct this field widget as a property widget. */
		void ConstructPropertyWidget();
		
	private:
		/** Weak pointer to the underlying RC Field. */
		TWeakPtr<FRemoteControlField> FieldWeakPtr;
		
		/** This exposed field's child widgets (ie. An array's rows) */
		TArray<TSharedPtr<SRenderPagesRemoteControlFieldChildNode>> ChildWidgets;
		
		/** The property row generator. */
		TSharedPtr<IPropertyRowGenerator> Generator;
	};
}
