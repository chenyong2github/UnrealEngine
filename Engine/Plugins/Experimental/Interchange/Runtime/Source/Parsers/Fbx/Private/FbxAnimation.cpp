// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxAnimation.h"

#include "CoreMinimal.h"
#include "FbxAPI.h"
#include "FbxConvert.h"
#include "FbxHelper.h"
#include "FbxInclude.h"
#include "Fbx/InterchangeFbxMessages.h"
#include "InterchangeCommonAnimationPayload.h"
#include "InterchangeMeshNode.h"
#include "InterchangeResultsContainer.h"
#include "InterchangeSceneNode.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Serialization/LargeMemoryWriter.h"
#include "SkeletalMeshAttributes.h"
#include "StaticMeshAttributes.h"


#define LOCTEXT_NAMESPACE "InterchangeFbxMesh"

namespace UE::Interchange::Private
{
	float GetTransformChannelValue(const EInterchangeTransformCurveChannel Channel, const FTransform& Transform)
	{
		switch (Channel)
		{
			case EInterchangeTransformCurveChannel::TranslationX:
			{
				return Transform.GetLocation().X;
			}
			break;
			case EInterchangeTransformCurveChannel::TranslationY:
			{
				return Transform.GetLocation().Y;
			}
			break;
			case EInterchangeTransformCurveChannel::TranslationZ:
			{
				return Transform.GetLocation().Z;
			}
			break;
			case EInterchangeTransformCurveChannel::EulerX:
			{
				return Transform.GetRotation().Euler().X;
			}
			break;
			case EInterchangeTransformCurveChannel::EulerY:
			{
				return Transform.GetRotation().Euler().Y;
			}
			break;
			case EInterchangeTransformCurveChannel::EulerZ:
			{
				return Transform.GetRotation().Euler().Z;
			}
			break;
			case EInterchangeTransformCurveChannel::ScaleX:
			{
				return Transform.GetScale3D().X;
			}
			break;
			case EInterchangeTransformCurveChannel::ScaleY:
			{
				return Transform.GetScale3D().Y;
			}
			break;
			case EInterchangeTransformCurveChannel::ScaleZ:
			{
				return Transform.GetScale3D().Z;
			}
			break;
		}
		return 0.0f;
	}

