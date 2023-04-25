// Copyright Epic Games, Inc. All Rights Reserved.


#include "AnimNextTest.h"
#include "CoreMinimal.h"
#include "Interface/AnimNextInterface.h"
#include "Param/Param.h"
#include "Interface/InterfaceContext.h"
#include "Param/ParamStorage.h"
#include "Misc/AutomationTest.h"
#include "DataRegistry.h"
#include "ReferencePose.h"

#include <chrono>

namespace 
{

class FHighResTimer
{
public:
	using FTimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

	static inline FTimePoint GetTimeMark()
	{
		return std::chrono::high_resolution_clock::now();
	}

	static inline int64 GetTimeDiffMicroSec(const FTimePoint& RefTime)
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(GetTimeMark() - RefTime).count();
	}
	static inline int64 GetTimeDiffNanoSec(const FTimePoint& RefTime)
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(GetTimeMark() - RefTime).count();
	}
};

} // end namespace

//----------------------------------------------------------------------------

/*static*/ FName UAnimNextInterface_TestData_Multiply::NameParamA(TEXT("TestDataA"));
/*static*/ FName UAnimNextInterface_TestData_Multiply::NameParamB(TEXT("TestDataB"));
/*static*/ FName UAnimNextInterface_TestData_Multiply::NameParamResult(TEXT("NameParamAxB"));


bool UAnimNextInterfaceTestDataLiteral::GetDataImpl(const UE::AnimNext::FContext& Context) const
{
	Context.SetResult(Value);

	return true;
}

bool UAnimNextInterface_TestData_Multiply::GetDataImpl(const UE::AnimNext::FContext& Context) const
{
	using namespace UE::AnimNext;

	bool bResult = false;

	const FTestData& ParamA = Context.GetParameterChecked<const FTestData>(NameParamA);
	const FTestData& ParamB = Context.GetParameterChecked<const FTestData>(NameParamB);
	FTestData& Result = Context.GetResult<FTestData>();

	bResult = true;

	Result.A = ParamA.A * ParamB.A;
	Result.B = ParamA.B * ParamB.B;

	return bResult;
}

//----------------------------------------------------------------------------

/*static*/ FName UAnimNextInterface_TestData_Split::InputName_AB(TEXT("Input_AB"));
/*static*/ FName UAnimNextInterface_TestData_Split::OutputName_A(TEXT("Output_A"));
/*static*/ FName UAnimNextInterface_TestData_Split::OutputName_B(TEXT("Output_B"));

bool UAnimNextInterface_TestData_Split::GetDataImpl(const UE::AnimNext::FContext& Context) const
{
	using namespace UE::AnimNext;

	bool bResult = false;

	const FTestData& Input_AB = Context.GetParameterChecked<const FTestData>(InputName_AB);
	float& Output_A = Context.GetParameterChecked<float>(OutputName_A);
	float& Output_B = Context.GetParameterChecked<float>(OutputName_B);

	bResult = true;

	Output_A = Input_AB.A;
	Output_B = Input_AB.B;

	return bResult;
}


