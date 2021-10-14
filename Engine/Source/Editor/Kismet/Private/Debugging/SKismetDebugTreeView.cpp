// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debugging/SKismetDebugTreeView.h"
#include "Debugging/SKismetDebuggingView.h"
#include "GenericPlatform/GenericPlatformApplicationMisc.h"
#include "HAL/PlatformApplicationMisc.h"
#include "PropertyInfoViewStyle.h"
#include "Widgets/Input/SHyperlink.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Breakpoint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "BlueprintEditor.h"
#include "Debugging/KismetDebugCommands.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateIconFinder.h"
#include "Styling/StyleColors.h"
#include "GraphEditorSettings.h"
#include "SourceCodeNavigation.h"
#include "Widgets/Layout/SSpacer.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/UIAction.h"
#include "Editor/EditorEngine.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "DebugViewUI"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintDebugTreeView, Log, All);

//////////////////////////////////////////////////////////////////////////

/** The editor object */
extern UNREALED_API class UEditorEngine* GEditor;

//////////////////////////////////////////////////////////////////////////

const FName SKismetDebugTreeView::ColumnId_Name("Name");
const FName SKismetDebugTreeView::ColumnId_Value("Value");

//////////////////////////////////////////////////////////////////////////
// FDebugLineItem

uint16 FDebugLineItem::ActiveTypeBitset = TNumericLimits<uint16>::Max(); // set all to active by default

FText FDebugLineItem::GetName() const
{
	return FText::GetEmpty();
}

FText FDebugLineItem::GetDisplayName() const
{
	return FText::GetEmpty();
}

FText FDebugLineItem::GetDescription() const
{
	return FText::GetEmpty();
}

bool FDebugLineItem::HasName() const
{
	return !GetDisplayName().IsEmpty();
}

bool FDebugLineItem::HasValue() const
{
	return !GetDescription().IsEmpty();
}

void FDebugLineItem::CopyNameToClipboard() const
{
	FPlatformApplicationMisc::ClipboardCopy(ToCStr(GetDisplayName().ToString()));
}

void FDebugLineItem::CopyValueToClipboard() const
{
	FPlatformApplicationMisc::ClipboardCopy(ToCStr(GetDescription().ToString()));
}

TSharedRef<SWidget> FDebugLineItem::GenerateNameWidget(TSharedPtr<FString> InSearchString)
{
	return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
		.FullText(this, &FDebugLineItem::GetDisplayName)
		.HighlightText(this, &FDebugLineItem::GetHighlightText, InSearchString)
		[
			SNew(STextBlock)
				.ToolTipText(this, &FDebugLineItem::GetDisplayName)
				.Text(this, &FDebugLineItem::GetDisplayName)
		];
}

TSharedRef<SWidget> FDebugLineItem::GenerateValueWidget(TSharedPtr<FString> InSearchString)
{
	return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
		.FullText(this, &FDebugLineItem::GetDescription)
		.HighlightText(this, &FDebugLineItem::GetHighlightText, InSearchString)
		[
			SNew(STextBlock)
				.ToolTipText(this, &FDebugLineItem::GetDescription)
				.Text(this, &FDebugLineItem::GetDescription)
		];
}

void FDebugLineItem::MakeMenu(FMenuBuilder& MenuBuilder)
{
	const FUIAction CopyName(
		FExecuteAction::CreateRaw(this, &FDebugLineItem::CopyNameToClipboard),
		FCanExecuteAction::CreateRaw(this, &FDebugLineItem::HasName)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CopyName", "Copy Name"),
		LOCTEXT("CopyName_ToolTip", "Copy name to clipboard"),
		FSlateIcon(),
		CopyName
	);

	const FUIAction CopyValue(
		FExecuteAction::CreateRaw(this, &FDebugLineItem::CopyValueToClipboard),
		FCanExecuteAction::CreateRaw(this, &FDebugLineItem::HasValue)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CopyValue", "Copy Value"),
		LOCTEXT("CopyValue_ToolTip", "Copy value to clipboard"),
		FSlateIcon(),
		CopyValue
	);
}

void FDebugLineItem::UpdateSearch(const FString& InSearchString, FDebugLineItem::ESearchFlags SearchFlags)
{
	const bool bIsRootNode = SearchFlags & SF_RootNode;
	const bool bIsContainerElement = SearchFlags & SF_ContainerElement;

	// Container elements share their parent's property name, so we shouldn't search them by name
	bVisible = (!bIsContainerElement && GetName().ToString().Contains(InSearchString)) ||
		GetDisplayName().ToString().Contains(InSearchString) ||
		GetDescription().ToString().Contains(InSearchString);

	// for root nodes, bParentsMatchSearch always matches bVisible
	if (bVisible || bIsRootNode)
	{
		bParentsMatchSearch = bVisible;
	}
}

bool FDebugLineItem::IsVisible()
{
	return bVisible;
}

bool FDebugLineItem::DoParentsMatchSearch()
{
	return bParentsMatchSearch;
}

bool FDebugLineItem::HasChildren() const
{
	return false;
}

TSharedRef<SWidget> FDebugLineItem::GetNameIcon()
{
	static const FSlateBrush* CachedBrush = FEditorStyle::GetBrush(TEXT("NoBrush"));
	return SNew(SImage).Image(CachedBrush);
}

TSharedRef<SWidget> FDebugLineItem::GetValueIcon()
{
	static const FSlateBrush* CachedBrush = FEditorStyle::GetBrush(TEXT("NoBrush"));
	return SNew(SImage).Image(CachedBrush);
}

FText FDebugLineItem::GetHighlightText(const TSharedPtr<FString> InSearchString) const
{
	return FText::FromString(*InSearchString);
}

UBlueprint* FDebugLineItem::GetBlueprintForObject(UObject* ParentObject)
{
	if (ParentObject == nullptr)
	{
		return nullptr;
	}

	if (UBlueprint* ParentBlueprint = Cast<UBlueprint>(ParentObject))
	{
		return ParentBlueprint;
	}

	if (UClass* ParentClass = ParentObject->GetClass())
	{
		if (UBlueprint* ParentBlueprint = Cast<UBlueprint>(ParentClass->ClassGeneratedBy))
		{
			return ParentBlueprint;
		}
	}

	// recursively walk up ownership hierarchy until we find the blueprint
	return GetBlueprintForObject(ParentObject->GetOuter());
}

UBlueprintGeneratedClass* FDebugLineItem::GetClassForObject(UObject* ParentObject)
{
	if (ParentObject != nullptr)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(ParentObject))
		{
			return Cast<UBlueprintGeneratedClass>(*Blueprint->GeneratedClass);
		}
		else if (UBlueprintGeneratedClass* Result = Cast<UBlueprintGeneratedClass>(ParentObject))
		{
			return Result;
		}
		else
		{
			return Cast<UBlueprintGeneratedClass>(ParentObject->GetClass());
		}
	}

	return nullptr;
}

bool FDebugLineItem::IsDebugLineTypeActive(EDebugLineType Type)
{
	const uint16 Mask = 1 << Type;
	return ActiveTypeBitset & Mask;
}

void FDebugLineItem::OnDebugLineTypeActiveChanged(ECheckBoxState CheckState, EDebugLineType Type)
{
	const uint16 Mask = 1 << Type;
	switch (CheckState)
	{
	case ECheckBoxState::Checked:
		ActiveTypeBitset |= Mask;
		break;
	default:
		ActiveTypeBitset &= ~Mask;
		break;
	}
}

//////////////////////////////////////////////////////////////////////////
// ILineItemWithChildren

class FLineItemWithChildren : public FDebugLineItem
{
public:
	FLineItemWithChildren(EDebugLineType InType) :
		FDebugLineItem(InType)
	{}

	virtual ~FLineItemWithChildren() override = default;

	virtual bool HasChildren() const override
	{
		return !ChildrenMirrors.IsEmpty();
	}

	virtual bool CanHaveChildren() override { return true; }

	/** Pilot for Recursive Search */
	bool SearchRecursive(const FString& InSearchString, TSharedPtr<STreeView<FDebugTreeItemPtr>> DebugTreeView)
	{
		TArray<FLineItemWithChildren*> Parents;
		return SearchRecursive(InSearchString, DebugTreeView, Parents);
	}