	bool ImportCurve(FbxNode* Node, const EInterchangeTransformCurveChannel TransformChannel, const FbxAnimCurve* SourceFloatCurves, const float ScaleValue, TArray<FInterchangeCurveKey>& DestinationFloatCurve)
	{
		bool bUseNodeTransform = false;
		if (TransformChannel != EInterchangeTransformCurveChannel::None)
		{
			bUseNodeTransform = Node != nullptr;
		}

		if (!SourceFloatCurves)
		{
			return true;
		}
		const float DefaultCurveWeight = FbxAnimCurveDef::sDEFAULT_WEIGHT;
		//We use the non const to query the left and right derivative of the key, for whatever reason those FBX API functions are not const
		FbxAnimCurve* NonConstSourceFloatCurves = const_cast<FbxAnimCurve*>(SourceFloatCurves);
		int32 KeyCount = SourceFloatCurves->KeyGetCount();
		for (int32 KeyIndex = 0; KeyIndex < KeyCount; ++KeyIndex)
		{
			FbxAnimCurveKey Key = SourceFloatCurves->KeyGet(KeyIndex);
			FbxTime KeyTime = Key.GetTime();
			const float KeyTimeValue = static_cast<float>(KeyTime.GetSecondDouble());
			float ValueAtTime = Key.GetValue();
			if (bUseNodeTransform)
			{
				FbxAMatrix NodeTransform = Node->EvaluateGlobalTransform(KeyTime);
				FbxNode* ParentNode = Node->GetParent();
				if (ParentNode)
				{
					FbxAMatrix ParentTransform = ParentNode->EvaluateGlobalTransform(KeyTime);
					NodeTransform = ParentTransform.Inverse() * NodeTransform;
				}
				FTransform Transform = UE::Interchange::Private::FFbxConvert::ConvertTransform(NodeTransform);
				ValueAtTime = GetTransformChannelValue(TransformChannel, Transform);
			}
			float Value = ValueAtTime * ScaleValue;
			FInterchangeCurveKey& InterchangeCurveKey = DestinationFloatCurve.AddDefaulted_GetRef();
			InterchangeCurveKey.Time = KeyTimeValue;
			InterchangeCurveKey.Value = Value;

			const bool bIncludeOverrides = true;
			FbxAnimCurveDef::ETangentMode KeyTangentMode = Key.GetTangentMode(bIncludeOverrides);
			FbxAnimCurveDef::EInterpolationType KeyInterpMode = Key.GetInterpolation();
			FbxAnimCurveDef::EWeightedMode KeyTangentWeightMode = Key.GetTangentWeightMode();

			EInterchangeCurveInterpMode NewInterpMode = EInterchangeCurveInterpMode::Linear;
			EInterchangeCurveTangentMode NewTangentMode = EInterchangeCurveTangentMode::Auto;
			EInterchangeCurveTangentWeightMode NewTangentWeightMode = EInterchangeCurveTangentWeightMode::WeightedNone;

			float RightTangent = NonConstSourceFloatCurves->KeyGetRightDerivative(KeyIndex) * ScaleValue;
			float LeftTangent = NonConstSourceFloatCurves->KeyGetLeftDerivative(KeyIndex) * ScaleValue;
			float RightTangentWeight = 0.0f;
			float LeftTangentWeight = 0.0f; //This one is dependent on the previous key.
			bool bLeftWeightActive = false;
			bool bRightWeightActive = false;

			const bool bPreviousKeyValid = KeyIndex > 0;
			const bool bNextKeyValid = KeyIndex < KeyCount - 1;
			float PreviousValue = 0.0f;
			float PreviousKeyTimeValue = 0.0f;
			float NextValue = 0.0f;
			float NextKeyTimeValue = 0.0f;
			if (bPreviousKeyValid)
			{
				FbxAnimCurveKey PreviousKey = SourceFloatCurves->KeyGet(KeyIndex - 1);
				FbxTime PreviousKeyTime = PreviousKey.GetTime();
				PreviousKeyTimeValue = static_cast<float>(PreviousKeyTime.GetSecondDouble());
				PreviousValue = PreviousKey.GetValue() * ScaleValue;
				//The left tangent is driven by the previous key. If the previous key have a the NextLeftweight or both flag weighted mode, it mean the next key is weighted on the left side
				bLeftWeightActive = (PreviousKey.GetTangentWeightMode() & FbxAnimCurveDef::eWeightedNextLeft) > 0;
				if (bLeftWeightActive)
				{
					LeftTangentWeight = PreviousKey.GetDataFloat(FbxAnimCurveDef::eNextLeftWeight);
				}
			}
			if (bNextKeyValid)
			{
				FbxAnimCurveKey NextKey = SourceFloatCurves->KeyGet(KeyIndex + 1);
				FbxTime NextKeyTime = NextKey.GetTime();
				NextKeyTimeValue = static_cast<float>(NextKeyTime.GetSecondDouble());
				NextValue = NextKey.GetValue() * ScaleValue;

				bRightWeightActive = (KeyTangentWeightMode & FbxAnimCurveDef::eWeightedRight) > 0;
				if (bRightWeightActive)
				{
					//The right tangent weight should be use only if we are not the last key since the last key do not have a right tangent.
					//Use the current key to gather the right tangent weight
					RightTangentWeight = Key.GetDataFloat(FbxAnimCurveDef::eRightWeight);
				}
			}

			// When this flag is true, the tangent is flat if the value has the same value as the previous or next key.
			const bool bTangentGenericClamp = (KeyTangentMode & FbxAnimCurveDef::eTangentGenericClamp);

			//Time independent tangent this is consider has a spline tangent key
			const bool bTangentGenericTimeIndependent = (KeyTangentMode & FbxAnimCurveDef::ETangentMode::eTangentGenericTimeIndependent);

			// When this flag is true, the tangent is flat if the value is outside of the [previous key, next key] value range.
			//Clamp progressive is (eTangentGenericClampProgressive |eTangentGenericTimeIndependent)
			const bool bTangentGenericClampProgressive = (KeyTangentMode & FbxAnimCurveDef::ETangentMode::eTangentGenericClampProgressive) == FbxAnimCurveDef::ETangentMode::eTangentGenericClampProgressive;

			if (KeyTangentMode & FbxAnimCurveDef::eTangentGenericBreak)
			{
				NewTangentMode = EInterchangeCurveTangentMode::Break;
			}
			else if (KeyTangentMode & FbxAnimCurveDef::eTangentUser)
			{
				NewTangentMode = EInterchangeCurveTangentMode::User;
			}

			switch (KeyInterpMode)
			{
			case FbxAnimCurveDef::eInterpolationConstant://! Constant value until next key.
				NewInterpMode = EInterchangeCurveInterpMode::Constant;
				break;
			case FbxAnimCurveDef::eInterpolationLinear://! Linear progression to next key.
				NewInterpMode = EInterchangeCurveInterpMode::Linear;
				break;
			case FbxAnimCurveDef::eInterpolationCubic://! Cubic progression to next key.
				NewInterpMode = EInterchangeCurveInterpMode::Cubic;
				// get tangents
				{
					bool bIsFlatTangent = false;
					bool bIsComputedTangent = false;
					if (bTangentGenericClampProgressive)
					{
						if (bPreviousKeyValid && bNextKeyValid)
						{
							const float PreviousNextHalfDelta = (NextValue - PreviousValue) * 0.5f;
							const float PreviousNextAverage = PreviousValue + PreviousNextHalfDelta;
							// If the value is outside of the previous-next value range, the tangent is flat.
							bIsFlatTangent = FMath::Abs(Value - PreviousNextAverage) >= FMath::Abs(PreviousNextHalfDelta);
						}
						else
						{
							//Start/End tangent with the ClampProgressive flag are flat.
							bIsFlatTangent = true;
						}
					}
					else if (bTangentGenericClamp && (bPreviousKeyValid || bNextKeyValid))
					{
						if (bPreviousKeyValid && PreviousValue == Value)
						{
							bIsFlatTangent = true;
						}
						if (bNextKeyValid)
						{
							bIsFlatTangent |= Value == NextValue;
						}
					}
					else if (bTangentGenericTimeIndependent)
					{
						//Spline tangent key, because bTangentGenericClampProgressive include bTangentGenericTimeIndependent, we must treat this case after bTangentGenericClampProgressive
						if (KeyCount == 1)
						{
							bIsFlatTangent = true;
						}
						else
						{
							//Spline tangent key must be User mode since we want to keep the tangents provide by the fbx key left and right derivatives
							NewTangentMode = EInterchangeCurveTangentMode::User;
						}
					}

					if (bIsFlatTangent)
					{
						RightTangent = 0;
						LeftTangent = 0;
						//To force flat tangent we need to set the tangent mode to user
						NewTangentMode = EInterchangeCurveTangentMode::User;
					}

				}
				break;
			}

			//auto with weighted give the wrong result, so when auto is weighted we set user mode and set the Right tangent equal to the left tangent.
			//Auto has only the left tangent set
			if (NewTangentMode == EInterchangeCurveTangentMode::Auto && (bLeftWeightActive || bRightWeightActive))
			{

				NewTangentMode = EInterchangeCurveTangentMode::User;
				RightTangent = LeftTangent;
			}

			if (NewTangentMode != EInterchangeCurveTangentMode::Auto)
			{
				const bool bEqualTangents = FMath::IsNearlyEqual(LeftTangent, RightTangent);
				//If tangents are different then broken.
				if (bEqualTangents)
				{
					NewTangentMode = EInterchangeCurveTangentMode::User;
				}
				else
				{
					NewTangentMode = EInterchangeCurveTangentMode::Break;
				}
			}

			//Only cubic interpolation allow weighted tangents
			if (KeyInterpMode == FbxAnimCurveDef::eInterpolationCubic)
			{
				if (bLeftWeightActive && bRightWeightActive)
				{
					NewTangentWeightMode = EInterchangeCurveTangentWeightMode::WeightedBoth;
				}
				else if (bLeftWeightActive)
				{
					NewTangentWeightMode = EInterchangeCurveTangentWeightMode::WeightedArrive;
					RightTangentWeight = DefaultCurveWeight;
				}
				else if (bRightWeightActive)
				{
					NewTangentWeightMode = EInterchangeCurveTangentWeightMode::WeightedLeave;
					LeftTangentWeight = DefaultCurveWeight;
				}
				else
				{
					NewTangentWeightMode = EInterchangeCurveTangentWeightMode::WeightedNone;
					LeftTangentWeight = DefaultCurveWeight;
					RightTangentWeight = DefaultCurveWeight;
				}

				auto ComputeWeightInternal = [](float TimeA, float TimeB, const float TangentSlope, const float TangentWeight)
				{
					const float X = TimeA - TimeB;
					const float Y = TangentSlope * X;
					return FMath::Sqrt(X * X + Y * Y) * TangentWeight;
				};

				if (!FMath::IsNearlyZero(LeftTangentWeight))
				{
					if (bPreviousKeyValid)
					{
						LeftTangentWeight = ComputeWeightInternal(KeyTimeValue, PreviousKeyTimeValue, LeftTangent, LeftTangentWeight);
					}
					else
					{
						LeftTangentWeight = 0.0f;
					}
				}

				if (!FMath::IsNearlyZero(RightTangentWeight))
				{
					if (bNextKeyValid)
					{
						RightTangentWeight = ComputeWeightInternal(NextKeyTimeValue, KeyTimeValue, RightTangent, RightTangentWeight);
					}
					else
					{
						RightTangentWeight = 0.0f;
					}
				}
			}

			const bool bForceDisableTangentRecompute = false; //No need to recompute all the tangents of the curve every time we change de key.
			InterchangeCurveKey.InterpMode = NewInterpMode;
			InterchangeCurveKey.TangentMode = NewTangentMode;
			InterchangeCurveKey.TangentWeightMode = NewTangentWeightMode;

			InterchangeCurveKey.ArriveTangent = LeftTangent;
			InterchangeCurveKey.LeaveTangent = RightTangent;
			InterchangeCurveKey.ArriveTangentWeight = LeftTangentWeight;
			InterchangeCurveKey.LeaveTangentWeight = RightTangentWeight;
		}
		return true;
	}

