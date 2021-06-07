// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
  Implementation of animation export related functionality from FbxExporter
=============================================================================*/

#include "CoreMinimal.h"
#include "Misc/MessageDialog.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Animation/AnimTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Matinee/InterpData.h"
#include "Matinee/InterpTrackAnimControl.h"
#include "Animation/AnimSequence.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Matinee/MatineeActor.h"
#include "Animation/SkeletalMeshActor.h"
#include "FbxExporter.h"
#include "Exporters/FbxExportOption.h"
#include "Animation/CustomAttributesRuntime.h"

DEFINE_LOG_CATEGORY_STATIC(LogFbxAnimationExport, Log, All);

namespace UnFbx
{

	bool FFbxExporter::SetupAnimStack(const UAnimSequence* AnimSeq)
	{
		if (AnimSeq->SequenceLength == 0.f)
		{
			// something is wrong
			return false;
		}

		const float FrameRate = FMath::TruncToFloat(((AnimSeq->GetRawNumberOfFrames() - 1) / AnimSeq->SequenceLength) + 0.5f);
		//Configure the scene time line
		{
			FbxGlobalSettings& SceneGlobalSettings = Scene->GetGlobalSettings();
			double CurrentSceneFrameRate = FbxTime::GetFrameRate(SceneGlobalSettings.GetTimeMode());
			if (!bSceneGlobalTimeLineSet || FrameRate > CurrentSceneFrameRate)
			{
				FbxTime::EMode ComputeTimeMode = FbxTime::ConvertFrameRateToTimeMode(FrameRate);
				FbxTime::SetGlobalTimeMode(ComputeTimeMode, ComputeTimeMode == FbxTime::eCustom ? FrameRate : 0.0);
				SceneGlobalSettings.SetTimeMode(ComputeTimeMode);
				if (ComputeTimeMode == FbxTime::eCustom)
				{
					SceneGlobalSettings.SetCustomFrameRate(FrameRate);
				}
				bSceneGlobalTimeLineSet = true;
			}
		}

		// set time correctly
		FbxTime ExportedStartTime, ExportedStopTime;
		ExportedStartTime.SetSecondDouble(0.f);
		ExportedStopTime.SetSecondDouble(AnimSeq->SequenceLength);

		FbxTimeSpan ExportedTimeSpan;
		ExportedTimeSpan.Set(ExportedStartTime, ExportedStopTime);
		AnimStack->SetLocalTimeSpan(ExportedTimeSpan);

		return true;
	}

void FFbxExporter::ExportAnimSequenceToFbx(const UAnimSequence* AnimSeq,
									 const USkeletalMesh* SkelMesh,
									 TArray<FbxNode*>& BoneNodes,
									 FbxAnimLayer* InAnimLayer,
									 float AnimStartOffset,
									 float AnimEndOffset,
									 float AnimPlayRate,
									 float StartTime)
{
	// stack allocator for extracting curve
	FMemMark Mark(FMemStack::Get());

	USkeleton* Skeleton = AnimSeq->GetSkeleton();

	if (Skeleton == nullptr || !SetupAnimStack(AnimSeq))
	{
		// something is wrong
		return;	
	}

	//Prepare root anim curves data to be exported
	TArray<FName> AnimCurveNames;
	TMap<FName, FbxAnimCurve*> CustomCurveMap;
	if (BoneNodes.Num() > 0)
	{
		const FSmartNameMapping* AnimCurveMapping = Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
		
		if (AnimCurveMapping)
		{
			AnimCurveMapping->FillNameArray(AnimCurveNames);

			const UFbxExportOption* ExportOptions = GetExportOptions();
			const bool bExportMorphTargetCurvesInMesh = ExportOptions && ExportOptions->bExportPreviewMesh && ExportOptions->bExportMorphTargets;

			for (auto AnimCurveName : AnimCurveNames)
			{
				const FCurveMetaData* CurveMetaData = AnimCurveMapping->GetCurveMetaData(AnimCurveName);

				//Only export the custom curve if it is not used in a MorphTarget that will be exported latter on.
				if(!(bExportMorphTargetCurvesInMesh && CurveMetaData && CurveMetaData->Type.bMorphtarget))
				{
					FbxProperty AnimCurveFbxProp = FbxProperty::Create(BoneNodes[0], FbxDoubleDT, TCHAR_TO_ANSI(*AnimCurveName.ToString()));
					AnimCurveFbxProp.ModifyFlag(FbxPropertyFlags::eAnimatable, true);
					AnimCurveFbxProp.ModifyFlag(FbxPropertyFlags::eUserDefined, true);
					FbxAnimCurve* AnimFbxCurve = AnimCurveFbxProp.GetCurve(InAnimLayer, true);
					CustomCurveMap.Add(AnimCurveName, AnimFbxCurve);
				}
			}
		}
	}

	ExportCustomAnimCurvesToFbx(CustomCurveMap, AnimSeq, AnimStartOffset, AnimEndOffset, AnimPlayRate, StartTime);

	TArray<FCustomAttribute> CustomAttributes;

	// Add the animation data to the bone nodes
	for(int32 BoneIndex = 0; BoneIndex < BoneNodes.Num(); ++BoneIndex)
	{
		FbxNode* CurrentBoneNode = BoneNodes[BoneIndex];
		int32 BoneTreeIndex = Skeleton->GetSkeletonBoneIndexFromMeshBoneIndex(SkelMesh, BoneIndex);
		int32 BoneTrackIndex = Skeleton->GetRawAnimationTrackIndex(BoneTreeIndex, AnimSeq);
		FName BoneName = Skeleton->GetReferenceSkeleton().GetBoneName(BoneTreeIndex);
		
		CustomAttributes.Reset();
		AnimSeq->GetCustomAttributesForBone(BoneName, CustomAttributes);

		TArray<TPair<int32, FbxAnimCurve*>> FloatCustomAttributeIndices;
		TArray<TPair<int32, FbxAnimCurve*>> IntCustomAttributeIndices;

		// Setup custom attribute properties and curves
		for (int32 AttributeIndex = 0; AttributeIndex < CustomAttributes.Num(); ++AttributeIndex)
		{
			const FCustomAttribute& Attribute = CustomAttributes[AttributeIndex];
			const FName& AttributeName = Attribute.Name;

			const EVariantTypes VariantType = static_cast<EVariantTypes>(Attribute.VariantType);

			if (VariantType == EVariantTypes::Int32)
			{
				FbxProperty AnimCurveFbxProp = FbxProperty::Create(CurrentBoneNode, FbxIntDT, TCHAR_TO_UTF8(*AttributeName.ToString()));
				AnimCurveFbxProp.ModifyFlag(FbxPropertyFlags::eAnimatable, true);
				AnimCurveFbxProp.ModifyFlag(FbxPropertyFlags::eUserDefined, true);

				FbxAnimCurve* AnimFbxCurve = AnimCurveFbxProp.GetCurve(InAnimLayer, true);
				AnimFbxCurve->KeyModifyBegin();
				IntCustomAttributeIndices.Emplace(AttributeIndex, AnimFbxCurve);
			}
			else if (VariantType == EVariantTypes::Float)
			{
				FbxProperty AnimCurveFbxProp = FbxProperty::Create(CurrentBoneNode, FbxFloatDT, TCHAR_TO_UTF8(*AttributeName.ToString()));
				AnimCurveFbxProp.ModifyFlag(FbxPropertyFlags::eAnimatable, true);
				AnimCurveFbxProp.ModifyFlag(FbxPropertyFlags::eUserDefined, true);

				FbxAnimCurve* AnimFbxCurve = AnimCurveFbxProp.GetCurve(InAnimLayer, true);
				AnimFbxCurve->KeyModifyBegin();
				FloatCustomAttributeIndices.Emplace(AttributeIndex, AnimFbxCurve);
			}
			else if (VariantType == EVariantTypes::String)
			{
				FbxProperty AnimCurveFbxProp = FbxProperty::Create(CurrentBoneNode, FbxStringDT, TCHAR_TO_UTF8(*AttributeName.ToString()));
				AnimCurveFbxProp.ModifyFlag(FbxPropertyFlags::eUserDefined, true);

				// String attributes can't be keyed, simply set a normal value.
				FString AttributeValue;
				FCustomAttributesRuntime::GetAttributeValue(Attribute, 0.f, AttributeValue);
				FbxString FbxValueString(TCHAR_TO_UTF8(*AttributeValue));
				AnimCurveFbxProp.Set(FbxValueString);
			}
			else
			{
				ensureMsgf(false, TEXT("Trying to export unsupported custom attribte (float, int32 and FString are currently supported)"));
			}
		}

		// Create the transform AnimCurves
		const uint32 NumberOfCurves = 9;
		FbxAnimCurve* Curves[NumberOfCurves];
		
		// Individual curves for translation, rotation and scaling
		Curves[0] = CurrentBoneNode->LclTranslation.GetCurve(InAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
		Curves[1] = CurrentBoneNode->LclTranslation.GetCurve(InAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
		Curves[2] = CurrentBoneNode->LclTranslation.GetCurve(InAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);
		
		Curves[3] = CurrentBoneNode->LclRotation.GetCurve(InAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
		Curves[4] = CurrentBoneNode->LclRotation.GetCurve(InAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
		Curves[5] = CurrentBoneNode->LclRotation.GetCurve(InAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);
		
		Curves[6] = CurrentBoneNode->LclScaling.GetCurve(InAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
		Curves[7] = CurrentBoneNode->LclScaling.GetCurve(InAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
		Curves[8] = CurrentBoneNode->LclScaling.GetCurve(InAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

		if(BoneTrackIndex == INDEX_NONE)
		{
			// If this sequence does not have a track for the current bone, then skip it
			continue;
		}
		
		for (FbxAnimCurve* Curve : Curves)
		{
			Curve->KeyModifyBegin();
		}

		auto ExportLambda = [&](float AnimTime, FbxTime ExportTime, bool bLastKey) {
			FTransform BoneAtom;
			AnimSeq->GetBoneTransform(BoneAtom, BoneTrackIndex, AnimTime, true);
			FbxAMatrix FbxMatrix = Converter.ConvertMatrix(BoneAtom.ToMatrixWithScale());
			
			FbxVector4 Translation = FbxMatrix.GetT();
			FbxVector4 Rotation = FbxMatrix.GetR();
			FbxVector4 Scale = FbxMatrix.GetS();
			FbxVector4 Vectors[3] = { Translation, Rotation, Scale };

			// Loop over each curve and channel to set correct values
			for (uint32 CurveIndex = 0; CurveIndex < 3; ++CurveIndex)
			{
				for (uint32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
				{
					uint32 OffsetCurveIndex = (CurveIndex * 3) + ChannelIndex;

					int32 lKeyIndex = Curves[OffsetCurveIndex]->KeyAdd(ExportTime);
					Curves[OffsetCurveIndex]->KeySetValue(lKeyIndex, Vectors[CurveIndex][ChannelIndex]);
					Curves[OffsetCurveIndex]->KeySetInterpolation(lKeyIndex, bLastKey ? FbxAnimCurveDef::eInterpolationConstant : FbxAnimCurveDef::eInterpolationCubic);

					if (bLastKey)
					{
						Curves[OffsetCurveIndex]->KeySetConstantMode(lKeyIndex, FbxAnimCurveDef::eConstantStandard);
					}
				}
			}

			for (TPair<int32, FbxAnimCurve*>& CurrentAttributeCurve : FloatCustomAttributeIndices)
			{
				float AttributeValue = 0.f;
				FCustomAttributesRuntime::GetAttributeValue(CustomAttributes[CurrentAttributeCurve.Key], AnimTime, AttributeValue);
				int32 KeyIndex = CurrentAttributeCurve.Value->KeyAdd(ExportTime);
				CurrentAttributeCurve.Value->KeySetValue(KeyIndex, AttributeValue);
			}

			for (TPair<int32, FbxAnimCurve*>& CurrentAttributeCurve : IntCustomAttributeIndices)
			{
				int32 AttributeValue = 0;
				FCustomAttributesRuntime::GetAttributeValue(CustomAttributes[CurrentAttributeCurve.Key], AnimTime, AttributeValue);
				int32 KeyIndex = CurrentAttributeCurve.Value->KeyAdd(ExportTime);
				CurrentAttributeCurve.Value->KeySetValue(KeyIndex, static_cast<float>(AttributeValue));
			}
		};

		IterateInsideAnimSequence(AnimSeq, AnimStartOffset, AnimEndOffset, AnimPlayRate, StartTime, ExportLambda);

		for (FbxAnimCurve* Curve : Curves)
		{
			Curve->KeyModifyEnd();
		}

		auto MarkCurveEnd = [](auto& CurvesArray)
		{
			for (auto& CurvePair : CurvesArray)
			{
				CurvePair.Value->KeyModifyEnd();
			}
		};

		MarkCurveEnd(FloatCustomAttributeIndices);
		MarkCurveEnd(IntCustomAttributeIndices);
	}
}

void FFbxExporter::ExportCustomAnimCurvesToFbx(const TMap<FName, FbxAnimCurve*>& CustomCurves, const UAnimSequence* AnimSeq, 
	float AnimStartOffset, float AnimEndOffset, float AnimPlayRate, float StartTime, float ValueScale)
{
	// stack allocator for extracting curve
	FMemMark Mark(FMemStack::Get());
	const USkeleton* Skeleton = AnimSeq->GetSkeleton();
	const FSmartNameMapping* SmartNameMapping = Skeleton ? Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName) : nullptr;

	if (!Skeleton || !SmartNameMapping || !SetupAnimStack(AnimSeq))
	{
		//Something is wrong.
		return;
	}

	TArray<SmartName::UID_Type> AnimCurveUIDs;
	{
		//We need to recreate the UIDs array manually so that we keep the empty entries otherwise the BlendedCurve won't have the correct mapping.
		TArray<FName> UID_ToNameArray;
		SmartNameMapping->FillUIDToNameArray(UID_ToNameArray);
		AnimCurveUIDs.Reserve(UID_ToNameArray.Num());
		for (int32 NameIndex = 0; NameIndex < UID_ToNameArray.Num(); ++NameIndex)
		{
			AnimCurveUIDs.Add(NameIndex);
		}
	}

	for (auto CustomCurve : CustomCurves)
	{
		CustomCurve.Value->KeyModifyBegin();
	}
	
	auto ExportLambda = [&](float AnimTime, FbxTime ExportTime, bool bLastKey) {
		FBlendedCurve BlendedCurve;
		BlendedCurve.InitFrom(&AnimCurveUIDs);
		AnimSeq->EvaluateCurveData(BlendedCurve, AnimTime, true);
		if (BlendedCurve.IsValid())
		{
			//Loop over the custom curves and add the actual keys
			for (auto CustomCurve : CustomCurves)
			{
				SmartName::UID_Type NameUID = Skeleton->GetUIDByName(USkeleton::AnimCurveMappingName, CustomCurve.Key);
				if (NameUID != SmartName::MaxUID)
				{
					float CurveValueAtTime = BlendedCurve.Get(NameUID) * ValueScale;
					int32 KeyIndex = CustomCurve.Value->KeyAdd(ExportTime);
					CustomCurve.Value->KeySetValue(KeyIndex, CurveValueAtTime);
				}
			}
		}
	};

	IterateInsideAnimSequence(AnimSeq, AnimStartOffset, AnimEndOffset, AnimPlayRate, StartTime, ExportLambda);

	for (auto CustomCurve : CustomCurves)
	{
		CustomCurve.Value->KeyModifyEnd();
	}
}

void FFbxExporter::IterateInsideAnimSequence(const UAnimSequence* AnimSeq, float AnimStartOffset, float AnimEndOffset, float AnimPlayRate, float StartTime, TFunctionRef<void(float, FbxTime, bool)> IterationLambda)
{
	float AnimTime = AnimStartOffset;
	float AnimEndTime = (AnimSeq->SequenceLength - AnimEndOffset);
	// Subtracts 1 because NumFrames includes an initial pose for 0.0 second
	double TimePerKey = (AnimSeq->SequenceLength / (AnimSeq->GetRawNumberOfFrames() - 1));
	const float AnimTimeIncrement = TimePerKey * AnimPlayRate;
	uint32 AnimFrameIndex = 0;

	FbxTime ExportTime;
	ExportTime.SetSecondDouble(StartTime);

	FbxTime ExportTimeIncrement;
	ExportTimeIncrement.SetSecondDouble(TimePerKey);

	// Step through each frame and add custom curve data
	bool bLastKey = false;
	while (!bLastKey)
	{
		bLastKey = (AnimTime + KINDA_SMALL_NUMBER) > AnimEndTime;

		IterationLambda(AnimTime, ExportTime, bLastKey);

		ExportTime += ExportTimeIncrement;
		AnimFrameIndex++;
		AnimTime = AnimStartOffset + ((float)AnimFrameIndex * AnimTimeIncrement);
	}
}

// The curve code doesn't differentiate between angles and other data, so an interpolation from 179 to -179
// will cause the bone to rotate all the way around through 0 degrees.  So here we make a second pass over the 
// rotation tracks to convert the angles into a more interpolation-friendly format.  
void FFbxExporter::CorrectAnimTrackInterpolation( TArray<FbxNode*>& BoneNodes, FbxAnimLayer* InAnimLayer )
{
	// Add the animation data to the bone nodes
	for(int32 BoneIndex = 0; BoneIndex < BoneNodes.Num(); ++BoneIndex)
	{
		FbxNode* CurrentBoneNode = BoneNodes[BoneIndex];

		// Fetch the AnimCurves
		FbxAnimCurve* Curves[3];
		Curves[0] = CurrentBoneNode->LclRotation.GetCurve(InAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
		Curves[1] = CurrentBoneNode->LclRotation.GetCurve(InAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
		Curves[2] = CurrentBoneNode->LclRotation.GetCurve(InAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

		for(int32 CurveIndex = 0; CurveIndex < 3; ++CurveIndex)
		{
			FbxAnimCurve* CurrentCurve = Curves[CurveIndex];
			CurrentCurve->KeyModifyBegin();

			float CurrentAngleOffset = 0.f;
			for(int32 KeyIndex = 1; KeyIndex < CurrentCurve->KeyGetCount(); ++KeyIndex)
			{
				float PreviousOutVal	= CurrentCurve->KeyGetValue( KeyIndex-1 );
				float CurrentOutVal		= CurrentCurve->KeyGetValue( KeyIndex );

				float DeltaAngle = (CurrentOutVal + CurrentAngleOffset) - PreviousOutVal;

				if(DeltaAngle >= 180)
				{
					CurrentAngleOffset -= 360;
				}
				else if(DeltaAngle <= -180)
				{
					CurrentAngleOffset += 360;
				}

				CurrentOutVal += CurrentAngleOffset;

				CurrentCurve->KeySetValue(KeyIndex, CurrentOutVal);
			}

			CurrentCurve->KeyModifyEnd();
		}
	}
}


FbxNode* FFbxExporter::ExportAnimSequence( const UAnimSequence* AnimSeq, const USkeletalMesh* SkelMesh, bool bExportSkelMesh, const TCHAR* MeshName, FbxNode* ActorRootNode, const TArray<UMaterialInterface*>* OverrideMaterials /*= nullptr*/ )
{
	if( Scene == NULL || AnimSeq == NULL || SkelMesh == NULL )
	{
 		return NULL;
	}


	FbxNode* RootNode = (ActorRootNode)? ActorRootNode : Scene->GetRootNode();

	//Create a temporary node attach to the scene root.
	//This will allow us to do the binding without the scene transform (non uniform scale is not supported when binding the skeleton)
	//We then detach from the temp node and attach to the parent and remove the temp node
	FString FbxNodeName = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FbxNode* TmpNodeNoTransform = FbxNode::Create(Scene, TCHAR_TO_UTF8(*FbxNodeName));
	Scene->GetRootNode()->AddChild(TmpNodeNoTransform);


	// Create the Skeleton
	TArray<FbxNode*> BoneNodes;
	FbxNode* SkeletonRootNode = CreateSkeleton(SkelMesh, BoneNodes);
	TmpNodeNoTransform->AddChild(SkeletonRootNode);


	// Export the anim sequence
	{
		ExportAnimSequenceToFbx(AnimSeq,
			SkelMesh,
			BoneNodes,
			AnimLayer,
			0.f,		// AnimStartOffset
			0.f,		// AnimEndOffset
			1.f,		// AnimPlayRate
			0.f);		// StartTime

		CorrectAnimTrackInterpolation(BoneNodes, AnimLayer);
	}


	// Optionally export the mesh
	if(bExportSkelMesh)
	{
		FString MeshNodeName;
		
		if (MeshName)
		{
			MeshNodeName = MeshName;
		}
		else
		{
			SkelMesh->GetName(MeshNodeName);
		}

		FbxNode* MeshRootNode = nullptr;
		if (GetExportOptions()->LevelOfDetail && SkelMesh->GetLODNum() > 1)
		{
			FString LodGroup_MeshName = MeshNodeName + TEXT("_LodGroup");
			MeshRootNode = FbxNode::Create(Scene, TCHAR_TO_UTF8(*LodGroup_MeshName));
			TmpNodeNoTransform->AddChild(MeshRootNode);
			LodGroup_MeshName = MeshNodeName + TEXT("_LodGroupAttribute");
			FbxLODGroup *FbxLodGroupAttribute = FbxLODGroup::Create(Scene, TCHAR_TO_UTF8(*LodGroup_MeshName));
			MeshRootNode->AddNodeAttribute(FbxLodGroupAttribute);

			FbxLodGroupAttribute->ThresholdsUsedAsPercentage = true;
			//Export an Fbx Mesh Node for every LOD and child them to the fbx node (LOD Group)
			for (int CurrentLodIndex = 0; CurrentLodIndex < SkelMesh->GetLODNum(); ++CurrentLodIndex)
			{
				FString FbxLODNodeName = MeshNodeName + TEXT("_LOD") + FString::FromInt(CurrentLodIndex);
				if (CurrentLodIndex + 1 < SkelMesh->GetLODNum())
				{
					//Convert the screen size to a threshold, it is just to be sure that we set some threshold, there is no way to convert this precisely
					double LodScreenSize = (double)(10.0f / SkelMesh->GetLODInfo(CurrentLodIndex)->ScreenSize.Default);
					FbxLodGroupAttribute->AddThreshold(LodScreenSize);
				}
				FbxNode* FbxActorLOD = CreateMesh(SkelMesh, *FbxLODNodeName, CurrentLodIndex, AnimSeq, OverrideMaterials);
				if (FbxActorLOD)
				{
					MeshRootNode->AddChild(FbxActorLOD);
					if (SkeletonRootNode)
					{
						// Bind the mesh to the skeleton
						BindMeshToSkeleton(SkelMesh, FbxActorLOD, BoneNodes, CurrentLodIndex);
						// Add the bind pose
						CreateBindPose(FbxActorLOD);
					}
				}
			}
		}
		else
		{
			const int32 LodIndex = 0;
			MeshRootNode = CreateMesh(SkelMesh, *MeshNodeName, LodIndex, AnimSeq, OverrideMaterials);
			if (MeshRootNode)
			{
				TmpNodeNoTransform->AddChild(MeshRootNode);
				if (SkeletonRootNode)
				{
					// Bind the mesh to the skeleton
					BindMeshToSkeleton(SkelMesh, MeshRootNode, BoneNodes, LodIndex);

					// Add the bind pose
					CreateBindPose(MeshRootNode);
				}
			}
		}

		if (MeshRootNode)
		{
			TmpNodeNoTransform->RemoveChild(MeshRootNode);
			RootNode->AddChild(MeshRootNode);
		}
	}
	
	if (SkeletonRootNode)
	{
		TmpNodeNoTransform->RemoveChild(SkeletonRootNode);
		RootNode->AddChild(SkeletonRootNode);
	}

	Scene->GetRootNode()->RemoveChild(TmpNodeNoTransform);
	Scene->RemoveNode(TmpNodeNoTransform);

	return SkeletonRootNode;
}


void FFbxExporter::ExportAnimSequencesAsSingle( USkeletalMesh* SkelMesh, const ASkeletalMeshActor* SkelMeshActor, const FString& ExportName, const TArray<UAnimSequence*>& AnimSeqList, const TArray<struct FAnimControlTrackKey>& TrackKeys )
{
	if (Scene == NULL || SkelMesh == NULL || AnimSeqList.Num() == 0 || AnimSeqList.Num() != TrackKeys.Num()) return;

	FbxNode* BaseNode = FbxNode::Create(Scene, Converter.ConvertToFbxString(ExportName));
	Scene->GetRootNode()->AddChild(BaseNode);

	if( SkelMeshActor )
	{
		// Set the default position of the actor on the transforms
		// The Unreal transformation is different from FBX's Z-up: invert the Y-axis for translations and the Y/Z angle values in rotations.
		BaseNode->LclTranslation.Set(Converter.ConvertToFbxPos(SkelMeshActor->GetActorLocation()));
		BaseNode->LclRotation.Set(Converter.ConvertToFbxRot(SkelMeshActor->GetActorRotation().Euler()));
		BaseNode->LclScaling.Set(Converter.ConvertToFbxScale(SkelMeshActor->GetRootComponent()->GetRelativeScale3D()));

	}

	// Create the Skeleton
	TArray<FbxNode*> BoneNodes;
	FbxNode* SkeletonRootNode = CreateSkeleton(SkelMesh, BoneNodes);
	BaseNode->AddChild(SkeletonRootNode);

	bool bAnyObjectMissingSourceData = false;
	float ExportStartTime = 0.f;
	for(int32 AnimSeqIndex = 0; AnimSeqIndex < AnimSeqList.Num(); ++AnimSeqIndex)
	{
		const UAnimSequence* AnimSeq = AnimSeqList[AnimSeqIndex];
		const FAnimControlTrackKey& TrackKey = TrackKeys[AnimSeqIndex];

		// Shift the anim sequences so the first one is at time zero in the FBX file
		const float CurrentStartTime = TrackKey.StartTime - ExportStartTime;

		ExportAnimSequenceToFbx(AnimSeq,
			SkelMesh,
			BoneNodes,
			AnimLayer,
			TrackKey.AnimStartOffset,
			TrackKey.AnimEndOffset,
			TrackKey.AnimPlayRate,
			CurrentStartTime);
	}

	CorrectAnimTrackInterpolation(BoneNodes, AnimLayer);

	if (bAnyObjectMissingSourceData)
	{
		FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "Exporter_Error_SourceDataUnavailable", "No source data available for some objects.  See the log for details.") );
	}

}



/**
 * Exports all the animation sequences part of a single Group in a Matinee sequence
 * as a single animation in the FBX document.  The animation is created by sampling the
 * sequence at DEFAULT_SAMPLERATE updates/second and extracting the resulting bone transforms from the given
 * skeletal mesh
 */
void FFbxExporter::ExportMatineeGroup(class AMatineeActor* MatineeActor, USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (Scene == NULL || MatineeActor == NULL || SkeletalMeshComponent == NULL || MatineeActor->MatineeData->InterpLength == 0)
	{
		return;
	}

	FbxString NodeName("MatineeSequence");

	FbxNode* BaseNode = FbxNode::Create(Scene, NodeName);
	Scene->GetRootNode()->AddChild(BaseNode);

	AActor* Owner = SkeletalMeshComponent->GetOwner();
	if(Owner && Owner->GetRootComponent())
	{
		// Set the default position of the actor on the transforms
		// The UE3 transformation is different from FBX's Z-up: invert the Y-axis for translations and the Y/Z angle values in rotations.
		BaseNode->LclTranslation.Set(Converter.ConvertToFbxPos(Owner->GetActorLocation()));
		BaseNode->LclRotation.Set(Converter.ConvertToFbxRot(Owner->GetActorRotation().Euler()));
		BaseNode->LclScaling.Set(Converter.ConvertToFbxScale(Owner->GetRootComponent()->GetRelativeScale3D()));
	}
	// Create the Skeleton
	TArray<FbxNode*> BoneNodes;
	FbxNode* SkeletonRootNode = CreateSkeleton(SkeletalMeshComponent->SkeletalMesh, BoneNodes);
	FbxSkeletonRoots.Add(SkeletalMeshComponent, SkeletonRootNode);
	BaseNode->AddChild(SkeletonRootNode);

	static const float SamplingRate = 1.f / DEFAULT_SAMPLERATE;

	FMatineeAnimTrackAdapter AnimTrackAdapter(MatineeActor);
	ExportAnimTrack(AnimTrackAdapter, Owner, SkeletalMeshComponent, SamplingRate);
}

void FFbxExporter::ExportAnimTrack(IAnimTrackAdapter& AnimTrackAdapter, AActor* Actor, USkeletalMeshComponent* InSkeletalMeshComponent, float SamplingRate)
{
	// show a status update every 1 second worth of samples
	const float UpdateFrequency = 1.0f;
	float NextUpdateTime = UpdateFrequency;

	// find root and find the bone array
	TArray<FbxNode*> BoneNodes;

	if ( FindSkeleton(InSkeletalMeshComponent, BoneNodes)==false )
	{
		UE_LOG(LogFbx, Warning, TEXT("Error FBX Animation Export, no root skeleton found."));
		return;		
	}
	//if we have no allocated bone space transforms something wrong so try to recalc them
	if (InSkeletalMeshComponent->GetBoneSpaceTransforms().Num() <= 0 )
	{
		InSkeletalMeshComponent->RecalcRequiredBones(0);
		if (InSkeletalMeshComponent->GetBoneSpaceTransforms().Num() <= 0)
		{
			UE_LOG(LogFbx, Warning, TEXT("Error FBX Animation Export, no bone transforms."));
			return;
		}
	}
	
	TArray<FCustomAttribute> CustomAttributes;
	
	FTransform InitialInvParentTransform;

	int32 LocalStartFrame = AnimTrackAdapter.GetLocalStartFrame();
	int32 StartFrame = AnimTrackAdapter.GetStartFrame();
	int32 AnimationLength = AnimTrackAdapter.GetLength();
	float FrameRate = AnimTrackAdapter.GetFrameRate();

	TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
	Actor->GetComponents(SkeletalMeshComponents);

	const float TickRate = 1.0f/FrameRate;

	FScopedSlowTask SlowTask(AnimationLength, NSLOCTEXT("UnrealEd", "ExportAnimationProgress", "Exporting Animation"));
	SlowTask.MakeDialog(true);

	for (int32 FrameCount = 0; FrameCount <= AnimationLength; ++FrameCount)
	{
		SlowTask.EnterProgressFrame();
		
		int32 LocalFrame = LocalStartFrame + FrameCount;
		float SampleTime = (StartFrame + FrameCount) / FrameRate;

		// This will call UpdateSkelPose on the skeletal mesh component to move bones based on animations in the matinee group
		AnimTrackAdapter.UpdateAnimation(LocalFrame);

		if (FrameCount == 0)
		{
			InitialInvParentTransform = Actor->GetRootComponent()->GetComponentTransform().Inverse();
		}

		// This will retrieve the currently active anim sequence (topmost) for custom attributes
		const UAnimSequence* AnimSeq = AnimTrackAdapter.GetAnimSequence(LocalFrame);
		float AnimTime = AnimTrackAdapter.GetAnimTime(LocalFrame);

		// Update space bases so new animation position has an effect.
		// @todo - hack - this will be removed at some point
		for (USkeletalMeshComponent* SkeletalMeshComponent : SkeletalMeshComponents)
		{
			USceneComponent* Child = SkeletalMeshComponent;
			while (Child)
			{
				if (USkeletalMeshComponent* ChildSkeletalMeshComponent = Cast<USkeletalMeshComponent>(Child))
				{
					SkeletalMeshComponent->TickAnimation(TickRate, false);

					SkeletalMeshComponent->RefreshBoneTransforms();
					SkeletalMeshComponent->RefreshSlaveComponents();
					SkeletalMeshComponent->UpdateComponentToWorld();
					SkeletalMeshComponent->FinalizeBoneTransform();
					SkeletalMeshComponent->MarkRenderTransformDirty();
					SkeletalMeshComponent->MarkRenderDynamicDataDirty();
				}

				if (Child->GetOwner())
				{
					Child->GetOwner()->Tick(TickRate);
				}

				Child = Child->GetAttachParent();
			}
		}

		FbxTime ExportTime; 
		ExportTime.SetSecondDouble(GetExportOptions()->bExportLocalTime ? LocalFrame / FrameRate : SampleTime);

		NextUpdateTime -= SamplingRate;

		if( NextUpdateTime <= 0.0f )
		{
			NextUpdateTime = UpdateFrequency;
			GWarn->StatusUpdate( FMath::RoundToInt( SampleTime ), FMath::RoundToInt(AnimationLength), NSLOCTEXT("FbxExporter", "ExportingToFbxStatus", "Exporting to FBX") );
		}

		TArray<FTransform> LocalBoneTransforms = InSkeletalMeshComponent->GetBoneSpaceTransforms();

		if (LocalBoneTransforms.Num() == 0)
		{
			continue;
		}

		// Add the animation data to the bone nodes
		for(int32 BoneIndex = 0; BoneIndex < BoneNodes.Num(); ++BoneIndex)
		{
			FName BoneName = InSkeletalMeshComponent->SkeletalMesh->GetRefSkeleton().GetBoneName(BoneIndex);
			FbxNode* CurrentBoneNode = BoneNodes[BoneIndex];

			// Create the AnimCurves
			FbxAnimCurve* Curves[6];
			Curves[0] = CurrentBoneNode->LclTranslation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
			Curves[1] = CurrentBoneNode->LclTranslation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
			Curves[2] = CurrentBoneNode->LclTranslation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

			Curves[3] = CurrentBoneNode->LclRotation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
			Curves[4] = CurrentBoneNode->LclRotation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
			Curves[5] = CurrentBoneNode->LclRotation.GetCurve(AnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

			for(int32 i = 0; i < 6; ++i)
			{
				Curves[i]->KeyModifyBegin();
			}

			FTransform BoneTransform = LocalBoneTransforms[BoneIndex];

			if (GetExportOptions()->MapSkeletalMotionToRoot && BoneIndex == 0)
			{
				BoneTransform = InSkeletalMeshComponent->GetSocketTransform(BoneName) * InitialInvParentTransform;
			}

			FbxVector4 Translation = Converter.ConvertToFbxPos(BoneTransform.GetLocation());
			FbxVector4 Rotation = Converter.ConvertToFbxRot(BoneTransform.GetRotation().Euler());

			int32 lKeyIndex;

			for(int32 i = 0, j=3; i < 3; ++i, ++j)
			{
				lKeyIndex = Curves[i]->KeyAdd(ExportTime);
				Curves[i]->KeySetValue(lKeyIndex, Translation[i]);
				Curves[i]->KeySetInterpolation(lKeyIndex, FbxAnimCurveDef::eInterpolationCubic);

				lKeyIndex = Curves[j]->KeyAdd(ExportTime);
				Curves[j]->KeySetValue(lKeyIndex, Rotation[i]);
				Curves[j]->KeySetInterpolation(lKeyIndex, FbxAnimCurveDef::eInterpolationCubic);
			}

			for(int32 i = 0; i < 6; ++i)
			{
				Curves[i]->KeyModifyEnd();
			}

			// Custom attributes
			if (!AnimSeq)
			{
				continue;
			}

			CustomAttributes.Reset();
			AnimSeq->GetCustomAttributesForBone(BoneName, CustomAttributes);

			TArray<TPair<int32, FbxAnimCurve*>> FloatCustomAttributeIndices;
			TArray<TPair<int32, FbxAnimCurve*>> IntCustomAttributeIndices;

			// Setup custom attribute properties and curves
			for (int32 AttributeIndex = 0; AttributeIndex < CustomAttributes.Num(); ++AttributeIndex)
			{
				const FCustomAttribute& Attribute = CustomAttributes[AttributeIndex];
				const FName& AttributeName = Attribute.Name;

				const EVariantTypes VariantType = static_cast<EVariantTypes>(Attribute.VariantType);

				if (VariantType == EVariantTypes::Int32)
				{
					FbxProperty AnimCurveFbxProp = FbxProperty::Create(CurrentBoneNode, FbxIntDT, TCHAR_TO_UTF8(*AttributeName.ToString()));
					AnimCurveFbxProp.ModifyFlag(FbxPropertyFlags::eAnimatable, true);
					AnimCurveFbxProp.ModifyFlag(FbxPropertyFlags::eUserDefined, true);

					FbxAnimCurve* AnimFbxCurve = AnimCurveFbxProp.GetCurve(AnimLayer, true);
					AnimFbxCurve->KeyModifyBegin();
					IntCustomAttributeIndices.Emplace(AttributeIndex, AnimFbxCurve);
				}
				else if (VariantType == EVariantTypes::Float)
				{
					FbxProperty AnimCurveFbxProp = FbxProperty::Create(CurrentBoneNode, FbxFloatDT, TCHAR_TO_UTF8(*AttributeName.ToString()));
					AnimCurveFbxProp.ModifyFlag(FbxPropertyFlags::eAnimatable, true);
					AnimCurveFbxProp.ModifyFlag(FbxPropertyFlags::eUserDefined, true);

					FbxAnimCurve* AnimFbxCurve = AnimCurveFbxProp.GetCurve(AnimLayer, true);
					AnimFbxCurve->KeyModifyBegin();
					FloatCustomAttributeIndices.Emplace(AttributeIndex, AnimFbxCurve);
				}
				else if (VariantType == EVariantTypes::String)
				{
					FbxProperty AnimCurveFbxProp = FbxProperty::Create(CurrentBoneNode, FbxStringDT, TCHAR_TO_UTF8(*AttributeName.ToString()));
					AnimCurveFbxProp.ModifyFlag(FbxPropertyFlags::eUserDefined, true);

					// String attributes can't be keyed, simply set a normal value.
					FString AttributeValue;
					FCustomAttributesRuntime::GetAttributeValue(Attribute, 0.f, AttributeValue);
					FbxString FbxValueString(TCHAR_TO_UTF8(*AttributeValue));
					AnimCurveFbxProp.Set(FbxValueString);
				}
				else
				{
					ensureMsgf(false, TEXT("Trying to export unsupported custom attribte (float, int32 and FString are currently supported)"));
				}

				for (TPair<int32, FbxAnimCurve*>& CurrentAttributeCurve : FloatCustomAttributeIndices)
				{
					float AttributeValue = 0.f;
					FCustomAttributesRuntime::GetAttributeValue(CustomAttributes[CurrentAttributeCurve.Key], AnimTime, AttributeValue);
					int32 KeyIndex = CurrentAttributeCurve.Value->KeyAdd(ExportTime);
					CurrentAttributeCurve.Value->KeySetValue(KeyIndex, AttributeValue);
				}

				for (TPair<int32, FbxAnimCurve*>& CurrentAttributeCurve : IntCustomAttributeIndices)
				{
					int32 AttributeValue = 0;
					FCustomAttributesRuntime::GetAttributeValue(CustomAttributes[CurrentAttributeCurve.Key], AnimTime, AttributeValue);
					int32 KeyIndex = CurrentAttributeCurve.Value->KeyAdd(ExportTime);
					CurrentAttributeCurve.Value->KeySetValue(KeyIndex, static_cast<float>(AttributeValue));
				}
			}

			auto MarkCurveEnd = [](auto& CurvesArray)
			{
				for (auto& CurvePair : CurvesArray)
				{
					CurvePair.Value->KeyModifyEnd();
				}
			};

			MarkCurveEnd(FloatCustomAttributeIndices);
			MarkCurveEnd(IntCustomAttributeIndices);
		}
	}

	CorrectAnimTrackInterpolation(BoneNodes, AnimLayer);
}

} // namespace UnFbx