	/**
	* returns whether this node should be visible according to the users
	* search query
	*
	* O( number of recursive children )
	*/
	bool SearchRecursive(const FString& InSearchString,
		TSharedPtr<STreeView<FDebugTreeItemPtr>> DebugTreeView,
		TArray<FLineItemWithChildren*>& Parents,
		ESearchFlags SearchFlags = SF_None)
	{
		TSharedPtr<ITableRow> Row = DebugTreeView->WidgetFromItem(SharedThis(this));
		bVisible = false;

		UpdateSearch(InSearchString, SearchFlags);

		bool bChildMatch = false;
		Parents.Push(this);

		ESearchFlags ChildSearchFlags = IsContainer() ? SF_ContainerElement : SF_None;

		TArray<FDebugTreeItemPtr> Children;
		GatherChildrenBase(Children, InSearchString,/*bRespectSearch =*/ false);
		for (const FDebugTreeItemPtr& ChildRef : Children)
		{
			if (ChildRef->CanHaveChildren())
			{
				ChildRef->bParentsMatchSearch = bParentsMatchSearch;
				FLineItemWithChildren* Child = StaticCast<FLineItemWithChildren*>(ChildRef.Get());

				// check if the child has been seen already in parents.
				// if it has, skip it. (avoids stack overflows)
				if (Parents.FindByPredicate(
					[Child](const FLineItemWithChildren* Relative)
					{
						return (Relative->Type == Child->Type) && Relative->Compare(Child);
					}
				))
				{
					continue;
				}

					// if any children need to expand, so should this
					if (Child->SearchRecursive(InSearchString, DebugTreeView, Parents, ChildSearchFlags))
					{
						bVisible = true;
						bChildMatch = true;

						// exit early if children aren't in the tree yet anyway and
						// we already know to expand this
						if (!Row)
						{
							break;
						}
					}
			}
			else
			{
				ChildRef->UpdateSearch(InSearchString, ChildSearchFlags);

				// if any children need to expand, so should this
				if (ChildRef->IsVisible())
				{
					bVisible = true;
					bChildMatch = true;

					// exit early if children aren't in the tree yet anyway and
					// we already know to expand this
					if (!Row)
					{
						break;
					}
				}
			}
		}

		Parents.Pop(/*bAllowShrinking =*/ false);
		if (bChildMatch)
		{
			if (Row && !Row->IsItemExpanded())
			{
				Row->ToggleExpansion();
			}
		}

		return bVisible;
	}

	// ensures that ChildrenMirrors are set up for calls to EnsureChildIsAdded
	virtual void GatherChildrenBase(TArray<FDebugTreeItemPtr>& OutChildren, const FString& InSearchString, bool bRespectSearch) override
	{
		Swap(PrevChildrenMirrors, ChildrenMirrors);
		ChildrenMirrors.Empty();
		GatherChildren(OutChildren, InSearchString, bRespectSearch);
	}

	// allows FDebugTreeItemPtr to be stored in TSets 
	class FDebugTreeItemKeyFuncs
	{
	public:
		typedef FDebugTreeItemPtr ElementType;
		typedef TTypeTraits<ElementType>::ConstPointerType KeyInitType;
		typedef TCallTraits<ElementType>::ParamType ElementInitType;
		enum { bAllowDuplicateKeys = false };

		/**
		* @return The key used to index the given element.
		*/
		static FORCEINLINE KeyInitType GetSetKey(ElementInitType Element)
		{
			return Element;
		}

		/**
		* @return True if the keys match.
		*/
		static FORCEINLINE bool Matches(KeyInitType A, KeyInitType B)
		{
			FDebugLineItem* APtr = A.Get();
			FDebugLineItem* BPtr = B.Get();
			if (APtr && BPtr)
			{
				return (APtr->Type == BPtr->Type) && APtr->Compare(BPtr);
			}
			return APtr == BPtr;
		}

		/** Calculates a hash index for a key. */
		static FORCEINLINE uint32 GetKeyHash(KeyInitType Key)
		{
			if (FDebugLineItem* KeyPtr = Key.Get())
			{
				return KeyPtr->GetHash();
			}
			return GetTypeHash(Key);
		}
	};
protected:
	// Last frames cached children
	TSet<FDebugTreeItemPtr, FDebugTreeItemKeyFuncs> PrevChildrenMirrors;
	// This frames children
	TSet<FDebugTreeItemPtr, FDebugTreeItemKeyFuncs> ChildrenMirrors;

	/** @returns whether this item represents a container property */
	virtual bool IsContainer() const
	{
		return false;
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, const FString& InSearchString, bool bRespectSearch) {}

	/**
	 * Adds either Item or an identical node that was previously
	 * created (present in ChildrenMirrors) as a child to OutChildren.
	 *
	 * O( 1 )
	 */
	void EnsureChildIsAdded(TArray<FDebugTreeItemPtr>& OutChildren, const FDebugLineItem& Item, const FString& InSearchString, bool bRespectSearch)
	{
		const FDebugTreeItemPtr Shareable = MakeShareable(Item.Duplicate());
		if (FDebugTreeItemPtr* Found = PrevChildrenMirrors.Find(Shareable))
		{
			FDebugTreeItemPtr FoundItem = *Found;
			FoundItem->UpdateData(Item);
			ChildrenMirrors.Add(FoundItem);

			// only add item if it matches search
			if (!bRespectSearch || InSearchString.IsEmpty() || FoundItem->IsVisible() || FoundItem->DoParentsMatchSearch())
			{
				OutChildren.Add(FoundItem);
			}
		}
		else
		{
			ChildrenMirrors.Add(Shareable);
			OutChildren.Add(Shareable);
		}
	}
};


//////////////////////////////////////////////////////////////////////////
// FMessageLineItem

struct FMessageLineItem : public FDebugLineItem
{
protected:
	FString Message;
public:
	// Message line
	FMessageLineItem(const FString& InMessage)
		: FDebugLineItem(DLT_Message)
		, Message(InMessage)
	{
	}
protected:
	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FMessageLineItem* Other = (FMessageLineItem*)BaseOther;
		return Message == Other->Message;
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FMessageLineItem(Message);
	}

	virtual FText GetDescription() const override
	{
		return FText::FromString(Message);
	}

	virtual uint32 GetHash() override
	{
		return GetTypeHash(Message);
	}
};

//////////////////////////////////////////////////////////////////////////
// FLatentActionLineItem

struct FLatentActionLineItem : public FDebugLineItem
{
protected:
	int32 UUID;
	TWeakObjectPtr< UObject > ParentObjectRef;
public:
	FLatentActionLineItem(int32 InUUID, UObject* ParentObject)
		: FDebugLineItem(DLT_LatentAction)
	{
		UUID = InUUID;
		check(UUID != INDEX_NONE);
		ParentObjectRef = ParentObject;
	}

protected:
	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FLatentActionLineItem* Other = (FLatentActionLineItem*)BaseOther;
		return (ParentObjectRef.Get() == Other->ParentObjectRef.Get()) &&
			(UUID == Other->UUID);
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FLatentActionLineItem(UUID, ParentObjectRef.Get());
	}

	virtual uint32 GetHash() override
	{
		return HashCombine(GetTypeHash(UUID), GetTypeHash(ParentObjectRef));
	}
protected:
	virtual TSharedRef<SWidget> GenerateNameWidget(TSharedPtr<FString> InSearchString) override;
	virtual TSharedRef<SWidget> GetNameIcon() override;
	virtual FText GetDescription() const override;
	virtual FText GetDisplayName() const override;
	void OnNavigateToLatentNode();

	class UEdGraphNode* FindAssociatedNode() const;
};

FText FLatentActionLineItem::GetDescription() const
{
	if (UObject* ParentObject = ParentObjectRef.Get())
	{
		if (UWorld* World = GEngine->GetWorldFromContextObject(ParentObject, EGetWorldErrorMode::ReturnNull))
		{
			FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
			return FText::FromString(LatentActionManager.GetDescription(ParentObject, UUID));
		}
	}

	return LOCTEXT("nullptrObject", "Object has been destroyed");
}

TSharedRef<SWidget> FLatentActionLineItem::GenerateNameWidget(TSharedPtr<FString> InSearchString)
{
	return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
		.FullText(this, &FLatentActionLineItem::GetDisplayName)
		.HighlightText(this, &FLatentActionLineItem::GetHighlightText, InSearchString)
		[
			SNew(SHyperlink)
				.Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
				.OnNavigate(this, &FLatentActionLineItem::OnNavigateToLatentNode)
				.Text(this, &FLatentActionLineItem::GetDisplayName)
				.ToolTipText(LOCTEXT("NavLatentActionLoc_Tooltip", "Navigate to the latent action location"))
		];
}

TSharedRef<SWidget> FLatentActionLineItem::GetNameIcon()
{
	return SNew(SImage)
		.Image(FEditorStyle::GetBrush(TEXT("Kismet.LatentActionIcon")));
}

UEdGraphNode* FLatentActionLineItem::FindAssociatedNode() const
{
	if (UBlueprintGeneratedClass* Class = GetClassForObject(ParentObjectRef.Get()))
	{
		return Class->GetDebugData().FindNodeFromUUID(UUID);
	}

	return nullptr;
}