	bool ImportCurve(const FbxAnimCurve* SourceFloatCurves, const float ScaleValue, TArray<FInterchangeCurveKey>& DestinationFloatCurve)
	{
		return ImportCurve(nullptr, EInterchangeTransformCurveChannel::None, SourceFloatCurves, ScaleValue, DestinationFloatCurve);
	}

	bool ImportTranslationCurves(FbxNode* Node, TArray<FbxAnimCurve*>& SourceTransformChannelCurves, TArray<FInterchangeCurve>& TransformChannelCurves)
	{
		bool bResult = true;
		//Translation X
		bResult &= ImportCurve(Node, EInterchangeTransformCurveChannel::TranslationX, SourceTransformChannelCurves[0], 1.0f, TransformChannelCurves[0].Keys);
		TransformChannelCurves[0].TransformChannel = EInterchangeTransformCurveChannel::TranslationX;
		//Translation Y
		bResult &= ImportCurve(Node, EInterchangeTransformCurveChannel::TranslationY, SourceTransformChannelCurves[1], 1.0f, TransformChannelCurves[1].Keys);
		TransformChannelCurves[1].TransformChannel = EInterchangeTransformCurveChannel::TranslationY;
		//Translation Z
		bResult &= ImportCurve(Node, EInterchangeTransformCurveChannel::TranslationZ, SourceTransformChannelCurves[2], 1.0f, TransformChannelCurves[2].Keys);
		TransformChannelCurves[2].TransformChannel = EInterchangeTransformCurveChannel::TranslationZ;

		return bResult;
	}
	
