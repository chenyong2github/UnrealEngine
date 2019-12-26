// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "IPropertyUtilities.h"
#include "DetailTreeNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "SDetailsViewBase.h"
#include "SDetailTableRowBase.h"

class IDetailRootObjectCustomization;

class SDetailMultiTopLevelObjectTableRow : public SDetailTableRowBase
{
public:
	SLATE_BEGIN_ARGS(SDetailMultiTopLevelObjectTableRow)
		: _DisplayName()
		, _ShowExpansionArrow(false)
	{}
		SLATE_ARGUMENT( FText, DisplayName )
		SLATE_ARGUMENT( bool, ShowExpansionArrow )
	SLATE_END_ARGS()

	/**
	* Use this construct when the toggle functionality (with the SExpanderArrow) is not desired.
	* GenerateStandaloneWidget(Row) must be called before this function.
	*/
	void Construct( const FArguments& InArgs, const TSharedRef<FDetailTreeNode>& InOwnerTreeNode, const TSharedRef<SWidget>& InCustomizedWidgetContents, const TSharedRef<STableViewBase>& InOwnerTableView );
	/**
	* Use this construct together with ChildSlotConstruct when the toggle functionality (with the SExpanderArrow) is desired.
	* GenerateStandaloneWidget(Row, DetailMultiTopLevelObjectTableRow) and ChildSlotConstruct(Row.NameWidget.Widget, OwnerTable) must be called after this function.
	*/
	void Construct( const FArguments& InArgs, const TSharedRef<FDetailTreeNode>& InOwnerTreeNode );
	void ChildSlotConstruct(const TSharedRef<SWidget>& InCustomizedWidgetContents, const TSharedRef<STableViewBase>& InOwnerTableView);
private:
	const FSlateBrush* GetBackgroundImage() const;
private:
	virtual FReply OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;
	virtual FReply OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent ) override;
private:
	bool bShowExpansionArrow;
};


class FDetailMultiTopLevelObjectRootNode : public FDetailTreeNode, public TSharedFromThis<FDetailMultiTopLevelObjectRootNode>
{
public:
	FDetailMultiTopLevelObjectRootNode( const FDetailNodeList& InChildNodes, const TSharedPtr<IDetailRootObjectCustomization>& RootObjectCustomization, IDetailsViewPrivate* InDetailsView, const UObject& InRootObject );

private:
	virtual IDetailsViewPrivate* GetDetailsView() const override{ return DetailsView; }
	virtual void OnItemExpansionChanged( bool bIsExpanded, bool bShouldSaveState ) override {}
	virtual bool ShouldBeExpanded() const override { return true; }
	virtual ENodeVisibility GetVisibility() const override;
	virtual TSharedRef< ITableRow > GenerateWidgetForTableView( const TSharedRef<STableViewBase>& OwnerTable, const FDetailColumnSizeData& ColumnSizeData, bool bAllowFavoriteSystem) override;
	/**
	* Use this version of GenerateStandaloneWidget when the toggle functionality (with the SExpanderArrow) is not used.
	*/
	virtual bool GenerateStandaloneWidget(FDetailWidgetRow& OutRow) const override;
	/**
	* Use this version of GenerateStandaloneWidget when the toggle functionality (with the SExpanderArrow) is being used.
	*/
	virtual bool GenerateStandaloneWidget(FDetailWidgetRow& OutRow, const TSharedRef<ITableRow>& InTableRow) const;
	virtual void GetChildren(FDetailNodeList& OutChildren )  override;
	virtual void FilterNode( const FDetailFilter& InFilter ) override;
	virtual void Tick( float DeltaTime ) override {}
	virtual bool ShouldShowOnlyChildren() const override;
	virtual FName GetNodeName() const override { return NodeName; }
	virtual EDetailNodeType GetNodeType() const override { return EDetailNodeType::Object; }
	virtual TSharedPtr<IPropertyHandle> CreatePropertyHandle() const override { return nullptr; }
private:
	FDetailNodeList ChildNodes;
	IDetailsViewPrivate* DetailsView;
	TWeakPtr<IDetailRootObjectCustomization> RootObjectCustomization;
	const TWeakObjectPtr<UObject> RootObject;
	FName NodeName;
	bool bShouldBeVisible;
};
