// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Determinism/PCGDeterminismTestsCommon.h"

#include "PCGContext.h"
#include "Data/PCGPointData.h"
#include "Elements/PCGNoise.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGNoise_CalcLocalCoordinates2D, "pcg.tests.Noise.CalcLocalCoordinates2D", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGNoise_CalcLocalCoordinates2D::RunTest(const FString& Parameters)
{
	const FBox TestBox(-FVector::One(), FVector::One());
	const FVector2D Scale = FVector2D::One();

	UTEST_EQUAL("Disabled Edge value", CalcEdgeBlendAmount2D(PCGNoise::CalcLocalCoordinates2D(TestBox, FTransform::Identity, Scale, FVector::One()), 0.0), 0.0);
	UTEST_EQUAL("Disabled Edge value", CalcEdgeBlendAmount2D(PCGNoise::CalcLocalCoordinates2D(TestBox, FTransform::Identity, Scale, FVector::Zero()), 0.0), 0.0);

	PCGNoise::FLocalCoordinates2D Test0 = PCGNoise::CalcLocalCoordinates2D(TestBox, FTransform::Identity, Scale, FVector::One());
	const double EdgeBlendAmount0 = CalcEdgeBlendAmount2D(Test0, 1.0);
	UTEST_EQUAL_TOLERANCE("Enabled Edge Value", EdgeBlendAmount0, 1.0, 0.001);
	UTEST_EQUAL_TOLERANCE("Edge Sample X0", Test0.X0, 2.0, 0.001);
	UTEST_EQUAL_TOLERANCE("Edge Sample Y0", Test0.Y0, 2.0, 0.001);
	UTEST_EQUAL_TOLERANCE("Edge Sample X1", Test0.X1, 0.0, 0.001);
	UTEST_EQUAL_TOLERANCE("Edge Sample Y1", Test0.Y1, 0.0, 0.001);

	UTEST_EQUAL_TOLERANCE("Enabled Edge Value Center", CalcEdgeBlendAmount2D(PCGNoise::CalcLocalCoordinates2D(TestBox, FTransform::Identity, Scale, FVector::Zero()), 1.0), 0.0, 0.001);

	PCGNoise::FLocalCoordinates2D Test1 = PCGNoise::CalcLocalCoordinates2D(TestBox, FTransform::Identity, Scale, FVector(0.9));
	const double EdgeBlendAmount1 = CalcEdgeBlendAmount2D(Test1, 0.5);
	UTEST_EQUAL_TOLERANCE("Blended Edge Value", EdgeBlendAmount1, 0.8, 0.001);
	UTEST_EQUAL_TOLERANCE("Blended Edge Sample X", Test1.X0, 1.9, 0.001);
	UTEST_EQUAL_TOLERANCE("Blended Edge Sample Y", Test1.Y0, 1.9, 0.001);
	UTEST_EQUAL_TOLERANCE("Blended Edge Sample X", Test1.X1, -0.1, 0.001);
	UTEST_EQUAL_TOLERANCE("Blended Edge Sample Y", Test1.Y1, -0.1, 0.001);

	return true;
}

// just make sure stuff doesn't crash

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGNoise_Perlin2D, FPCGTestBaseClass, "pcg.tests.Noise.Perlin2D", PCGTestsCommon::TestFlags)