	bool ImportRotationCurves(FbxNode* Node, TArray<FbxAnimCurve*>& SourceTransformChannelCurves, TArray<FInterchangeCurve>& TransformChannelCurves)
	{
		bool bResult = true;
		//Euler X
		bResult &= ImportCurve(Node, EInterchangeTransformCurveChannel::EulerX, SourceTransformChannelCurves[3], 1.0f, TransformChannelCurves[3].Keys);
		TransformChannelCurves[3].TransformChannel = EInterchangeTransformCurveChannel::EulerX;
		//Euler Y
		bResult &= ImportCurve(Node, EInterchangeTransformCurveChannel::EulerY, SourceTransformChannelCurves[4], 1.0f, TransformChannelCurves[4].Keys);
		TransformChannelCurves[4].TransformChannel = EInterchangeTransformCurveChannel::EulerY;
		//Euler Z
		bResult &= ImportCurve(Node, EInterchangeTransformCurveChannel::EulerZ, SourceTransformChannelCurves[5], 1.0f, TransformChannelCurves[5].Keys);
		TransformChannelCurves[5].TransformChannel = EInterchangeTransformCurveChannel::EulerZ;

		return bResult;
	}
	
	bool ImportScaleCurves(FbxNode* Node, TArray<FbxAnimCurve*>& SourceTransformChannelCurves, TArray<FInterchangeCurve>& TransformChannelCurves)
	{
		bool bResult = true;
		//Scale X
		bResult &= ImportCurve(Node, EInterchangeTransformCurveChannel::ScaleX, SourceTransformChannelCurves[6], 1.0f, TransformChannelCurves[6].Keys);
		TransformChannelCurves[6].TransformChannel = EInterchangeTransformCurveChannel::ScaleX;
		//Scale Y
		bResult &= ImportCurve(Node, EInterchangeTransformCurveChannel::ScaleY, SourceTransformChannelCurves[7], 1.0f, TransformChannelCurves[7].Keys);
		TransformChannelCurves[7].TransformChannel = EInterchangeTransformCurveChannel::ScaleY;
		//Scale Z
		bResult &= ImportCurve(Node, EInterchangeTransformCurveChannel::ScaleZ, SourceTransformChannelCurves[8], 1.0f, TransformChannelCurves[8].Keys);
		TransformChannelCurves[8].TransformChannel = EInterchangeTransformCurveChannel::ScaleZ;

		return bResult;
	}

