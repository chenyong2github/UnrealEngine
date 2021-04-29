// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"

#include <type_traits>
#include <limits>

#if WITH_AUTOMATION_WORKER

#define LOCTEXT_NAMESPACE "Slate.Attribute"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlateAttributeTest, "Slate.Attribute", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

namespace UE
{
namespace Slate
{
namespace Private
{

struct FConstructionCounter
{
	FConstructionCounter() : Value(0) { ++DefaultConstructionCounter; }
	FConstructionCounter(int32 InValue) : Value(InValue) { ++DefaultConstructionCounter; }
	FConstructionCounter(const FConstructionCounter& Other) : Value(Other.Value) { ++CopyConstructionCounter; }
	FConstructionCounter(FConstructionCounter&& Other) :Value(Other.Value) { ++MoveConstructionCounter; }
	FConstructionCounter& operator=(const FConstructionCounter& Other) { Value = Other.Value; ++CopyOperatorCounter; return *this; }
	FConstructionCounter& operator=(FConstructionCounter&& Other) { Value = Other.Value; ++MoveOperatorCounter;  return *this; }

	int32 Value;
	bool operator== (const FConstructionCounter& Other) const { return Other.Value == Value; }

	static int32 DefaultConstructionCounter;
	static int32 CopyConstructionCounter;
	static int32 MoveConstructionCounter;
	static int32 CopyOperatorCounter;
	static int32 MoveOperatorCounter;
	static void ResetCounter()
	{
		DefaultConstructionCounter = 0;
		CopyConstructionCounter = 0;
		MoveConstructionCounter = 0;
		CopyOperatorCounter = 0;
		MoveOperatorCounter = 0;
	}
};
int32 FConstructionCounter::DefaultConstructionCounter = 0;
int32 FConstructionCounter::CopyConstructionCounter = 0;
int32 FConstructionCounter::MoveConstructionCounter = 0;
int32 FConstructionCounter::CopyOperatorCounter = 0;
int32 FConstructionCounter::MoveOperatorCounter = 0;


int32 CallbackForIntAttribute(int32 Value)
{
	return Value;
}
FVector2D CallbackForFVectorAttribute()
{
	return FVector2D(1,1);
}

class SAttributeLeftWidget_Parent : public SLeafWidget
{
	SLATE_DECLARE_WIDGET(SAttributeLeftWidget_Parent, SLeafWidget)
public:
	SLATE_BEGIN_ARGS(SAttributeLeftWidget_Parent) {}
	SLATE_END_ARGS()


	SAttributeLeftWidget_Parent()
		: IntAttributeA(*this, 99)
		, IntAttributeB(*this, 99)
		, IntAttributeC(*this, 99)
		, IntAttributeD(*this, 99)
	{
		static_assert(std::is_same<TSlateAttribute<bool>, typename TSlateAttributeRef<bool>::SlateAttributeType>::value, "TSlateAttributeRef doesn't have the same type as TSlateAttribute for bool");
		static_assert(std::is_same<TSlateAttribute<int32>, typename TSlateAttributeRef<int32>::SlateAttributeType>::value, "TSlateAttributeRef doesn't have the same type as TSlateAttribute for int32");
		static_assert(std::is_same<TSlateAttribute<FText>, typename TSlateAttributeRef<FText>::SlateAttributeType>::value, "TSlateAttributeRef doesn't have the same type as TSlateAttribute for FText");
		static_assert(std::is_same<TSlateAttribute<FVector>, typename TSlateAttributeRef<FVector>::SlateAttributeType>::value, "TSlateAttributeRef doesn't have the same type as TSlateAttribute for FVector");
	}

	void Construct(const FArguments& InArgs){}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D{ 100, 100 }; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		return LayerId;
	}


	TSlateAttribute<int32> IntAttributeA;
	TSlateAttribute<int32> IntAttributeB;
	TSlateAttribute<int32> IntAttributeC;
	TSlateAttribute<int32> IntAttributeD;
	TArray<TSlateManagedAttribute<int32, EInvalidateWidgetReason::ChildOrder>> IntManagedAttributes;
};

SLATE_IMPLEMENT_WIDGET(SAttributeLeftWidget_Parent)
void SAttributeLeftWidget_Parent::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
	// The update order is B, A, D, C
	// C updates when D is invalidated, so D needs to be before C
	// A updates after B, so B needs to be before A
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeD, EInvalidateWidgetReason::ChildOrder);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeC, EInvalidateWidgetReason::ChildOrder)
		.UpdateDependency(GET_MEMBER_NAME_CHECKED(PrivateThisType, IntAttributeD));
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeB, EInvalidateWidgetReason::ChildOrder);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeA, EInvalidateWidgetReason::ChildOrder)
		.UpdatePrerequisite(GET_MEMBER_NAME_CHECKED(PrivateThisType, IntAttributeB));

	AttributeInitializer.OverrideInvalidationReason(GET_MEMBER_NAME_CHECKED(PrivateThisType, IntAttributeD), FSlateAttributeDescriptor::FInvalidateWidgetReasonAttribute{ EInvalidateWidgetReason::Paint});
}


class SAttributeLeftWidget_Child : public SAttributeLeftWidget_Parent
{
	SLATE_DECLARE_WIDGET(SAttributeLeftWidget_Child, SAttributeLeftWidget_Parent)
public:
	SLATE_BEGIN_ARGS(SAttributeLeftWidget_Child) {}
	SLATE_END_ARGS()


	SAttributeLeftWidget_Child()
		: IntAttributeH(*this, 99)
		, IntAttributeI(*this, 99)
		, IntAttributeJ(*this, 99)
		, IntAttributeK(*this, 99)
		, IntAttributeL(*this, 99)
		, IntAttributeM(*this, 99)
	{
	}

	void Construct(const FArguments& InArgs) {}

	TSlateAttribute<int32, EInvalidateWidgetReason::ChildOrder> IntAttributeH;
	TSlateAttribute<int32> IntAttributeI;
	TSlateAttribute<int32> IntAttributeJ;
	TSlateAttribute<int32> IntAttributeK;
	TSlateAttribute<int32> IntAttributeL;
	TSlateAttribute<int32> IntAttributeM;
};

SLATE_IMPLEMENT_WIDGET(SAttributeLeftWidget_Child)
void SAttributeLeftWidget_Child::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
	// The update order is M, B, A, I, J, D, C, L, H, K
	//SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeH, EInvalidateWidgetReason::ChildOrder);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeJ, EInvalidateWidgetReason::ChildOrder)
		.UpdateDependency("IntAttributeA");
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeK, EInvalidateWidgetReason::ChildOrder);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeI, EInvalidateWidgetReason::ChildOrder)
		.UpdatePrerequisite("IntAttributeB");
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeL, EInvalidateWidgetReason::ChildOrder)
		.UpdatePrerequisite("IntAttributeC");
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeM, EInvalidateWidgetReason::ChildOrder)
		.UpdatePrerequisite("Visibility")
		.AffectVisibility();
}

class SAttributeLeftWidget_OnInvalidationParent : public SLeafWidget
{
	SLATE_DECLARE_WIDGET(SAttributeLeftWidget_OnInvalidationParent, SLeafWidget)
public:
	SLATE_BEGIN_ARGS(SAttributeLeftWidget_OnInvalidationParent) {}
	SLATE_END_ARGS()


	SAttributeLeftWidget_OnInvalidationParent()
		: IntAttributeA(*this, 99)
		, IntAttributeB(*this, 99)
		, IntAttributeC(*this, 99)
	{
	}

	void Construct(const FArguments& InArgs) {}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D{ 100, 100 }; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		return LayerId;
	}

	void SetAttributeA(TAttribute<int32> ValueA) { IntAttributeA.Assign(*this, MoveTemp(ValueA)); }
	void SetAttributeB(TAttribute<int32> ValueB) { IntAttributeB.Assign(*this, MoveTemp(ValueB)); }
	void SetAttributeC(TAttribute<int32> ValueC) { IntAttributeC.Assign(*this, MoveTemp(ValueC)); }
	virtual void SetCallbackValue(int32 Value) { CallbackValueA = Value; CallbackValueB = Value; CallbackValueC = Value; }

	TSlateAttribute<int32> IntAttributeA;
	TSlateAttribute<int32> IntAttributeB;
	TSlateAttribute<int32> IntAttributeC;
	int32 CallbackValueA;
	int32 CallbackValueB;
	int32 CallbackValueC;
};

}
}
}