//****************************************************************************
// AnimNext Interface Test
//****************************************************************************
#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextInterfaceTest_SingleElement, "Animation.AnimNext.Interface.Single", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextInterfaceTest_SingleElement::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	UAnimNextInterface_TestData_Multiply* AnimNextInterface_TestData_Multiply = Cast<UAnimNextInterface_TestData_Multiply>(UAnimNextInterface_TestData_Multiply::StaticClass()->GetDefaultObject());
	TScriptInterface<IAnimNextInterface> TestDataMultiply = AnimNextInterface_TestData_Multiply;

	AddErrorIfFalse(TestDataMultiply.GetInterface() != nullptr, "UAnimNextInterface_TestData_Multiply -> Interface is Null.");

	constexpr FTestData TestDataA = { 1.f, 1.f };
	constexpr FTestData TestDataB = { 1.f, 2.f };
	FTestData TestDataResult = { 0.f, 0.f };

	FState State;
	FParamStorage ParamStorage(16, 256, 2);
	FContext Context(0.f, State, ParamStorage);

	const FContext TestContext = Context.WithParameters({
		TPairInitializer(UAnimNextInterface_TestData_Multiply::NameParamA, TWrapParam<const FTestData>(TestDataA)),
		TPairInitializer(UAnimNextInterface_TestData_Multiply::NameParamB, TWrapParam<const FTestData>(TestDataB))
		});

	Interface::GetDataSafe(TestDataMultiply, TestContext, TestDataResult);

	// --- Check the results from the batched operation ---
	AddErrorIfFalse(TestDataResult.A == TestDataA.A * TestDataB.A, FString::Printf(TEXT("TestDataResult.A is : [%.04f] Expected : [%.04f]"), TestDataResult.A, TestDataA.A * TestDataB.A));
	AddErrorIfFalse(TestDataResult.B == TestDataA.B * TestDataB.B, FString::Printf(TEXT("TestDataResult.B is : [%.04f] Expected : [%.04f]"), TestDataResult.B, TestDataA.B * TestDataB.B));

	// Final GC to make sure everything is cleaned up
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextInterfaceTest_Chained, "Animation.AnimNext.Interface.Chained", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextInterfaceTest_Chained::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	FState RootState;
	FParamStorage ParamStorage(16, 2048, 8);
	const FContext RootContext(0.f, RootState, ParamStorage);
	
	// Simulate we pass a const context and have to return data
	TParamValue<FTestData> RootContextResultParam;
	const FContext InContext = RootContext.WithResult(RootContextResultParam);

	FTestData LiteralResult;
	TParamValue<FTestData> ParamLiteralResult;
	//TContextStorageParam<FTestData> ParamLiteralResult;

	// Retrieve the literal value
	{
		TScriptInterface<IAnimNextInterface> TestDataLiteral = Cast<UAnimNextInterfaceTestDataLiteral>(UAnimNextInterfaceTestDataLiteral::StaticClass()->GetDefaultObject());
		AddErrorIfFalse(TestDataLiteral.GetInterface() != nullptr, "UAnimNextInterfaceTestDataLiteral -> Interface is Null.");

		const FContext LiteralContext = InContext.WithResult(ParamLiteralResult);

		Interface::GetDataSafe(TestDataLiteral, LiteralContext);

		AddErrorIfFalse(ParamLiteralResult->A == 1, FString::Printf(TEXT("LiteralResult.A is : [%.04f] Expected : [%.04f]"), ParamLiteralResult->A, 1));
		AddErrorIfFalse(ParamLiteralResult->B == 1, FString::Printf(TEXT("LiteralResult.B is : [%.04f] Expected : [%.04f]"), ParamLiteralResult->B, 1));
	}

	const FTestData TestDataB = { 2.f, 2.f };

	// now chain an operation using the literal result as a parameter
	{
		UAnimNextInterface_TestData_Multiply* AnimNextInterface_TestData_Multiply = Cast<UAnimNextInterface_TestData_Multiply>(UAnimNextInterface_TestData_Multiply::StaticClass()->GetDefaultObject());
		TScriptInterface<IAnimNextInterface> TestDataMultiply = AnimNextInterface_TestData_Multiply;
		AddErrorIfFalse(TestDataMultiply.GetInterface() != nullptr, "UAnimNextInterface_TestData_Multiply -> Interface is Null.");

		const FContext ChainContext = InContext.WithResultAndParameters(InContext.GetResultParam(), {
			TPairInitializer(UAnimNextInterface_TestData_Multiply::NameParamA, ParamLiteralResult),						// chain the result of the literal call as the input
			TPairInitializer(UAnimNextInterface_TestData_Multiply::NameParamB, TWrapParam<const FTestData>(TestDataB))	// set the second operand
			});

		Interface::GetDataSafe(TestDataMultiply, ChainContext);
	}

	// --- Check the results from the chained operation on the root component ---
	AddErrorIfFalse(RootContextResultParam->A == ParamLiteralResult->A * TestDataB.A, FString::Printf(TEXT("TestDataResult.A is : [%.04f] Expected : [%.04f]"), RootContextResultParam->A, ParamLiteralResult->A * TestDataB.A));
	AddErrorIfFalse(RootContextResultParam->B == ParamLiteralResult->B * TestDataB.B, FString::Printf(TEXT("TestDataResult.B is : [%.04f] Expected : [%.04f]"), RootContextResultParam->B, ParamLiteralResult->B * TestDataB.B));

	// now chain an operation using the literal result as a parameter but copying the input parameters on the Context Parameter storage
	{
		UAnimNextInterface_TestData_Multiply* AnimNextInterface_TestData_Multiply = Cast<UAnimNextInterface_TestData_Multiply>(UAnimNextInterface_TestData_Multiply::StaticClass()->GetDefaultObject());
		TScriptInterface<IAnimNextInterface> TestDataMultiply = AnimNextInterface_TestData_Multiply;
		AddErrorIfFalse(TestDataMultiply.GetInterface() != nullptr, "UAnimNextInterface_TestData_Multiply -> Interface is Null.");

		const FContext ChainContext = InContext.WithResultAndParameters(InContext.GetResultParam(), {
			TPairInitializer(UAnimNextInterface_TestData_Multiply::NameParamA, TContextStorageParam<const FTestData>(ParamLiteralResult)),	// chain the result of the literal call as the input
			TPairInitializer(UAnimNextInterface_TestData_Multiply::NameParamB, TContextStorageParam<const FTestData>(TestDataB))			// set the second operand
			});

		Interface::GetDataSafe(TestDataMultiply, ChainContext);
	}

	// --- Check the results from the chained operation on the root component ---
	AddErrorIfFalse(RootContextResultParam->A == ParamLiteralResult->A * TestDataB.A, FString::Printf(TEXT("TestDataResult.A is : [%.04f] Expected : [%.04f]"), RootContextResultParam->A, ParamLiteralResult->A * TestDataB.A));
	AddErrorIfFalse(RootContextResultParam->B == ParamLiteralResult->B * TestDataB.B, FString::Printf(TEXT("TestDataResult.B is : [%.04f] Expected : [%.04f]"), RootContextResultParam->B, ParamLiteralResult->B * TestDataB.B));

	// Final GC to make sure everything is cleaned up
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextInterfaceTest_AltParamStorage, "Animation.AnimNext.Interface.AltParamStorage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextInterfaceTest_AltParamStorage::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	FState RootState;
	FParamStorage ParamStorage(16, 2048, 8);
	const FContext RootContext(0.f, RootState, ParamStorage);

	UAnimNextInterface_TestData_Multiply* AnimNextInterface_TestData_Multiply = Cast<UAnimNextInterface_TestData_Multiply>(UAnimNextInterface_TestData_Multiply::StaticClass()->GetDefaultObject());
	TScriptInterface<IAnimNextInterface> TestDataMultiply = AnimNextInterface_TestData_Multiply;

	AddErrorIfFalse(TestDataMultiply.GetInterface() != nullptr, "UAnimNextInterface_TestData_Multiply -> Interface is Null.");

	constexpr FTestData TestDataA = { 1.f, 1.f };
	constexpr FTestData TestDataB = { 1.f, 2.f };
	FTestData TestDataResult = { 0.f, 0.f };

	FContext TestContext = RootContext.CreateSubContext();

	TestContext.AddValue(FContext::EParamType::Input, UAnimNextInterface_TestData_Multiply::NameParamA, TestDataA);
	TestContext.AddValue(FContext::EParamType::Input, UAnimNextInterface_TestData_Multiply::NameParamB, TestDataB);
	FParamHandle HOutput = TestContext.AddReference(FContext::EParamType::Output, UAnimNextInterface_TestData_Multiply::NameParamResult, TestDataResult);

	// for compatibility reasons, as the current code expects to have a return value set
	TestContext.SetHParamAsResult(HOutput);  

	const bool bOk = TestDataMultiply->GetData(TestContext);

	AddErrorIfFalse(bOk, "TestDataMultiply -> GetData returned FAILURE.");

	// --- Check the results from the operation ---
	AddErrorIfFalse(TestDataResult.A == TestDataA.A * TestDataB.A, FString::Printf(TEXT("TestDataResult.A is : [%.04f] Expected : [%.04f]"), TestDataResult.A, TestDataA.A * TestDataB.A));
	AddErrorIfFalse(TestDataResult.B == TestDataA.B * TestDataB.B, FString::Printf(TEXT("TestDataResult.B is : [%.04f] Expected : [%.04f]"), TestDataResult.B, TestDataA.B * TestDataB.B));

	// Final GC to make sure everything is cleaned up
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextInterfaceTest_AltParamStorageMultipleOutputs, "Animation.AnimNext.Interface.AltParamStorageMultipleOutputs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextInterfaceTest_AltParamStorageMultipleOutputs::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	FState RootState;
	FParamStorage ParamStorage(16, 2048, 8);
	const FContext RootContext(0.f, RootState, ParamStorage);

	TScriptInterface<IAnimNextInterface> TestDataSplit(Cast<UAnimNextInterface_TestData_Split>(UAnimNextInterface_TestData_Split::StaticClass()->GetDefaultObject()));

	AddErrorIfFalse(TestDataSplit.GetInterface() != nullptr, "UAnimNextInterface_TestData_Split -> Interface is Null.");

	constexpr FTestData Input_AB = { 1.f, 2.f };
	float Output_A = 0.f;
	float Output_B = 0.f;

	FContext TestContext = RootContext.CreateSubContext();

	// Adding an input as a value (will be copied and the copy will become mutable)
	FParamHandle RetValCompatiblility = TestContext.AddValue(FContext::EParamType::Input, UAnimNextInterface_TestData_Split::InputName_AB, Input_AB);
	// Adding first output as a reference
	TestContext.AddReference(FContext::EParamType::Output, UAnimNextInterface_TestData_Split::OutputName_A, Output_A);
	// Adding second output as a reference
	TestContext.AddReference(FContext::EParamType::Output, UAnimNextInterface_TestData_Split::OutputName_B, Output_B);

	// Not used, but added for compatibility reasons, as the current code expects to have a return value set of the interface type
	TestContext.SetHParamAsResult(RetValCompatiblility);

	const bool bOk = TestDataSplit->GetData(TestContext);

	AddErrorIfFalse(bOk, "TestDataSplit -> GetData returned FAILURE.");

	// --- Check the results from the operation ---
	AddErrorIfFalse(Output_A == Input_AB.A, FString::Printf(TEXT("Output_A is : [%.04f] Expected : [%.04f]"), Output_A, Input_AB.A));
	AddErrorIfFalse(Output_B == Input_AB.B, FString::Printf(TEXT("Output_B is : [%.04f] Expected : [%.04f]"), Output_B, Input_AB.B));

	// Final GC to make sure everything is cleaned up
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextInterfaceTest_AltParamStorageChained, "Animation.AnimNext.Interface.AltParamStorageChained", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextInterfaceTest_AltParamStorageChained::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	FState RootState;
	FParamStorage ParamStorage(16, 2048, 8);
	const FContext RootContext(0.f, RootState, ParamStorage);

	UAnimNextInterface_TestData_Multiply* AnimNextInterface_TestData_Multiply = Cast<UAnimNextInterface_TestData_Multiply>(UAnimNextInterface_TestData_Multiply::StaticClass()->GetDefaultObject());
	TScriptInterface<IAnimNextInterface> TestDataMultiply = AnimNextInterface_TestData_Multiply;

	AddErrorIfFalse(TestDataMultiply.GetInterface() != nullptr, "UAnimNextInterface_TestData_Multiply -> Interface is Null.");

	// --- Initial Test : Multiply two TestData and store the value at Result ---

	FParamHandle TestOutput_AxB_Param;

	{
		constexpr FTestData TestDataA = { 1.f, 1.f };
		constexpr FTestData TestDataB = { 1.f, 2.f };

		FContext TestContext = RootContext.CreateSubContext();

		// Adding a const value. Note that adding a value will convert const into mutable, as the copy will not be const anymore
		TestContext.AddValue(FContext::EParamType::Input, UAnimNextInterface_TestData_Multiply::NameParamA, TestDataA);
		// Adding a reference to const. This param will keep the const
		TestContext.AddReference(FContext::EParamType::Input, UAnimNextInterface_TestData_Multiply::NameParamB, TestDataB);
		// Adding a value using a temp taht will be copied into the value
		TestOutput_AxB_Param = TestContext.AddValue(FContext::EParamType::Output, UAnimNextInterface_TestData_Multiply::NameParamResult, FTestData{ 0.f, 0.f });

		// for compatibility reasons, as the current code expects to have a return value set
		TestContext.SetHParamAsResult(TestOutput_AxB_Param);  

		const bool bOk = TestDataMultiply->GetData(TestContext);

		AddErrorIfFalse(bOk, "TestDataMultiply -> GetData returned FAILURE.");

		// --- Check the results from the operation ---
		const FTestData& TestDataResult = TestContext.GetParameterAs<const FTestData>(TestOutput_AxB_Param);
		AddErrorIfFalse(TestDataResult.A == TestDataA.A * TestDataB.A, FString::Printf(TEXT("TestDataResult.A is : [%.04f] Expected : [%.04f]"), TestDataResult.A, TestDataA.A * TestDataB.A));
		AddErrorIfFalse(TestDataResult.B == TestDataA.B * TestDataB.B, FString::Printf(TEXT("TestDataResult.B is : [%.04f] Expected : [%.04f]"), TestDataResult.B, TestDataA.B * TestDataB.B));
	}

	// --- Chained Test : Use the previous call Result as an input ---

	{
		FContext TestContext = RootContext.CreateSubContext();

		constexpr FTestData TestDataC = { 2.f, 2.f };

		// Add the result of the previous call as an input for this operation
		TestContext.AddValue(FContext::EParamType::Input, UAnimNextInterface_TestData_Multiply::NameParamA, TestOutput_AxB_Param);
		// Add a reference to const data
		TestContext.AddReference(FContext::EParamType::Input, UAnimNextInterface_TestData_Multiply::NameParamB, TestDataC);
		// Add a value
		const FParamHandle ChainedResultParam = TestContext.AddValue(FContext::EParamType::Output, UAnimNextInterface_TestData_Multiply::NameParamResult, FTestData{ 0.f, 0.f });

		// for compatibility reasons, as the current code expects to have a return value set
		TestContext.SetHParamAsResult(ChainedResultParam);

		const bool bOk = TestDataMultiply->GetData(TestContext);

		AddErrorIfFalse(bOk, "TestDataMultiply -> GetData returned FAILURE.");

		// --- Check the results from the operation ---
		const FTestData& TestOutput_AxB = TestContext.GetParameterAs<const FTestData>(TestOutput_AxB_Param);
		const FTestData& ChainedResult = TestContext.GetParameterAs<const FTestData>(ChainedResultParam);
		
		AddErrorIfFalse(ChainedResult.A == TestOutput_AxB.A * TestDataC.A, FString::Printf(TEXT("Chained TestDataResult.A is : [%.04f] Expected : [%.04f]"), ChainedResult.A, TestOutput_AxB.A * TestDataC.A));
		AddErrorIfFalse(ChainedResult.B == TestOutput_AxB.B * TestDataC.B, FString::Printf(TEXT("Chained TestDataResult.B is : [%.04f] Expected : [%.04f]"), ChainedResult.B, TestOutput_AxB.B * TestDataC.B));
	}


	// Final GC to make sure everything is cleaned up
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationAnimNextInterfaceTest_AnimationDataRegistry, "Animation.AnimNext.AnimationDataRegistry.TransformsTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationAnimNextInterfaceTest_AnimationDataRegistry::RunTest(const FString& InParameters)
{
	using namespace UE::AnimNext;

	FDataRegistry* AnimDataReg = FDataRegistry::Get();


	//constexpr int32 BASIC_TYPE_ALLOC_BLOCK = 1000;
	//AnimDataReg.RegisterDataType<uint8>(EAnimationDataType::RawMemory, BASIC_TYPE_ALLOC_BLOCK);
	//AnimDataReg.RegisterDataType<FVector>(EAnimationDataType::Translation, BASIC_TYPE_ALLOC_BLOCK);
	//AnimDataReg.RegisterDataType<FVector>(EAnimationDataType::Scale, BASIC_TYPE_ALLOC_BLOCK);
	//AnimDataReg.RegisterDataType<FQuat>(EAnimationDataType::Rotation, BASIC_TYPE_ALLOC_BLOCK);
	//AnimDataReg.RegisterDataType<FTransform>(EAnimationDataType::Transform, BASIC_TYPE_ALLOC_BLOCK);
	//AnimDataReg.RegisterDataType<FTransform>(EAnimationDataType::Displacement, BASIC_TYPE_ALLOC_BLOCK);

	//AnimDataReg.RegisterDataType<FAnimTransformArray>(EAnimationDataType::TransformArray, BASIC_TYPE_ALLOC_BLOCK);
	//AnimDataReg.RegisterDataType<FAnimTransformArraySoA>(EAnimationDataType::TransformArraySoA, BASIC_TYPE_ALLOC_BLOCK);

	constexpr int32 NumElements = 1;
	constexpr int32 NumPasses = 100;
	constexpr int32 NumTransforms = 100;

	TArray<int64> TimeDiffAoS;
	TArray<int64> TimeDiffSoA;
	TimeDiffAoS.SetNumZeroed(NumPasses);
	TimeDiffSoA.SetNumZeroed(NumPasses);

	const double BlendFactor = 0.5f;
	const FVector Half = FVector(0.5, 0.5, 0.5);
	const FQuat Rot90 = FQuat::MakeFromEuler(FVector(90.0, 0.0, 0.0));

	const FTransform TargetBlendValue(Rot90, FVector::OneVector, Half);
	FTransform ReferenceResult;
	ReferenceResult.Blend(FTransform::Identity, TargetBlendValue, BlendFactor);

	// --- AoS Test ---

	FTransformArrayAoSHeap ResultAoS(NumTransforms);
	for (int i = 0; i < NumPasses; ++i)
	{
		// *** TransformArrayAoS Test *** 
		{
			FTransformArrayAoSHeap TransformsA(NumTransforms);
			FTransformArrayAoSHeap TransformsB(NumTransforms);

			for (auto& Transform : TransformsB.GetTransforms())
			{
				Transform.SetTranslation(FVector::OneVector);
				Transform.SetRotation(Rot90);
				Transform.SetScale3D(Half);
			}

			FHighResTimer::FTimePoint StartTime = FHighResTimer::GetTimeMark();

			ResultAoS.Blend(TransformsA, TransformsB, BlendFactor);

			TimeDiffAoS[i] = FHighResTimer::GetTimeDiffNanoSec(StartTime);
		}
	}
	for (auto& Transform : ResultAoS.Transforms)
	{
		AddErrorIfFalse(Transform.GetTranslation() == ReferenceResult.GetTranslation(), "AoS Translation Blend error.");
		AddErrorIfFalse(Transform.GetRotation() == ReferenceResult.GetRotation(), "AoS Rotation Blend error.");
		AddErrorIfFalse(Transform.GetScale3D() == ReferenceResult.GetScale3D(), "AoS Scale Blend error.");
	}

	// --- SoA Test --- 
	FTransformArraySoAHeap ResultSoA(NumTransforms);
	for (int i = 0; i < NumPasses; ++i)
	{
		//FAnimTransformArray* TransoformArray = AnimDataReg->AllocateData<FAnimTransformArray>(NumElements, NumTransforms);

		// *** TransformArraySoA Test *** 
		{
			FTransformArraySoAHeap TransformsA(NumTransforms);
			FTransformArraySoAHeap TransformsB(NumTransforms);

			for (auto& Translation : TransformsB.Translations)
			{
				Translation = FVector::OneVector;
			}
			for (auto& Rotation : TransformsB.Rotations)
			{
				Rotation = Rot90;
			}
			for (auto& Scale3D : TransformsB.Scales3D)
			{
				Scale3D = Half;
			}

			FHighResTimer::FTimePoint StartTime = FHighResTimer::GetTimeMark();

			ResultSoA.Blend(TransformsA, TransformsB, BlendFactor);

			TimeDiffSoA[i] = FHighResTimer::GetTimeDiffNanoSec(StartTime);
		}
	}
	for (const auto& Translation : ResultSoA.Translations)
	{
		AddErrorIfFalse(Translation == ReferenceResult.GetTranslation(), "SoA Translation Blend error.");
	}
	for (const auto& Rotation : ResultSoA.Rotations)
	{
		AddErrorIfFalse(Rotation == ReferenceResult.GetRotation(), "SoA Rotation Blend error.");
	}
	for (const auto& Scale3D : ResultSoA.Scales3D)
	{
		AddErrorIfFalse(Scale3D == ReferenceResult.GetScale3D(), "SoA Scale Blend error.");
	}

	// --- Time Averaging ---

	TimeDiffAoS.Sort();
	TimeDiffSoA.Sort();

	const int32 NumToAverage = FMath::Min(3, TimeDiffAoS.Num());
	int64 AverageAoS = 0LL;
	for (int i = 0; i < NumToAverage; ++i)
	{
		AverageAoS += TimeDiffAoS[i];
	}
	AverageAoS /= NumToAverage;

	int64 AverageSoA = 0LL;
	for (int i = 0; i < NumToAverage; ++i)
	{
		AverageSoA += TimeDiffSoA[i];
	}
	AverageSoA /= NumToAverage;


	AddInfo(FString::Printf(TEXT("Average AoS Duration: %d nanosecs."), AverageAoS));
	AddInfo(FString::Printf(TEXT("Average SoA Duration: %d nanosecs."), AverageSoA));

	// Final GC to make sure everything is cleaned up
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS