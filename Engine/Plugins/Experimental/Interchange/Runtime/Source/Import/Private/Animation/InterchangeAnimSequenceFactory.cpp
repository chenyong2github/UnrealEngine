// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Animation/InterchangeAnimSequenceFactory.h"

#include "Animation/AnimSequence.h"
#include "Animation/InterchangeAnimationPayload.h"
#include "Animation/InterchangeAnimationPayloadInterface.h"
#include "InterchangeCommonPipelineDataFactoryNode.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeCommonAnimationPayload.h"
#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeSceneNode.h"
#include "InterchangeAnimSequenceFactoryNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Nodes/InterchangeSourceNode.h"

#if WITH_EDITORONLY_DATA

#include "EditorFramework/AssetImportData.h"

#endif //WITH_EDITORONLY_DATA

#if WITH_EDITOR
namespace UE::Interchange::Private
{
	void GetSkeletonSceneNodeFlatListRecursive(const UInterchangeBaseNodeContainer* NodeContainer, const FString& NodeUid, TArray<FString>& SkeletonSceneNodeUids)
	{
		SkeletonSceneNodeUids.Add(NodeUid);
		TArray<FString> Children = NodeContainer->GetNodeChildrenUids(NodeUid);
		for (const FString& ChildUid : Children)
		{
			GetSkeletonSceneNodeFlatListRecursive(NodeContainer, ChildUid, SkeletonSceneNodeUids);
		}
	}