bool FPCGNoise_Perlin2D::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::GenerateSettings<UPCGNoiseSettings>(TestData);
	UPCGNoiseSettings* Settings = CastChecked<UPCGNoiseSettings>(TestData.Settings);

	Settings->Mode = PCGNoiseMode::Perlin2D;
	Settings->ValueTarget.AttributeName = TEXT("Noise");

	FPCGElementPtr TestElement = TestData.Settings->GetElement();

	{
		FPCGTaggedData& SourcePin = TestData.InputData.TaggedData.Emplace_GetRef();
		SourcePin.Pin = PCGPinConstants::DefaultInputLabel;
		SourcePin.Data = PCGTestsCommon::CreateRandomPointData(100, 42);
	}

	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();

	while (!TestElement->Execute(Context.Get())) {}	

	const TArray<FPCGTaggedData>& Outputs = Context->OutputData.GetInputs();

	UTEST_EQUAL("Output count", Outputs.Num(), 1);

	UPCGPointData* OutputData = Cast<UPCGPointData>(Outputs[0].Data);

	UTEST_NOT_NULL("Output data", OutputData);
	UTEST_EQUAL("Output point data count", OutputData->GetPoints().Num(), 100);

	const FPCGMetadataAttribute<double>* NoiseAttribute = OutputData->Metadata->GetConstTypedAttribute<double>(Settings->ValueTarget.AttributeName);
	UTEST_NOT_NULL("Noise Attribute Created", NoiseAttribute);

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGNoise_Caustic2D, FPCGTestBaseClass, "pcg.tests.Noise.Caustic2D", PCGTestsCommon::TestFlags)

bool FPCGNoise_Caustic2D::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::GenerateSettings<UPCGNoiseSettings>(TestData);
	UPCGNoiseSettings* Settings = CastChecked<UPCGNoiseSettings>(TestData.Settings);

	Settings->Mode = PCGNoiseMode::Caustic2D;
	Settings->ValueTarget.AttributeName = TEXT("Noise");

	FPCGElementPtr TestElement = TestData.Settings->GetElement();

	{
		FPCGTaggedData& SourcePin = TestData.InputData.TaggedData.Emplace_GetRef();
		SourcePin.Pin = PCGPinConstants::DefaultInputLabel;
		SourcePin.Data = PCGTestsCommon::CreateRandomPointData(100, 42);
	}

	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();

	while (!TestElement->Execute(Context.Get())) {}	

	const TArray<FPCGTaggedData>& Outputs = Context->OutputData.GetInputs();

	UTEST_EQUAL("Output count", Outputs.Num(), 1);

	UPCGPointData* OutputData = Cast<UPCGPointData>(Outputs[0].Data);

	UTEST_NOT_NULL("Output data", OutputData);
	UTEST_EQUAL("Output point data count", OutputData->GetPoints().Num(), 100);

	const FPCGMetadataAttribute<double>* NoiseAttribute = OutputData->Metadata->GetConstTypedAttribute<double>(Settings->ValueTarget.AttributeName);
	UTEST_NOT_NULL("Noise Attribute Created", NoiseAttribute);

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGNoise_Voronoi2D, FPCGTestBaseClass, "pcg.tests.Noise.Voronoi2D", PCGTestsCommon::TestFlags)

bool FPCGNoise_Voronoi2D::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::GenerateSettings<UPCGNoiseSettings>(TestData);
	UPCGNoiseSettings* Settings = CastChecked<UPCGNoiseSettings>(TestData.Settings);

	Settings->Mode = PCGNoiseMode::Voronoi2D;
	Settings->ValueTarget.AttributeName = TEXT("Distance");
	Settings->VoronoiCellIDTarget.AttributeName = TEXT("CellID");

	FPCGElementPtr TestElement = TestData.Settings->GetElement();

	{
		FPCGTaggedData& SourcePin = TestData.InputData.TaggedData.Emplace_GetRef();
		SourcePin.Pin = PCGPinConstants::DefaultInputLabel;
		SourcePin.Data = PCGTestsCommon::CreateRandomPointData(100, 42);
	}

	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();

	while (!TestElement->Execute(Context.Get())) {}	

	const TArray<FPCGTaggedData>& Outputs = Context->OutputData.GetInputs();

	UTEST_EQUAL("Output count", Outputs.Num(), 1);

	UPCGPointData* OutputData = Cast<UPCGPointData>(Outputs[0].Data);

	UTEST_NOT_NULL("Output data", OutputData);
	UTEST_EQUAL("Output point data count", OutputData->GetPoints().Num(), 100);

	const FPCGMetadataAttribute<double>* NoiseAttribute = OutputData->Metadata->GetConstTypedAttribute<double>(Settings->ValueTarget.AttributeName);
	UTEST_NOT_NULL("Distance Attribute Created", NoiseAttribute);

	const FPCGMetadataAttribute<double>* CellIDAttribute = OutputData->Metadata->GetConstTypedAttribute<double>(Settings->VoronoiCellIDTarget.AttributeName);
	UTEST_NOT_NULL("Cell ID Attribute Created", CellIDAttribute);


	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGNoise_FractionalBrownian2D, FPCGTestBaseClass, "pcg.tests.Noise.FractionalBrownian2D", PCGTestsCommon::TestFlags)

