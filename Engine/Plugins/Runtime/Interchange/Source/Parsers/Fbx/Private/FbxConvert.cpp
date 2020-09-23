// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxConvert.h"

#include "CoreMinimal.h"
#include "FbxInclude.h"

namespace UE
{
	namespace FbxParser
	{
		namespace Private
		{
			void FFbxConvert::ConvertScene(FbxScene* SDKScene)
			{
				if (!ensure(SDKScene))
				{
					//Cannot convert a null scene
					return;
				}

				const FbxGlobalSettings& GlobalSettings = SDKScene->GetGlobalSettings();
				FbxTime::EMode TimeMode = GlobalSettings.GetTimeMode();
				//Set the original framerate from the current fbx file
				float FbxFramerate = FbxTime::GetFrameRate(TimeMode);

				//Merge the anim stack before the conversion since the above 0 layer will not be converted
				int32 AnimStackCount = SDKScene->GetSrcObjectCount<FbxAnimStack>();
				//Merge the animation stack layer before converting the scene
				for (int32 AnimStackIndex = 0; AnimStackIndex < AnimStackCount; AnimStackIndex++)
				{
					FbxAnimStack* CurAnimStack = SDKScene->GetSrcObject<FbxAnimStack>(AnimStackIndex);
					if (CurAnimStack->GetMemberCount() > 1)
					{
						MergeAllLayerAnimation(SDKScene, CurAnimStack, FbxFramerate);
					}
				}

				//Set the original file information
				FbxAxisSystem FileAxisSystem = SDKScene->GetGlobalSettings().GetAxisSystem();
				FbxSystemUnit FileUnitSystem = SDKScene->GetGlobalSettings().GetSystemUnit();


				//UE is: z up, front x, left handed
				FbxAxisSystem::EUpVector UpVector = FbxAxisSystem::eZAxis;
				FbxAxisSystem::EFrontVector FrontVector = (FbxAxisSystem::EFrontVector)FbxAxisSystem::eParityEven;
				FbxAxisSystem::ECoordSystem CoordSystem = FbxAxisSystem::eLeftHanded;
				FbxAxisSystem UnrealImportAxis(UpVector, FrontVector, CoordSystem);

				if (FileAxisSystem != UnrealImportAxis)
				{
					FbxRootNodeUtility::RemoveAllFbxRoots(SDKScene);
					UnrealImportAxis.ConvertScene(SDKScene);
				}

				if (FileUnitSystem != FbxSystemUnit::cm)
				{
					FbxSystemUnit::cm.ConvertScene(SDKScene);
				}

				//Reset all the transform evaluation cache since we change some node transform
				SDKScene->GetAnimationEvaluator()->Reset();
			}

			FTransform FFbxConvert::GetTransform(const FbxAMatrix& Matrix)
			{
				FTransform Transform = FTransform::Identity;
				FbxVector4 FbxLocalT = Matrix.GetT();
				FbxVector4 FbxLocalS = Matrix.GetS();
				FbxQuaternion FbxLocalQ = Matrix.GetQ();
				FVector LocalT = FVector(FbxLocalT[0], FbxLocalT[1], FbxLocalT[2]);
				Transform.SetTranslation(LocalT);
				FVector LocalS = FVector(FbxLocalS[0], FbxLocalS[1], FbxLocalS[2]);
				Transform.SetScale3D(LocalS);
				FQuat LocalQ;
				LocalQ.X = FbxLocalQ[0];
				LocalQ.Y = FbxLocalQ[1];
				LocalQ.Z = FbxLocalQ[2];
				LocalQ.W = FbxLocalQ[3];
				Transform.SetRotation(LocalQ);
				return Transform;
			}

			ANSICHAR* FFbxConvert::MakeName(const ANSICHAR* Name)
			{
				const int SpecialChars[] = { '.', ',', '/', '`', '%' };

				const int len = FCStringAnsi::Strlen(Name);
				ANSICHAR* TmpName = new ANSICHAR[len + 1];

				FCStringAnsi::Strcpy(TmpName, len + 1, Name);

				for (int32 i = 0; i < UE_ARRAY_COUNT(SpecialChars); i++)
				{
					ANSICHAR* CharPtr = TmpName;
					while ((CharPtr = FCStringAnsi::Strchr(CharPtr, SpecialChars[i])) != NULL)
					{
						CharPtr[0] = '_';
					}
				}

				// Remove namespaces
				ANSICHAR* NewName;
				NewName = FCStringAnsi::Strchr(TmpName, ':');

				// there may be multiple namespace, so find the last ':'
				while (NewName && FCStringAnsi::Strchr(NewName + 1, ':'))
				{
					NewName = FCStringAnsi::Strchr(NewName + 1, ':');
				}

				if (NewName)
				{
					return NewName + 1;
				}

				return TmpName;
			}

			/**
			 * Convert ANSI char to a FString using ANSI_TO_TCHAR macro
			 */
			FString FFbxConvert::MakeString(const ANSICHAR* Name)
			{
				return FString(ANSI_TO_TCHAR(Name));
			}

			void FFbxConvert::ApplyUnroll(FbxNode* Node, FbxAnimLayer* Layer, FbxAnimCurveFilterUnroll* UnrollFilter)
			{
				if (!ensure(Node) || !ensure(Layer) || !ensure(UnrollFilter))
				{
					return;
				}

				FbxAnimCurveNode* lCN = Node->LclRotation.GetCurveNode(Layer);
				if (lCN)
				{
					FbxAnimCurve* lRCurve[3];
					lRCurve[0] = lCN->GetCurve(0);
					lRCurve[1] = lCN->GetCurve(1);
					lRCurve[2] = lCN->GetCurve(2);


					// Set bone rotation order
					EFbxRotationOrder RotationOrder = eEulerXYZ;
					Node->GetRotationOrder(FbxNode::eSourcePivot, RotationOrder);
					UnrollFilter->SetRotationOrder((FbxEuler::EOrder)(RotationOrder));

					UnrollFilter->Apply(lRCurve, 3);
				}

				for (int32 i = 0; i < Node->GetChildCount(); i++)
				{
					ApplyUnroll(Node->GetChild(i), Layer, UnrollFilter);
				}
			}

			void FFbxConvert::MergeAllLayerAnimation(FbxScene* SDKScene, FbxAnimStack* AnimStack, float ResampleRate)
			{
				if (!ensure(SDKScene) || !ensure(AnimStack))
				{
					return;
				}
				FbxTime FramePeriod;
				FramePeriod.SetSecondDouble(1.0 / (double)ResampleRate);

				FbxTimeSpan TimeSpan = AnimStack->GetLocalTimeSpan();
				AnimStack->BakeLayers(SDKScene->GetAnimationEvaluator(), TimeSpan.GetStart(), TimeSpan.GetStop(), FramePeriod);

				// always apply unroll filter
				FbxAnimCurveFilterUnroll UnrollFilter;

				FbxAnimLayer* Layer = AnimStack->GetMember<FbxAnimLayer>(0);
				UnrollFilter.Reset();
				ApplyUnroll(SDKScene->GetRootNode(), Layer, &UnrollFilter);
			}
		}
	}
}