	bool ImportTransformCurves(FbxNode* Node
		, TArray<FbxAnimCurve*>& SourceTransformChannelCurves
		, TArray<FInterchangeCurve>& TransformChannelCurves)
	{
		bool bResult = true;
		bResult &= ImportTranslationCurves(Node, SourceTransformChannelCurves, TransformChannelCurves);
		bResult &= ImportRotationCurves(Node, SourceTransformChannelCurves, TransformChannelCurves);
		bResult &= ImportScaleCurves(Node, SourceTransformChannelCurves, TransformChannelCurves);
		return bResult;
	}

	bool ImportBakeTransforms(FbxNode* Node, FAnimationBakeTransformPayloadData& AnimationBakeTransformPayloadData)
	{
		if (!ensure(!FMath::IsNearlyZero(AnimationBakeTransformPayloadData.BakeFrequency)))
		{
			return false;
		}
		
		FbxTime StartTime;
		StartTime.SetSecondDouble(AnimationBakeTransformPayloadData.RangeStartTime);
		FbxTime EndTime;
		EndTime.SetSecondDouble(AnimationBakeTransformPayloadData.RangeEndTime);
		if (!ensure(AnimationBakeTransformPayloadData.RangeEndTime > AnimationBakeTransformPayloadData.RangeStartTime))
		{
			return false;
		}

		const double TimeStepSecond = 1.0 / AnimationBakeTransformPayloadData.BakeFrequency;
		FbxTime TimeStep = 0;
		TimeStep.SetSecondDouble(TimeStepSecond);

		const int32 NumFrame = FMath::RoundToInt32((AnimationBakeTransformPayloadData.RangeEndTime - AnimationBakeTransformPayloadData.RangeStartTime * AnimationBakeTransformPayloadData.BakeFrequency));
		
		//Add a threshold when we compare if we have reach the end of the animation
		const FbxTime TimeComparisonThreshold = (UE_DOUBLE_KINDA_SMALL_NUMBER * static_cast<double>(FBXSDK_TC_SECOND));
		AnimationBakeTransformPayloadData.Transforms.Empty(NumFrame);

		for (FbxTime CurTime = StartTime; CurTime < (EndTime + TimeComparisonThreshold); CurTime += TimeStep)
		{
			FbxAMatrix NodeTransform = Node->EvaluateGlobalTransform(CurTime);
			FbxNode* ParentNode = Node->GetParent();
			if (ParentNode)
			{
				FbxAMatrix ParentTransform = ParentNode->EvaluateGlobalTransform(CurTime);
				NodeTransform = ParentTransform.Inverse() * NodeTransform;
			}
			AnimationBakeTransformPayloadData.Transforms.Add(UE::Interchange::Private::FFbxConvert::ConvertTransform(NodeTransform));
		}

		return true;
	}

	struct FGetFbxTransformCurvesParameters
	{
		FGetFbxTransformCurvesParameters(FbxScene* InSDKScene, FbxNode* InNode)
		{
			SDKScene = InSDKScene;
			check(SDKScene);
			Node = InNode;
			check(Node);
		}

