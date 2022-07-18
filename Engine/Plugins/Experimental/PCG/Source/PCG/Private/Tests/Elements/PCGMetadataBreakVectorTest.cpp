// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Determinism/PCGDeterminismTestsCommon.h"
#include "Tests/PCGTestsCommon.h"

#include "PCGComponent.h"
#include "PCGData.h"
#include "PCGParamData.h"

#include "Data/PCGPointData.h"
#include "Elements/PCGMetadataBreakVector.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#if WITH_EDITOR

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGMetadataBreakVectorTest, FPCGTestBaseClass, "pcg.tests.Metadata.BreakVector", PCGTestsCommon::TestFlags)

namespace
{
	enum class EPCGComponentToCheck : uint8
	{
		X,
		Z,
		Y,
		W
	};
}

bool FPCGMetadataBreakVectorTest::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::GenerateSettings<UPCGMetadataBreakVectorSettings>(TestData);
	UPCGMetadataBreakVectorSettings* Settings = CastChecked<UPCGMetadataBreakVectorSettings>(TestData.Settings);
	FPCGElementPtr MetadataBreakVectorElement = TestData.Settings->GetElement();

	FPCGTaggedData& SpatialTaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
	TObjectPtr<UPCGPointData> PointData = PCGTestsCommon::CreateRandomPointData(10, TestData.Seed);
	SpatialTaggedData.Data = PointData;

	FPCGTaggedData& ParamTaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
	TObjectPtr<UPCGParamData> ParamData = PCGTestsCommon::CreateEmptyParamData();
	ParamTaggedData.Data = ParamData;

	const FName Vec3Attribute = TEXT("Vec3");
	const FName Vec4Attribute = TEXT("Vec4");
	const FName RotatorAttribute = TEXT("Rotator");
	const FName FloatAttribute = TEXT("Float");

	const bool bAllowsInterpolation = false;
	const bool bOverrideParent = false;

	PointData->Metadata->CreateVectorAttribute(Vec3Attribute, FVector::Zero(), bAllowsInterpolation, bOverrideParent);
	PointData->Metadata->CreateVector4Attribute(Vec4Attribute, FVector4::Zero(), bAllowsInterpolation, bOverrideParent);
	PointData->Metadata->CreateRotatorAttribute(RotatorAttribute, FRotator::ZeroRotator, bAllowsInterpolation, bOverrideParent);
	PointData->Metadata->CreateFloatAttribute(FloatAttribute, 0, bAllowsInterpolation, bOverrideParent);

	TArray<FPCGPoint>& SourcePoints = PointData->GetMutablePoints();

	// Randomize attribute values, and leave the second half to default value
	FRandomStream RandomSource(TestData.Seed);
	for (int I = 0; I < SourcePoints.Num() / 2; ++I)
	{
		UPCGMetadataAccessorHelpers::SetVectorAttribute(SourcePoints[I], PointData->Metadata, Vec3Attribute, RandomSource.VRand());
		UPCGMetadataAccessorHelpers::SetVector4Attribute(SourcePoints[I], PointData->Metadata, Vec4Attribute, FVector4(RandomSource.VRand(), RandomSource.FRand()));
		UPCGMetadataAccessorHelpers::SetRotatorAttribute(SourcePoints[I], PointData->Metadata, RotatorAttribute, FRotator(RandomSource.FRand(), RandomSource.FRand(), RandomSource.FRand()));
		UPCGMetadataAccessorHelpers::SetFloatAttribute(SourcePoints[I], PointData->Metadata, FloatAttribute, RandomSource.FRand());
	}

	ParamData->Metadata->InitializeAsCopy(PointData->Metadata);

	auto ValidateMetadataBreakVector = [this, &TestData, MetadataBreakVectorElement, Settings]() -> bool
	{
		TUniquePtr<FPCGContext> Context = MakeUnique<FPCGContext>(*MetadataBreakVectorElement->Initialize(TestData.InputData, TestData.TestPCGComponent, nullptr));
		Context->NumAvailableTasks = 1;

		while (!MetadataBreakVectorElement->Execute(Context.Get()))
		{}

		const TArray<FPCGTaggedData>& Inputs = Context->InputData.TaggedData;

		const TArray<FPCGTaggedData> OutputsX = Context->OutputData.GetInputsByPin(PCGMetadataBreakVectorConstants::XLabel);
		const TArray<FPCGTaggedData> OutputsY = Context->OutputData.GetInputsByPin(PCGMetadataBreakVectorConstants::YLabel);
		const TArray<FPCGTaggedData> OutputsZ = Context->OutputData.GetInputsByPin(PCGMetadataBreakVectorConstants::ZLabel);
		const TArray<FPCGTaggedData> OutputsW = Context->OutputData.GetInputsByPin(PCGMetadataBreakVectorConstants::WLabel);

		const FName DestinationAttributeForX = FName(Settings->SourceAttributeName.ToString() + ".X");
		const FName DestinationAttributeForY = FName(Settings->SourceAttributeName.ToString() + ".Y");
		const FName DestinationAttributeForZ = FName(Settings->SourceAttributeName.ToString() + ".Z");
		const FName DestinationAttributeForW = FName(Settings->SourceAttributeName.ToString() + ".W");

		bool bTestPassed = true;

		for (int DataIndex = 0; DataIndex < Inputs.Num(); ++DataIndex)
		{
			const FPCGTaggedData& Input = Inputs[DataIndex];

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
				continue;
			}

			check(SourceMetadata);

			const FPCGMetadataAttributeBase* SourceAttributeBase = SourceMetadata->GetConstAttribute(Settings->SourceAttributeName);
			check(SourceAttributeBase);

			auto ValidateComponentOutput = [&](const FPCGTaggedData& Output, const FName& OutAttributeName, EPCGComponentToCheck ComponentToCheck)
			{
				if (!TestTrue("Valid output data", Output.Data != nullptr))
				{
					bTestPassed = false;
					return;
				}

				UPCGMetadata* OutMetadata = nullptr;
				if (const UPCGSpatialData* SpatialInput = Cast<const UPCGSpatialData>(Output.Data))
				{
					OutMetadata = SpatialInput->Metadata;
				}
				else if (const UPCGParamData* ParamsInput = Cast<const UPCGParamData>(Output.Data))
				{
					OutMetadata = ParamsInput->Metadata;
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

				if (!TestEqual("Output attribute is a valid type (double)", OutAttributeBase->GetTypeId(), PCG::Private::MetadataTypes<double>::Id))
				{
					bTestPassed = false;
					return;
				}

				const FPCGMetadataAttribute<double>* OutAttribute = static_cast<const FPCGMetadataAttribute<double>*>(OutAttributeBase);
				check(OutAttribute);

				const PCGMetadataEntryKey InEntryKeyCount = SourceMetadata->GetItemCountForChild();
				const PCGMetadataEntryKey OutEntryKeyCount = OutMetadata->GetItemCountForChild();

				if (!TestEqual("Identical EntryKey counts", InEntryKeyCount, OutEntryKeyCount))
				{
					bTestPassed = false;
					return;
				}

				const PCGMetadataValueKey InValueKeyCount = SourceAttributeBase->GetValueKeyOffsetForChild();
				const PCGMetadataValueKey OutValueKeyCount = OutAttribute->GetValueKeyOffsetForChild();

				if (!TestEqual("Identical ValueKey counts", InValueKeyCount, OutValueKeyCount))
				{
					bTestPassed = false;
					return;
				}

				for (PCGMetadataEntryKey EntryKey = 0; EntryKey < InEntryKeyCount; ++EntryKey)
				{
					const PCGMetadataValueKey InValueKey = SourceAttributeBase->GetValueKey(EntryKey);
					const PCGMetadataValueKey OutValueKey = OutAttribute->GetValueKey(EntryKey);

					bTestPassed &= TestEqual("Identical value keys", InValueKey, OutValueKey);

					FVector4 SourceValue;
					if (SourceAttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FVector>::Id)
					{
						SourceValue = static_cast<const FPCGMetadataAttribute<FVector>*>(SourceAttributeBase)->GetValue(InValueKey);
					}
					else if (SourceAttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FVector4>::Id)
					{
						SourceValue = static_cast<const FPCGMetadataAttribute<FVector4>*>(SourceAttributeBase)->GetValue(InValueKey);
					}
					else //if (SourceAttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FRotator>::Id)
					{
						const FRotator Rotator = static_cast<const FPCGMetadataAttribute<FRotator>*>(SourceAttributeBase)->GetValue(InValueKey);
						SourceValue.X = Rotator.Roll;
						SourceValue.Y = Rotator.Pitch;
						SourceValue.Z = Rotator.Yaw;
					}

					const double OutValue = OutAttribute->GetValue(OutValueKey);

					if (ComponentToCheck == EPCGComponentToCheck::X)
					{
						bTestPassed &= TestEqual("Identical values", SourceValue.X, OutValue);
					}
					else if (ComponentToCheck == EPCGComponentToCheck::Y)
					{
						bTestPassed &= TestEqual("Identical values", SourceValue.Y, OutValue);
					}
					else if (ComponentToCheck == EPCGComponentToCheck::Z)
					{
						bTestPassed &= TestEqual("Identical values", SourceValue.Z, OutValue);
					}
					else if (ComponentToCheck == EPCGComponentToCheck::W)
					{
						bTestPassed &= TestEqual("Identical values", SourceValue.W, OutValue);
					}
				}
			};

			if (Settings->bForceConnectX)
			{
				if (TestTrue("Appropriate number of outputs generated", OutputsX.Num() > DataIndex))
				{
					ValidateComponentOutput(OutputsX[DataIndex], DestinationAttributeForX, EPCGComponentToCheck::X);
				}
				else
				{
					bTestPassed = false;
				}
			}

			if (Settings->bForceConnectY)
			{
				if (TestTrue("Appropriate number of outputs generated", OutputsY.Num() > DataIndex))
				{
					ValidateComponentOutput(OutputsY[DataIndex], DestinationAttributeForY, EPCGComponentToCheck::Y);
				}
				else
				{
					bTestPassed = false;
				}
			}

			if (Settings->bForceConnectZ)
			{
				if (TestTrue("Appropriate number of outputs generated", OutputsZ.Num() > DataIndex))
				{
					ValidateComponentOutput(OutputsZ[DataIndex], DestinationAttributeForZ, EPCGComponentToCheck::Z);
				}
				else
				{
					bTestPassed = false;
				}
			}

			if (Settings->bForceConnectW && SourceAttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FVector4>::Id)
			{
				if (TestTrue("Appropriate number of outputs generated", OutputsW.Num() > DataIndex))
				{
					ValidateComponentOutput(OutputsW[DataIndex], DestinationAttributeForW, EPCGComponentToCheck::W);
				}
				else
				{
					bTestPassed = false;
				}
			}
		}

		return bTestPassed;
	};

	bool bTestPassed = true;

	Settings->bForceConnectX = true;
	Settings->bForceConnectY = true;
	Settings->bForceConnectZ = true;

	// Test FVector source attribute
	{
		Settings->SourceAttributeName = Vec3Attribute;
		bTestPassed &= ValidateMetadataBreakVector();
	}

	// Test FVector4 source attribute
	{
		Settings->SourceAttributeName = Vec4Attribute;
		Settings->bForceConnectW = true;
		bTestPassed &= ValidateMetadataBreakVector();
		Settings->bForceConnectW = false;
	}

	// Test FRotator source attribute
	{
		Settings->SourceAttributeName = RotatorAttribute;
		bTestPassed &= ValidateMetadataBreakVector();
	}

	// Test invalid source attribute type
	{
		Settings->SourceAttributeName = FloatAttribute;
		Settings->bForceConnectX = false;
		Settings->bForceConnectY = false;
		Settings->bForceConnectZ = false;

		AddExpectedError(TEXT("Attribute ") + FloatAttribute.ToString() + TEXT(" is not a breakable type"), EAutomationExpectedErrorFlags::Contains, 2);
		bTestPassed &= ValidateMetadataBreakVector();
	}

	return bTestPassed;
}

#endif // WITH_EDITOR