bool FPCGNoise_FractionalBrownian2D::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::GenerateSettings<UPCGNoiseSettings>(TestData);
	UPCGNoiseSettings* Settings = CastChecked<UPCGNoiseSettings>(TestData.Settings);

	Settings->Mode = PCGNoiseMode::FractionalBrownian2D;
	Settings->ValueTarget.AttributeName = TEXT("Noise");

	FPCGElementPtr TestElement = TestData.Settings->GetElement();

	{
		FPCGTaggedData& SourcePin = TestData.InputData.TaggedData.Emplace_GetRef();
		SourcePin.Pin = PCGPinConstants::DefaultInputLabel;
		SourcePin.Data = PCGTestsCommon::CreateRandomPointData(100, 42);
	}

	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();

	while (!TestElement->Execute(Context.Get())) {}	

	const TArray<FPCGTaggedData>& Outputs = Context->OutputData.GetInputs();

	UTEST_EQUAL("Output count", Outputs.Num(), 1);

	UPCGPointData* OutputData = Cast<UPCGPointData>(Outputs[0].Data);

	UTEST_NOT_NULL("Output data", OutputData);
	UTEST_EQUAL("Output point data count", OutputData->GetPoints().Num(), 100);

	const FPCGMetadataAttribute<double>* NoiseAttribute = OutputData->Metadata->GetConstTypedAttribute<double>(Settings->ValueTarget.AttributeName);
	UTEST_NOT_NULL("Noise Attribute Created", NoiseAttribute);

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGNoise_EdgeMask2D, FPCGTestBaseClass, "pcg.tests.Noise.EdgeMask2D", PCGTestsCommon::TestFlags)

