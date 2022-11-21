// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Determinism/PCGDeterminismTestsCommon.h"
#include "Tests/PCGTestsCommon.h"

#include "PCGComponent.h"
#include "PCGData.h"
#include "PCGParamData.h"

#include "Data/PCGPointData.h"
#include "Elements/Metadata/PCGMetadataBreakTransform.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#if WITH_EDITOR

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGMetadataBreakTransformTest, FPCGTestBaseClass, "pcg.tests.Metadata.BreakTransform", PCGTestsCommon::TestFlags)

namespace PCGBreakTransformTest
{
	enum class EPCGComponentToCheck
	{
		Translation,
		Rotation,
		Scale
	};

	const FName TransformAttribute = TEXT("Transform");
	const FName InvalidAttribute = TEXT("Float");
	
	FTransform RandomTransform(FRandomStream& RandomSource)
	{
		FVector RandomTranslation = RandomSource.VRand();
		FQuat RandomRotation = FRotator(RandomSource.FRand(), RandomSource.FRand(), RandomSource.FRand()).Quaternion();
		FVector RandomScale = RandomSource.VRand();

		return FTransform(RandomRotation, RandomTranslation, RandomScale);
	}

	void GenerateSpatialData(PCGTestsCommon::FTestData& TestData)
	{
		FPCGTaggedData& SpatialTaggedData = TestData.InputData.TaggedData.Emplace_GetRef();
		TObjectPtr<UPCGPointData> PointData = PCGTestsCommon::CreateRandomPointData(10, TestData.Seed);
		SpatialTaggedData.Data = PointData;
		SpatialTaggedData.Pin = PCGPinConstants::DefaultInputLabel;

		const bool bAllowsInterpolation = false;
		const bool bOverrideParent = false;

		PointData->Metadata->CreateTransformAttribute(TransformAttribute, FTransform::Identity, bAllowsInterpolation, bOverrideParent);
		PointData->Metadata->CreateFloatAttribute(InvalidAttribute, 0.0f, bAllowsInterpolation, bOverrideParent);

		TArray<FPCGPoint>& SourcePoints = PointData->GetMutablePoints();

		// Randomize attribute values, and leave the second half to default value
		FRandomStream RandomSource(TestData.Seed);
		for (int I = 0; I < SourcePoints.Num() / 2; ++I)
		{
			UPCGMetadataAccessorHelpers::SetTransformAttribute(SourcePoints[I], PointData->Metadata, TransformAttribute, RandomTransform(RandomSource));
			UPCGMetadataAccessorHelpers::SetFloatAttribute(SourcePoints[I], PointData->Metadata, InvalidAttribute, RandomSource.FRand());
		}
	}

	void GenerateParamData(PCGTestsCommon::FTestData& TestData)
	{
		FPCGTaggedData& ParamTaggedData = TestData.InputData.TaggedData.Emplace_GetRef();
		TObjectPtr<UPCGParamData> ParamData = PCGTestsCommon::CreateEmptyParamData();
		ParamTaggedData.Data = ParamData;
		ParamTaggedData.Pin = PCGPinConstants::DefaultInputLabel;

		PCGMetadataEntryKey EntryKey = ParamData->Metadata->AddEntry();
		const bool bAllowsInterpolation = false;
		const bool bOverrideParent = false;

		ParamData->Metadata->CreateTransformAttribute(TransformAttribute, FTransform::Identity, bAllowsInterpolation, bOverrideParent);
		ParamData->Metadata->CreateFloatAttribute(InvalidAttribute, 0.0f, bAllowsInterpolation, bOverrideParent);

		// Randomize attribute values
		FRandomStream RandomSource(TestData.Seed);
		if (FPCGMetadataAttribute<FTransform>* Attribute = static_cast<FPCGMetadataAttribute<FTransform>*>(ParamData->Metadata->GetMutableAttribute(TransformAttribute)))
		{
			Attribute->SetValue(EntryKey, RandomTransform(RandomSource));
		}

		if (FPCGMetadataAttribute<float>* Attribute = static_cast<FPCGMetadataAttribute<float>*>(ParamData->Metadata->GetMutableAttribute(InvalidAttribute)))
		{
			Attribute->SetValue(EntryKey, RandomSource.FRand());
		}
	}

	TArray<PCGMetadataEntryKey> GatherEntryKeys(const UPCGData* InData, const FPCGMetadataAttributeBase* InAttribute)
	{
		TArray<PCGMetadataEntryKey> Keys;
		if (const UPCGPointData* PointOutput = Cast<const UPCGPointData>(InData))
		{
			for (const FPCGPoint& Point : PointOutput->GetPoints())
			{
				Keys.Add(Point.MetadataEntry);
			}
		}
		else
		{
			const FPCGMetadataAttributeBase* Current = InAttribute;
			while (Current)
			{
				TArray<PCGMetadataEntryKey> Temp;
				Current->GetEntryToValueKeyMap_NotThreadSafe().GenerateKeyArray(Temp);
				Keys.Append(Temp);
				Current = Current->GetParent();
			}
		}

		return Keys;
	}
}