	void RetrieveAnimationPayloads(UAnimSequence* AnimSequence
		, const UInterchangeAnimSequenceFactoryNode* AnimSequenceFactoryNode
		, const UInterchangeBaseNodeContainer* NodeContainer
		, const UInterchangeSkeletonFactoryNode* SkeletonFactoryNode
		, const USkeleton* Skeleton
		, const IInterchangeAnimationPayloadInterface* AnimSequenceTranslatorPayloadInterface
		, const FString& AssetName)
	{
		TMap<FString, TFuture<TOptional<UE::Interchange::FAnimationTransformPayloadData>>> AnimationPayloads;

		FString SkeletonRootUid;
		if (!SkeletonFactoryNode->GetCustomRootJointUid(SkeletonRootUid))
		{
			//Cannot import animation without a skeleton
			return;
		}
		bool bImportBoneTracks = false;
		AnimSequenceFactoryNode->GetCustomImportBoneTracks(bImportBoneTracks);
		if (bImportBoneTracks)
		{
			TArray<FString> SkeletonNodes;
			GetSkeletonSceneNodeFlatListRecursive(NodeContainer, SkeletonRootUid, SkeletonNodes);
			for (const FString& NodeUid : SkeletonNodes)
			{
				if (const UInterchangeSceneNode* SkeletonSceneNode = Cast<UInterchangeSceneNode>(NodeContainer->GetNode(NodeUid)))
				{
					FString PayloadKey;
					if (SkeletonSceneNode->GetCustomTransformCurvePayloadKey(PayloadKey))
					{
						AnimationPayloads.Add(PayloadKey, AnimSequenceTranslatorPayloadInterface->GetAnimationTransformPayloadData(PayloadKey));
					}
				}
			}
			//Get the sample rate, default to 30Hz in case the attribute is missing
			double SampleRate = 30.0;
			AnimSequenceFactoryNode->GetCustomImportBoneTracksSampleRate(SampleRate);

			double RangeStart = 0.0;
			AnimSequenceFactoryNode->GetCustomImportBoneTracksRangeStart(RangeStart);

			double RangeStop = 1.0 / SampleRate; //One frame duration per default
			AnimSequenceFactoryNode->GetCustomImportBoneTracksRangeStop(RangeStop);

			const double BakeInterval = 1.0 / SampleRate;
			const double SequenceLength = RangeStop - RangeStart;
			int32 BakeKeyCount = (SequenceLength / BakeInterval)+1;

			IAnimationDataController& Controller = AnimSequence->GetController();
			//This destroy all previously imported animation raw data
			Controller.RemoveAllBoneTracks();
			Controller.SetPlayLength(FGenericPlatformMath::Max<float>(SequenceLength, MINIMUM_ANIMATION_LENGTH));

			FTransform3f GlobalOffsetTransform;
			{
				FTransform TempTransform = FTransform::Identity;
				if (UInterchangeCommonPipelineDataFactoryNode* CommonPipelineDataFactoryNode = UInterchangeCommonPipelineDataFactoryNode::GetUniqueInstance(NodeContainer))
				{
					CommonPipelineDataFactoryNode->GetCustomGlobalOffsetTransform(TempTransform);
				}
				GlobalOffsetTransform = FTransform3f(TempTransform);
			}

			for (const FString& NodeUid : SkeletonNodes)
			{
				if (const UInterchangeSceneNode* SkeletonSceneNode = Cast<UInterchangeSceneNode>(NodeContainer->GetNode(NodeUid)))
				{
					const FName BoneName = FName(*(SkeletonSceneNode->GetDisplayLabel()));
					const int32 BoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);
					if (BoneIndex == INDEX_NONE)
					{
						//Skip this bone, we did not found it in the skeleton
						continue;
					}
					//If we are getting the root 
					bool bApplyGlobalOffset = NodeUid.Equals(SkeletonRootUid);

					FString PayloadKey;
					if (SkeletonSceneNode->GetCustomTransformCurvePayloadKey(PayloadKey))
					{
						TOptional<UE::Interchange::FAnimationTransformPayloadData> AnimationTransformPayload = AnimationPayloads.FindChecked(PayloadKey).Get();
						if (!AnimationTransformPayload.IsSet())
						{
							UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid animation transform payload key [%s] AnimSequence asset %s"), *PayloadKey, *AssetName);
							continue;
						}

						FRawAnimSequenceTrack RawTrack;
						RawTrack.PosKeys.Reserve(BakeKeyCount);
						RawTrack.RotKeys.Reserve(BakeKeyCount);
						RawTrack.ScaleKeys.Reserve(BakeKeyCount);
						TArray<float> TimeKeys;
						TimeKeys.Reserve(BakeKeyCount);
						for (double CurrentTime = RangeStart; CurrentTime <= RangeStop + SMALL_NUMBER; CurrentTime += BakeInterval)
						{
							//Default value to identity
							FVector3f Position(0.0f);
							FVector3f Euler(0.0f);
							FVector3f Scale(1.0f);
							//Fill the anim sequence raw data
							for (int32 CurveIndex = 0; CurveIndex < AnimationTransformPayload->TransformCurves.Num(); ++CurveIndex)
							{
								const UE::Interchange::FAnimationCurveTransformPayloadData& CurveData = AnimationTransformPayload->TransformCurves[CurveIndex];
								if (CurveData.Curve.GetNumKeys() <= 0)
								{
									//skip empty curve, the default value will be applied
									continue;
								}

								if (CurveData.TransformChannel == EInterchangeTransformCurveChannel::TranslationX)
								{
									Position.X = CurveData.Curve.Eval(CurrentTime);
								}
								else if (CurveData.TransformChannel == EInterchangeTransformCurveChannel::TranslationY)
								{
									Position.Y = CurveData.Curve.Eval(CurrentTime);
								}
								else if (CurveData.TransformChannel == EInterchangeTransformCurveChannel::TranslationZ)
								{
									Position.Z = CurveData.Curve.Eval(CurrentTime);
								}
								else if (CurveData.TransformChannel == EInterchangeTransformCurveChannel::EulerX)
								{
									Euler.X = CurveData.Curve.Eval(CurrentTime);
								}
								else if (CurveData.TransformChannel == EInterchangeTransformCurveChannel::EulerY)
								{
									Euler.Y = CurveData.Curve.Eval(CurrentTime);
								}
								else if (CurveData.TransformChannel == EInterchangeTransformCurveChannel::EulerZ)
								{
									Euler.Z = CurveData.Curve.Eval(CurrentTime);
								}
								else if (CurveData.TransformChannel == EInterchangeTransformCurveChannel::ScaleX)
								{
									Scale.X = CurveData.Curve.Eval(CurrentTime);
								}
								else if (CurveData.TransformChannel == EInterchangeTransformCurveChannel::ScaleY)
								{
									Scale.Y = CurveData.Curve.Eval(CurrentTime);
								}
								else if (CurveData.TransformChannel == EInterchangeTransformCurveChannel::ScaleZ)
								{
									Scale.Z = CurveData.Curve.Eval(CurrentTime);
								}
							}
							FQuat4f Quaternion = FQuat4f::MakeFromEuler(Euler);
							
							if (bApplyGlobalOffset)
							{
								FTransform3f AnimKeyTransform(Quaternion, Position, Scale);
								AnimKeyTransform = AnimKeyTransform * GlobalOffsetTransform;
								Position = AnimKeyTransform.GetLocation();
								Quaternion = AnimKeyTransform.GetRotation();
								Scale = AnimKeyTransform.GetScale3D();
							}
							RawTrack.ScaleKeys.Add(Scale);
							RawTrack.PosKeys.Add(Position);
							RawTrack.RotKeys.Add(Quaternion);
							//Animation are always translated to zero
							TimeKeys.Add(CurrentTime - RangeStart);
						}

						//Make sure we create the correct amount of keys
						check(RawTrack.ScaleKeys.Num() == BakeKeyCount
							&& RawTrack.PosKeys.Num() == BakeKeyCount
							&& RawTrack.RotKeys.Num() == BakeKeyCount
							&& TimeKeys.Num() == BakeKeyCount);

						//add new track
						Controller.AddBoneTrack(BoneName);
						Controller.SetBoneTrackKeys(BoneName, RawTrack.PosKeys, RawTrack.RotKeys, RawTrack.ScaleKeys);
					}
				}
			}
		}
	}

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

} //namespace UE::Interchange::Private
#endif //WITH_EDITOR

UClass* UInterchangeAnimSequenceFactory::GetFactoryClass() const
{
	return UAnimSequence::StaticClass();
}

UObject* UInterchangeAnimSequenceFactory::CreateEmptyAsset(const FCreateAssetParams& Arguments)
{
#if !WITH_EDITOR || !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import animsequence asset in runtime, this is an editor only feature."));
	return nullptr;
#else
	UAnimSequence* AnimSequence = nullptr;
	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeAnimSequenceFactoryNode* AnimSequenceFactoryNode = Cast<UInterchangeAnimSequenceFactoryNode>(Arguments.AssetNode);
	if (AnimSequenceFactoryNode == nullptr)
	{
		return nullptr;
	}

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		AnimSequence = NewObject<UAnimSequence>(Arguments.Parent, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if (ExistingAsset->GetClass()->IsChildOf(UAnimSequence::StaticClass()))
	{
		//This is a reimport, we are just re-updating the source data
		AnimSequence = Cast<UAnimSequence>(ExistingAsset);
	}

	if (!AnimSequence)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("Could not create AnimSequence asset %s"), *Arguments.AssetName);
		return nullptr;
	}

	AnimSequence->PreEditChange(nullptr);

	return AnimSequence;
#endif //else !WITH_EDITOR || !WITH_EDITORONLY_DATA
}

