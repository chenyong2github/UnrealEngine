// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
	#include "RigUnit_RigLogic_Test.h"
#endif

#include "RigUnit_RigLogic.h"
#include "Components/SkeletalMeshComponent.h"
#include "Units/RigUnitContext.h"
#include "ControlRig.h"
#include "Math/TransformNonVectorized.h"
#include "DNAUtils.h"

#include "riglogic/RigLogic.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"
#include "RigUnit_RigLogic.h"

const uint8 FRigUnit_RigLogic::TestAccessor::MAX_ATTRS_PER_JOINT = 9;

FRigUnit_RigLogic::TestAccessor::TestAccessor(FRigUnit_RigLogic* Unit)
{
	this->Unit = Unit;
}

/** ====== Map Input Curves ===== **/

TSharedPtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderEmpty()
{
	return MakeShared<TestBehaviorReader>();
}

TUniquePtr<FRigCurveContainer> FRigUnit_RigLogic::TestAccessor::CreateCurveContainerEmpty()
{
	return MakeUnique<FRigCurveContainer>();
}

TSharedPtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderOneCurve(FString ControlNameStr)
{
	TSharedPtr<TestBehaviorReader> BehaviorReader = MakeShared<TestBehaviorReader>();
	//StringView ControlName(TCHAR_TO_ANSI(*ControlNameStr), ControlNameStr.Len());
	BehaviorReader->rawControls.Add(ControlNameStr);
	BehaviorReader->LODCount = 1;
	return BehaviorReader;
}

TUniquePtr<FRigCurveContainer> FRigUnit_RigLogic::TestAccessor::CreateCurveContainerOneCurve(FString CurveNameStr)
{
	TUniquePtr<FRigCurveContainer> ValidCurveContainer = MakeUnique<FRigCurveContainer>();
	ValidCurveContainer->Add(FName(*CurveNameStr));
	ValidCurveContainer->Initialize();
	return ValidCurveContainer;
}

void FRigUnit_RigLogic::TestAccessor::Exec_MapInputCurve(FSharedRigRuntimeContext* Context, FRigCurveContainer* TestCurveContainer)
{
	Unit->Data.MapInputCurveIndices(Context, TestCurveContainer); //inside the method so we can access data, which is a private member
}

/** ====== Map Joints ===== **/


TUniquePtr<FRigBoneHierarchy> FRigUnit_RigLogic::TestAccessor::CreateBoneHierarchyEmpty()
{
	return MakeUnique<FRigBoneHierarchy>();
}

TUniquePtr<FRigBoneHierarchy> FRigUnit_RigLogic::TestAccessor::CreateBoneHierarchyTwoBones(FString Bone1NameStr, FString Bone2NameStr)
{
	TUniquePtr<FRigBoneHierarchy> TestHierarchy = MakeUnique<FRigBoneHierarchy>();
	TestHierarchy->Reset();
	TestHierarchy->Add(*Bone1NameStr, NAME_None, ERigBoneType::User, FTransform(FVector(1.f, 0.f, 0.f)));
	TestHierarchy->Add(*Bone2NameStr, *Bone1NameStr, ERigBoneType::User, FTransform(FVector(1.f, 2.f, 0.f)));
	TestHierarchy->Initialize();
	return TestHierarchy;
}

TSharedPtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderTwoJoints(FString Joint1NameStr, FString Joint2NameStr)
{
	TSharedPtr<TestBehaviorReader> TestReader = MakeShared<TestBehaviorReader>();
	TestReader->addJoint(Joint1NameStr);
	TestReader->addJoint(Joint2NameStr);
	TestReader->LODCount = 1;
	return TestReader;
}

void FRigUnit_RigLogic::TestAccessor::Exec_MapJoints(FSharedRigRuntimeContext* Context, FRigBoneHierarchy* TestHierarchy)
{
	Unit->Data.MapJoints(Context, TestHierarchy);
}

/** ====== Map Morph Targets ===== **/

TSharedPtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderNoBlendshapes(FString MeshNameStr)
{
	TSharedPtr<TestBehaviorReader> BehaviorReader = MakeShared<TestBehaviorReader>();
	BehaviorReader->addMeshName(*MeshNameStr);
	BehaviorReader->LODCount = 1; //there is one mesh, so LODs exist
	return BehaviorReader;
}

TSharedPtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderOneBlendShape(FString MeshNameStr, FString BlendShapeNameStr)
{
	TSharedPtr<TestBehaviorReader> BehaviorReader = MakeShared<TestBehaviorReader>();
	BehaviorReader->addBlendShapeChannelName(*BlendShapeNameStr);
	BehaviorReader->addBlendShapeChannelName(*BlendShapeNameStr);
	BehaviorReader->addMeshName(*MeshNameStr);
	BehaviorReader->addBlendShapeMapping(0, 0);
	BehaviorReader->addBlendShapeMappingIndicesToLOD(0, 0); //mapping 0 to LOD0
	BehaviorReader->LODCount = 1;

	return BehaviorReader;
}

TSharedPtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderTwoBlendShapes(FString MeshNameStr, FString BlendShape1Str, FString BlendShape2Str)
{
	TSharedPtr<TestBehaviorReader> BehaviorReader = MakeShared<TestBehaviorReader>();
	BehaviorReader->addBlendShapeChannelName(*BlendShape1Str);
	BehaviorReader->addBlendShapeChannelName(*BlendShape2Str);
	BehaviorReader->addMeshName(*MeshNameStr);
	BehaviorReader->addBlendShapeMapping(0, 0);
	BehaviorReader->addBlendShapeMapping(0, 1);
	//call 	BehaviorReader->addBlendShapeMappingIndicesToLOD(x, y) outside of this method to assign to various LODs
	BehaviorReader->LODCount = 1;
	return BehaviorReader;
}