bool FPCGMetadataBreakTransformTest::RunTest(const FString& Parameters)
{	
	auto ValidateMetadataBreakVector = [this](PCGTestsCommon::FTestData& TestData, bool bIsValid = true) -> bool
	{
		const FName OutputAttributeName = TEXT("Output");

		UPCGMetadataBreakTransformSettings* Settings = CastChecked<UPCGMetadataBreakTransformSettings>(TestData.Settings);
		Settings->OutputTarget.Selection = EPCGAttributePropertySelection::Attribute;
		Settings->OutputTarget.AttributeName = OutputAttributeName;
		FPCGElementPtr MetadataBreakTransformElement = TestData.Settings->GetElement();

		TUniquePtr<FPCGContext> Context = TUniquePtr<FPCGContext>(MetadataBreakTransformElement->Initialize(TestData.InputData, TestData.TestPCGComponent, nullptr));
		Context->NumAvailableTasks = 1;

		while (!MetadataBreakTransformElement->Execute(Context.Get()))
		{}

		// If the test is not valid, just check that we have no output and early out
		if (!bIsValid)
		{
			return TestTrue(TEXT("Invalid operation, no outputs"), Context->OutputData.GetInputs().IsEmpty());
		}

		const TArray<FPCGTaggedData>& Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

		if (!TestEqual("Right number of inputs", Inputs.Num(), 1))
		{
			return false;
		}

		bool bTestPassed = true;

		const FPCGTaggedData& Input = Inputs[0];

		UPCGMetadata* SourceMetadata = nullptr; 
		if (const UPCGSpatialData* SpatialInput = Cast<const UPCGSpatialData>(Input.Data))
		{
			SourceMetadata = SpatialInput->Metadata;
		}
		else if (const UPCGParamData* ParamsInput = Cast<const UPCGParamData>(Input.Data))
		{
			SourceMetadata = ParamsInput->Metadata;
		}
		else
		{
			return false;
		}

		check(SourceMetadata);

		const FPCGMetadataAttributeBase* SourceAttributeBase = SourceMetadata->GetConstAttribute(Settings->InputSource.GetName());
		check(SourceAttributeBase);

		TArray<PCGMetadataEntryKey> InputKeys = PCGBreakTransformTest::GatherEntryKeys(Input.Data, SourceAttributeBase);

		auto ValidateComponentOutput = [&](const FPCGTaggedData& Output, const FName& OutAttributeName, PCGBreakTransformTest::EPCGComponentToCheck ComponentToCheck, auto DummyValue)
		{
			using OutType = decltype(DummyValue);

			if (!TestTrue("Valid output data", Output.Data != nullptr))
			{
				bTestPassed = false;
				return;
			}

			UPCGMetadata* OutMetadata = nullptr;
			if (const UPCGSpatialData* SpatialOutput = Cast<const UPCGSpatialData>(Output.Data))
			{
				OutMetadata = SpatialOutput->Metadata;
			}
			else if (const UPCGParamData* ParamsOutput = Cast<const UPCGParamData>(Output.Data))
			{
				OutMetadata = ParamsOutput->Metadata;
			}
			else
			{
				return;
			}

			if (!TestNotNull("Valid output metadata", OutMetadata))
			{
				bTestPassed = false;
				return;
			}

			const FPCGMetadataAttributeBase* OutAttributeBase = OutMetadata->GetConstAttribute(OutAttributeName);

			if (!TestNotNull("Valid output attribute", OutAttributeBase))
			{
				bTestPassed = false;
				return;
			}

			if (!TestEqual("Output attribute is a valid type", OutAttributeBase->GetTypeId(), PCG::Private::MetadataTypes<OutType>::Id))
			{
				bTestPassed = false;
				return;
			}

			const FPCGMetadataAttribute<OutType>* OutAttribute = static_cast<const FPCGMetadataAttribute<OutType>*>(OutAttributeBase);
			check(OutAttribute);

			TArray<PCGMetadataEntryKey> OutputKeys = PCGBreakTransformTest::GatherEntryKeys(Output.Data, OutAttribute);

			if (!TestEqual("Identical EntryKeys count", InputKeys.Num(), OutputKeys.Num()))
			{
				bTestPassed = false;
				return;
			}

			for (int32 i = 0; i < OutputKeys.Num(); ++i)
			{
				const PCGMetadataValueKey InValueKey = SourceAttributeBase->GetValueKey(InputKeys[i]);
				const PCGMetadataValueKey OutValueKey = OutAttribute->GetValueKey(OutputKeys[i]);

				FTransform SourceValue = static_cast<const FPCGMetadataAttribute<FTransform>*>(SourceAttributeBase)->GetValue(InValueKey);

				const OutType OutValue = OutAttribute->GetValue(OutValueKey);

				if constexpr (std::is_same_v<FVector, OutType>)
				{
					if (ComponentToCheck == PCGBreakTransformTest::EPCGComponentToCheck::Translation)
					{
						bTestPassed &= TestEqual("Translation", SourceValue.GetTranslation(), OutValue);
					}
					else if (ComponentToCheck == PCGBreakTransformTest::EPCGComponentToCheck::Scale)
					{
						bTestPassed &= TestEqual("Scale", SourceValue.GetScale3D(), OutValue);
					}
					else
					{
						bTestPassed = false;
					}
				}
				else
				{
					if (ComponentToCheck == PCGBreakTransformTest::EPCGComponentToCheck::Rotation)
					{
						bTestPassed &= TestEqual("Rotation", SourceValue.GetRotation(), OutValue);
					}
					else
					{
						bTestPassed = false;
					}
				}
			}
		};

		const TArray<FPCGTaggedData> OutputsTranslation = Context->OutputData.GetInputsByPin(PCGMetadataTransformConstants::Translation);
		const TArray<FPCGTaggedData> OutputsRotation = Context->OutputData.GetInputsByPin(PCGMetadataTransformConstants::Rotation);
		const TArray<FPCGTaggedData> OutputsScale = Context->OutputData.GetInputsByPin(PCGMetadataTransformConstants::Scale);

		const FName DestinationAttributeForTranslation = Settings->OutputTarget.AttributeName; //Settings->GetOutputAttributeName(Settings->OutputName.AttributeName, 0);
		const FName DestinationAttributeForRotation = Settings->OutputTarget.AttributeName; //Settings->GetOutputAttributeName(Settings->OutputName.AttributeName, 1);
		const FName DestinationAttributeForScale = Settings->OutputTarget.AttributeName; //Settings->GetOutputAttributeName(Settings->OutputName.AttributeName, 2);

		if (TestEqual("Appropriate number of outputs generated for Translation", OutputsTranslation.Num(), 1))
		{
			ValidateComponentOutput(OutputsTranslation[0], DestinationAttributeForTranslation, PCGBreakTransformTest::EPCGComponentToCheck::Translation, FVector{});
		}
		else
		{
			bTestPassed = false;
		}

		if (TestEqual("Appropriate number of outputs generated for Rotation", OutputsRotation.Num(), 1))
		{
			ValidateComponentOutput(OutputsRotation[0], DestinationAttributeForRotation, PCGBreakTransformTest::EPCGComponentToCheck::Rotation, FQuat{});
		}
		else
		{
			bTestPassed = false;
		}

		if (TestEqual("Appropriate number of outputs generated for Scale", OutputsScale.Num(), 1))
		{
			ValidateComponentOutput(OutputsScale[0], DestinationAttributeForScale, PCGBreakTransformTest::EPCGComponentToCheck::Scale, FVector{});
		}
		else
		{
			bTestPassed = false;
		}

		return bTestPassed;
	};

	bool bTestPassed = true;

	PCGTestsCommon::FTestData TestDataSpatial(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::GenerateSettings<UPCGMetadataBreakTransformSettings>(TestDataSpatial);
	PCGBreakTransformTest::GenerateSpatialData(TestDataSpatial);

	PCGTestsCommon::FTestData TestDataParams(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::GenerateSettings<UPCGMetadataBreakTransformSettings>(TestDataParams);
	PCGBreakTransformTest::GenerateParamData(TestDataParams);

	using PairWhatData = TPair<FString, PCGTestsCommon::FTestData*>;

	TStaticArray<PairWhatData, 2> AllTestData;
	AllTestData[0] = PairWhatData("Testing with point data as input", &TestDataSpatial);
	AllTestData[1] = PairWhatData("Testing with param data as input", &TestDataParams);

	// Setup error catching. This error should happen only twice (invalid type)
	AddExpectedError(TEXT("Attribute/Property Float is not a supported type for input 0"), EAutomationExpectedErrorFlags::Contains, 2);

	for (PairWhatData& PairTestData : AllTestData)
	{
		AddInfo(PairTestData.Key);
		PCGTestsCommon::FTestData* TestData = PairTestData.Value;

		UPCGMetadataBreakTransformSettings* Settings = CastChecked<UPCGMetadataBreakTransformSettings>(TestData->Settings);

		Settings->ForceOutputConnections[0] = true;
		Settings->ForceOutputConnections[1] = true;
		Settings->ForceOutputConnections[2] = true;

		Settings->InputSource.Selection = EPCGAttributePropertySelection::Attribute;

		{
			AddInfo(TEXT("Test with Transform as input attribute"));
			Settings->InputSource.AttributeName = PCGBreakTransformTest::TransformAttribute;
			bTestPassed &= ValidateMetadataBreakVector(*TestData);
		}

		{
			AddInfo(TEXT("Test with Float as input attribute (invalid type)"));
			Settings->InputSource.AttributeName = PCGBreakTransformTest::InvalidAttribute;

			bTestPassed &= ValidateMetadataBreakVector(*TestData, /*bIsValid=*/false);
		}
	}

	return bTestPassed;
}

#endif // WITH_EDITOR
