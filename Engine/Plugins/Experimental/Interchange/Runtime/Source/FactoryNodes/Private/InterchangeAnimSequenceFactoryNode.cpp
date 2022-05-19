// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeAnimSequenceFactoryNode.h"
#include "Animation/AnimSequence.h"

namespace UE::Interchange::Animation
{
	FFrameRate ConvertSampleRatetoFrameRate(double SampleRate)
	{
		double IntegralPart = FMath::FloorToDouble(SampleRate);
		double FractionalPart = SampleRate - IntegralPart;
		const int32 Tolerance = 1000000;
		double Divisor = static_cast<double>(FMath::GreatestCommonDivisor(FMath::RoundToInt(FractionalPart * Tolerance), Tolerance));
		int32 Denominator = static_cast<int32>(Tolerance / Divisor);
		int32 Numerator = static_cast<int32>(IntegralPart * Denominator + FMath::RoundToDouble(FractionalPart * Tolerance) / Divisor);
		check(Denominator != 0);
		return FFrameRate(Numerator, Denominator);
	}
}

UInterchangeAnimSequenceFactoryNode::UInterchangeAnimSequenceFactoryNode()
{
}

void UInterchangeAnimSequenceFactoryNode::InitializeAnimSequenceNode(const FString& UniqueID, const FString& DisplayLabel)
{
	InitializeNode(UniqueID, DisplayLabel, EInterchangeNodeContainerType::FactoryData);
}

FString UInterchangeAnimSequenceFactoryNode::GetTypeName() const
{
	const FString TypeName = TEXT("AnimSequenceNode");
	return TypeName;
}

FString UInterchangeAnimSequenceFactoryNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	FString KeyDisplayName = NodeAttributeKey.ToString();
	if (NodeAttributeKey == Macro_CustomSkeletonFactoryNodeUidKey)
	{
		KeyDisplayName = TEXT("Skeleton Uid");
	}
	else if (NodeAttributeKey == Macro_CustomSkeletonSoftObjectPathKey)
	{
		KeyDisplayName = TEXT("Specified Existing Skeleton");
	}
	else
	{
		KeyDisplayName = Super::GetKeyDisplayName(NodeAttributeKey);
	}
	return KeyDisplayName;
}

UClass* UInterchangeAnimSequenceFactoryNode::GetObjectClass() const
{
	return UAnimSequence::StaticClass();
}

bool UInterchangeAnimSequenceFactoryNode::GetCustomSkeletonFactoryNodeUid(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(SkeletonFactoryNodeUid, FString);
}

bool UInterchangeAnimSequenceFactoryNode::SetCustomSkeletonFactoryNodeUid(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SkeletonFactoryNodeUid, FString);
}

bool UInterchangeAnimSequenceFactoryNode::GetCustomImportBoneTracks(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(ImportBoneTracks, bool);
}

bool UInterchangeAnimSequenceFactoryNode::SetCustomImportBoneTracks(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ImportBoneTracks, bool);
}

bool UInterchangeAnimSequenceFactoryNode::GetCustomImportBoneTracksSampleRate(double& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(ImportBoneTracksSampleRate, double);
}

bool UInterchangeAnimSequenceFactoryNode::SetCustomImportBoneTracksSampleRate(const double& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ImportBoneTracksSampleRate, double);
}

bool UInterchangeAnimSequenceFactoryNode::GetCustomImportBoneTracksRangeStart(double& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(ImportBoneTracksRangeStart, double);
}

bool UInterchangeAnimSequenceFactoryNode::SetCustomImportBoneTracksRangeStart(const double& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ImportBoneTracksRangeStart, double);
}

bool UInterchangeAnimSequenceFactoryNode::GetCustomImportBoneTracksRangeStop(double& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(ImportBoneTracksRangeStop, double);
}

bool UInterchangeAnimSequenceFactoryNode::SetCustomImportBoneTracksRangeStop(const double& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ImportBoneTracksRangeStop, double);
}

bool UInterchangeAnimSequenceFactoryNode::GetCustomSkeletonSoftObjectPath(FSoftObjectPath& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(SkeletonSoftObjectPath, FSoftObjectPath)
}

bool UInterchangeAnimSequenceFactoryNode::SetCustomSkeletonSoftObjectPath(const FSoftObjectPath& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SkeletonSoftObjectPath, FSoftObjectPath)
}

/************************************************************************/
/* Automation tests                                                     */
/************************************************************************/
#if WITH_EDITOR
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInterchangeAnimSequenceTest, "System.Runtime.Interchange.ConvertSampleRatetoFrameRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInterchangeAnimSequenceTest::RunTest(const FString& Parameters)
{
	FFrameRate FrameRate;
	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(120.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 120.0 to FFrameRate"), FrameRate.Numerator, 120);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 120.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(100.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 100.0 to FFrameRate"), FrameRate.Numerator, 100);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 100.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(60.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 60.0 to FFrameRate"), FrameRate.Numerator, 60);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 60.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(50.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 50.0 to FFrameRate"), FrameRate.Numerator, 50);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 50.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(48.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 48.0 to FFrameRate"), FrameRate.Numerator, 48);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 48.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(30.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 30.0 to FFrameRate"), FrameRate.Numerator, 30);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 30.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(29.97);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 29.97 to FFrameRate"), FrameRate.Numerator, 2997);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 29.97 to FFrameRate"), FrameRate.Denominator, 100);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(25.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 25.0 to FFrameRate"), FrameRate.Numerator, 25);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 25.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(24.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 24.0 to FFrameRate"), FrameRate.Numerator, 24);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 24.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(23.976);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 23.976 to FFrameRate"), FrameRate.Numerator, 2997);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 23.976 to FFrameRate"), FrameRate.Denominator, 125);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(96.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 96.0 to FFrameRate"), FrameRate.Numerator, 96);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 96.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(72.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 72.0 to FFrameRate"), FrameRate.Numerator, 72);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 72.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(59.94);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 59.94 to FFrameRate"), FrameRate.Numerator, 2997);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 59.94 to FFrameRate"), FrameRate.Denominator, 50);

	FrameRate = UE::Interchange::Animation::ConvertSampleRatetoFrameRate(119.88);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 119.88 to FFrameRate"), FrameRate.Numerator, 2997);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 119.88 to FFrameRate"), FrameRate.Denominator, 25);

	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
#endif //WITH_EDITOR
