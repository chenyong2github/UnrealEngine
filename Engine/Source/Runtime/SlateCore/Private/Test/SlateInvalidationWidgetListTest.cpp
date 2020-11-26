// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "FastUpdate/SlateInvalidationWidgetList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"


#if WITH_AUTOMATION_WORKER & WITH_SLATE_DEBUGGING

#define LOCTEXT_NAMESPACE "Slate.FastPath.InvalidationWidgetList"

//IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlateInvalidationWidgetListTest, "Slate.FastPath.InvalidationWidgetList.AddBuildRemove", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlateInvalidationWidgetListTest, "Slate.FastPath.InvalidationWidgetList.AddBuildRemove", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

namespace UE
{
namespace Slate
{
namespace Private
{

class SEmptyLeftWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SEmptyLeftWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs){}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D{ 100, 100 }; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		return LayerId;
	}
};

TSharedRef<SVerticalBox> AddVerticalBox(TSharedPtr<SVerticalBox> VerticalBox, TCHAR Letter)
{
	FName NewName = *FString::Printf(TEXT("TagVerticalBox-%c"), Letter);
	TSharedRef<SVerticalBox> Result = SNew(SVerticalBox).Tag(NewName);
	VerticalBox->AddSlot()[Result];
	return Result;
}

TSharedRef<SWidget> AddEmptyWidget(TSharedPtr<SVerticalBox> VerticalBox, int32 Number)
{
	static FName NewName = TEXT("TagEmptyLeftWidget");
	NewName.SetNumber(Number);
	TSharedRef<SWidget> Result = SNew(SEmptyLeftWidget).Tag(NewName);
	VerticalBox->AddSlot()[Result];
	return Result;
}

TSharedRef<SVerticalBox> BuildTestUI_ChildOrder(TSharedPtr<SVerticalBox>& WidgetC, TSharedPtr<SVerticalBox>& WidgetF)
{
	// A
	//  B (1, 2, 3)
	//  C (4, 5, 6, 7)
	//  Null
	//  D
	//  E (8, 9, 10)
	//  F
	//   G (11, Null, 12)
	//   H (13)
	//   I
	//  J (14)

	TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);
	{
		TSharedRef<SVerticalBox> Sub = AddVerticalBox(Root, TEXT('B'));
		AddEmptyWidget(Sub, 1);
		AddEmptyWidget(Sub, 2);
		AddEmptyWidget(Sub, 3);
	}
	{
		WidgetC = AddVerticalBox(Root, TEXT('C'));
		AddEmptyWidget(WidgetC, 4);
		AddEmptyWidget(WidgetC, 5);
		AddEmptyWidget(WidgetC, 6);
		AddEmptyWidget(WidgetC, 7);
	}
	Root->AddSlot()[SNullWidget::NullWidget];
	AddVerticalBox(Root, TEXT('D'));
	{
		TSharedRef<SVerticalBox> Sub = AddVerticalBox(Root, TEXT('E'));
		AddEmptyWidget(Sub, 8);
		AddEmptyWidget(Sub, 9);
		AddEmptyWidget(Sub, 10);
	}
	{
		WidgetF = AddVerticalBox(Root, TEXT('F'));
		{
			TSharedRef<SVerticalBox> SubSub = AddVerticalBox(WidgetF, TEXT('G'));
			AddEmptyWidget(SubSub, 11);
			SubSub->AddSlot()[SNullWidget::NullWidget];
			AddEmptyWidget(SubSub, 12);
		}
		{
			TSharedRef<SVerticalBox> SubSub = AddVerticalBox(WidgetF, TEXT('H'));
			AddEmptyWidget(SubSub, 13);
		}
		{
			AddVerticalBox(WidgetF, TEXT('I'));
		}
	}
	{
		TSharedRef<SVerticalBox> SubSub = AddVerticalBox(Root, TEXT('J'));
		AddEmptyWidget(SubSub, 14);
	}

	return Root;
}

