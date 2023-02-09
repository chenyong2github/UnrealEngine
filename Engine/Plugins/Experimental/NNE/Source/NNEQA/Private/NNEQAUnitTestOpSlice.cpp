// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "NNECoreTensor.h"
#include "NNECoreTypes.h"
#include "NNEQAUnitTestHelper.h"
#include "NNERuntimeRDGHelperSlice.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace UE::NNEQA::Private::NNERuntimeRDG::SliceOp
{

#if WITH_DEV_AUTOMATION_TESTS

	using namespace NNECore::Internal;
	using namespace UE::NNERuntimeRDG::Internal;

	IMPLEMENT_NNE_SHAPEINFERENCEHELPER_UNIT_AUTOMATION_TEST(FSliceCPUHelperConstOuput, "System.Engine.MachineLearning.NNE.UnitTest.SliceHelper.ConstOutput");
	bool FSliceCPUHelperConstOuput::RunTest(const FString& Parameter)
	{
		FTensor XC1 = MakeConstTensor(TEXT("XC1"), { 1 }, { 1.0f });
		FTensor XC20 = MakeConstTensor(TEXT("XC20"), { 20 }, { 3.0f, 4.0f, 3.0f, 4.0f, 3.0f, 3.0f, 4.0f, 3.0f, 4.0f, 3.0f, 3.0f, 4.0f, 3.0f, 4.0f, 3.0f, 3.0f, 4.0f, 3.0f, 4.0f, 3.0f });
		FTensor X1 = MakeTensor(TEXT("X"), { 1 });

		FTensor Y = MakeTensor(TEXT("Y"), { 1 });
		CPUHelper::Slice::Apply(XC1 , Y, { 0 });
		UTEST_TRUE(TEXT("Y const if input is const"), Y.HasPreparedData());

		Y = MakeTensor(TEXT("Y"), { 1 });
		CPUHelper::Slice::Apply(X1, Y, { 0 });
		UTEST_FALSE(TEXT("Y not const if input not const"), Y.HasPreparedData());

		Y = MakeTensor(TEXT("Y"), { 20 });
		CPUHelper::Slice::Apply(XC20, Y, { 0 });
		UTEST_FALSE(TEXT("Y not const if input is too large "), Y.HasPreparedData());

		return true;
	}

	IMPLEMENT_NNE_SHAPEINFERENCEHELPER_UNIT_AUTOMATION_TEST(FSliceCPUHelperRank1, "System.Engine.MachineLearning.NNE.UnitTest.SliceHelper.Rank1");
	bool FSliceCPUHelperRank1::RunTest(const FString& Parameter)
	{
		FTensor XC6 = MakeConstTensor(TEXT("XC6"), { 6 }, { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f });

		FTensor Y = MakeTensor(TEXT("Y"), { 6 });
		CPUHelper::Slice::Apply(XC6, Y, { 0 });
		UTEST_TRUE(TEXT("Y const if input is const"), Y.HasPreparedData());
		UTEST_EQUAL(TEXT("Slice(XC6,6,0)[0]"), Y.GetPreparedData<float>()[0], 1.0f);
		UTEST_EQUAL(TEXT("Slice(XC6,6,0)[1]"), Y.GetPreparedData<float>()[1], 2.0f);
		UTEST_EQUAL(TEXT("Slice(XC6,6,0)[2]"), Y.GetPreparedData<float>()[2], 3.0f);
		UTEST_EQUAL(TEXT("Slice(XC6,6,0)[3]"), Y.GetPreparedData<float>()[3], 4.0f);
		UTEST_EQUAL(TEXT("Slice(XC6,6,0)[4]"), Y.GetPreparedData<float>()[4], 5.0f);
		UTEST_EQUAL(TEXT("Slice(XC6,6,0)[5]"), Y.GetPreparedData<float>()[5], 6.0f);

		Y = MakeTensor(TEXT("Y"), { 2 });
		CPUHelper::Slice::Apply(XC6, Y, { 4 });
		UTEST_TRUE(TEXT("Y const if input is const"), Y.HasPreparedData());
		UTEST_EQUAL(TEXT("Slice(XC6,2,4)[0]"), Y.GetPreparedData<float>()[0], 5.0f);
		UTEST_EQUAL(TEXT("Slice(XC6,2,4)[1]"), Y.GetPreparedData<float>()[1], 6.0f);

		Y = MakeTensor(TEXT("Y"), { 2 });
		CPUHelper::Slice::Apply(XC6, Y, { 1 });
		UTEST_TRUE(TEXT("Y const if input is const"), Y.HasPreparedData());
		UTEST_EQUAL(TEXT("Slice(XC6,2,1)[0]"), Y.GetPreparedData<float>()[0], 2.0f);
		UTEST_EQUAL(TEXT("Slice(XC6,2,1)[1]"), Y.GetPreparedData<float>()[1], 3.0f);

		return true;
	}

	IMPLEMENT_NNE_SHAPEINFERENCEHELPER_UNIT_AUTOMATION_TEST(FSliceCPUHelperRank3, "System.Engine.MachineLearning.NNE.UnitTest.SliceHelper.Rank3");
	bool FSliceCPUHelperRank3::RunTest(const FString& Parameter)
	{
		FTensor XC1x2x3 = MakeConstTensor(TEXT("XC1x2x3"), { 1,2,3 }, { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f });

		FTensor Y = MakeTensor(TEXT("Y"), { 1,2,3 });
		CPUHelper::Slice::Apply(XC1x2x3, Y, { 0,0,0 });
		UTEST_TRUE(TEXT("Y const if input is const"), Y.HasPreparedData());
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x2x3,0-0-0)[0]"), Y.GetPreparedData<float>()[0], 1.0f);
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x2x3,0-0-0)[1]"), Y.GetPreparedData<float>()[1], 2.0f);
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x2x3,0-0-0)[2]"), Y.GetPreparedData<float>()[2], 3.0f);
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x2x3,0-0-0)[3]"), Y.GetPreparedData<float>()[3], 4.0f);
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x2x3,0-0-0)[4]"), Y.GetPreparedData<float>()[4], 5.0f);
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x2x3,0-0-0)[5]"), Y.GetPreparedData<float>()[5], 6.0f);

		Y = MakeTensor(TEXT("Y"), { 1,1,3 });
		CPUHelper::Slice::Apply(XC1x2x3, Y, { 0,1,0 });
		UTEST_TRUE(TEXT("Y const if input is const"), Y.HasPreparedData());
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x1x3,0-1-0)[0]"), Y.GetPreparedData<float>()[0], 4.0f);
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x1x3,0-1-0)[1]"), Y.GetPreparedData<float>()[1], 5.0f);
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x1x3,0-1-0)[2]"), Y.GetPreparedData<float>()[2], 6.0f);

		Y = MakeTensor(TEXT("Y"), { 1,2,2 });
		CPUHelper::Slice::Apply(XC1x2x3, Y, { 0,0,1 });
		UTEST_TRUE(TEXT("Y const if input is const"), Y.HasPreparedData());
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x2x2,0-0-1)[0]"), Y.GetPreparedData<float>()[0], 2.0f);
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x2x2,0-0-1)[1]"), Y.GetPreparedData<float>()[1], 3.0f);
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x2x2,0-0-1)[2]"), Y.GetPreparedData<float>()[2], 5.0f);
		UTEST_EQUAL(TEXT("Slice(XC1x2x3,1x2x2,0-0-1)[3]"), Y.GetPreparedData<float>()[3], 6.0f);

		return true;
	}

#endif //WITH_DEV_AUTOMATION_TESTS

} // namespace UE::NNEQA::Private::NNERuntimeRDG::SliceOp