FText FLatentActionLineItem::GetDisplayName() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("ID"), UUID);
	if (UK2Node* Node = Cast<UK2Node>(FindAssociatedNode()))
	{
		Args.Add(TEXT("Title"), Node->GetCompactNodeTitle());

		return FText::Format(LOCTEXT("ID", "{Title} (ID: {ID})"), Args);
	}
	else
	{
		return FText::Format(LOCTEXT("LatentAction", "Latent action # {ID}"), Args);
	}
}

void FLatentActionLineItem::OnNavigateToLatentNode()
{
	if (UEdGraphNode* Node = FindAssociatedNode())
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Node);
	}
}

struct FWatchChildLineItem : public FLineItemWithChildren
{
protected:
	FPropertyInstanceInfo Data;
private:
	bool bIconHovered = false;
public:
	FWatchChildLineItem(const FPropertyInstanceInfo& Child) :
		FLineItemWithChildren(DLT_WatchChild),
		Data(Child)
	{}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FWatchChildLineItem* Other = (FWatchChildLineItem*)BaseOther;

		return Data.Property == Other->Data.Property &&
			Data.DisplayName.CompareTo(Other->Data.DisplayName) == 0;
	}

	virtual void UpdateData(const FDebugLineItem& NewerData) override
	{
		// Compare returns true even if the value or children of this node
		// is different. use this function to update the data without completely
		// replacing the node
		FWatchChildLineItem& Other = (FWatchChildLineItem&)NewerData;
		Data = Other.Data;
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FWatchChildLineItem(Data);
	}

	virtual uint32 GetHash() override
	{
		return HashCombine(GetTypeHash(Data.Property), GetTypeHash(Data.DisplayName.ToString()));
	}

	virtual FText GetName() const override
	{
		return Data.Name;
	}

	virtual FText GetDescription() const override
	{
		const FString ValStr = Data.Value.ToString();
		return FText::FromString(ValStr.Replace(TEXT("\n"), TEXT(" ")));
	}

	virtual FText GetDisplayName() const override
	{
		return Data.DisplayName;
	}

	// if data is pointing to an asset, get it's UPackage
	const UPackage* GetDataPackage() const
	{
		if (Data.Object.IsValid())
		{
			if (const UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Data.Object->GetClass()))
			{
				if (const UPackage* Package = GeneratedClass->GetPackage())
				{
					return Package;
				}
			}
			if (const UPackage* Package = Data.Object->GetPackage())
			{
				return Package;
			}
		}
		return {};
	}

	// opens result of GetDataPackage in editor
	FReply OnFocusAsset() const
	{
		const UPackage* Package = GetDataPackage();
		if (!Package)
		{
			return FReply::Unhandled();
		}

		const FString Path = Package->GetPathName();
		if (Path.IsEmpty())
		{
			return FReply::Unhandled();
		}

		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Path);
		return FReply::Handled();
	}

	// returns the icon color given a precalculated color associated with this datatype.
	// the color changes slightly based on whether it's null or a hovered button
	FSlateColor ModifiedIconColor(FSlateColor BaseColor) const
	{
		// check if Data is a UObject
		if (CastField<FObjectPropertyBase>(Data.Property.Get()))
		{
			FLinearColor LinearHSV = BaseColor.GetSpecifiedColor().LinearRGBToHSV();

			// if it's a null object, darken the icon so it's clear that it's not a button
			if (Data.Object == nullptr)
			{
				LinearHSV.B *= 0.5f; // decrease value
				LinearHSV.A *= 0.5f; // decrease alpha
				return LinearHSV.HSVToLinearRGB();
			}

			// if the icon is hovered, lighten the icon
			if (bIconHovered)
			{
				LinearHSV.B *= 2.f;  // increase value
				LinearHSV.G *= 0.8f; // decrease Saturation
				return LinearHSV.HSVToLinearRGB();
			}
		}
		return BaseColor;
	}

	FText IconTooltipText() const
	{
		const UPackage* Package = GetDataPackage();
		if (Package)
		{
			return FText::Format(LOCTEXT("OpenPackage", "Open: {0}"), FText::FromString(Package->GetName()));
		}
		return Data.Type;
	}

	// uses the icon and color associated with the property type
	virtual TSharedRef<SWidget> GetNameIcon() override
	{
		FSlateColor BaseColor;
		FSlateColor SecondaryColor;
		FSlateBrush const* SecondaryIcon;
		const FSlateBrush* Icon = FBlueprintEditor::GetVarIconAndColorFromProperty(
			Data.Property.Get(),
			BaseColor,
			SecondaryIcon,
			SecondaryColor
		);

		// make the icon a button so the user can open the asset in editor if there is one
		return SNew(SButton)
			.OnClicked(this, &FWatchChildLineItem::OnFocusAsset)
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.ContentPadding(0.0f)
			.OnHovered_Lambda(
				[&bIconHovered = bIconHovered]() {bIconHovered = true; }
			)
			.OnUnhovered_Lambda(
				[&bIconHovered = bIconHovered]() {bIconHovered = false; }
			)
			[
				SNew(SImage)
					.Image(Icon)
					.ColorAndOpacity(this, &FWatchChildLineItem::ModifiedIconColor, BaseColor)
					.ToolTipText(this, &FWatchChildLineItem::IconTooltipText)
			];
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, const FString& InSearchString, bool bRespectSearch) override
	{
		for (const TSharedPtr<FPropertyInstanceInfo>& ChildData : Data.Children)
		{
			EnsureChildIsAdded(OutChildren, FWatchChildLineItem(*ChildData), InSearchString, bRespectSearch);
		}
	}

	virtual TSharedRef<SWidget> GenerateValueWidget(TSharedPtr<FString> InSearchString) override
	{
		if (const UObject* Object = Data.Object.Get())
		{
			return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(PropertyInfoViewStyle::STextHighlightOverlay)
						.FullText(this, &FWatchChildLineItem::GetObjectValueText)
						.HighlightText(this, &FWatchChildLineItem::GetHighlightText, InSearchString)
						[
							SNew(STextBlock)
								.ToolTipText(this, &FWatchChildLineItem::GetValueTooltipText)
								.Text(this, &FWatchChildLineItem::GetObjectValueText)
						]
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SSpacer)
						.Size(FVector2D(2.0f, 1.0f))
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SHyperlink)
						.ToolTipText(this, &FWatchChildLineItem::GetClassLinkTooltipText)
						.Text(this, &FWatchChildLineItem::GetObjectClassText)
						.OnNavigate(this, &FWatchChildLineItem::OnNavigateToClass)
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SSpacer)
						.Size(FVector2D(2.0f, 1.0f))
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(LOCTEXT("ObjectValueEnd", ")"))
				];
		}

		return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
			.FullText(this, &FWatchChildLineItem::GetDescription)
			.HighlightText(this, &FWatchChildLineItem::GetHighlightText, InSearchString)
			[
				SNew(STextBlock)
					.ToolTipText(this, &FWatchChildLineItem::GetDescription)
					.Text(this, &FWatchChildLineItem::GetDescription)
			];
	}