TSharedRef<SVerticalBox> BuildTestUI_Child(TSharedPtr<SVerticalBox>& WidgetA, TArray<TSharedPtr<SWidget>>& ChildOfWidgetA
	, TSharedPtr<SVerticalBox>& WidgetB, TArray<TSharedPtr<SWidget>>& ChildOfWidgetB
	, TSharedPtr<SVerticalBox>& WidgetC, TArray<TSharedPtr<SWidget>>& ChildOfWidgetC
	, TSharedPtr<SVerticalBox>& WidgetD, TArray<TSharedPtr<SWidget>>& ChildOfWidgetD
	, TSharedPtr<SVerticalBox>& WidgetH, TArray<TSharedPtr<SWidget>>& ChildOfWidgetH)
{
	// A
	//  B (1, 2, 3)
	//  C
	//  D
	//   E (4, 5)
	//   F (6)
	//   G (7)
	//  H (8)
	//  I

	WidgetA = SNew(SVerticalBox);
	{
		WidgetB = AddVerticalBox(WidgetA, TEXT('B'));
		ChildOfWidgetA.Add(WidgetB);
		ChildOfWidgetB.Add(AddEmptyWidget(WidgetB, 1));
		ChildOfWidgetB.Add(AddEmptyWidget(WidgetB, 2));
		ChildOfWidgetB.Add(AddEmptyWidget(WidgetB, 3));
	}
	{
		WidgetC = AddVerticalBox(WidgetA, TEXT('C'));
		ChildOfWidgetA.Add(WidgetC);
	}
	{
		WidgetD = AddVerticalBox(WidgetA, TEXT('D'));
		ChildOfWidgetA.Add(WidgetD);
		{
			TSharedRef<SVerticalBox> SubSub = AddVerticalBox(WidgetD, TEXT('E'));
			ChildOfWidgetD.Add(SubSub);
			AddEmptyWidget(SubSub, 4);
			AddEmptyWidget(SubSub, 5);
		}
		{
			TSharedRef<SVerticalBox> SubSub = AddVerticalBox(WidgetD, TEXT('F'));
			ChildOfWidgetD.Add(SubSub);
			AddEmptyWidget(SubSub, 6);
		}
		{
			TSharedRef<SVerticalBox> SubSub = AddVerticalBox(WidgetD, TEXT('G'));
			ChildOfWidgetD.Add(SubSub);
			AddEmptyWidget(SubSub, 7);
		}
	}
	{
		WidgetH = AddVerticalBox(WidgetA, TEXT('H'));
		ChildOfWidgetA.Add(WidgetH);
		ChildOfWidgetH.Add(AddEmptyWidget(WidgetH, 8));
	}
	ChildOfWidgetA.Add(AddVerticalBox(WidgetA, TEXT('I')));
	return WidgetA.ToSharedRef();
}
}
}
}