		FbxScene* SDKScene = nullptr;
		FbxNode* Node = nullptr;
		TArray<FbxAnimCurve*> TransformChannelCurves;
		int32 TransformChannelCount = 0;
		bool IsNodeAnimated = false;
		FbxTimeSpan ExportedTimeSpan = FbxTimeSpan(FBXSDK_TIME_INFINITE, FBXSDK_TIME_MINUS_INFINITE);
		FbxTime StartTime = FBXSDK_TIME_INFINITE;
		FbxTime EndTime = FBXSDK_TIME_MINUS_INFINITE;
		int32 KeyCount = 0;
	};

	void GetFbxTransformCurves(FGetFbxTransformCurvesParameters& Parameters)
	{
		if (!ensure(Parameters.SDKScene) || !ensure(Parameters.Node))
		{
			return;
		}
		//Get the node transform curve keys, the transform components are separate into float curve
		//Translation X
		//Translation Y
		//Translation Z
		//Euler X
		//Euler Y
		//Euler Z
		//Scale X
		//Scale Y
		//Scale Z
		int32 NumAnimations = Parameters.SDKScene->GetSrcObjectCount<FbxAnimStack>();
		//Anim stack should be merge so we expect to have only one stack here
		ensure(NumAnimations <= 1);

		Parameters.IsNodeAnimated = false;

		Parameters.TransformChannelCount = 9;
		Parameters.TransformChannelCurves.Reset(Parameters.TransformChannelCount);
		for (int32 AnimationIndex = 0; AnimationIndex < NumAnimations && !Parameters.IsNodeAnimated; AnimationIndex++)
		{
			FbxAnimStack* AnimStack = (FbxAnimStack*)Parameters.SDKScene->GetSrcObject<FbxAnimStack>(AnimationIndex);
			int32 NumLayers = AnimStack->GetMemberCount();
			for (int LayerIndex = 0; LayerIndex < NumLayers && !Parameters.IsNodeAnimated; LayerIndex++)
			{
				FbxAnimLayer* AnimLayer = (FbxAnimLayer*)AnimStack->GetMember(LayerIndex);
				// Display curves specific to properties
				Parameters.TransformChannelCurves.Add(Parameters.Node->LclTranslation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_X, false));
				Parameters.TransformChannelCurves.Add(Parameters.Node->LclTranslation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, false));
				Parameters.TransformChannelCurves.Add(Parameters.Node->LclTranslation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, false));