protected:
	virtual bool IsContainer() const override
	{
		return Data.Property->IsA<FSetProperty>() || Data.Property->IsA<FArrayProperty>() || Data.Property->IsA<FMapProperty>();
	}

	virtual FText GetObjectValueText() const
	{
		if (const UObject* Object = Data.Object.Get())
		{
			return FText::Format(LOCTEXT("ObjectValueBegin", "{0} (Class: "), FText::FromString(Object->GetName()));
		}

		return LOCTEXT("UnknownObjectValueBegin", "[Unknown] (Class: ");
	}

	virtual FText GetObjectClassText() const
	{
		if (const UObject* Object = Data.Object.Get())
		{
			return FText::FromString(Object->GetClass()->GetName());
		}

		return LOCTEXT("UnknownClassName", "[Unknown]");
	}

	virtual void OnNavigateToClass() const
	{
		if (const UObject* Object = Data.Object.Get())
		{
			if (UClass* Class = Object->GetClass())
			{
				if (UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
				{
					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
				}
				else
				{
					// this is a native class
					FSourceCodeNavigation::NavigateToClass(Class);
				}
			}
		}
	}

	virtual FText GetClassLinkTooltipText() const
	{
		if (const UObject* Object = Data.Object.Get())
		{
			if (UClass* Class = Object->GetClass())
			{
				if (UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
				{
					return LOCTEXT("OpenBlueprintClass", "Opens this Class in the Blueprint Editor");
				}
				else
				{
					// this is a native class
					return LOCTEXT("OpenNativeClass", "Navigates to this class' source file");
				}
			}
		}

		return LOCTEXT("UnknownClassName", "[Unknown]");
	}

	virtual FText GetValueTooltipText() const
	{
		// if this is an Object property, tooltip text should include its full name
		if (const UObject* Object = Data.Object.Get())
		{
			return FText::Format(LOCTEXT("ObjectValueTooltip", "{0}\nClass: {1}"),
				FText::FromString(Object->GetFullName()),
				FText::FromString(Object->GetClass()->GetFullName()));
		}

		return GetDescription();
	}
};

//////////////////////////////////////////////////////////////////////////\
// FSelfWatchLineItem
struct FSelfWatchLineItem : public FLineItemWithChildren
{
protected:
	// watches a UObject instead of a pin
	TWeakObjectPtr<UObject> ObjectToWatch;
public:
	FSelfWatchLineItem(UObject* Object) :
		FLineItemWithChildren(DLT_Watch),
		ObjectToWatch(Object)
	{}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FSelfWatchLineItem* Other = (FSelfWatchLineItem*)BaseOther;
		return (ObjectToWatch.Get() == Other->ObjectToWatch.Get());
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FSelfWatchLineItem(ObjectToWatch.Get());
	}

	virtual uint32 GetHash() override
	{
		return GetTypeHash(ObjectToWatch);
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, const FString& InSearchString, bool bRespectSearch) override
	{
		if (UObject* Object = ObjectToWatch.Get())
		{
			for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
			{
				TSharedPtr<FPropertyInstanceInfo> DebugInfo;
				FProperty* Property = *It;
				if (Property->HasAllPropertyFlags(CPF_BlueprintVisible))
				{
					void* Value = Property->ContainerPtrToValuePtr<void*>(Object);
					FKismetDebugUtilities::GetDebugInfoInternal(DebugInfo, Property, Value);

					EnsureChildIsAdded(OutChildren, FWatchChildLineItem(*DebugInfo), InSearchString, bRespectSearch);
				}
			}
		}
	}

protected:
	virtual FText GetDisplayName() const override
	{
		return LOCTEXT("SelfName", "Self");
	}

	virtual TSharedRef<SWidget> GetNameIcon() override
	{
		return SNew(SImage)
			.Image(FEditorStyle::GetBrush(TEXT("Kismet.WatchIcon")));
	}
};

//////////////////////////////////////////////////////////////////////////
// FWatchLineItem 


struct FWatchLineItem : public FLineItemWithChildren
{
protected:
	TWeakObjectPtr< UObject > ParentObjectRef;
	const FEdGraphPinReference ObjectRef;
public:
	FWatchLineItem(const UEdGraphPin* PinToWatch, UObject* ParentObject)
		: FLineItemWithChildren(DLT_Watch)
		, ObjectRef(PinToWatch)
	{
		ParentObjectRef = ParentObject;
	}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FWatchLineItem* Other = (FWatchLineItem*)BaseOther;
		return (ParentObjectRef == Other->ParentObjectRef) &&
			(ObjectRef == Other->ObjectRef);
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FWatchLineItem(ObjectRef.Get(), ParentObjectRef.Get());
	}

	virtual uint32 GetHash() override
	{
		return HashCombine(GetTypeHash(ParentObjectRef), GetTypeHash(ObjectRef));
	}

	virtual void MakeMenu(class FMenuBuilder& MenuBuilder) override
	{
		if (UEdGraphPin* WatchedPin = ObjectRef.Get())
		{
			FUIAction ClearThisWatch(
				FExecuteAction::CreateStatic(&FDebuggingActionCallbacks::ClearWatch, WatchedPin),
				FCanExecuteAction() // always allow
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("ClearWatch", "Stop watching"),
				LOCTEXT("ClearWatch_ToolTip", "Stop watching this variable"),
				FSlateIcon(),
				ClearThisWatch);
		}

		FDebugLineItem::MakeMenu(MenuBuilder);
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, const FString& InSearchString, bool bRespectSearch) override
	{
		if (UEdGraphPin* PinToWatch = ObjectRef.Get())
		{
			// Try to determine the blueprint that generated the watch
			UBlueprint* ParentBlueprint = GetBlueprintForObject(ParentObjectRef.Get());

			// Find a valid property mapping and display the current value
			UObject* ParentObject = ParentObjectRef.Get();
			if ((ParentBlueprint != ParentObject) && (ParentBlueprint != nullptr))
			{
				TSharedPtr<FPropertyInstanceInfo> DebugInfo;
				const FKismetDebugUtilities::EWatchTextResult WatchStatus = FKismetDebugUtilities::GetDebugInfo(DebugInfo, ParentBlueprint, ParentObject, PinToWatch);

				if (WatchStatus == FKismetDebugUtilities::EWTR_Valid)
				{
					check(DebugInfo);
					for (const TSharedPtr<FPropertyInstanceInfo>& ChildData : DebugInfo->Children)
					{
						EnsureChildIsAdded(OutChildren, FWatchChildLineItem(*ChildData), InSearchString, bRespectSearch);
					}
				}
			}
		}
	}


protected:
	virtual FText GetDescription() const override;
	virtual FText GetDisplayName() const override;
	virtual TSharedRef<SWidget> GenerateNameWidget(TSharedPtr<FString> InSearchString) override;
	virtual TSharedRef<SWidget> GetNameIcon() override;

	void OnNavigateToWatchLocation();
};

FText FWatchLineItem::GetDisplayName() const
{
	if (UEdGraphPin* PinToWatch = ObjectRef.Get())
	{
		if (UBlueprint* Blueprint = GetBlueprintForObject(ParentObjectRef.Get()))
		{
			if (FProperty* Property = FKismetDebugUtilities::FindClassPropertyForPin(Blueprint, PinToWatch))
			{
				return FText::FromString(UEditorEngine::GetFriendlyName(Property));
			}
		}

		FFormatNamedArguments Args;
		Args.Add(TEXT("PinWatchName"), FText::FromString(PinToWatch->GetName()));
		return FText::Format(LOCTEXT("DisplayNameNoProperty", "{PinWatchName} (no prop)"), Args);
	}
	else
	{
		return FText::GetEmpty();
	}
}

FText FWatchLineItem::GetDescription() const
{
	if (UEdGraphPin* PinToWatch = ObjectRef.Get())
	{
		// Try to determine the blueprint that generated the watch
		UBlueprint* ParentBlueprint = GetBlueprintForObject(ParentObjectRef.Get());

		// Find a valid property mapping and display the current value
		UObject* ParentObject = ParentObjectRef.Get();
		if ((ParentBlueprint != ParentObject) && (ParentBlueprint != nullptr))
		{
			TSharedPtr<FPropertyInstanceInfo> DebugInfo;
			const FKismetDebugUtilities::EWatchTextResult WatchStatus = FKismetDebugUtilities::GetDebugInfo(DebugInfo, ParentBlueprint, ParentObject, PinToWatch);

			switch (WatchStatus)
			{
			case FKismetDebugUtilities::EWTR_Valid:
			{
				check(DebugInfo);
				const FString ValStr = DebugInfo->Value.ToString();
				return FText::FromString(ValStr.Replace(TEXT("\n"), TEXT(" ")));
			}

			case FKismetDebugUtilities::EWTR_NotInScope:
				return LOCTEXT("NotInScope", "Not in scope");

			case FKismetDebugUtilities::EWTR_NoProperty:
				return LOCTEXT("UnknownProperty", "No debug data");

			default:
			case FKismetDebugUtilities::EWTR_NoDebugObject:
				return LOCTEXT("NoDebugObject", "No debug object");
			}
		}
	}

	return FText::GetEmpty();
}

TSharedRef<SWidget> FWatchLineItem::GenerateNameWidget(TSharedPtr<FString> InSearchString)
{
	return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
		.FullText(this, &FWatchLineItem::GetDisplayName)
		.HighlightText(this, &FWatchLineItem::GetHighlightText, InSearchString)
		[
			SNew(SHyperlink)
				.Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
				.OnNavigate(this, &FWatchLineItem::OnNavigateToWatchLocation)
				.Text(this, &FWatchLineItem::GetDisplayName)
				.ToolTipText(LOCTEXT("NavWatchLoc", "Navigate to the watch location"))
		];
}

// overlays the watch icon on top of a faded icon associated with the pin type
TSharedRef<SWidget> FWatchLineItem::GetNameIcon()
{
	const FSlateBrush* PinIcon;
	FLinearColor PinIconColor;
	FText Typename;
	if (UEdGraphPin* ObjectToFocus = ObjectRef.Get())
	{
		PinIcon = FBlueprintEditorUtils::GetIconFromPin(ObjectToFocus->PinType);

		const UEdGraphSchema* Schema = ObjectToFocus->GetSchema();
		PinIconColor = Schema->GetPinTypeColor(ObjectToFocus->PinType);
		PinIconColor.A = 0.3f;

		// Note: Currently tunnel node pins (e.g. macro/consolidated node outputs) won't return a property.
		UBlueprint* ParentBlueprint = GetBlueprintForObject(ParentObjectRef.Get());
		if (FProperty* Property = FKismetDebugUtilities::FindClassPropertyForPin(ParentBlueprint, ObjectToFocus))
		{
			Typename = UEdGraphSchema_K2::TypeToText(Property);
		}
		else
		{
			Typename = UEdGraphSchema_K2::TypeToText(ObjectToFocus->PinType);
		}
	}
	else
	{
		PinIcon = FEditorStyle::GetBrush(TEXT("NoBrush"));
	}

	return SNew(SOverlay)
		.ToolTipText(Typename)
		+ SOverlay::Slot()
			.Padding(FMargin(10.f, 0.f, 0.f, 0.f))
			[
				SNew(SImage)
					.Image(PinIcon)
					.ColorAndOpacity(PinIconColor)
			]
		+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			[
				SNew(SImage)
					.Image(FEditorStyle::GetBrush(TEXT("Kismet.WatchIcon")))
			];
}

void FWatchLineItem::OnNavigateToWatchLocation()
{
	if (UEdGraphPin* ObjectToFocus = ObjectRef.Get())
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnPin(ObjectToFocus);
	}
}

//////////////////////////////////////////////////////////////////////////
// FBreakpointLineItem

struct FBreakpointLineItem : public FDebugLineItem
{
protected:
	TWeakObjectPtr<UObject> ParentObjectRef;
	TSoftObjectPtr<UEdGraphNode> BreakpointNode;
public:
	FBreakpointLineItem(TSoftObjectPtr<UEdGraphNode> BreakpointToWatch, UObject* ParentObject)
		: FDebugLineItem(DLT_Breakpoint)
	{
		BreakpointNode = BreakpointToWatch;
		ParentObjectRef = ParentObject;
	}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FBreakpointLineItem* Other = (FBreakpointLineItem*)BaseOther;
		return (ParentObjectRef.Get() == Other->ParentObjectRef.Get()) &&
			(BreakpointNode == Other->BreakpointNode);
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FBreakpointLineItem(BreakpointNode, ParentObjectRef.Get());
	}

	virtual uint32 GetHash() override
	{
		return HashCombine(GetTypeHash(ParentObjectRef), GetTypeHash(BreakpointNode));
	}

	virtual void MakeMenu(class FMenuBuilder& MenuBuilder) override
	{
		FBlueprintBreakpoint* Breakpoint = GetBreakpoint();
		const UBlueprint* ParentBlueprint = GetBlueprintForObject(ParentObjectRef.Get());

		// By default, we don't allow actions to execute when in debug mode.
		// Create an empty action to always allow execution for these commands (they are allowed in debug mode)
		FCanExecuteAction AlwaysAllowExecute;

		if (Breakpoint != nullptr)
		{
			const bool bNewEnabledState = !Breakpoint->IsEnabledByUser();

			FUIAction ToggleThisBreakpoint(
				FExecuteAction::CreateStatic(
					&FDebuggingActionCallbacks::SetBreakpointEnabled, BreakpointNode, ParentBlueprint, bNewEnabledState
				),
				AlwaysAllowExecute
			);

			if (bNewEnabledState)
			{
				// Enable
				MenuBuilder.AddMenuEntry(
					LOCTEXT("EnableBreakpoint", "Enable breakpoint"),
					LOCTEXT("EnableBreakpoint_ToolTip", "Enable this breakpoint; the debugger will appear when this node is about to be executed."),
					FSlateIcon(),
					ToggleThisBreakpoint);
			}
			else
			{
				// Disable
				MenuBuilder.AddMenuEntry(
					LOCTEXT("DisableBreakpoint", "Disable breakpoint"),
					LOCTEXT("DisableBreakpoint_ToolTip", "Disable this breakpoint."),
					FSlateIcon(),
					ToggleThisBreakpoint);
			}
		}

		if ((Breakpoint != nullptr) && (ParentBlueprint != nullptr))
		{
			FUIAction ClearThisBreakpoint(
				FExecuteAction::CreateStatic(&FDebuggingActionCallbacks::ClearBreakpoint, BreakpointNode, ParentBlueprint),
				AlwaysAllowExecute
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("ClearBreakpoint", "Remove breakpoint"),
				LOCTEXT("ClearBreakpoint_ToolTip", "Remove the breakpoint from this node."),
				FSlateIcon(),
				ClearThisBreakpoint);
		}

		FDebugLineItem::MakeMenu(MenuBuilder);
	}
protected:
	FBlueprintBreakpoint* GetBreakpoint() const
	{
		if (UEdGraphNode* Node = BreakpointNode.Get())
		{
			if (const UBlueprint* Blueprint = GetBlueprintForObject(Node))
			{
				return FKismetDebugUtilities::FindBreakpointForNode(Node, Blueprint);
			}
		}
		return nullptr;
	}

	virtual TSharedRef<SWidget> GenerateNameWidget(TSharedPtr<FString> InSearchString) override
	{
		return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
			.FullText(this, &FBreakpointLineItem::GetDisplayName)
			.HighlightText(this, &FBreakpointLineItem::GetHighlightText, InSearchString)
			[
				SNew(SHyperlink)
					.Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
					.Text(this, &FBreakpointLineItem::GetDisplayName)
					.ToolTipText(LOCTEXT("NavBreakpointLoc", "Navigate to the breakpoint location"))
					.OnNavigate(this, &FBreakpointLineItem::OnNavigateToBreakpointLocation)
			];
	}

	virtual TSharedRef<SWidget> GetNameIcon() override
	{
		return SNew(SButton)
			.OnClicked(this, &FBreakpointLineItem::OnUserToggledEnabled)
			.ToolTipText(LOCTEXT("ToggleBreakpointButton_ToolTip", "Toggle this breakpoint"))
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.ContentPadding(0.0f)
			[
				SNew(SImage)
					.Image(this, &FBreakpointLineItem::GetStatusImage)
					.ToolTipText(this, &FBreakpointLineItem::GetStatusTooltip)
			];
	}


	virtual FText GetDisplayName() const override;
	FReply OnUserToggledEnabled();

	void OnNavigateToBreakpointLocation();

	const FSlateBrush* GetStatusImage() const;
	FText GetStatusTooltip() const;
};

FText FBreakpointLineItem::GetDisplayName() const
{
	if (FBlueprintBreakpoint* MyBreakpoint = GetBreakpoint())
	{
		return MyBreakpoint->GetLocationDescription();
	}
	return FText::GetEmpty();
}

FReply FBreakpointLineItem::OnUserToggledEnabled()
{
	if (FBlueprintBreakpoint* MyBreakpoint = GetBreakpoint())
	{
		FKismetDebugUtilities::SetBreakpointEnabled(*MyBreakpoint, !MyBreakpoint->IsEnabledByUser());
	}
	return FReply::Handled();
}

void FBreakpointLineItem::OnNavigateToBreakpointLocation()
{
	if (FBlueprintBreakpoint* MyBreakpoint = GetBreakpoint())
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(MyBreakpoint->GetLocation());
	}
}

const FSlateBrush* FBreakpointLineItem::GetStatusImage() const
{
	if (FBlueprintBreakpoint* MyBreakpoint = GetBreakpoint())
	{
		if (MyBreakpoint->IsEnabledByUser())
		{
			return FEditorStyle::GetBrush(FKismetDebugUtilities::IsBreakpointValid(*MyBreakpoint) ? TEXT("Kismet.Breakpoint.EnabledAndValid") : TEXT("Kismet.Breakpoint.EnabledAndInvalid"));
		}
		else
		{
			return FEditorStyle::GetBrush(TEXT("Kismet.Breakpoint.Disabled"));
		}
	}

	return FEditorStyle::GetDefaultBrush();
}

FText FBreakpointLineItem::GetStatusTooltip() const
{
	if (FBlueprintBreakpoint* MyBreakpoint = GetBreakpoint())
	{
		if (!FKismetDebugUtilities::IsBreakpointValid(*MyBreakpoint))
		{
			return LOCTEXT("Breakpoint_NoHit", "This breakpoint will not be hit because its node generated no code");
		}
		else
		{
			return MyBreakpoint->IsEnabledByUser() ? LOCTEXT("ActiveBreakpoint", "Active breakpoint") : LOCTEXT("InactiveBreakpoint", "Inactive breakpoint");
		}
	}
	else
	{
		return LOCTEXT("NoBreakpoint", "No Breakpoint");
	}
}