bool FSlateInvalidationWidgetListTest::RunTest(const FString& Parameters)
{
	FSlateInvalidationWidgetList::FArguments ArgsToTest[] = {{4,2}, {4,3}, {5,1}, {6, 1}};
	for (FSlateInvalidationWidgetList::FArguments Args : ArgsToTest)
	{
		{
			TSharedPtr<SVerticalBox> WidgetA, WidgetB, WidgetC, WidgetD, WidgetH;
			TArray<TSharedPtr<SWidget>> ChildOfWidgetA, ChildOfWidgetB, ChildOfWidgetC, ChildOfWidgetD, ChildOfWidgetH;
			TSharedRef<SVerticalBox> RootChild = UE::Slate::Private::BuildTestUI_Child(WidgetA, ChildOfWidgetA, WidgetB, ChildOfWidgetB, WidgetC, ChildOfWidgetC, WidgetD, ChildOfWidgetD, WidgetH, ChildOfWidgetH);

			FSlateInvalidationWidgetList List = { FSlateInvalidationRootHandle(), Args };
			List.BuildWidgetList(RootChild);
			AddErrorIfFalse(List.VerifyWidgetsIndex(), TEXT("The widget list integrity has failed."));

			{
				TArray<TSharedPtr<SWidget>> FoundChildren = List.FindChildren(WidgetA.ToSharedRef());
				if (ChildOfWidgetA != FoundChildren)
				{
					AddError(TEXT("Was not able to find the child of VerticalBox A."));
				}
			}
			{
				TArray<TSharedPtr<SWidget>> FoundChildren = List.FindChildren(WidgetB.ToSharedRef());
				if (ChildOfWidgetB != FoundChildren)
				{
					AddError(TEXT("Was not able to find the child of VerticalBox B."));
				}
			}
			{
				TArray<TSharedPtr<SWidget>> FoundChildren = List.FindChildren(WidgetC.ToSharedRef());
				if (ChildOfWidgetC != FoundChildren)
				{
					AddError(TEXT("Was not able to find the child of VerticalBox C."));
				}
			}
			{
				TArray<TSharedPtr<SWidget>> FoundChildren = List.FindChildren(WidgetD.ToSharedRef());
				if (ChildOfWidgetD != FoundChildren)
				{
					AddError(TEXT("Was not able to find the child of VerticalBox D."));
				}
			}
			{
				TArray<TSharedPtr<SWidget>> FoundChildren = List.FindChildren(WidgetH.ToSharedRef());
				if (ChildOfWidgetH != FoundChildren)
				{
					AddError(TEXT("Was not able to find the child of VerticalBox H."));
				}
			}
		}

		{
			TSharedPtr<SVerticalBox> WidgetC, WidgetF;
			TSharedRef<SVerticalBox> RootChildOrder = UE::Slate::Private::BuildTestUI_ChildOrder(WidgetC, WidgetF);

			FSlateInvalidationWidgetList List = { FSlateInvalidationRootHandle(), Args };
			List.BuildWidgetList(RootChildOrder);
			AddErrorIfFalse(List.VerifyWidgetsIndex(), TEXT("The widget list integrity has failed."));

			// Remove Second child of F
			{
				List.RemoveWidget(WidgetF->GetAllChildren()->GetChildAt(1));
				WidgetF->RemoveSlot(WidgetF->GetAllChildren()->GetChildAt(1));
				{
					FSlateInvalidationWidgetList TempList = { FSlateInvalidationRootHandle(), Args };
					TempList.BuildWidgetList(RootChildOrder);
					if (!TempList.DeapCompare(List))
					{
						AddError(TEXT("Was not able to remove a child of F."));
					}
				}
			}

			// Remove C and F
			{
				{
					const FSlateInvalidationWidgetIndex WidgetIndexF = List.FindWidget(WidgetF.ToSharedRef());
					if (!List.IsValidIndex(WidgetIndexF) || List[WidgetIndexF].GetWidget() != WidgetF.Get())
					{
						AddError(TEXT("The index of F is not valid anymore."));
					}
					List.RemoveWidget(WidgetIndexF);
				}
				{
					const FSlateInvalidationWidgetIndex WidgetIndexC = List.FindWidget(WidgetC.ToSharedRef());
					if (!List.IsValidIndex(WidgetIndexC) || List[WidgetIndexC].GetWidget() != WidgetC.Get())
					{
						AddError(TEXT("The index of C is not valid anymore."));
					}
					List.RemoveWidget(WidgetIndexC);
				}
				RootChildOrder->RemoveSlot(WidgetF.ToSharedRef());
				RootChildOrder->RemoveSlot(WidgetC.ToSharedRef());
				{
					FSlateInvalidationWidgetList TempList = { FSlateInvalidationRootHandle(), Args };
					TempList.BuildWidgetList(RootChildOrder);
					if (!TempList.DeapCompare(List))
					{
						AddError(TEXT("Was not able to remove F and C."));
					}
				}
			}
			// remove last item
			{
				int32 ToRemoveIndex = RootChildOrder->GetAllChildren()->Num() - 1;
				TSharedRef<SWidget> RemovedWidget = RootChildOrder->GetAllChildren()->GetChildAt(ToRemoveIndex);
				RootChildOrder->RemoveSlot(RemovedWidget);
				List.RemoveWidget(List.FindWidget(RemovedWidget));

				{
					FSlateInvalidationWidgetList TempList = { FSlateInvalidationRootHandle(), Args };
					TempList.BuildWidgetList(RootChildOrder);
					if (!TempList.DeapCompare(List))
					{
						AddError(TEXT("Was not able to remove the last item of A."));
					}
				}
			}
		}
		{
			TSharedPtr<SVerticalBox> WidgetC, WidgetF;
			TSharedRef<SVerticalBox> RootChildOrder = UE::Slate::Private::BuildTestUI_ChildOrder(WidgetC, WidgetF);

			// Child invalidation
			FSlateInvalidationWidgetList List = { FSlateInvalidationRootHandle(), Args };
			List.BuildWidgetList(RootChildOrder);
			AddErrorIfFalse(List.VerifyWidgetsIndex(), TEXT("The widget list integrity has failed."));

			WidgetF->RemoveSlot(WidgetF->GetAllChildren()->GetChildAt(0));
			WidgetC->RemoveSlot(WidgetC->GetAllChildren()->GetChildAt(1));

			TArray<TWeakPtr<SWidget>> InvalidationWidgetIndexes;
			InvalidationWidgetIndexes.Add(WidgetF->AsShared());
			InvalidationWidgetIndexes.Add(WidgetC->AsShared());
			InvalidationWidgetIndexes.Add(WidgetC->AsShared());
			List.ProcessChildOrderInvalidation(InvalidationWidgetIndexes);
			AddErrorIfFalse(List.VerifyWidgetsIndex(), TEXT("The widget list integrity has failed."));

			{
				FSlateInvalidationWidgetList TempList = { FSlateInvalidationRootHandle(), Args };
				TempList.BuildWidgetList(RootChildOrder);
				if (!TempList.DeapCompare(List))
				{
					AddError(TEXT("Was not able to process invalidation C and F."));
				}
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE 

#endif //WITH_AUTOMATION_WORKER & WITH_SLATE_DEBUGGING