TUniquePtr<FRigCurveContainer> FRigUnit_RigLogic::TestAccessor::CreateCurveContainerOneMorphTarget(FString MorphTargetStr)
{
	TUniquePtr<FRigCurveContainer> ValidCurveContainer = MakeUnique<FRigCurveContainer>();
	ValidCurveContainer->Add(FName(*MorphTargetStr));
	ValidCurveContainer->Initialize();
	return ValidCurveContainer;
}

TUniquePtr<FRigCurveContainer> FRigUnit_RigLogic::TestAccessor::CreateCurveContainerTwoMorphTargets(FString MorphTarget1Str, FString MorphTarget2Str)
{
	TUniquePtr<FRigCurveContainer> ValidCurveContainer = MakeUnique<FRigCurveContainer>();
	ValidCurveContainer->Add(FName(*MorphTarget1Str));
	ValidCurveContainer->Add(FName(*MorphTarget2Str));
	ValidCurveContainer->Initialize();
	return ValidCurveContainer;
}

void FRigUnit_RigLogic::TestAccessor::Exec_MapMorphTargets(FSharedRigRuntimeContext* Context, FRigCurveContainer* TestCurveContainer)
{
	//put into a separate method so we can access the private Data member
	Unit->Data.MapMorphTargets(Context, TestCurveContainer); 
}

/** ====== Map Mask Multipliers ===== **/

TSharedPtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderOneAnimatedMap(FString AnimatedMapNameStr)
{
	TSharedPtr<TestBehaviorReader> BehaviorReader = MakeShared<TestBehaviorReader>();
	BehaviorReader->animatedMaps.Add(AnimatedMapNameStr);
	BehaviorReader->addAnimatedMapIndicesToLOD(0, 0);
	BehaviorReader->LODCount = 1;
	return BehaviorReader;
}

void FRigUnit_RigLogic::TestAccessor::Exec_MapMaskMultipliers(FSharedRigRuntimeContext* Context, FRigCurveContainer* TestCurveContainer)
{
	Unit->Data.MapMaskMultipliers(Context, TestCurveContainer); //inside the method so we can access data, which is a private member
}

void FRigUnit_RigLogic::TestAccessor::AddToTransformArray(float* InArray, FTransform& Transform)
{
	uint32 FirstAttributeIndex = 0;
	
	FVector Rotation = Transform.GetRotation().Euler();
	InArray[FirstAttributeIndex + 0] = Rotation.X;
	InArray[FirstAttributeIndex + 1] = Rotation.Y;
	InArray[FirstAttributeIndex + 2] = Rotation.Z;

	FVector Translation = Transform.GetTranslation();
	InArray[FirstAttributeIndex + 3] = Translation.X;
	InArray[FirstAttributeIndex + 4] = Translation.Y;
	InArray[FirstAttributeIndex + 5] = Translation.Z;

	FVector Scale = Transform.GetScale3D();
	InArray[FirstAttributeIndex + 6] = Scale.X;
	InArray[FirstAttributeIndex + 7] = Scale.Y;
	InArray[FirstAttributeIndex + 8] = Scale.Z;
}


FTransformArrayView FRigUnit_RigLogic::TestAccessor::CreateTwoJointNeutralTransforms(float *InValueArray)
{
	float* Transform1Ptr = InValueArray;

	FTransform Joint1Transform;
	Joint1Transform.TransformRotation(FQuat::MakeFromEuler(FVector(1.0f, 0.0f, 0.0f)));
	Joint1Transform.TransformPosition(FVector(1.0f, 0.0f, 0.0f));
	Joint1Transform.SetScale3D(FVector(1.0f, 1.0f, 0.0f));
	AddToTransformArray(Transform1Ptr, Joint1Transform);

	float* Transform2Ptr = InValueArray + MAX_ATTRS_PER_JOINT;
	FTransform Joint2Transform;
	Joint2Transform.TransformRotation(FQuat::MakeFromEuler(FVector(1.0f, 0.0f, 0.0f)));
	Joint2Transform.TransformPosition(FVector(1.0f, 0.0f, 0.0f));
	Joint2Transform.SetScale3D(FVector(1.0f, 1.0f, 0.0f));
	AddToTransformArray(Transform2Ptr, Joint2Transform);

	const float* ValuesPtr = InValueArray;
	return FTransformArrayView(ValuesPtr, sizeof(FTransform));
}