UObject* UInterchangeAnimSequenceFactory::CreateAsset(const FCreateAssetParams& Arguments)
{
#if !WITH_EDITOR || !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import AnimSequence asset in runtime, this is an editor only feature."));
	return nullptr;

#else

	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	UInterchangeAnimSequenceFactoryNode* AnimSequenceFactoryNode = Cast<UInterchangeAnimSequenceFactoryNode>(Arguments.AssetNode);
	if (AnimSequenceFactoryNode == nullptr)
	{
		return nullptr;
	}

	FString SkeletonUid;
	if (!AnimSequenceFactoryNode->GetCustomSkeletonFactoryNodeUid(SkeletonUid))
	{
		//Do not create a empty anim sequence, we need skeleton that contain animation
		return nullptr;
	}

	const UInterchangeSkeletonFactoryNode* SkeletonFactoryNode = Cast<UInterchangeSkeletonFactoryNode>(Arguments.NodeContainer->GetNode(SkeletonUid));
	if (!SkeletonFactoryNode)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid skeleton factory node, the skeleton factory node is obligatory to import this animsequence [%s]!"), *Arguments.AssetName);
		return nullptr;
	}

	USkeleton* Skeleton = nullptr;

	FSoftObjectPath SpecifiedSkeleton;
	AnimSequenceFactoryNode->GetCustomSkeletonSoftObjectPath(SpecifiedSkeleton);
	if (Skeleton == nullptr)
	{
		UObject* SkeletonObject = nullptr;

		if (SpecifiedSkeleton.IsValid())
		{
			SkeletonObject = SpecifiedSkeleton.TryLoad();
		}
		else if (SkeletonFactoryNode->ReferenceObject.IsValid())
		{
			SkeletonObject = SkeletonFactoryNode->ReferenceObject.TryLoad();
		}

		if (SkeletonObject)
		{
			Skeleton = Cast<USkeleton>(SkeletonObject);

		}

		if (!ensure(Skeleton))
		{
			UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeleton when importing animation sequence asset %s"), *Arguments.AssetName);
			return nullptr;
		}
	}

	const IInterchangeAnimationPayloadInterface* AnimSequenceTranslatorPayloadInterface = Cast<IInterchangeAnimationPayloadInterface>(Arguments.Translator);
	if (!AnimSequenceTranslatorPayloadInterface)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import AnimSequence, the translator do not implement the IInterchangeAnimationPayloadInterface."));
		return nullptr;
	}

	const UClass* AnimSequenceClass = AnimSequenceFactoryNode->GetObjectClass();
	check(AnimSequenceClass && AnimSequenceClass->IsChildOf(GetFactoryClass()));

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	UObject* AnimSequenceObject = nullptr;
	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		//NewObject is not thread safe, the asset registry directory watcher tick on the main thread can trig before we finish initializing the UObject and will crash
		//The UObject should have been create by calling CreateEmptyAsset on the main thread.
		check(IsInGameThread());
		AnimSequenceObject = NewObject<UObject>(Arguments.Parent, AnimSequenceClass, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if (ExistingAsset->GetClass()->IsChildOf(AnimSequenceClass))
	{
		//This is a reimport, we are just re-updating the source data
		AnimSequenceObject = ExistingAsset;
	}

	if (!AnimSequenceObject)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Could not create AnimSequence asset %s"), *Arguments.AssetName);
		return nullptr;
	}

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimSequenceObject);

	const bool bIsReImport = (Arguments.ReimportObject != nullptr);

	if (!ensure(AnimSequence))
	{
		if (!bIsReImport)
		{
			UE_LOG(LogInterchangeImport, Error, TEXT("Could not create AnimSequence asset %s"), *Arguments.AssetName);
		}
		else
		{
			UE_LOG(LogInterchangeImport, Error, TEXT("Could not find reimported AnimSequence asset %s"), *Arguments.AssetName);
		}
		return nullptr;
	}

	//Fill the animsequence data, we need to retrieve the skeleton and then ask the payload for every joint
	{
		FFrameRate FrameRate(30, 1);
		double SampleRate = 30.0;
		
		//If we import bone track we will set the framerate according to the bone track settings
		//The default is 30Hz when there is no bone tracks, i.e. like when we just have morphtarget attribute curve animation.
		//Curve do not need framerate, framerate is needed if we want to bake curve animation data.
		//TODO: discuss with animation team so we can pass curves for bones transform animation instead of baked data.
		bool bImportBoneTracks = false;
		if (AnimSequenceFactoryNode->GetCustomImportBoneTracks(bImportBoneTracks) && bImportBoneTracks)
		{
			if (AnimSequenceFactoryNode->GetCustomImportBoneTracksSampleRate(SampleRate))
			{
				FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(SampleRate);
			}
		}

		IAnimationDataController& Controller = AnimSequence->GetController();
		Controller.OpenBracket(NSLOCTEXT("InterchangeAnimSequenceFactory", "ImportAnimationInterchange_Bracket", "Importing Animation (Interchange)"));
		AnimSequence->SetSkeleton(Skeleton);
		AnimSequence->ImportFileFramerate = SampleRate;
		AnimSequence->ImportResampleFramerate = SampleRate;
		Controller.SetFrameRate(FrameRate);

		UE::Interchange::Private::RetrieveAnimationPayloads(AnimSequence
			, AnimSequenceFactoryNode
			, Arguments.NodeContainer
			, SkeletonFactoryNode
			, Skeleton
			, AnimSequenceTranslatorPayloadInterface
			, Arguments.AssetName);

		Controller.NotifyPopulated();
		Controller.CloseBracket(false);
	}

	if (!bIsReImport)
	{
		/** Apply all AnimSequenceFactoryNode custom attributes to the skeletal mesh asset */
		AnimSequenceFactoryNode->ApplyAllCustomAttributeToObject(AnimSequence);
	}
	else
	{
		//Apply the re import strategy 
		UInterchangeAssetImportData* InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(AnimSequence->AssetImportData);
		UInterchangeFactoryBaseNode* PreviousNode = nullptr;
		if (InterchangeAssetImportData)
		{
			PreviousNode = InterchangeAssetImportData->NodeContainer->GetFactoryNode(InterchangeAssetImportData->NodeUniqueID);
		}
		UInterchangeFactoryBaseNode* CurrentNode = NewObject<UInterchangeAnimSequenceFactoryNode>(GetTransientPackage());
		UInterchangeBaseNode::CopyStorage(AnimSequenceFactoryNode, CurrentNode);
		CurrentNode->FillAllCustomAttributeFromObject(AnimSequence);
		UE::Interchange::FFactoryCommon::ApplyReimportStrategyToAsset(AnimSequence, PreviousNode, CurrentNode, AnimSequenceFactoryNode);
	}

	//Getting the file Hash will cache it into the source data
	Arguments.SourceData->GetFileContentHash();

	//The interchange completion task (call in the GameThread after the factories pass), will call PostEditChange which will trig another asynchronous system that will build all material in parallel
	return AnimSequenceObject;