bool FPCGNoise_EdgeMask2D::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::GenerateSettings<UPCGNoiseSettings>(TestData);
	UPCGNoiseSettings* Settings = CastChecked<UPCGNoiseSettings>(TestData.Settings);

	Settings->Mode = PCGNoiseMode::EdgeMask2D;
	Settings->ValueTarget.AttributeName = TEXT("Noise");

	FPCGElementPtr TestElement = TestData.Settings->GetElement();

	{
		FPCGTaggedData& SourcePin = TestData.InputData.TaggedData.Emplace_GetRef();
		SourcePin.Pin = PCGPinConstants::DefaultInputLabel;
		SourcePin.Data = PCGTestsCommon::CreateRandomPointData(100, 42);
	}

	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();

	while (!TestElement->Execute(Context.Get())) {}	

	const TArray<FPCGTaggedData>& Outputs = Context->OutputData.GetInputs();

	UTEST_EQUAL("Output count", Outputs.Num(), 1);

	UPCGPointData* OutputData = Cast<UPCGPointData>(Outputs[0].Data);

	UTEST_NOT_NULL("Output data", OutputData);
	UTEST_EQUAL("Output point data count", OutputData->GetPoints().Num(), 100);

	const FPCGMetadataAttribute<double>* NoiseAttribute = OutputData->Metadata->GetConstTypedAttribute<double>(Settings->ValueTarget.AttributeName);
	UTEST_NOT_NULL("Noise Attribute Created", NoiseAttribute);

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGNoise_TilingPerlin2D, FPCGTestBaseClass, "pcg.tests.Noise.TilingPerlin2D", PCGTestsCommon::TestFlags)
bool FPCGNoise_TilingPerlin2D::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::GenerateSettings<UPCGNoiseSettings>(TestData);
	UPCGNoiseSettings* Settings = CastChecked<UPCGNoiseSettings>(TestData.Settings);

	Settings->Mode = PCGNoiseMode::Perlin2D;
	Settings->bTiling = true;
	Settings->ValueTarget.AttributeName = TEXT("Noise");

	FPCGElementPtr TestElement = TestData.Settings->GetElement();

	{
		FPCGTaggedData& SourcePin = TestData.InputData.TaggedData.Emplace_GetRef();
		SourcePin.Pin = PCGPinConstants::DefaultInputLabel;
		SourcePin.Data = PCGTestsCommon::CreateRandomPointData(100, 42);
	}

	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();

	while (!TestElement->Execute(Context.Get())) {}	

	const TArray<FPCGTaggedData>& Outputs = Context->OutputData.GetInputs();

	UTEST_EQUAL("Output count", Outputs.Num(), 1);

	UPCGPointData* OutputData = Cast<UPCGPointData>(Outputs[0].Data);

	UTEST_NOT_NULL("Output data", OutputData);
	UTEST_EQUAL("Output point data count", OutputData->GetPoints().Num(), 100);

	const FPCGMetadataAttribute<double>* NoiseAttribute = OutputData->Metadata->GetConstTypedAttribute<double>(Settings->ValueTarget.AttributeName);
	UTEST_NOT_NULL("Noise Attribute Created", NoiseAttribute);

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGNoise_TilingVoronoi2D, FPCGTestBaseClass, "pcg.tests.Noise.TilingVoronoi2D", PCGTestsCommon::TestFlags)
bool FPCGNoise_TilingVoronoi2D::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::GenerateSettings<UPCGNoiseSettings>(TestData);
	UPCGNoiseSettings* Settings = CastChecked<UPCGNoiseSettings>(TestData.Settings);

	Settings->Mode = PCGNoiseMode::Voronoi2D;
	Settings->bTiling = true;
	Settings->ValueTarget.AttributeName = TEXT("Distance");
	Settings->VoronoiCellIDTarget.AttributeName = TEXT("CellID");

	FPCGElementPtr TestElement = TestData.Settings->GetElement();

	{
		FPCGTaggedData& SourcePin = TestData.InputData.TaggedData.Emplace_GetRef();
		SourcePin.Pin = PCGPinConstants::DefaultInputLabel;
		SourcePin.Data = PCGTestsCommon::CreateRandomPointData(100, 42);
	}

	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();

	while (!TestElement->Execute(Context.Get())) {}	

	const TArray<FPCGTaggedData>& Outputs = Context->OutputData.GetInputs();

	UTEST_EQUAL("Output count", Outputs.Num(), 1);

	UPCGPointData* OutputData = Cast<UPCGPointData>(Outputs[0].Data);

	UTEST_NOT_NULL("Output data", OutputData);
	UTEST_EQUAL("Output point data count", OutputData->GetPoints().Num(), 100);

	const FPCGMetadataAttribute<double>* NoiseAttribute = OutputData->Metadata->GetConstTypedAttribute<double>(Settings->ValueTarget.AttributeName);
	UTEST_NOT_NULL("Distance Attribute Created", NoiseAttribute);

	const FPCGMetadataAttribute<double>* CellIDAttribute = OutputData->Metadata->GetConstTypedAttribute<double>(Settings->VoronoiCellIDTarget.AttributeName);
	UTEST_NOT_NULL("Cell ID Attribute Created", CellIDAttribute);

	return true;
}