TArrayView<const uint16> FRigUnit_RigLogic::TestAccessor::CreateTwoJointVariableAttributes(uint16* InVariableAttributeIndices, uint8 LOD)
{
	InVariableAttributeIndices[0] = 0;
	InVariableAttributeIndices[1] = 1;
	InVariableAttributeIndices[2] = 2;
	InVariableAttributeIndices[3] = 3;
	InVariableAttributeIndices[4] = 4;
	InVariableAttributeIndices[5] = 5;
	InVariableAttributeIndices[6] = 6;
	InVariableAttributeIndices[7] = 7;
	InVariableAttributeIndices[8] = 8;

	if (LOD == 0) //LOD0 includes attributes for both bones
	{
		InVariableAttributeIndices[9 + 0] = 9 + 0;
		InVariableAttributeIndices[9 + 1] = 9 + 1;
		InVariableAttributeIndices[9 + 2] = 9 + 2;
		InVariableAttributeIndices[9 + 3] = 9 + 3;
		InVariableAttributeIndices[9 + 4] = 9 + 4;
		InVariableAttributeIndices[9 + 5] = 9 + 5;
		InVariableAttributeIndices[9 + 6] = 9 + 6;
		InVariableAttributeIndices[9 + 7] = 9 + 7;
		InVariableAttributeIndices[9 + 8] = 9 + 8;
	
		return TArrayView<const uint16>(InVariableAttributeIndices, 2 * FRigUnit_RigLogic::TestAccessor::MAX_ATTRS_PER_JOINT);
	}

	return TArrayView<const uint16>(InVariableAttributeIndices, FRigUnit_RigLogic::TestAccessor::MAX_ATTRS_PER_JOINT);
}