bool FSlateAttributeTest::RunTest(const FString& Parameters)
{
	const int NumberOfAttributeInSWidget = 4;
	int32 OrderCounter = 0;
	auto OrderLambda = [this, &OrderCounter]() -> int32
	{
		++OrderCounter;
		return OrderCounter;
	};
	bool bWasUpdate = false;
	int32 ReturnValue = 0;
	auto UpdateLambda = [this, &bWasUpdate, &ReturnValue]() -> int32
	{
		bWasUpdate = true;
		return ReturnValue;
	};

	{
		TSharedRef<UE::Slate::Private::SAttributeLeftWidget_Parent> WidgetParent = SNew(UE::Slate::Private::SAttributeLeftWidget_Parent);

		AddErrorIfFalse(&WidgetParent->GetWidgetClass() == &UE::Slate::Private::SAttributeLeftWidget_Parent::StaticWidgetClass()
			, TEXT("The static data do not matches"));

		FSlateAttributeDescriptor const& AttributeDescriptor = WidgetParent->GetWidgetClass().GetAttributeDescriptor();
		AddErrorIfFalse(AttributeDescriptor.GetAttributeNum() == 4 + NumberOfAttributeInSWidget, TEXT("Invalid number of attributes"));

		const int32 IndexA = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeA");
		const int32 IndexB = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeB");
		const int32 IndexC = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeC");
		const int32 IndexD = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeD");
		const int32 IndexI = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeI");
		const int32 IndexJ = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeJ");
		const int32 IndexK = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeK");

		AddErrorIfFalse(IndexA != INDEX_NONE, TEXT("Could not find the Attribute A"));
		AddErrorIfFalse(IndexB != INDEX_NONE, TEXT("Could not find the Attribute B"));
		AddErrorIfFalse(IndexC != INDEX_NONE, TEXT("Could not find the Attribute C"));
		AddErrorIfFalse(IndexD != INDEX_NONE, TEXT("Could not find the Attribute D"));
		AddErrorIfFalse(IndexI == INDEX_NONE, TEXT("Was not supposed to find the Attribute I"));
		AddErrorIfFalse(IndexJ == INDEX_NONE, TEXT("Was not supposed to find the Attribute J"));
		AddErrorIfFalse(IndexK == INDEX_NONE, TEXT("Was not supposed to find the Attribute K"));

		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexA) == AttributeDescriptor.FindAttribute("IntAttributeA"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexB) == AttributeDescriptor.FindAttribute("IntAttributeB"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexC) == AttributeDescriptor.FindAttribute("IntAttributeC"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexD) == AttributeDescriptor.FindAttribute("IntAttributeD"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(AttributeDescriptor.FindAttribute("IntAttributeI") == nullptr, TEXT("Was not supposed to find the Attribute I"));
		AddErrorIfFalse(AttributeDescriptor.FindAttribute("IntAttributeJ") == nullptr, TEXT("Was not supposed to find the Attribute J"));
		AddErrorIfFalse(AttributeDescriptor.FindAttribute("IntAttributeK") == nullptr, TEXT("Was not supposed to find the Attribute K"));

		//B, A, D, C
		AddErrorIfFalse(AttributeDescriptor.GetAttributeAtIndex(IndexB).SortOrder < AttributeDescriptor.GetAttributeAtIndex(IndexA).SortOrder, TEXT("B should have a lower value than A"));
		AddErrorIfFalse(AttributeDescriptor.GetAttributeAtIndex(IndexD).SortOrder < AttributeDescriptor.GetAttributeAtIndex(IndexC).SortOrder, TEXT("D should have a lower value than C"));
		AddErrorIfFalse(AttributeDescriptor.GetAttributeAtIndex(IndexA).SortOrder < AttributeDescriptor.GetAttributeAtIndex(IndexD).SortOrder, TEXT("A should have a lower value than D"));

		{
			OrderCounter = 0;
			WidgetParent->IntAttributeA.Assign(WidgetParent.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetParent->IntAttributeA.Get() == 99, TEXT("A It is not the expected value."));
			WidgetParent->IntAttributeB.Assign(WidgetParent.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetParent->IntAttributeB.Get() == 99, TEXT("B It is not the expected value."));
			WidgetParent->IntAttributeC.Assign(WidgetParent.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetParent->IntAttributeC.Get() == 99, TEXT("C It is not the expected value."));
			WidgetParent->IntAttributeD.Assign(WidgetParent.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetParent->IntAttributeD.Get() == 99, TEXT("D It is not the expected value."));

			OrderCounter = 0;
			bWasUpdate = false;
			ReturnValue = 4;
			WidgetParent->MarkPrepassAsDirty();
			WidgetParent->SlatePrepass(1.f);
			AddErrorIfFalse(WidgetParent->IntAttributeA.Get() == 2, TEXT("A It is not the expected value."));
			AddErrorIfFalse(WidgetParent->IntAttributeB.Get() == 1, TEXT("B It is not the expected value."));
			AddErrorIfFalse(WidgetParent->IntAttributeC.Get() == 4, TEXT("C It is not the expected value."));
			AddErrorIfFalse(WidgetParent->IntAttributeD.Get() == 3, TEXT("D It is not the expected value."));
		}

		{
			OrderCounter = 0;
			bWasUpdate = false;
			ReturnValue = 5;
			WidgetParent->IntAttributeC.Assign(WidgetParent.Get(), MakeAttributeLambda(UpdateLambda));
			AddErrorIfFalse(!bWasUpdate, TEXT("C should not have be updated."));
			AddErrorIfFalse(WidgetParent->IntAttributeC.Get() == 4, TEXT("C It is not the expected value."));
			WidgetParent->MarkPrepassAsDirty();
			WidgetParent->SlatePrepass(1.f);
			AddErrorIfFalse(bWasUpdate, TEXT("C should be updated."));
			AddErrorIfFalse(WidgetParent->IntAttributeC.Get() == 5, TEXT("C It is not the expected value."));
		}

		{
			OrderCounter = 0;
			bWasUpdate = false;
			ReturnValue = 10;	//10 show that C didn't change
			WidgetParent->MarkPrepassAsDirty();
			WidgetParent->SlatePrepass(1.f);
			AddErrorIfFalse(WidgetParent->IntAttributeA.Get() == 2, TEXT("A It is not the expected value."));
			AddErrorIfFalse(WidgetParent->IntAttributeB.Get() == 1, TEXT("B It is not the expected value."));
			AddErrorIfFalse(!bWasUpdate, TEXT("C should not be updated."));
			AddErrorIfFalse(WidgetParent->IntAttributeC.Get() == 5, TEXT("C It is not the expected value."));
			AddErrorIfFalse(WidgetParent->IntAttributeD.Get() == 3, TEXT("D It is not the expected value."));
		}

		{
			WidgetParent->IntAttributeD.Set(WidgetParent.Get(), 8);
			AddErrorIfFalse(WidgetParent->IntAttributeC.Get() == 5, TEXT("C It is not the expected value."));
			AddErrorIfFalse(WidgetParent->IntAttributeD.Get() == 8, TEXT("D It is not the expected value."));

			OrderCounter = 0;
			bWasUpdate = false;
			ReturnValue = 10;
			WidgetParent->MarkPrepassAsDirty();
			WidgetParent->SlatePrepass(1.f);
			AddErrorIfFalse(WidgetParent->IntAttributeA.Get() == 2, TEXT("A It is not the expected value."));
			AddErrorIfFalse(WidgetParent->IntAttributeB.Get() == 1, TEXT("B It is not the expected value."));
			AddErrorIfFalse(bWasUpdate, TEXT("C should be updated becaude D was."));
			AddErrorIfFalse(WidgetParent->IntAttributeC.Get() == 10, TEXT("C It is not the expected value."));
			AddErrorIfFalse(WidgetParent->IntAttributeD.Get() == 8, TEXT("D It is not the expected value."));
#ifndef PVS_STUDIO // Build machine refuses to disable this warning
			AddErrorIfFalse(OrderCounter == 2, TEXT("There is no D attribute anymore.")); //-V547
#endif
		}

		{
			OrderCounter = 0;
			bWasUpdate = false;
			ReturnValue = 10;
			WidgetParent->MarkPrepassAsDirty();
			WidgetParent->SlatePrepass(1.f);
			AddErrorIfFalse(!bWasUpdate, TEXT("C should not be updated."));
		}
	}

	{
		TSharedRef<UE::Slate::Private::SAttributeLeftWidget_Child> WidgetChild = SNew(UE::Slate::Private::SAttributeLeftWidget_Child);

		AddErrorIfFalse(&WidgetChild->GetWidgetClass() == &UE::Slate::Private::SAttributeLeftWidget_Child::StaticWidgetClass()
			, TEXT("The static data do not matches"));

		FSlateAttributeDescriptor const& AttributeDescriptor = WidgetChild->GetWidgetClass().GetAttributeDescriptor();
		AddErrorIfFalse(AttributeDescriptor.GetAttributeNum() == 9 + NumberOfAttributeInSWidget, TEXT("Invalid number of attributes")); // H is not counted

		const int32 IndexA = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeA");
		const int32 IndexB = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeB");
		const int32 IndexC = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeC");
		const int32 IndexD = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeD");
		const int32 IndexI = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeI");
		const int32 IndexJ = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeJ");
		const int32 IndexK = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeK");
		const int32 IndexL = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeL");
		const int32 IndexM = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeM");

		AddErrorIfFalse(IndexA != INDEX_NONE, TEXT("Could not find the Attribute A"));
		AddErrorIfFalse(IndexB != INDEX_NONE, TEXT("Could not find the Attribute B"));
		AddErrorIfFalse(IndexC != INDEX_NONE, TEXT("Could not find the Attribute C"));
		AddErrorIfFalse(IndexD != INDEX_NONE, TEXT("Could not find the Attribute D"));
		AddErrorIfFalse(IndexI != INDEX_NONE, TEXT("Could not find the Attribute I"));
		AddErrorIfFalse(IndexJ != INDEX_NONE, TEXT("Could not find the Attribute J"));
		AddErrorIfFalse(IndexK != INDEX_NONE, TEXT("Could not find the Attribute K"));
		AddErrorIfFalse(IndexL != INDEX_NONE, TEXT("Could not find the Attribute L"));
		AddErrorIfFalse(IndexM != INDEX_NONE, TEXT("Could not find the Attribute M"));

		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexA) == AttributeDescriptor.FindAttribute("IntAttributeA"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexB) == AttributeDescriptor.FindAttribute("IntAttributeB"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexC) == AttributeDescriptor.FindAttribute("IntAttributeC"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexD) == AttributeDescriptor.FindAttribute("IntAttributeD"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexI) == AttributeDescriptor.FindAttribute("IntAttributeI"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexJ) == AttributeDescriptor.FindAttribute("IntAttributeJ"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexK) == AttributeDescriptor.FindAttribute("IntAttributeK"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexL) == AttributeDescriptor.FindAttribute("IntAttributeL"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexM) == AttributeDescriptor.FindAttribute("IntAttributeM"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(AttributeDescriptor.FindAttribute("IntAttributeH") == nullptr, TEXT("H exist but is not defined."));

		AddErrorIfFalse(AttributeDescriptor.GetAttributeAtIndex(IndexM).SortOrder < AttributeDescriptor.GetAttributeAtIndex(IndexB).SortOrder, TEXT("M should have a lower value than B"));
		AddErrorIfFalse(AttributeDescriptor.GetAttributeAtIndex(IndexB).SortOrder < AttributeDescriptor.GetAttributeAtIndex(IndexA).SortOrder, TEXT("B should have a lower value than A"));
		AddErrorIfFalse(AttributeDescriptor.GetAttributeAtIndex(IndexA).SortOrder <= AttributeDescriptor.GetAttributeAtIndex(IndexI).SortOrder, TEXT("A should have a lower value than I"));
		AddErrorIfFalse(AttributeDescriptor.GetAttributeAtIndex(IndexI).SortOrder < AttributeDescriptor.GetAttributeAtIndex(IndexJ).SortOrder, TEXT("I should have a lower value than J"));
		AddErrorIfFalse(AttributeDescriptor.GetAttributeAtIndex(IndexJ).SortOrder < AttributeDescriptor.GetAttributeAtIndex(IndexD).SortOrder, TEXT("J should have a lower value than D"));
		AddErrorIfFalse(AttributeDescriptor.GetAttributeAtIndex(IndexD).SortOrder < AttributeDescriptor.GetAttributeAtIndex(IndexC).SortOrder, TEXT("D should have a lower value than C"));
		AddErrorIfFalse(AttributeDescriptor.GetAttributeAtIndex(IndexC).SortOrder < AttributeDescriptor.GetAttributeAtIndex(IndexL).SortOrder, TEXT("C should have a lower value than L"));
		AddErrorIfFalse(AttributeDescriptor.GetAttributeAtIndex(IndexL).SortOrder < AttributeDescriptor.GetAttributeAtIndex(IndexK).SortOrder, TEXT("L should have a lower value than K"));


		{
			OrderCounter = 49;
			WidgetChild->IntAttributeA.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeA.Get() == 99, TEXT("A It is not the expected value."));
			WidgetChild->IntAttributeB.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeB.Get() == 99, TEXT("B It is not the expected value."));
			WidgetChild->IntAttributeC.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeC.Get() == 99, TEXT("C It is not the expected value."));
			WidgetChild->IntAttributeD.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeD.Get() == 99, TEXT("D It is not the expected value."));
			WidgetChild->IntAttributeH.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeH.Get() == 99, TEXT("I It is not the expected value."));
			WidgetChild->IntAttributeI.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeI.Get() == 99, TEXT("I It is not the expected value."));
			WidgetChild->IntAttributeJ.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeJ.Get() == 99, TEXT("J It is not the expected value."));
			WidgetChild->IntAttributeK.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeK.Get() == 99, TEXT("K It is not the expected value."));
			WidgetChild->IntAttributeL.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeL.Get() == 99, TEXT("L It is not the expected value."));
			WidgetChild->IntAttributeM.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeM.Get() == 99, TEXT("L It is not the expected value."));


			OrderCounter = 0;
			bWasUpdate = false;
			ReturnValue = 4;
			WidgetChild->MarkPrepassAsDirty();
			WidgetChild->SlatePrepass(1.f);
			AddErrorIfFalse(WidgetChild->IntAttributeA.Get() == 3 || WidgetChild->IntAttributeA.Get() == 4, TEXT("A It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeB.Get() == 2, TEXT("B It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeC.Get() == 7, TEXT("C It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeD.Get() == 6, TEXT("D It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeH.Get() == 9, TEXT("H It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeI.Get() == 3 || WidgetChild->IntAttributeI.Get() == 4, TEXT("I It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeJ.Get() == 5, TEXT("J It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeK.Get() == 10, TEXT("K It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeL.Get() == 8, TEXT("L It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeM.Get() == 1, TEXT("L It is not the expected value."));
		}

		{
			OrderCounter = 0;
			bWasUpdate = false;
			ReturnValue = 4;
			WidgetChild->MarkPrepassAsDirty();
			WidgetChild->SlatePrepass(1.f);
			AddErrorIfFalse(WidgetChild->IntAttributeA.Get() == 3 || WidgetChild->IntAttributeA.Get() == 4, TEXT("A It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeB.Get() == 2, TEXT("B It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeC.Get() == 6, TEXT("C It is not the expected value.")); // will get updated because D changes
			AddErrorIfFalse(WidgetChild->IntAttributeD.Get() == 5, TEXT("D It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeH.Get() == 8, TEXT("H It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeI.Get() == 3 || WidgetChild->IntAttributeI.Get() == 4, TEXT("I It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeJ.Get() == 5, TEXT("J It is not the expected value.")); // should not get updated
			AddErrorIfFalse(WidgetChild->IntAttributeK.Get() == 9, TEXT("K It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeL.Get() == 7, TEXT("L It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeM.Get() == 1, TEXT("M It is not the expected value."));
		}

		// Check the ForEachDependency result
		{
			const FSlateAttributeDescriptor& ChildDescriptor = WidgetChild->GetWidgetClass().GetAttributeDescriptor();
			auto DependencyTest = [this, ChildDescriptor](auto& AttributeInstance, TSharedRef<SWidget> Widget, int32 Expected, FStringView VariableName)
			{
				auto FindOffet = [](const SWidget& OwningWidget, const FSlateAttributeBase& Attribute)
				{
					UPTRINT Offset = (UPTRINT)(&Attribute) - (UPTRINT)(&OwningWidget);
					ensure(Offset <= std::numeric_limits<FSlateAttributeDescriptor::OffsetType>::max());
					return (FSlateAttributeDescriptor::OffsetType)(Offset);
				};

				int32 Count = 0;
				const FSlateAttributeDescriptor::FAttribute* FoundAttribute = ChildDescriptor.FindMemberAttribute(FindOffet(Widget.Get(), AttributeInstance));
				if (FoundAttribute == nullptr)
				{
					AddError(FString::Printf(TEXT("Could not find attribute '%s'"), VariableName.GetData()));
				}
				else
				{
					ChildDescriptor.ForEachDependentsOn(*FoundAttribute, [&Count](uint8 Index) {++Count;});
					AddErrorIfFalse(Count == Expected, FString::Printf(TEXT("'%s' doesn't have the correct number of dependency (returned %d, expected %d).")
						, VariableName.GetData(), Count, Expected));
				}
			};
			DependencyTest(WidgetChild->IntAttributeA, WidgetChild, 1, TEXT("A"));//J
			DependencyTest(WidgetChild->IntAttributeB, WidgetChild, 0, TEXT("B"));//AIJ, they are prerequisite, not dependency
			DependencyTest(WidgetChild->IntAttributeC, WidgetChild, 0, TEXT("C"));//L
			DependencyTest(WidgetChild->IntAttributeD, WidgetChild, 1, TEXT("D"));//CL
			DependencyTest(WidgetChild->IntAttributeI, WidgetChild, 0, TEXT("I"));
			DependencyTest(WidgetChild->IntAttributeJ, WidgetChild, 0, TEXT("J"));
			DependencyTest(WidgetChild->IntAttributeK, WidgetChild, 0, TEXT("K"));
			DependencyTest(WidgetChild->IntAttributeL, WidgetChild, 0, TEXT("L"));
			DependencyTest(WidgetChild->IntAttributeM, WidgetChild, 0, TEXT("M"));
		}
	}


	// Make sure we call all the functions
	{
		{
			// This should just compile to TSlateAttribute
			struct SAttributeAttribute : public SLeafWidget
			{
				SAttributeAttribute()
					: Toto(6)
					, AttributeA(*this)
					, AttributeB(*this, 5)
					, AttributeC(*this, MoveTemp(Toto))
				{ }
				SLATE_BEGIN_ARGS(SAttributeAttribute) {}  SLATE_END_ARGS()
				virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override { return LayerId; }
				virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(0.f, 0.f); }
				void Construct(const FArguments&) {	}

				int32 Callback() const { return 0; }

				int32 Toto;
				TSlateAttribute<int32, EInvalidateWidgetReason::Paint> AttributeA;
				TSlateAttribute<int32, EInvalidateWidgetReason::Paint> AttributeB;
				TSlateAttribute<int32, EInvalidateWidgetReason::Paint> AttributeC;
			};
			TSharedPtr<SAttributeAttribute> Widget = SNew(SAttributeAttribute);

			{
				int32 Hello = 7;
				int32 Return1 = Widget->AttributeA.Get();
				Widget->AttributeA.UpdateNow(*Widget);
				Widget->AttributeA.Set(*Widget, 6);
				Widget->AttributeA.Set(*Widget, MoveTemp(Hello));
			}
			{
				auto Getter1 = TAttribute<int32>::FGetter::CreateStatic(UE::Slate::Private::CallbackForIntAttribute, 1);
				Widget->AttributeA.Bind(*Widget, Getter1);
				Widget->AttributeA.Bind(*Widget, MoveTemp(Getter1));
				Widget->AttributeA.Bind(*Widget, &SAttributeAttribute::Callback);
			}
			{
				int32 TmpInt1 = 7;
				int32 TmpInt2 = 7;
				auto Getter1 = TAttribute<int32>::FGetter::CreateStatic(UE::Slate::Private::CallbackForIntAttribute, 1);
				TAttribute<int32> Attribute1 = TAttribute<int32>::Create(Getter1);
				TAttribute<int32> Attribute2 = TAttribute<int32>::Create(Getter1);
				TAttribute<int32> Attribute3 = TAttribute<int32>::Create(Getter1);
				Widget->AttributeA.Assign(*Widget, Attribute1);
				Widget->AttributeA.Assign(*Widget, MoveTemp(Attribute1));
				Widget->AttributeA.Assign(*Widget, Attribute2, 7);
				Widget->AttributeA.Assign(*Widget, MoveTemp(Attribute2), 7);
				Widget->AttributeA.Assign(*Widget, Attribute3, MoveTemp(TmpInt1));
				Widget->AttributeA.Assign(*Widget, MoveTemp(Attribute3), MoveTemp(TmpInt2));
			}
			{
				bool bIsBound1 = Widget->AttributeA.IsBound(*Widget);
				bool bIsIdentical1 = Widget->AttributeA.IdenticalTo(*Widget, Widget->AttributeA);
				auto Getter1 = TAttribute<int32>::FGetter::CreateStatic(UE::Slate::Private::CallbackForIntAttribute, 1);
				TAttribute<int32> Attribute1 = TAttribute<int32>::Create(Getter1);
				bool bIsIdentical2 = Widget->AttributeA.IdenticalTo(*Widget, Attribute1);
			}
		}
		{
			typedef UE::Slate::Private::FConstructionCounter FLocalConstructionCounter;

			// This should just compile to TSlateManagedAttribute
			struct SAttributeAttribute : public SLeafWidget
			{
				SLATE_BEGIN_ARGS(SAttributeAttribute) {} SLATE_END_ARGS()
				virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override { return LayerId; }
				virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(0.f, 0.f); }
				void Construct(const FArguments&) {}

				FLocalConstructionCounter ReturnDefaultCounter() const { return FLocalConstructionCounter(0); }

				using ManagedSlateAttributeType = TSlateManagedAttribute<FLocalConstructionCounter, EInvalidateWidgetReason::Layout>;
			};
			TSharedPtr<SAttributeAttribute>	Widget = SNew(SAttributeAttribute);


			auto AddErrorIfCounterDoNotMatches = [this](int32 Construct, int32 Copy, int32 Move, int32 CopyAssign, int32 MoveAssign, const TCHAR* Message)
			{
				bool bSuccess = FLocalConstructionCounter::DefaultConstructionCounter == Construct
					&& FLocalConstructionCounter::CopyConstructionCounter == Copy
					&& FLocalConstructionCounter::MoveConstructionCounter == Move
					&& FLocalConstructionCounter::CopyOperatorCounter == CopyAssign
					&& FLocalConstructionCounter::MoveOperatorCounter == MoveAssign;
				AddErrorIfFalse(bSuccess, Message);
			};

			{
				FLocalConstructionCounter::ResetCounter();
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef() );
				AddErrorIfCounterDoNotMatches(1, 0, 0, 0, 0, TEXT("Default & Copy constructor was not used."));
			}
			{
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef(), Counter);
				AddErrorIfCounterDoNotMatches(0, 1, 0, 0, 0, TEXT("Default & Copy constructor was not used."));
			}
			{
				FLocalConstructionCounter::ResetCounter();
				FLocalConstructionCounter Counter = 1;
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef(), MoveTemp(Counter));
				AddErrorIfCounterDoNotMatches(1, 0, 1, 0, 0, TEXT("Default & Move constructor was not used."));
			}
			{
				TAttribute<FLocalConstructionCounter>::FGetter Getter1 = TAttribute<FLocalConstructionCounter>::FGetter::CreateLambda([](){return FLocalConstructionCounter(1);});
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType{Widget.ToSharedRef(), Getter1, Counter};
				AddErrorIfCounterDoNotMatches(0, 1, 0, 0, 0, TEXT("Getter & Copy constructor was not used."));
			}
			{
				TAttribute<FLocalConstructionCounter>::FGetter Getter1 = TAttribute<FLocalConstructionCounter>::FGetter::CreateLambda([]() {return FLocalConstructionCounter(1);});
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType{Widget.ToSharedRef(), Getter1, MoveTemp(Counter)};
				AddErrorIfCounterDoNotMatches(0, 0, 1, 0, 0, TEXT("Getter & Move constructor was not used."));
			}
			{
				TAttribute<FLocalConstructionCounter>::FGetter Getter1 = TAttribute<FLocalConstructionCounter>::FGetter::CreateLambda([]() {return FLocalConstructionCounter(1);});
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef(), MoveTemp(Getter1), Counter);
				AddErrorIfCounterDoNotMatches(0, 1, 0, 0, 0, TEXT("Move Getter & Copy constructor was not used."));
			}
			{
				TAttribute<FLocalConstructionCounter>::FGetter Getter1 = TAttribute<FLocalConstructionCounter>::FGetter::CreateLambda([]() {return FLocalConstructionCounter(1);});
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef(), MoveTemp(Getter1), MoveTemp(Counter));
				AddErrorIfCounterDoNotMatches(0, 0, 1, 0, 0, TEXT("Move Getter & Move constructor was not used."));
			}
			{
				TAttribute<FLocalConstructionCounter> Attribute1 = MakeAttributeLambda([]() {return FLocalConstructionCounter(1);});
				FLocalConstructionCounter Counter;
				FLocalConstructionCounter::ResetCounter();
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef(), Attribute1, Counter);
				AddErrorIfCounterDoNotMatches(0, 1, 0, 0, 0, TEXT("Attribute & Copy constructor was not used."));
			}
			{
				TAttribute<FLocalConstructionCounter> Attribute1 = MakeAttributeLambda([]() {return FLocalConstructionCounter(1);});
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef(), MoveTemp(Attribute1), MoveTemp(Counter));
				AddErrorIfCounterDoNotMatches(0, 0, 1, 0, 0, TEXT("Move Attribute & Move constructor was not used."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				FLocalConstructionCounter::ResetCounter();
				FLocalConstructionCounter Result = Attribute.Get();
				Attribute.UpdateNow();
				AddErrorIfCounterDoNotMatches(0, 1, 0, 0, 0, TEXT("Get and UpdateNow failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Set(Counter);
				AddErrorIfCounterDoNotMatches(0, 0, 0, 1, 0, TEXT("Set Copy failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Set(MoveTemp(Counter));
				AddErrorIfCounterDoNotMatches(0, 0, 0, 0, 1, TEXT("Set Move failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				auto Getter1 = TAttribute<FLocalConstructionCounter>::FGetter::CreateLambda([]() {return FLocalConstructionCounter(1);});
				FLocalConstructionCounter::ResetCounter();
				Attribute.Bind(Getter1);
				AddErrorIfCounterDoNotMatches(0, 0, 0, 0, 0, TEXT("Bind Copy failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				auto Getter1 = TAttribute<FLocalConstructionCounter>::FGetter::CreateLambda([]() {return FLocalConstructionCounter(1);});
				FLocalConstructionCounter::ResetCounter();
				Attribute.Bind(MoveTemp(Getter1));
				AddErrorIfCounterDoNotMatches(0, 0, 0, 0, 0, TEXT("Bind Copy failed."));
			}
			// Test Assign
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				TAttribute<FLocalConstructionCounter> Attribute1;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(Attribute1);
				AddErrorIfCounterDoNotMatches(0, 0, 0, 0, 0, TEXT("Assign Copy failed."));
				Attribute1.Set({1});
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(Attribute1);
				AddErrorIfCounterDoNotMatches(0, 0, 0, 1, 0, TEXT("Assign Copy failed."));
				Attribute1.Set({ 1 });
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(Attribute1);
				AddErrorIfCounterDoNotMatches(0, 0, 0, 0, 0, TEXT("Assign Move failed."));
				Attribute1.Set({ 2 });
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(MoveTemp(Attribute1));
				AddErrorIfCounterDoNotMatches(0, 0, 2, 0, 1, TEXT("Assign Move failed."));
			}
			// Test unbinded Attribute
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				TAttribute<FLocalConstructionCounter> Attribute1;
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(Attribute1, Counter);
				AddErrorIfCounterDoNotMatches(0, 0, 0, 1, 0, TEXT("Assign Copy/Copy failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				TAttribute<FLocalConstructionCounter> Attribute1;
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(Attribute1, MoveTemp(Counter));
				AddErrorIfCounterDoNotMatches(0, 0, 0, 0, 1, TEXT("Assign Copy/Move failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				TAttribute<FLocalConstructionCounter> Attribute1;
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(MoveTemp(Attribute1), Counter);
				AddErrorIfCounterDoNotMatches(0, 0, 0, 1, 0, TEXT("Assign Move/CCopy failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				TAttribute<FLocalConstructionCounter> Attribute1;
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(MoveTemp(Attribute1), MoveTemp(Counter));
				AddErrorIfCounterDoNotMatches(0, 0, 0, 0, 1, TEXT("Assign Move/Move failed."));
			}
			// Test binded Attribute
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				TAttribute<FLocalConstructionCounter> Attribute1 = MakeAttributeLambda([]() {return FLocalConstructionCounter(1);});
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(Attribute1, Counter);
				AddErrorIfCounterDoNotMatches(0, 0, 0, 0, 0, TEXT("Bind Copy with binded attribute failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				TAttribute<FLocalConstructionCounter> Attribute1 = MakeAttributeLambda([]() {return FLocalConstructionCounter(1);});
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(Attribute1, MoveTemp(Counter));
				AddErrorIfCounterDoNotMatches(0, 0, 0, 0, 0, TEXT("Bind Move with binded attribute failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				TAttribute<FLocalConstructionCounter> Attribute1 = MakeAttributeLambda([]() {return FLocalConstructionCounter(1);});
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(MoveTemp(Attribute1), Counter);
				AddErrorIfCounterDoNotMatches(0, 0, 0, 0, 0, TEXT("Assign Copy with binded attribute failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				TAttribute<FLocalConstructionCounter> Attribute1 = MakeAttributeLambda([]() {return FLocalConstructionCounter(1);});
				FLocalConstructionCounter Counter = 1;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(MoveTemp(Attribute1), MoveTemp(Counter));
				AddErrorIfCounterDoNotMatches(0, 0, 0, 0, 0, TEXT("Assign Move with binded attribute failed."));
			}
			// Test set Attribute
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				FLocalConstructionCounter Counter = 1;
				TAttribute<FLocalConstructionCounter> Attribute1 = Counter;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(Attribute1, Counter);
				AddErrorIfCounterDoNotMatches(0, 0, 0, 1, 0, TEXT("Assign Set Copy failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				FLocalConstructionCounter Counter = 1;
				TAttribute<FLocalConstructionCounter> Attribute1 = Counter;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(Attribute1, MoveTemp(Counter));
				AddErrorIfCounterDoNotMatches(0, 0, 0, 1, 0, TEXT("Assign Set Copy failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				FLocalConstructionCounter Counter = 1;
				TAttribute<FLocalConstructionCounter> Attribute1 = Counter;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(MoveTemp(Attribute1), Counter);
				AddErrorIfCounterDoNotMatches(0, 0, 2, 0, 1, TEXT("Assign Set Move failed."));
			}
			{
				SAttributeAttribute::ManagedSlateAttributeType Attribute = SAttributeAttribute::ManagedSlateAttributeType(Widget.ToSharedRef());
				FLocalConstructionCounter Counter = 1;
				TAttribute<FLocalConstructionCounter> Attribute1 = Counter;
				FLocalConstructionCounter::ResetCounter();
				Attribute.Assign(MoveTemp(Attribute1), MoveTemp(Counter));
				AddErrorIfCounterDoNotMatches(0, 0, 2, 0, 1, TEXT("Assign Set Move failed."));
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE 

#endif //WITH_AUTOMATION_WORKER