//////////////////////////////////////////////////////////////////////////
// FTraceStackParentItem

class FBreakpointParentItem : public FLineItemWithChildren
{
public:
	// The parent object
	TWeakObjectPtr<UBlueprint> Blueprint;

	FBreakpointParentItem(TWeakObjectPtr<UBlueprint> InBlueprint)
		: FLineItemWithChildren(DLT_BreakpointParent)
		, Blueprint(InBlueprint)
	{
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, const FString& InSearchString, bool bRespectSearch) override
	{
		// update search flags to match that of a root node
		UpdateSearch(InSearchString, FDebugLineItem::SF_RootNode);

		if (!Blueprint.IsValid())
		{
			return;
		}

		// Create children for each breakpoint
		FKismetDebugUtilities::ForeachBreakpoint(
			Blueprint.Get(),
			[this, &OutChildren, &InSearchString, bRespectSearch] (FBlueprintBreakpoint& Breakpoint)
			{
				EnsureChildIsAdded(OutChildren,
					FBreakpointLineItem(Breakpoint.GetLocation(), Blueprint.Get()), InSearchString, bRespectSearch);
			}
		);

		// Make sure there is something there, to let the user know if there is nothing
		if (OutChildren.Num() == 0)
		{
			EnsureChildIsAdded(OutChildren,
				FMessageLineItem(LOCTEXT("NoBreakpoints", "No breakpoints").ToString()), InSearchString, bRespectSearch);
		}
	}

	virtual void MakeMenu(FMenuBuilder& MenuBuilder) override
	{
		if (FKismetDebugUtilities::BlueprintHasBreakpoints(Blueprint.Get()))
		{
			const FUIAction ClearAllBreakpoints(
				FExecuteAction::CreateStatic(&FDebuggingActionCallbacks::ClearBreakpoints, Blueprint.Get()),
				FCanExecuteAction() // always allow
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("ClearBreakpoints", "Remove all breakpoints"),
				LOCTEXT("ClearBreakpoints_ToolTip", "Clear all breakpoints in this blueprint"),
				FSlateIcon(),
				ClearAllBreakpoints);

			const bool bEnabledBreakpointExists = FKismetDebugUtilities::FindBreakpointByPredicate(
				Blueprint.Get(),
				[](const FBlueprintBreakpoint& Breakpoint)->bool
				{
					return Breakpoint.IsEnabled();
				}
			) != nullptr;

			if (bEnabledBreakpointExists)
			{
				const FUIAction DisableAllBreakpoints(
					FExecuteAction::CreateStatic(
						&FDebuggingActionCallbacks::SetEnabledOnAllBreakpoints,
						const_cast<const UBlueprint*>(Blueprint.Get()),
						false
					),
					FCanExecuteAction() // always allow
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("DisableBreakpoints", "Disable all breakpoints"),
					LOCTEXT("DisableBreakpoints_ToolTip", "Disable all breakpoints in this blueprint"),
					FSlateIcon(),
					DisableAllBreakpoints);
			}

			const bool bDisabledBreakpointExists = FKismetDebugUtilities::FindBreakpointByPredicate(
				Blueprint.Get(),
				[](const FBlueprintBreakpoint& Breakpoint)->bool
				{
					return !Breakpoint.IsEnabled();
				}
			) != nullptr;

			if (bDisabledBreakpointExists)
			{
				const FUIAction EnableAllBreakpoints(
					FExecuteAction::CreateStatic(
						&FDebuggingActionCallbacks::SetEnabledOnAllBreakpoints,
						const_cast<const UBlueprint*>(Blueprint.Get()),
						true
					),
					FCanExecuteAction() // always allow
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("EnableBreakpoints", "Enable all breakpoints"),
					LOCTEXT("EnableBreakpoints_ToolTip", "Enable all breakpoints in this blueprint"),
					FSlateIcon(),
					EnableAllBreakpoints);
			}
		}
		FDebugLineItem::MakeMenu(MenuBuilder);
	}

protected:
	virtual FText GetDisplayName() const override
	{
		return LOCTEXT("Breakpoints", "Breakpoints");
	}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		check(false);
		return false;
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		check(false);
		return nullptr;
	}

	virtual uint32 GetHash() override
	{
		check(false);
		return 0;
	}
};

void FDebugLineItem::SetBreakpointParentItemBlueprint(FDebugTreeItemPtr InBreakpointParentItem, TWeakObjectPtr<UBlueprint> InBlueprint)
{
	if (ensureMsgf(InBreakpointParentItem.IsValid() && InBreakpointParentItem->Type == DLT_BreakpointParent, TEXT("TreeItem is not Valid!")))
	{
		TSharedPtr<FBreakpointParentItem> BreakpointItem = StaticCastSharedPtr<FBreakpointParentItem>(InBreakpointParentItem);
		BreakpointItem->Blueprint = InBlueprint;
	}
}

//////////////////////////////////////////////////////////////////////////
// FParentLineItem

class FParentLineItem : public FLineItemWithChildren
{
protected:
	// The parent object
	TWeakObjectPtr<UObject> ObjectRef;
public:
	FParentLineItem(UObject* Object)
		: FLineItemWithChildren(DLT_Parent)
	{
		ObjectRef = Object;
	}

	virtual UObject* GetParentObject() override
	{
		return ObjectRef.Get();
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, const FString& InSearchString, bool bRespectSearch) override
	{
		// update search flags to match that of a root node
		UpdateSearch(InSearchString, SF_RootNode);

		if (UObject* ParentObject = ObjectRef.Get())
		{
			// every instance should have an automatic watch for 'self'
			EnsureChildIsAdded(OutChildren, FSelfWatchLineItem(ParentObject), InSearchString, bRespectSearch);

			UBlueprint* ParentBP = FDebugLineItem::GetBlueprintForObject(ParentObject);
			if (ParentBP != nullptr)
			{
				// Create children for each watch
				if (IsDebugLineTypeActive(DLT_Watch))
				{
					FKismetDebugUtilities::ForeachPinWatch(
						ParentBP,
						[this, &OutChildren, ParentObject, &InSearchString, bRespectSearch](UEdGraphPin* WatchedPin)
						{
							EnsureChildIsAdded(OutChildren,
								FWatchLineItem(WatchedPin, ParentObject), InSearchString, bRespectSearch);
						}
					);
				}

				// It could also have active latent behaviors
				if (IsDebugLineTypeActive(DLT_LatentAction))
				{
					if (UWorld* World = GEngine->GetWorldFromContextObject(ParentObject, EGetWorldErrorMode::ReturnNull))
					{
						FLatentActionManager& LatentActionManager = World->GetLatentActionManager();

						// Get the current list of action UUIDs
						TSet<int32> UUIDSet;
						LatentActionManager.GetActiveUUIDs(ParentObject, /*inout*/ UUIDSet);

						// Add the new ones
						for (TSet<int32>::TConstIterator RemainingIt(UUIDSet); RemainingIt; ++RemainingIt)
						{
							const int32 UUID = *RemainingIt;
							EnsureChildIsAdded(OutChildren,
								FLatentActionLineItem(UUID, ParentObject), InSearchString, bRespectSearch);
						}
					}
				}

				// Make sure there is something there, to let the user know if there is nothing
				if (OutChildren.Num() == 0)
				{
					EnsureChildIsAdded(OutChildren,
						FMessageLineItem(LOCTEXT("NoDebugInfo", "No debugging info").ToString()), InSearchString, bRespectSearch);
				}
			}
			//@TODO: try to get at TArray<struct FDebugDisplayProperty> DebugProperties in UGameViewportClient, if available
		}
	}

	const FSlateBrush* GetStatusImage() const
	{
		if (SKismetDebuggingView::CurrentActiveObject == ObjectRef)
		{
			return FEditorStyle::GetBrush(TEXT("Kismet.Trace.CurrentIndex"));
		}
		if (ObjectRef.IsValid())
		{
			return FSlateIconFinder::FindIconBrushForClass(ObjectRef->GetClass());
		}
		return FEditorStyle::GetBrush(TEXT("None"));
	}

	FSlateColor GetStatusColor() const
	{
		if (SKismetDebuggingView::CurrentActiveObject == ObjectRef)
		{
			return FSlateColor(EStyleColor::AccentYellow);
		}
		const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
		return Settings->ObjectPinTypeColor;
	}