#endif //else !WITH_EDITOR || !WITH_EDITORONLY_DATA
}

/* This function is call in the completion task on the main thread, use it to call main thread post creation step for your assets*/
void UInterchangeAnimSequenceFactory::PreImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments)
{
	check(IsInGameThread());
	Super::PreImportPreCompletedCallback(Arguments);

	// TODO: make sure this works at runtime
#if WITH_EDITORONLY_DATA
	if (ensure(Arguments.ImportedObject && Arguments.SourceData))
	{
		// We must call the Update of the asset source file in the main thread because UAssetImportData::Update execute some delegate we do not control
		UAnimSequence* AnimSequence = CastChecked<UAnimSequence>(Arguments.ImportedObject);

		UAssetImportData* ImportDataPtr = AnimSequence->AssetImportData;
		UE::Interchange::FFactoryCommon::FUpdateImportAssetDataParameters UpdateImportAssetDataParameters(AnimSequence, ImportDataPtr, Arguments.SourceData, Arguments.NodeUniqueID, Arguments.NodeContainer, Arguments.Pipelines);
		ImportDataPtr = UE::Interchange::FFactoryCommon::UpdateImportAssetData(UpdateImportAssetDataParameters);
		AnimSequence->AssetImportData = ImportDataPtr;
	}
#endif
}

bool UInterchangeAnimSequenceFactory::GetSourceFilenames(const UObject* Object, TArray<FString>& OutSourceFilenames) const
{
#if WITH_EDITORONLY_DATA
	if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(Object))
	{
		return UE::Interchange::FFactoryCommon::GetSourceFilenames(AnimSequence->AssetImportData.Get(), OutSourceFilenames);
	}
#endif

	return false;
}

bool UInterchangeAnimSequenceFactory::SetSourceFilename(const UObject* Object, const FString& SourceFilename, int32 SourceIndex) const
{
#if WITH_EDITORONLY_DATA
	if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(Object))
	{
		return UE::Interchange::FFactoryCommon::SetSourceFilename(AnimSequence->AssetImportData.Get(), SourceFilename, SourceIndex);
	}
#endif

	return false;
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
	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(120.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 120.0 to FFrameRate"), FrameRate.Numerator, 120);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 120.0 to FFrameRate"), FrameRate.Denominator, 1);
	
	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(100.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 100.0 to FFrameRate"), FrameRate.Numerator, 100);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 100.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(60.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 60.0 to FFrameRate"), FrameRate.Numerator, 60);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 60.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(50.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 50.0 to FFrameRate"), FrameRate.Numerator, 50);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 50.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(48.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 48.0 to FFrameRate"), FrameRate.Numerator, 48);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 48.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(30.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 30.0 to FFrameRate"), FrameRate.Numerator, 30);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 30.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(29.97);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 29.97 to FFrameRate"), FrameRate.Numerator, 2997);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 29.97 to FFrameRate"), FrameRate.Denominator, 100);

	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(25.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 25.0 to FFrameRate"), FrameRate.Numerator, 25);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 25.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(24.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 24.0 to FFrameRate"), FrameRate.Numerator, 24);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 24.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(23.976);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 23.976 to FFrameRate"), FrameRate.Numerator, 2997);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 23.976 to FFrameRate"), FrameRate.Denominator, 125);

	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(96.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 96.0 to FFrameRate"), FrameRate.Numerator, 96);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 96.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(72.0);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 72.0 to FFrameRate"), FrameRate.Numerator, 72);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 72.0 to FFrameRate"), FrameRate.Denominator, 1);

	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(59.94);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 59.94 to FFrameRate"), FrameRate.Numerator, 2997);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 59.94 to FFrameRate"), FrameRate.Denominator, 50);

	FrameRate = UE::Interchange::Private::ConvertSampleRatetoFrameRate(119.88);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 119.88 to FFrameRate"), FrameRate.Numerator, 2997);
	TestEqual(TEXT("`ConvertSampleRatetoFrameRate` Error converting 119.88 to FFrameRate"), FrameRate.Denominator, 25);
	
	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
#endif //WITH_EDITOR