				Parameters.TransformChannelCurves.Add(Parameters.Node->LclRotation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_X, false));
				Parameters.TransformChannelCurves.Add(Parameters.Node->LclRotation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, false));
				Parameters.TransformChannelCurves.Add(Parameters.Node->LclRotation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, false));

				Parameters.TransformChannelCurves.Add(Parameters.Node->LclScaling.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_X, false));
				Parameters.TransformChannelCurves.Add(Parameters.Node->LclScaling.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, false));
				Parameters.TransformChannelCurves.Add(Parameters.Node->LclScaling.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, false));

				if (!Parameters.IsNodeAnimated)
				{
					for (int32 ChannelIndex = 0; ChannelIndex < Parameters.TransformChannelCount; ++ChannelIndex)
					{
						if (Parameters.TransformChannelCurves[ChannelIndex])
						{
							Parameters.IsNodeAnimated = true;
							const int32 LocalKeycount = Parameters.TransformChannelCurves[ChannelIndex]->KeyGetCount();
							if (LocalKeycount > Parameters.KeyCount)
							{
								Parameters.KeyCount = LocalKeycount;
							}
							if (LocalKeycount > 1)
							{
								FbxTimeSpan LocalAnimatedTimeSpan(FBXSDK_TIME_INFINITE, FBXSDK_TIME_MINUS_INFINITE);
								Parameters.TransformChannelCurves[ChannelIndex]->GetTimeInterval(LocalAnimatedTimeSpan);
								if (Parameters.StartTime > LocalAnimatedTimeSpan.GetStart())
								{
									Parameters.StartTime = LocalAnimatedTimeSpan.GetStart();
								}
								if (Parameters.EndTime < LocalAnimatedTimeSpan.GetStop())
								{
									Parameters.EndTime = LocalAnimatedTimeSpan.GetStop();
								}
							}
							else if (Parameters.KeyCount == 1)
							{
								//When there is only one key there is no interval
								FbxAnimCurveKey FbxKey = Parameters.TransformChannelCurves[ChannelIndex]->KeyGet(0);
								FbxTime KeyTime = FbxKey.GetTime();
								if (Parameters.StartTime > KeyTime)
								{
									Parameters.StartTime = KeyTime;
								}
								if (Parameters.EndTime < KeyTime)
								{
									Parameters.EndTime = KeyTime;
								}
							}
						}
					}
				}
			}
		}
	}

	bool FAnimationPayloadContextTransform::FetchPayloadToFile(FFbxParser& Parser, const FString& PayloadFilepath)
	{
		//Will need this one to import animated curves
		return false;
	}

	bool FAnimationPayloadContextTransform::FetchAnimationBakeTransformPayloadToFile(FFbxParser& Parser, const double BakeFrequency, const double RangeStartTime, const double RangeEndTime, const FString& PayloadFilepath)
	{
		if (!ensure(SDKScene != nullptr))
		{
			UInterchangeResultError_Generic* Message = Parser.AddMessage<UInterchangeResultError_Generic>();
			Message->InterchangeKey = FFbxHelper::GetFbxNodeHierarchyName(Node);
			Message->Text = LOCTEXT("FBXSceneNull", "Cannot fetch FBX animation transform payload because the FBX scene is null.");
			return false;
		}

		if (!ensure(Node != nullptr))
		{
			UInterchangeResultError_Generic* Message = Parser.AddMessage<UInterchangeResultError_Generic>();
			Message->InterchangeKey = FFbxHelper::GetFbxNodeHierarchyName(Node);
			Message->Text = LOCTEXT("FBXNodeNull", "Cannot fetch FBX animation transform payload because the FBX node is null.");
			return false;
		}

		bool bBakeTransform = false;
		FAnimationBakeTransformPayloadData AnimationBakeTransformPayloadData;
		AnimationBakeTransformPayloadData.BakeFrequency = BakeFrequency;
		AnimationBakeTransformPayloadData.RangeStartTime = RangeStartTime;
		AnimationBakeTransformPayloadData.RangeEndTime = RangeEndTime;

		ImportBakeTransforms(Node, AnimationBakeTransformPayloadData);
		{
			FLargeMemoryWriter Ar;
			AnimationBakeTransformPayloadData.Serialize(Ar);
			uint8* ArchiveData = Ar.GetData();
			int64 ArchiveSize = Ar.TotalSize();
			TArray64<uint8> Buffer(ArchiveData, ArchiveSize);
			FFileHelper::SaveArrayToFile(Buffer, *PayloadFilepath);
		}
		return true;
	}

	void FFbxAnimation::AddNodeTransformAnimation(FbxScene* SDKScene, FbxNode* JointNode, UInterchangeBaseNodeContainer& NodeContainer, UInterchangeSceneNode* SceneNode, TMap<FString, TSharedPtr<FPayloadContextBase>>& PayloadContexts)
	{
		int32 NumAnimations = SDKScene->GetSrcObjectCount<FbxAnimStack>();
		//Anim stack should be merge so we expect to have only one stack here
		ensure(NumAnimations <= 1);

		double FrameRate = FbxTime::GetFrameRate(SDKScene->GetGlobalSettings().GetTimeMode());

		FGetFbxTransformCurvesParameters Parameters(SDKScene, JointNode);
		GetFbxTransformCurves(Parameters);

		if (Parameters.IsNodeAnimated)
		{
			FString PayLoadKey = FFbxHelper::GetFbxNodeHierarchyName(JointNode) + TEXT("_AnimationPayloadKey");
			if (ensure(!PayloadContexts.Contains(PayLoadKey)))
			{
				TSharedPtr<FAnimationPayloadContextTransform> AnimPayload = MakeShared<FAnimationPayloadContextTransform>();
				AnimPayload->Node = JointNode;
				AnimPayload->SDKScene = SDKScene;
				
				PayloadContexts.Add(PayLoadKey, AnimPayload);
			}
			SceneNode->SetCustomTransformCurvePayloadKey(PayLoadKey);
			SceneNode->SetCustomIsNodeTransformAnimated(Parameters.IsNodeAnimated);
			SceneNode->SetCustomNodeTransformAnimationKeyCount(Parameters.KeyCount);
			SceneNode->SetCustomNodeTransformAnimationStartTime(Parameters.StartTime.GetSecondDouble());
			SceneNode->SetCustomNodeTransformAnimationEndTime(Parameters.EndTime.GetSecondDouble());
		}
	}

}//ns UE::Interchange::Private

#undef LOCTEXT_NAMESPACE