	FText GetStatusTooltip() const
	{
		if (SKismetDebuggingView::CurrentActiveObject == ObjectRef)
		{
			return LOCTEXT("BreakpointHIt", "Breakpoint Hit");
		}
		return FText::GetEmpty();
	}

protected:
	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		FParentLineItem* Other = (FParentLineItem*)BaseOther;
		return ObjectRef.Get() == Other->ObjectRef.Get();
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		return new FParentLineItem(ObjectRef.Get());
	}

	virtual uint32 GetHash() override
	{
		return GetTypeHash(ObjectRef);
	}

	virtual FText GetDisplayName() const override
	{
		UObject* Object = ObjectRef.Get();
		AActor* Actor = Cast<AActor>(Object);

		if (Actor != nullptr)
		{
			return FText::FromString(Actor->GetActorLabel());
		}
		else
		{
			return (Object != nullptr) ? FText::FromString(Object->GetName()) : LOCTEXT("nullptr", "(nullptr)");
		}
	}

	virtual TSharedRef<SWidget> GetNameIcon() override
	{
		return SNew(SImage)
			.Image(this, &FParentLineItem::GetStatusImage)
			.ColorAndOpacity_Raw(this, &FParentLineItem::GetStatusColor)
			.ToolTipText(this, &FParentLineItem::GetStatusTooltip);
	}

	virtual void MakeMenu(class FMenuBuilder& MenuBuilder) override
	{
		if (UBlueprint* BP = Cast<UBlueprint>(ObjectRef.Get()))
		{
			if (FKismetDebugUtilities::BlueprintHasPinWatches(BP))
			{
				FUIAction ClearAllWatches(
					FExecuteAction::CreateStatic(&FDebuggingActionCallbacks::ClearWatches, BP),
					FCanExecuteAction() // always allow
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("ClearWatches", "Clear all watches"),
					LOCTEXT("ClearWatches_ToolTip", "Clear all watches in this blueprint"),
					FSlateIcon(),
					ClearAllWatches);
			}
		}

		FDebugLineItem::MakeMenu(MenuBuilder);
	}
};

//////////////////////////////////////////////////////////////////////////
// FTraceStackChildItem

class FTraceStackChildItem : public FDebugLineItem
{
protected:
	int32 StackIndex;
public:
	FTraceStackChildItem(int32 InStackIndex)
		: FDebugLineItem(DLT_TraceStackChild)
	{
		StackIndex = InStackIndex;
	}
protected:
	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		check(false);
		return false;
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		check(false);
		return nullptr;
	}

	virtual uint32 GetHash() override
	{
		check(false);
		return 0;
	}

	UEdGraphNode* GetNode() const
	{
		const TSimpleRingBuffer<FKismetTraceSample>& TraceStack = FKismetDebugUtilities::GetTraceStack();
		if (StackIndex < TraceStack.Num())
		{
			const FKismetTraceSample& Sample = TraceStack(StackIndex);
			UObject* ObjectContext = Sample.Context.Get();

			FString ContextName = (ObjectContext != nullptr) ? ObjectContext->GetName() : LOCTEXT("ObjectDoesNotExist", "(object no longer exists)").ToString();
			FString NodeName = TEXT(" ");

			if (ObjectContext != nullptr)
			{
				// Try to find the node that got executed
				UEdGraphNode* Node = FKismetDebugUtilities::FindSourceNodeForCodeLocation(ObjectContext, Sample.Function.Get(), Sample.Offset);
				return Node;
			}
		}

		return nullptr;
	}

	virtual FText GetDisplayName() const override
	{
		UEdGraphNode* Node = GetNode();
		if (Node != nullptr)
		{
			return Node->GetNodeTitle(ENodeTitleType::ListView);
		}
		else
		{
			return LOCTEXT("Unknown", "(unknown)");
		}
	}

	virtual TSharedRef<SWidget> GenerateNameWidget(TSharedPtr<FString> InSearchString) override
	{
		return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
			.FullText(this, &FTraceStackChildItem::GetDisplayName)
			.HighlightText(this, &FTraceStackChildItem::GetHighlightText, InSearchString)
			[
				SNew(SHyperlink)
					.Text(this, &FTraceStackChildItem::GetDisplayName)
					.Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
					.ToolTipText(LOCTEXT("NavigateToDebugTraceLocationHyperlink_ToolTip", "Navigate to the trace location"))
					.OnNavigate(this, &FTraceStackChildItem::OnNavigateToNode)
			];
	}

	virtual TSharedRef<SWidget> GetNameIcon() override
	{
		return SNew(SImage)
			.Image(FEditorStyle::GetBrush(
				(StackIndex > 0) ?
				TEXT("Kismet.Trace.PreviousIndex") :
				TEXT("Kismet.Trace.CurrentIndex"))
			);
	}

	// Visit time and actor name
	virtual TSharedRef<SWidget> GenerateValueWidget(TSharedPtr<FString> InSearchString) override
	{
		return SNew(PropertyInfoViewStyle::STextHighlightOverlay)
			.FullText(this, &FTraceStackChildItem::GetDescription)
			.HighlightText(this, &FTraceStackChildItem::GetHighlightText, InSearchString)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SHyperlink)
							.Text(this, &FTraceStackChildItem::GetContextObjectName)
							.Style(FEditorStyle::Get(), "HoverOnlyHyperlink")
							.ToolTipText(LOCTEXT("SelectActor_Tooltip", "Select this actor"))
							.OnNavigate(this, &FTraceStackChildItem::OnSelectContextObject)
					]
				+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(this, &FTraceStackChildItem::GetVisitTime)
					]
			];
	}

	FText GetVisitTime() const
	{
		const TSimpleRingBuffer<FKismetTraceSample>& TraceStack = FKismetDebugUtilities::GetTraceStack();
		if (StackIndex < TraceStack.Num())
		{
			static const FNumberFormattingOptions TimeFormatOptions = FNumberFormattingOptions()
				.SetMinimumFractionalDigits(2)
				.SetMaximumFractionalDigits(2);
			return FText::Format(LOCTEXT("VisitTimeFmt", " @ {0} s"), FText::AsNumber(TraceStack(StackIndex).ObservationTime - GStartTime, &TimeFormatOptions));
		}

		return FText::GetEmpty();
	}

	FText GetContextObjectName() const
	{
		const TSimpleRingBuffer<FKismetTraceSample>& TraceStack = FKismetDebugUtilities::GetTraceStack();

		UObject* ObjectContext = (StackIndex < TraceStack.Num()) ? TraceStack(StackIndex).Context.Get() : nullptr;

		return (ObjectContext != nullptr) ? FText::FromString(ObjectContext->GetName()) : LOCTEXT("ObjectDoesNotExist", "(object no longer exists)");
	}

	virtual FText GetDescription() const override
	{
		return FText::FromString(GetContextObjectName().ToString() + GetVisitTime().ToString());
	}

	void OnNavigateToNode()
	{
		if (UEdGraphNode* Node = GetNode())
		{
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Node);
		}
	}

	void OnSelectContextObject()
	{
		const TSimpleRingBuffer<FKismetTraceSample>& TraceStack = FKismetDebugUtilities::GetTraceStack();

		UObject* ObjectContext = (StackIndex < TraceStack.Num()) ? TraceStack(StackIndex).Context.Get() : nullptr;

		// Add the object to the selection set
		if (AActor* Actor = Cast<AActor>(ObjectContext))
		{
			GEditor->SelectActor(Actor, true, true, true);
		}
		else
		{
			UE_LOG(LogBlueprintDebugTreeView, Warning, TEXT("Cannot select the non-actor object '%s'"), (ObjectContext != nullptr) ? *ObjectContext->GetName() : TEXT("(nullptr)"));
		}
	}

};

//////////////////////////////////////////////////////////////////////////
// FTraceStackParentItem

class FTraceStackParentItem : public FLineItemWithChildren
{
public:
	FTraceStackParentItem()
		: FLineItemWithChildren(DLT_TraceStackParent)
	{
	}

	virtual bool HasChildren() const override
	{
		return !ChildrenMirrorsArr.IsEmpty();
	}

	virtual void GatherChildren(TArray<FDebugTreeItemPtr>& OutChildren, const FString& InSearchString, bool bRespectSearch) override
	{
		// update search flags to match that of a root node
		UpdateSearch(InSearchString, SF_RootNode);

		const TSimpleRingBuffer<FKismetTraceSample>& TraceStack = FKismetDebugUtilities::GetTraceStack();
		const int32 NumVisible = TraceStack.Num();

		// Create any new stack entries that are needed
		for (int32 i = ChildrenMirrorsArr.Num(); i < NumVisible; ++i)
		{
			ChildrenMirrorsArr.Add(MakeShareable(new FTraceStackChildItem(i)));
		}

		// Add the visible stack entries as children
		for (int32 i = 0; i < NumVisible; ++i)
		{
			OutChildren.Add(ChildrenMirrorsArr[i]);
		}
	}

protected:
	virtual FText GetDisplayName() const override
	{
		return LOCTEXT("ExecutionTrace", "Execution Trace");
	}

