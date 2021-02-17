// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"

#if WITH_AUTOMATION_WORKER

#define LOCTEXT_NAMESPACE "Slate.Attribute"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlateAttributeTest, "Slate.Attribute", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

namespace UE
{
namespace Slate
{
namespace Private
{

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
		: IntAttributeA(*this, 1)
		, IntAttributeB(*this, 1)
		, IntAttributeC(*this, 1)
		, IntAttributeD(*this, 1)
		, IntAttributeWithPredicate(*this, 1)
		//, IntManagedAttributes{*this, 4}
	{
	}
	void Construct(const FArguments& InArgs){}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D{ 100, 100 }; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		return LayerId;
	}

	int32 TestMember()
	{
		// this should not compile
		{
			//TSlateAttribute<int32> Other = IntAttribute;
			//Other.Set(*this, 4);
			//return Other.Get();
		}
		// this should compile but failed with check at runtime
		{
			//TSlateAttribute<int32> Other{ *this, 0 };
			//Other.Set(*this, 4);
			//return Other.Get();
		}
		{
			int32 DefaultValue = 0;
			TSlateAttribute<int32> Attribute = { *this, DefaultValue };
		}
		{
			int32 DefaultValue = 0;
			auto Getter1 = TAttribute<int32>::FGetter::CreateStatic(UE::Slate::Private::CallbackForIntAttribute, 1);
			TSlateAttribute<int32> Attribute = { *this, DefaultValue, Getter1 };
		}
		{
			int32 DefaultValue = 0;
			auto Getter1 = TAttribute<int32>::FGetter::CreateStatic(UE::Slate::Private::CallbackForIntAttribute, 1);
			TSlateAttribute<int32> Attribute = { *this, DefaultValue, MoveTemp(Getter1) };
		} 
		{
			int32 DefaultValue = 0;
			TSlateAttribute<int32> Attribute = { *this, DefaultValue, MakeAttributeLambda([](){return 5;}) };
		}
		{
			FVector2D DefaultValue = FVector2D(0.f, 0.f);
			TSlateAttribute<FVector2D> Attribute = { *this, MoveTemp(DefaultValue) };
		}
		{
			FVector2D DefaultValue = FVector2D(0.f, 0.f);
			auto Getter1 = TAttribute<FVector2D>::FGetter::CreateStatic(UE::Slate::Private::CallbackForFVectorAttribute);
			TSlateAttribute<FVector2D> Attribute = { *this, MoveTemp(DefaultValue), Getter1 };
		}
		{
			FVector2D DefaultValue = FVector2D(0.f, 0.f);
			auto Getter1 = TAttribute<FVector2D>::FGetter::CreateStatic(UE::Slate::Private::CallbackForFVectorAttribute);
			TSlateAttribute<FVector2D> Attribute = { *this, MoveTemp(DefaultValue), MoveTemp(Getter1) };
		}
		{
			FVector2D DefaultValue = FVector2D(0.f, 0.f);
			TSlateAttribute<FVector2D> Attribute = { *this, MoveTemp(DefaultValue), MakeAttributeLambda([]() {return FVector2D(1.f, 1.f);}) };
		}

		return 0; 
	}

	void TestExternalBind()
	{
		// Copy should not compile
		{
			//TSlateManagedAttribute<int32, EInvalidateWidgetReason::ChildOrder> ExtAtt1 {AsShared(), 10};
			//IntManagedAttributes.Add(ExtAtt1);
		}

		// Move should compile
		{
			TSlateManagedAttribute<int32, EInvalidateWidgetReason::ChildOrder> ExtAtt1 {AsShared(), 10};
			IntManagedAttributes.Add(MoveTemp(ExtAtt1));
		}
	}

	TSlateAttribute<int32> IntAttributeA;
	TSlateAttribute<int32> IntAttributeB;
	TSlateAttribute<int32> IntAttributeC;
	TSlateAttribute<int32> IntAttributeD;
	TSlateAttributeWithPredicate<int32, TSlateAttributeInvalidationReason<EInvalidateWidgetReason::ChildOrder>> IntAttributeWithPredicate;
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
		: IntAttributeH(*this, 1)
		, IntAttributeI(*this, 1)
		, IntAttributeJ(*this, 1)
		, IntAttributeK(*this, 1)
		, IntAttributeL(*this, 1)
	{
	}

	void Construct(const FArguments& InArgs) {}

	TSlateAttribute<int32, EInvalidateWidgetReason::ChildOrder> IntAttributeH;
	TSlateAttribute<int32> IntAttributeI;
	TSlateAttribute<int32> IntAttributeJ;
	TSlateAttribute<int32> IntAttributeK;
	TSlateAttribute<int32> IntAttributeL;
};

SLATE_IMPLEMENT_WIDGET(SAttributeLeftWidget_Child)
void SAttributeLeftWidget_Child::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
	// The update order is B, A, I, J, D, C, L, H, K
	//SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeH, EInvalidateWidgetReason::ChildOrder);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeJ, EInvalidateWidgetReason::ChildOrder)
		.UpdateDependency("IntAttributeA");
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeK, EInvalidateWidgetReason::ChildOrder);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeI, EInvalidateWidgetReason::ChildOrder)
		.UpdatePrerequisite("IntAttributeB");
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, IntAttributeL, EInvalidateWidgetReason::ChildOrder)
		.UpdatePrerequisite("IntAttributeC");
}

}
}
}


bool FSlateAttributeTest::RunTest(const FString& Parameters)
{
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
		AddErrorIfFalse(AttributeDescriptor.AttributeNum() == 4, TEXT(""));

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
			AddErrorIfFalse(WidgetParent->IntAttributeA.Get() == 1, TEXT("A It is not the expected value."));
			WidgetParent->IntAttributeB.Assign(WidgetParent.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetParent->IntAttributeB.Get() == 2, TEXT("B It is not the expected value."));
			WidgetParent->IntAttributeC.Assign(WidgetParent.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetParent->IntAttributeC.Get() == 3, TEXT("C It is not the expected value."));
			WidgetParent->IntAttributeD.Assign(WidgetParent.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetParent->IntAttributeD.Get() == 4, TEXT("D It is not the expected value."));

			OrderCounter = 0;
			bWasUpdate = false;
			ReturnValue = 4;
			WidgetParent->InvalidatePrepass();
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
			AddErrorIfFalse(bWasUpdate, TEXT("C should be updated."));
			AddErrorIfFalse(WidgetParent->IntAttributeC.Get() == 5, TEXT("C It is not the expected value."));
		}

		{
			OrderCounter = 0;
			bWasUpdate = false;
			ReturnValue = 10;	//10 show that C didn't change
			WidgetParent->InvalidatePrepass();
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
			WidgetParent->InvalidatePrepass();
			WidgetParent->SlatePrepass(1.f);
			AddErrorIfFalse(WidgetParent->IntAttributeA.Get() == 2, TEXT("A It is not the expected value."));
			AddErrorIfFalse(WidgetParent->IntAttributeB.Get() == 1, TEXT("B It is not the expected value."));
			AddErrorIfFalse(bWasUpdate, TEXT("C should be updated becaude D was."));
			AddErrorIfFalse(WidgetParent->IntAttributeC.Get() == 10, TEXT("C It is not the expected value."));
			AddErrorIfFalse(WidgetParent->IntAttributeD.Get() == 8, TEXT("D It is not the expected value."));
			AddErrorIfFalse(OrderCounter == 2, TEXT("There is no D attribute anymore.")); //-V547
		}

		{
			OrderCounter = 0;
			bWasUpdate = false;
			ReturnValue = 10;
			WidgetParent->InvalidatePrepass();
			WidgetParent->SlatePrepass(1.f);
			AddErrorIfFalse(!bWasUpdate, TEXT("C should not be updated."));
		}
	}

	{
		TSharedRef<UE::Slate::Private::SAttributeLeftWidget_Child> WidgetChild = SNew(UE::Slate::Private::SAttributeLeftWidget_Child);

		AddErrorIfFalse(&WidgetChild->GetWidgetClass() == &UE::Slate::Private::SAttributeLeftWidget_Child::StaticWidgetClass()
			, TEXT("The static data do not matches"));

		FSlateAttributeDescriptor const& AttributeDescriptor = WidgetChild->GetWidgetClass().GetAttributeDescriptor();
		AddErrorIfFalse(AttributeDescriptor.AttributeNum() == 8, TEXT("")); // H is not counted

		const int32 IndexA = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeA");
		const int32 IndexB = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeB");
		const int32 IndexC = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeC");
		const int32 IndexD = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeD");
		const int32 IndexI = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeI");
		const int32 IndexJ = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeJ");
		const int32 IndexK = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeK");
		const int32 IndexL = AttributeDescriptor.IndexOfMemberAttribute("IntAttributeL");

		AddErrorIfFalse(IndexA != INDEX_NONE, TEXT("Could not find the Attribute A"));
		AddErrorIfFalse(IndexB != INDEX_NONE, TEXT("Could not find the Attribute B"));
		AddErrorIfFalse(IndexC != INDEX_NONE, TEXT("Could not find the Attribute C"));
		AddErrorIfFalse(IndexD != INDEX_NONE, TEXT("Could not find the Attribute D"));
		AddErrorIfFalse(IndexI != INDEX_NONE, TEXT("Could not find the Attribute I"));
		AddErrorIfFalse(IndexJ != INDEX_NONE, TEXT("Could not find the Attribute J"));
		AddErrorIfFalse(IndexK != INDEX_NONE, TEXT("Could not find the Attribute K"));
		AddErrorIfFalse(IndexL != INDEX_NONE, TEXT("Could not find the Attribute L"));

		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexA) == AttributeDescriptor.FindAttribute("IntAttributeA"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexB) == AttributeDescriptor.FindAttribute("IntAttributeB"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexC) == AttributeDescriptor.FindAttribute("IntAttributeC"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexD) == AttributeDescriptor.FindAttribute("IntAttributeD"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexI) == AttributeDescriptor.FindAttribute("IntAttributeI"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexJ) == AttributeDescriptor.FindAttribute("IntAttributeJ"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexK) == AttributeDescriptor.FindAttribute("IntAttributeK"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(&AttributeDescriptor.GetAttributeAtIndex(IndexL) == AttributeDescriptor.FindAttribute("IntAttributeL"), TEXT("Index and Attribute should return the same value."));
		AddErrorIfFalse(AttributeDescriptor.FindAttribute("IntAttributeH") == nullptr, TEXT("H exist but is not defined."));

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
			AddErrorIfFalse(WidgetChild->IntAttributeA.Get() == 50, TEXT("A It is not the expected value."));
			WidgetChild->IntAttributeB.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeB.Get() == 51, TEXT("B It is not the expected value."));
			WidgetChild->IntAttributeC.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeC.Get() == 52, TEXT("C It is not the expected value."));
			WidgetChild->IntAttributeD.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeD.Get() == 53, TEXT("D It is not the expected value."));
			WidgetChild->IntAttributeH.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeH.Get() == 54, TEXT("I It is not the expected value."));
			WidgetChild->IntAttributeI.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeI.Get() == 55, TEXT("I It is not the expected value."));
			WidgetChild->IntAttributeJ.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeJ.Get() == 56, TEXT("J It is not the expected value."));
			WidgetChild->IntAttributeK.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeK.Get() == 57, TEXT("K It is not the expected value."));
			WidgetChild->IntAttributeL.Assign(WidgetChild.Get(), MakeAttributeLambda(OrderLambda));
			AddErrorIfFalse(WidgetChild->IntAttributeL.Get() == 58, TEXT("L It is not the expected value."));


			OrderCounter = 0;
			bWasUpdate = false;
			ReturnValue = 4;
			WidgetChild->InvalidatePrepass();
			WidgetChild->SlatePrepass(1.f);
			AddErrorIfFalse(WidgetChild->IntAttributeA.Get() == 2 || WidgetChild->IntAttributeA.Get() == 3, TEXT("A It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeB.Get() == 1, TEXT("B It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeC.Get() == 6, TEXT("C It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeD.Get() == 5, TEXT("D It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeH.Get() == 8, TEXT("H It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeI.Get() == 2 || WidgetChild->IntAttributeI.Get() == 3, TEXT("I It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeJ.Get() == 4, TEXT("J It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeK.Get() == 9, TEXT("K It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeL.Get() == 7, TEXT("L It is not the expected value."));
		}

		{
			OrderCounter = 0;
			bWasUpdate = false;
			ReturnValue = 4;
			WidgetChild->InvalidatePrepass();
			WidgetChild->SlatePrepass(1.f);
			AddErrorIfFalse(WidgetChild->IntAttributeA.Get() == 2 || WidgetChild->IntAttributeA.Get() == 3, TEXT("A It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeB.Get() == 1, TEXT("B It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeC.Get() == 5, TEXT("C It is not the expected value.")); // will get updated because D changes
			AddErrorIfFalse(WidgetChild->IntAttributeD.Get() == 4, TEXT("D It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeH.Get() == 7, TEXT("H It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeI.Get() == 2 || WidgetChild->IntAttributeI.Get() == 3, TEXT("I It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeJ.Get() == 4, TEXT("J It is not the expected value.")); // should not get updated
			AddErrorIfFalse(WidgetChild->IntAttributeK.Get() == 8, TEXT("K It is not the expected value."));
			AddErrorIfFalse(WidgetChild->IntAttributeL.Get() == 6, TEXT("L It is not the expected value."));
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE 

#endif //WITH_AUTOMATION_WORKER