void FRigUnit_RigLogic::TestAccessor::Exec_UpdateJoints(FSharedRigRuntimeContext* Context, FRigHierarchyContainer* TestHierarchyContainer, FRigUnit_RigLogic_JointUpdateParams& JointUpdateParams)
{
	Unit->Data.UpdateJoints(Context, TestHierarchyContainer, JointUpdateParams);
}

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_RigLogic)
{
	FRigUnit_RigLogic::TestAccessor Test(&Unit);

	TSharedPtr<FSharedRigRuntimeContext> SharedRigRuntimeContext = MakeShared<FSharedRigRuntimeContext>();
	Test.GetData()->SharedRigRuntimeContext = SharedRigRuntimeContext;
	//=============== INPUT CURVES MAPPING ====================


	//=== MapInputCurve ValidReader ValidCurvesNameMismatch ===

	//Prepare
	TSharedPtr<TestBehaviorReader> TestReaderValid = Test.CreateBehaviorReaderOneCurve("CTRL_Expressions.Some_Control");
	TUniquePtr<FRigCurveContainer> TestCurveContainerNameMismatch = Test.CreateCurveContainerOneCurve("CTRL_Expressions_NOT_ThatControl");
	SharedRigRuntimeContext->BehaviorReader = TestReaderValid;
	//Test
	Test.Exec_MapInputCurve(SharedRigRuntimeContext.Get(), TestCurveContainerNameMismatch.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->InputCurveIndices.Num() == 1 &&
		SharedRigRuntimeContext->InputCurveIndices[0] == INDEX_NONE,
		TEXT("MapInputCurve - ValidReader CurveContainerWithNameMismatch")
	);

	//=== MapInputCurve EmptyReader ValidCurve ===

	//Prepare
	TSharedPtr<TestBehaviorReader> TestReaderEmpty = Test.CreateBehaviorReaderEmpty();
	TUniquePtr<FRigCurveContainer> TestCurveContainerValid = Test.CreateCurveContainerOneCurve("CTRL_Expressions_Some_Control");
	SharedRigRuntimeContext->BehaviorReader = TestReaderEmpty;
	//Test
	Test.Exec_MapInputCurve(SharedRigRuntimeContext.Get(), TestCurveContainerValid.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->InputCurveIndices.Num() == 0,
		TEXT("MapInputCurve - EmptyReader ValidCurveContainer")
	);

	//=== MapInputCurve ValidReader EmptyCurveContainer ===

	//Prepare
	TUniquePtr<FRigCurveContainer> TestCurveContainerEmpty = Test.CreateCurveContainerEmpty();
	TestCurveContainerEmpty->Initialize();
	SharedRigRuntimeContext->BehaviorReader = TestReaderValid;
	//Test
	Test.Exec_MapInputCurve(SharedRigRuntimeContext.Get(), TestCurveContainerEmpty.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->InputCurveIndices.Num() == 1 &&
		SharedRigRuntimeContext->InputCurveIndices[0] == INDEX_NONE,
		TEXT("MapInputCurve - ValidReader EmptyCurveContainer")
	);

	//=== MapInputCurve InvalidReader ValidCurveContainer ===
	//Prepare
	TSharedPtr<TestBehaviorReader> TestInvalidReader = Test.CreateBehaviorReaderOneCurve("InvalidControlNameNoDot");
	SharedRigRuntimeContext->BehaviorReader = TestInvalidReader;
	//Expected error
	AddExpectedError("RigUnit_R: Missing '.' in ");
	//Test
	Test.Exec_MapInputCurve(SharedRigRuntimeContext.Get(), TestCurveContainerValid.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->InputCurveIndices.Num() == 0,
		TEXT("MapInputCurve - InvalidReader ValidCurveContainer")
	);

	//=== MapInputCurve Valid Inputs ===

	//Prepare
	//  ---
	//Test
	SharedRigRuntimeContext->BehaviorReader = TestReaderValid;
	Test.Exec_MapInputCurve(SharedRigRuntimeContext.Get(), TestCurveContainerValid.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->InputCurveIndices.Num() == 1 &&
		SharedRigRuntimeContext->InputCurveIndices[0] == 0,
		TEXT("MapInputCurve - Valid Inputs")
	);

	//===================== JOINTS MAPPING =====================


	//=== MapJoints EmptyInputs ===
	//Prepare
	TUniquePtr<FRigBoneHierarchy> TestHierarchyEmpty = Test.CreateBoneHierarchyEmpty();
	SharedRigRuntimeContext->BehaviorReader = TestReaderEmpty;
	//Test
	Test.Exec_MapJoints(SharedRigRuntimeContext.Get(), TestHierarchyEmpty.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->HierarchyBoneIndices.Num() == 0,
		TEXT("MapJoints - Empty Inputs")
	);

	//=== MapJoints EmptyReader TwoBones ===
	//Prepare
	TUniquePtr<FRigBoneHierarchy> TestHierarchyTwoBones = Test.CreateBoneHierarchyTwoBones("BoneA", "BoneB");
	SharedRigRuntimeContext->BehaviorReader = TestReaderEmpty;
	//Test
	Test.Exec_MapJoints(SharedRigRuntimeContext.Get(), TestHierarchyTwoBones.Get());
	AddErrorIfFalse(
		SharedRigRuntimeContext->HierarchyBoneIndices.Num() == 0,
		TEXT("MapJoints - EmptyReader TwoBones")
	);

	//=== MapJoints TwoJoints NoBones ===
	//Prepare
	TSharedPtr<TestBehaviorReader> TestReaderTwoJoints = Test.CreateBehaviorReaderTwoJoints("BoneA", "BoneB");
	SharedRigRuntimeContext->BehaviorReader = TestReaderTwoJoints;
	//Test
	Test.Exec_MapJoints(SharedRigRuntimeContext.Get(), TestHierarchyEmpty.Get());
	AddErrorIfFalse(
		SharedRigRuntimeContext->HierarchyBoneIndices.Num() == 2,
		TEXT("MapJoints - TwoJoints NoBones - expected 2 bone indices")
	);
	AddErrorIfFalse(
		SharedRigRuntimeContext->HierarchyBoneIndices.Num() == 2 && //repeated condition to prevent crash
		SharedRigRuntimeContext->HierarchyBoneIndices[0] == INDEX_NONE,
		TEXT("MapJoints - TwoJoints NoBones - Expected joint 0 index to be NONE")
	);
	AddErrorIfFalse(
		SharedRigRuntimeContext->HierarchyBoneIndices.Num() == 2 && //repeated condition to prevent crash
		SharedRigRuntimeContext->HierarchyBoneIndices[1] == INDEX_NONE,
		TEXT("MapJoints - TwoJoints NoBones - Expected joint 1 index to be NONE")
	);

	//=== MapJoints TwoJoints TwoBones ===
	//Prepare
	//  already done
	//Test
	SharedRigRuntimeContext->BehaviorReader = TestReaderTwoJoints;
	Test.Exec_MapJoints(SharedRigRuntimeContext.Get(), TestHierarchyTwoBones.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->HierarchyBoneIndices.Num() == 2,
		TEXT("MapJoints - TwoJoints TwoBones - Expected 2 bone indices")
	);
	AddErrorIfFalse(
		SharedRigRuntimeContext->HierarchyBoneIndices.Num() == 2 && //prevent crash
		SharedRigRuntimeContext->HierarchyBoneIndices[0] == 0,
		TEXT("MapJoints - TwoJoints TwoBones - Expected bone 0 index to be 0")
	);
	AddErrorIfFalse(
		SharedRigRuntimeContext->HierarchyBoneIndices.Num() == 2 && //prevent crash
		SharedRigRuntimeContext->HierarchyBoneIndices[1] == 1,
		TEXT("MapJoints - TwoJoints TwoBones - Expected bone index 1 index to be 1")
	);

	//===================== BLENDSHAPES MAPPING =====================


	//=== MapMorphTargets ValidReader MorphTargetWithNameMismatch ===

	//Prepare
	TUniquePtr<FRigCurveContainer> TestMorphTargetNameMismatch = Test.CreateCurveContainerOneMorphTarget("head_NOT_that_blendshape");
	TSharedPtr<TestBehaviorReader> TestReaderBlendshapeValid = Test.CreateBehaviorReaderOneBlendShape("head", "blendshape");
	SharedRigRuntimeContext->BehaviorReader = TestReaderBlendshapeValid;
	//Test
	Test.Exec_MapMorphTargets(SharedRigRuntimeContext.Get(), TestMorphTargetNameMismatch.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 1 && //LOD 0
		SharedRigRuntimeContext->MorphTargetCurveIndices.Num() == 1 && //LOD 0
		SharedRigRuntimeContext->BlendShapeIndices[0].Values.Num() == 1 && //at least one blendshape exists
		SharedRigRuntimeContext->BlendShapeIndices[0].Values[0] == 0 && //has index 0
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values.Num() == 1 && //but morph target corresponding to that blendshape
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values[0] == INDEX_NONE, //wasn't found
		TEXT("MapMorphTargets - ValidReader MorphTargetWithNameMismatch")
	);

	//=== MapMorphTargets EmptyReader ValidMorphTargetCurve ===

	//Prepare
	//Empty reader (no meshes, no blendshapes)
	TUniquePtr<FRigCurveContainer> TestMorphTargetCurveValid = Test.CreateCurveContainerOneMorphTarget("head__blendshape");
	SharedRigRuntimeContext->BehaviorReader = TestReaderEmpty;
	//Test
	Test.Exec_MapMorphTargets(SharedRigRuntimeContext.Get(), TestMorphTargetCurveValid.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->MorphTargetCurveIndices.Num() == 0 &&
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 0,
		TEXT("MapMorphTargets - EmptyReader ValidMorphTargetCurve")
	);


	//=== MapMorphTargets NoBlendShapes ValidMorphTargetCurve ===

	//Prepare
	TSharedPtr<TestBehaviorReader> TestReaderNoBlendshapes = Test.CreateBehaviorReaderNoBlendshapes("head"); //has a mesh, but no blend shapes
	SharedRigRuntimeContext->BehaviorReader = TestReaderNoBlendshapes;
	//Test
	Test.Exec_MapMorphTargets(SharedRigRuntimeContext.Get(), TestMorphTargetCurveValid.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 1 && //LOD 0 exists
		SharedRigRuntimeContext->BlendShapeIndices[0].Values.Num() == 0 && //but no blend shapes mapped
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values.Num() == 0, //or morph targets
		TEXT("MapMorphTargets - NoBlendShapes ValidMorphTargetCurve")
	);

	//=== MapMorphTargets ValidReader EmptyCurveContainer ===

	//Prepare
	//  ---
	//Test
	SharedRigRuntimeContext->BehaviorReader = TestReaderBlendshapeValid;
	Test.Exec_MapMorphTargets(SharedRigRuntimeContext.Get(), TestCurveContainerEmpty.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 1 && //LOD 0
		SharedRigRuntimeContext->BlendShapeIndices[0].Values.Num() == 1 && //one blend shape
		SharedRigRuntimeContext->BlendShapeIndices[0].Values[0] == 0 && //of index 0
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values.Num() == 1 && //we put in a morph target index corresponding to it
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values[0] == INDEX_NONE, //but just to signal that it is not found
		TEXT("MapMorphTargets - ValidReader EmptyCurveContainer")
	);

	//=== MapMorphTargets InvalidReader ValidMorphTargetCurve ===

	//Prepare
	TSharedPtr<TestBehaviorReader> TestReaderBlendshapesInvalid = Test.CreateBehaviorReaderOneBlendShape("head", "");
	SharedRigRuntimeContext->BehaviorReader = TestReaderBlendshapesInvalid;
	//Test
	Test.Exec_MapMorphTargets(SharedRigRuntimeContext.Get(), TestMorphTargetCurveValid.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 1 && //LOD 0
		SharedRigRuntimeContext->BlendShapeIndices[0].Values.Num() == 1 && //one blend shape (empty named)
		SharedRigRuntimeContext->BlendShapeIndices[0].Values[0] == 0 && //of index 0
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values.Num() == 1 && //we put in a morph target index corresponding to it
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values[0] == INDEX_NONE, //but just to signal that it is not found
		TEXT("MapMorphTargets - InvalidReader ValidMorphTargetCurve")
	);

	//=== MapMorphTargets ValidReader InvalidMorphTargetCurve ===

	//Prepare
	TUniquePtr<FRigCurveContainer> TestMorphTargetCurvesInvalid = Test.CreateCurveContainerOneMorphTarget("");
	SharedRigRuntimeContext->BehaviorReader = TestReaderBlendshapeValid;
	//Test
	Test.Exec_MapMorphTargets(SharedRigRuntimeContext.Get(), TestMorphTargetCurvesInvalid.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 1 && //LOD 0
		SharedRigRuntimeContext->BlendShapeIndices[0].Values.Num() == 1 && //one blend shape
		SharedRigRuntimeContext->BlendShapeIndices[0].Values[0] == 0 && //of index 0
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values.Num() == 1 && //we put in a morph target index corresponding to it
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values[0] == INDEX_NONE, //but just to signal that it is not found
		TEXT("MapMorphTargets - ValidReader InvalidMorphTargetCurve")
	);

	//=== MapMorphTargets Valid Inputs ===

	//Prepare
	//  ---
	//Test
	SharedRigRuntimeContext->BehaviorReader = TestReaderBlendshapeValid;
	Test.Exec_MapMorphTargets(SharedRigRuntimeContext.Get(), TestMorphTargetCurveValid.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 1 && //LOD 0
		SharedRigRuntimeContext->MorphTargetCurveIndices.Num() == 1 && //LOD 0
		SharedRigRuntimeContext->BlendShapeIndices[0].Values.Num() == 1 && //at least one blendshape exists
		SharedRigRuntimeContext->BlendShapeIndices[0].Values[0] == 0 && //has index 0
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values.Num() == 1 && //morph target corresponding to that blendshape exists
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values[0] == 0, //and actually points to the right index
		TEXT("MapMorphTargets - ValidReader ValidTestMorphTarget")
	);

	//=== MapMorphTargets LOD0(AB) LOD1(A) ===

	//Prepare
	TSharedPtr<TestBehaviorReader> TestReaderBlendshapes_LOD0AB_LOD1A = Test.CreateBehaviorReaderTwoBlendShapes("head", "blendshapeA", "blendshapeB");
	TUniquePtr<FRigCurveContainer> TestMorphTargetTwoCurves = Test.CreateCurveContainerTwoMorphTargets("head__blendshapeA", "head__blendshapeB");
	//NOTE: indices in the first param here are not blendshape indices, but rather mappings from blendshapes to meshes
	//in this test, they will correspond to blendshape indices


	TestReaderBlendshapes_LOD0AB_LOD1A->addBlendShapeMappingIndicesToLOD(0, 0); //A -> LOD 0
	TestReaderBlendshapes_LOD0AB_LOD1A->addBlendShapeMappingIndicesToLOD(1, 0); //B -> -||-
	TestReaderBlendshapes_LOD0AB_LOD1A->addBlendShapeMappingIndicesToLOD(0, 1); //A -> LOD 1
	TestReaderBlendshapes_LOD0AB_LOD1A->LODCount = 2; //needs to be set explicitly if not default (=1)

	SharedRigRuntimeContext->BehaviorReader = TestReaderBlendshapes_LOD0AB_LOD1A;
	//Test
	Test.Exec_MapMorphTargets(SharedRigRuntimeContext.Get(), TestMorphTargetTwoCurves.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 2 &&  //2 LODs
		SharedRigRuntimeContext->MorphTargetCurveIndices.Num() == 2,
		TEXT("MapMorphTargets LOD0(AB) LOD1(A) - Expected 2 LODs for both blendshapes and morph targets")
	);

	AddErrorIfFalse(
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 2 &&         //condition repeated for crash prevention
		SharedRigRuntimeContext->BlendShapeIndices[0].Values.Num() == 2 && //two blendshapes at LOD 0
		SharedRigRuntimeContext->BlendShapeIndices[0].Values[0] == 0 &&  // A
		SharedRigRuntimeContext->BlendShapeIndices[0].Values[1] == 1 &&  // B
		SharedRigRuntimeContext->BlendShapeIndices[1].Values.Num() == 1 && //one blendshape at LOD 1
		SharedRigRuntimeContext->BlendShapeIndices[1].Values[0] == 0,    // A
			TEXT("MapMorphTargets LOD0(AB) LOD1(A) - resulting blendshape indices not correct")
	);

	AddErrorIfFalse(
		SharedRigRuntimeContext->MorphTargetCurveIndices.Num() == 2 &&         //condition repeated for crash prevention
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values.Num() == 2 && //two morph targets at LOD 0
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values[0] == 0 &&  // A
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values[1] == 1 &&  // B
		SharedRigRuntimeContext->MorphTargetCurveIndices[1].Values.Num() == 1 && //one morph target at LOD 1
		SharedRigRuntimeContext->MorphTargetCurveIndices[1].Values[0] == 0,    // A
		TEXT("MapMorphTargets LOD0(AB) LOD1(A) - resulting morph target indices not correct")
	);

	//=== MapMorphTargets LOD0(AB) LOD1(-) ===

	//Prepare
	TSharedPtr<TestBehaviorReader> TestReaderBlendshapes_LOD0AB_LOD1N = Test.CreateBehaviorReaderTwoBlendShapes("head", "blendshapeA", "blendshapeB");
	TestReaderBlendshapes_LOD0AB_LOD1N->addBlendShapeMappingIndicesToLOD(0, 0); //LOD 0
	TestReaderBlendshapes_LOD0AB_LOD1N->addBlendShapeMappingIndicesToLOD(1, 0); //LOD 0
	//LODCount = 1 by default

	//Test
	SharedRigRuntimeContext->BehaviorReader = TestReaderBlendshapes_LOD0AB_LOD1N;
	Test.Exec_MapMorphTargets(SharedRigRuntimeContext.Get(), TestMorphTargetTwoCurves.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 1 &&  //1 LOD
		SharedRigRuntimeContext->MorphTargetCurveIndices.Num() == 1, //1 LOD
		TEXT("MapMorphTargets LOD0(AB) LOD1(-) - Expected 1 LOD for both blendshapes and morph targets")
	);


	AddErrorIfFalse(
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 1 &&         //condition repeated for crash prevention
		SharedRigRuntimeContext->BlendShapeIndices[0].Values.Num() == 2 && //two blendshapes at LOD 0
		SharedRigRuntimeContext->BlendShapeIndices[0].Values[0] == 0 &&  // A
		SharedRigRuntimeContext->BlendShapeIndices[0].Values[1] == 1,    // B
		TEXT("MapMorphTargets LOD0(AB) LOD1(-) - Resulting blendshapes not correct")
	);

	AddErrorIfFalse(
		SharedRigRuntimeContext->MorphTargetCurveIndices.Num() == 1 &&         //condition repeated for crash prevention
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values.Num() == 2 && //two morph targets at LOD 0
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values[0] == 0 &&  // A
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values[1] == 1,    // B
		TEXT("MapMorphTargets LOD0(AB) LOD1(-) - Resulting morph targets not correct")
	);

	//=== MapMorphTargets LOD0(A) LOD1(B) ===
	//Prepare
	TSharedPtr<TestBehaviorReader> TestReaderBlendshapes_LOD0A_LOD1B = Test.CreateBehaviorReaderTwoBlendShapes("head", "blendshapeA", "blendshapeB");
	TestReaderBlendshapes_LOD0A_LOD1B->addBlendShapeMappingIndicesToLOD(0, 0);
	TestReaderBlendshapes_LOD0A_LOD1B->addBlendShapeMappingIndicesToLOD(1, 1);
	TestReaderBlendshapes_LOD0A_LOD1B->LODCount = 2;

	//Test
	SharedRigRuntimeContext->BehaviorReader = TestReaderBlendshapes_LOD0A_LOD1B;
	Test.Exec_MapMorphTargets(SharedRigRuntimeContext.Get(), TestMorphTargetTwoCurves.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 2 &&  //2 LODs
		SharedRigRuntimeContext->MorphTargetCurveIndices.Num() == 2, //2 LODs
		TEXT("MapMorphTargets LOD0(A) LOD1(B) - Expected 2 LODs for both blendshapes and morph targets")
	);

	AddErrorIfFalse(
		SharedRigRuntimeContext->BlendShapeIndices.Num() == 2 &&         //condition repeated for crash prevention
		SharedRigRuntimeContext->BlendShapeIndices[0].Values.Num() == 1 && //1 blendshape at LOD 0
		SharedRigRuntimeContext->BlendShapeIndices[0].Values[0] == 0 &&  // A
		SharedRigRuntimeContext->BlendShapeIndices[1].Values.Num() == 1 && //1 blendshape at LOD 1
		SharedRigRuntimeContext->BlendShapeIndices[1].Values[0] == 1,    // B
		TEXT("MapMorphTargets LOD0(A) LOD1(B) - Resulting blendshape indices not correct")
	);

	AddErrorIfFalse(
		SharedRigRuntimeContext->MorphTargetCurveIndices.Num() == 2 &&         //condition repeated for crash prevention
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values.Num() == 1 && //1 morph target at LOD 0
		SharedRigRuntimeContext->MorphTargetCurveIndices[0].Values[0] == 0 &&  // A
		SharedRigRuntimeContext->MorphTargetCurveIndices[1].Values.Num() == 1 && //1 morph target at LOD 1
		SharedRigRuntimeContext->MorphTargetCurveIndices[1].Values[0] == 1,    // B
		TEXT("MapMorphTargets LOD0(A) LOD1(B) - Resulting morph target indices not correct")
	);

	//=============== MASK MULTIPLIERS MAPPING ====================


	//=== MapMaskMultipliers ValidReader ValidAnimatedMapNameMismatch ===

	//Prepare
	TSharedPtr<TestBehaviorReader> TestReaderAnimMapsValid = Test.CreateBehaviorReaderOneAnimatedMap("CTRL_AnimMap.Some_Multiplier");
	TUniquePtr<FRigCurveContainer> TestCurveContainerForAnimMapsNameMismatch = Test.CreateCurveContainerOneCurve("CTRL_AnimMap_NOT_ThatMultiploer");
	//Test
	SharedRigRuntimeContext->BehaviorReader = TestReaderAnimMapsValid;
	Test.Exec_MapMaskMultipliers(SharedRigRuntimeContext.Get(), TestCurveContainerForAnimMapsNameMismatch.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->CurveContainerIndicesForAnimMaps.Num() == 1 &&
		SharedRigRuntimeContext->CurveContainerIndicesForAnimMaps[0].Values.Num() == 1 &&
		SharedRigRuntimeContext->CurveContainerIndicesForAnimMaps[0].Values[0] == INDEX_NONE &&
		SharedRigRuntimeContext->RigLogicIndicesForAnimMaps.Num() == 1 &&
		SharedRigRuntimeContext->RigLogicIndicesForAnimMaps[0].Values.Num() == 1 &&
		SharedRigRuntimeContext->RigLogicIndicesForAnimMaps[0].Values[0] == 0,
		TEXT("MapMaskMultipliers - ValidReader ValidAnimatedMapNameMismatch")
	);

	//=== MapMaskMultipliers EmptyReader ValidAnimatedMap ===

	//Prepare
	TUniquePtr<FRigCurveContainer> TestCurveContainerForAnimMapsValid = Test.CreateCurveContainerOneCurve("CTRL_AnimMap_Some_Multiplier");
	//Test
	SharedRigRuntimeContext->BehaviorReader = TestReaderEmpty;
	Test.Exec_MapMaskMultipliers(SharedRigRuntimeContext.Get(), TestCurveContainerForAnimMapsValid.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->CurveContainerIndicesForAnimMaps.Num() == 0 &&
		SharedRigRuntimeContext->RigLogicIndicesForAnimMaps.Num() == 0,
		TEXT("MapMaskMultipliers - EmptyReader ValidAnimatedMap")
	);

	//=== MapMaskMultipliers ValidReader EmptyCurveContainer ===

	//Test
	SharedRigRuntimeContext->BehaviorReader = TestReaderAnimMapsValid;
	Test.Exec_MapMaskMultipliers(SharedRigRuntimeContext.Get(), TestCurveContainerEmpty.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->CurveContainerIndicesForAnimMaps.Num() == 1 &&
		SharedRigRuntimeContext->CurveContainerIndicesForAnimMaps[0].Values.Num() == 1 &&
		SharedRigRuntimeContext->CurveContainerIndicesForAnimMaps[0].Values[0] == INDEX_NONE &&
		SharedRigRuntimeContext->RigLogicIndicesForAnimMaps.Num() == 1 &&
		SharedRigRuntimeContext->RigLogicIndicesForAnimMaps[0].Values.Num() == 1 &&
		SharedRigRuntimeContext->RigLogicIndicesForAnimMaps[0].Values[0] == 0,
		TEXT("MapMaskMultipliers - ValidReader EmptyCurveContainer")
	);

	//=== MapMaskMultipliers Valid Inputs ===

	//Prepare
	//  ---
	//Test
	SharedRigRuntimeContext->BehaviorReader = TestReaderAnimMapsValid;
	Test.Exec_MapMaskMultipliers(SharedRigRuntimeContext.Get(), TestCurveContainerForAnimMapsValid.Get());
	//Assert
	AddErrorIfFalse(
		SharedRigRuntimeContext->CurveContainerIndicesForAnimMaps.Num() == 1 &&
		SharedRigRuntimeContext->CurveContainerIndicesForAnimMaps[0].Values.Num() == 1 &&
		SharedRigRuntimeContext->CurveContainerIndicesForAnimMaps[0].Values[0] == 0 &&
		SharedRigRuntimeContext->RigLogicIndicesForAnimMaps.Num() == 1 &&
		SharedRigRuntimeContext->RigLogicIndicesForAnimMaps[0].Values.Num() == 1 &&
		SharedRigRuntimeContext->RigLogicIndicesForAnimMaps[0].Values[0] == 0,
		TEXT("MapMaskMultipliers - Valid Inputs")
	);

	Unit.ExecuteContext.Hierarchy = &HierarchyContainer;
	//BoneHierarchy belongs to HierarchyContainer
	BoneHierarchy = *TestHierarchyTwoBones;
	BoneHierarchy.Initialize();
	Unit.ExecuteContext.Hierarchy = &HierarchyContainer;
	BoneHierarchy.ResetTransforms();
	
	FRigHierarchyContainer* HierarchyContainerPtr = &HierarchyContainer;

	//Prepare
	//-----
	//create neutral transforms for two bones
	const uint8 TransformArraySize = 2 * FRigUnit_RigLogic::TestAccessor::MAX_ATTRS_PER_JOINT;
	float Values[TransformArraySize];  //two bones, nine attributes
	FTransformArrayView TwoJointNeutralTransforms = Test.CreateTwoJointNeutralTransforms(Values);
	//create delta transforms
	float DeltaTransformData[TransformArraySize] = { 0.f };
	//first bone translation
	DeltaTransformData[0] = 1.f;
	DeltaTransformData[1] = 0.f;
	DeltaTransformData[2] = 0.f;
	//second bone translation
	DeltaTransformData[9] = 1.f;
	DeltaTransformData[10] = 2.f;
	DeltaTransformData[11] = 7.f;
	FTransformArrayView DeltaTransforms = FTransformArrayView(DeltaTransformData, sizeof(FTransform));
	//create variable attribute index arrays for two bones
	uint16 VariableAttributeIndices_LOD0[TransformArraySize];
	TArrayView<const uint16> VariableAttributes_LOD0 = Test.CreateTwoJointVariableAttributes(VariableAttributeIndices_LOD0, 0); //LOD0: both bones included
	FRigUnit_RigLogic_JointUpdateParams TestJointUpdateParamsTwoJoints_LOD0(
		VariableAttributes_LOD0,
		TwoJointNeutralTransforms,
		DeltaTransforms);
	Test.GetData()->UpdatedJoints.SetNumZeroed(2);
	//Test
	Test.Exec_UpdateJoints(SharedRigRuntimeContext.Get(), HierarchyContainerPtr, TestJointUpdateParamsTwoJoints_LOD0);
	//Assert
	//Note that BoneB.GlobalTransform.Z should be zero since the scale.Z is zero. Also, translation Y becomes -Y 
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("UpdateJoints LOD0 Bone 01 - unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(2.f, -2.f, 0.f)), TEXT("UpdateJoints LOD0 Bone 02 - unexpected transform"));


	//Prepare
	//-----
	//Create skeleton, skeletal mesh and skeletal mesh component
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage());
	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(GetTransientPackage());
	SkeletalMesh->SetSkeleton(Skeleton);
	USkeletalMeshComponent* SkelMeshComponent = NewObject<USkeletalMeshComponent>();
	SkelMeshComponent->SetSkeletalMesh(SkeletalMesh);

	const FString DNAAssetFileName(TEXT("rl_unit_behavior_test.dna"));
	const FString DNAFolder = FPackageName::LongPackageNameToFilename(TEXT("/RigLogic/Test/DNA/"));
	FString FullFolderPath = FPaths::ConvertRelativePathToFull(DNAFolder);
	FString DNAFilePath = FPaths::Combine(FullFolderPath, DNAAssetFileName);
	UDNAAsset* MockDNAAsset = NewObject< UDNAAsset >(SkeletalMesh, FName(*DNAAssetFileName)); //SkelMesh has to be its outer, otherwise DNAAsset won't be saved			
	if (MockDNAAsset->Init(DNAFilePath))  //will set BehaviorReader we need to execute the rig unit
	{
		UAssetUserData* DNAAssetUserData = Cast<UAssetUserData>(MockDNAAsset);
		SkeletalMesh->AddAssetUserData(DNAAssetUserData);
	}

	Test.GetData()->SkelMeshComponent = SkelMeshComponent;

	//Test
	InitAndExecute();
	SharedRigRuntimeContext = MockDNAAsset->GetSharedRigRuntimeContext();
	//Assert
	AddErrorIfFalse(
		// Check rig logic initialized
		SharedRigRuntimeContext->RigLogic != nullptr &&
		Test.GetData()->RigInstance != nullptr &&
		
		// Check joints
		SharedRigRuntimeContext->HierarchyBoneIndices.Num() > 0 &&
		
		// Check input curves
		SharedRigRuntimeContext->InputCurveIndices.Num() > 0 &&
		
		// Check morph targets
		SharedRigRuntimeContext->MorphTargetCurveIndices.Num() > 0 &&
		SharedRigRuntimeContext->BlendShapeIndices.Num() > 0 &&

		// Check mask multipliers
		SharedRigRuntimeContext->RigLogicIndicesForAnimMaps.Num() > 0 &&
		SharedRigRuntimeContext->CurveContainerIndicesForAnimMaps.Num() > 0,

		TEXT("InitAndExecute failed to initialize rig logic.")
	);

	return true;
}

#endif