	virtual bool Compare(const FDebugLineItem* BaseOther) const override
	{
		check(false);
		return false;
	}

	virtual FDebugLineItem* Duplicate() const override
	{
		check(false);
		return nullptr;
	}

	virtual uint32 GetHash() override
	{
		check(false);
		return 0;
	}

	// use an array to store children mirrors instead of a set so it's ordered
	TArray<FDebugTreeItemPtr> ChildrenMirrorsArr;
};

//////////////////////////////////////////////////////////////////////////
// SDebugLineItem

class SDebugLineItem : public SMultiColumnTableRow< FDebugTreeItemPtr >
{
protected:
	FDebugTreeItemPtr ItemToEdit;
	TSharedPtr<FString> SearchString;
public:
	SLATE_BEGIN_ARGS(SDebugLineItem) {}
	SLATE_END_ARGS()

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		TSharedPtr<SWidget> ColumnContent = nullptr;
		if (ColumnName == SKismetDebugTreeView::ColumnId_Name)
		{
			SAssignNew(ColumnContent, SHorizontalBox)
			+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					SNew(PropertyInfoViewStyle::SIndent, SharedThis(this))
				]
			+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(PropertyInfoViewStyle::SExpanderArrow, SharedThis(this))
					.HasChildren_Lambda([ItemToEdit = ItemToEdit]()
						{
							const bool HasChildren = ItemToEdit->HasChildren();
							return HasChildren;
						})
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					ItemToEdit->GetNameIcon()
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(5.f, 0.f, 0.f, 0.f)
				[
					ItemToEdit->GenerateNameWidget(SearchString)
				];
		}
		else if (ColumnName == SKismetDebugTreeView::ColumnId_Value)
		{
			SAssignNew(ColumnContent, SHorizontalBox)
			+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					ItemToEdit->GetValueIcon()
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.Padding(.5f, 1.f)
				[
					ItemToEdit->GenerateValueWidget(SearchString)
				];
		}
		else
		{
			SAssignNew(ColumnContent, STextBlock)
				.Text(LOCTEXT("Error", "Error"));
		}

		return SNew(SBox)
			.Padding(FMargin(0.5f, 0.5f))
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
					.BorderBackgroundColor_Static(
						PropertyInfoViewStyle::GetRowBackgroundColor,
						static_cast<ITableRow*>(this)
					)
					[
						ColumnContent.ToSharedRef()
					]
			];
	}

	void Construct(const FArguments& InArgs, TSharedRef<STableViewBase> OwnerTableView, FDebugTreeItemPtr InItemToEdit, TSharedPtr<FString> InSearchString)
	{
		ItemToEdit = InItemToEdit;
		SearchString = InSearchString;
		SMultiColumnTableRow<FDebugTreeItemPtr>::Construct(FSuperRowType::FArguments(), OwnerTableView);
	}

protected:

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		const TSharedRef<SWidget>* NameWidget = GetWidgetFromColumnId(SKismetDebugTreeView::ColumnId_Name);
		const TSharedRef<SWidget>* ValWidget = GetWidgetFromColumnId(SKismetDebugTreeView::ColumnId_Value);

		if (NameWidget && ValWidget)
		{
			return FMath::Max((*NameWidget)->GetDesiredSize(), (*ValWidget)->GetDesiredSize()) * FVector2D(2.0f, 1.0f);
		}

		return STableRow<FDebugTreeItemPtr>::ComputeDesiredSize(LayoutScaleMultiplier);
	}
};

//////////////////////////////////////////////////////////////////////////
// SKismetDebugTreeView

void SKismetDebugTreeView::Construct(const FArguments& InArgs)
{
	bFilteredItemsDirty = false;
	SearchString = MakeShared<FString>();

	ChildSlot
	[
		SAssignNew(TreeView, STreeView< FDebugTreeItemPtr >)
			.TreeItemsSource(&FilteredTreeRoots)
			.SelectionMode(InArgs._SelectionMode)
			.OnGetChildren(this, &SKismetDebugTreeView::OnGetChildren)
			.OnGenerateRow(this, &SKismetDebugTreeView::OnGenerateRow)
			.OnExpansionChanged(InArgs._OnExpansionChanged)
			.OnContextMenuOpening(InArgs._OnContextMenuOpening)
			.TreeViewStyle(&FAppStyle::Get().GetWidgetStyle<FTableViewStyle>("PropertyTable.InViewport.ListView"))
			.HeaderRow(InArgs._HeaderRow)
	];
}

void SKismetDebugTreeView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bFilteredItemsDirty)
	{
		UpdateFilteredItems();
		bFilteredItemsDirty = false;
	}
}

void SKismetDebugTreeView::AddTreeItemUnique(const FDebugTreeItemPtr& Item)
{
	RootTreeItems.AddUnique(Item);
	RequestUpdateFilteredItems();
}

bool SKismetDebugTreeView::RemoveTreeItem(const FDebugTreeItemPtr& Item)
{
	if (RootTreeItems.Remove(Item) != 0)
	{
		RequestUpdateFilteredItems();
		return true;
	}

	return false;
}

void SKismetDebugTreeView::ClearTreeItems()
{
	if (!RootTreeItems.IsEmpty())
	{
		RootTreeItems.Empty();
		RequestUpdateFilteredItems();
	}
}

void SKismetDebugTreeView::SetSearchText(const FText& InSearchText)
{
	*SearchString = InSearchText.ToString();
	RequestUpdateFilteredItems();
}

void SKismetDebugTreeView::RequestUpdateFilteredItems()
{
	bFilteredItemsDirty = true;
}

const TArray<FDebugTreeItemPtr>& SKismetDebugTreeView::GetRootTreeItems() const
{
	return RootTreeItems;
}

int32 SKismetDebugTreeView::GetSelectedItems(TArray<FDebugTreeItemPtr>& OutItems)
{
	return TreeView->GetSelectedItems(OutItems);
}

void SKismetDebugTreeView::ClearExpandedItems()
{
	TreeView->ClearExpandedItems();
}

bool SKismetDebugTreeView::IsScrolling() const
{
	return TreeView->IsScrolling();
}

void SKismetDebugTreeView::SetItemExpansion(FDebugTreeItemPtr InItem, bool bInShouldExpandItem)
{
	TreeView->SetItemExpansion(InItem, bInShouldExpandItem);
}

void SKismetDebugTreeView::UpdateFilteredItems()
{
	FilteredTreeRoots.Empty();
	for (FDebugTreeItemPtr Item : RootTreeItems)
	{
		if (Item.IsValid())
		{
			if (Item->CanHaveChildren())
			{
				FLineItemWithChildren* ItemWithChildren = StaticCast<FLineItemWithChildren*>(Item.Get());
				if (SearchString->IsEmpty() || ItemWithChildren->SearchRecursive(*SearchString, TreeView))
				{
					FilteredTreeRoots.Add(Item);
				}
			}
			else
			{
				Item->UpdateSearch(*SearchString, FDebugLineItem::SF_RootNode);
				if (SearchString->IsEmpty() || Item->IsVisible())
				{
					FilteredTreeRoots.Add(Item);
				}
			}
		}
	}

	TreeView->RequestTreeRefresh();
}

TSharedRef<ITableRow> SKismetDebugTreeView::OnGenerateRow(FDebugTreeItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SDebugLineItem, OwnerTable, InItem, SearchString);
}

void SKismetDebugTreeView::OnGetChildren(FDebugTreeItemPtr InParent, TArray<FDebugTreeItemPtr>& OutChildren)
{
	InParent->GatherChildrenBase(OutChildren, *SearchString);
}

FDebugTreeItemPtr SKismetDebugTreeView::MakeTraceStackParentItem()
{
	return MakeShared<FTraceStackParentItem>();
}

FDebugTreeItemPtr SKismetDebugTreeView::MakeBreakpointParentItem(TWeakObjectPtr<UBlueprint> InBlueprint)
{
	return MakeShared<FBreakpointParentItem>(InBlueprint);
}

FDebugTreeItemPtr SKismetDebugTreeView::MakeMessageItem(const FString& InMessage)
{
	return MakeShared<FMessageLineItem>(InMessage);
}

FDebugTreeItemPtr SKismetDebugTreeView::MakeParentItem(UObject* InObject)
{
	return MakeShared<FParentLineItem>(InObject);
}

FDebugTreeItemPtr SKismetDebugTreeView::MakeWatchChildItem(TSharedPtr<struct FPropertyInstanceInfo> InPropertyInfo)
{
	return MakeShared<FWatchChildLineItem>(*InPropertyInfo);
}

#undef LOCTEXT_NAMESPACE
