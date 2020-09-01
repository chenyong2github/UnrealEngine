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

TUniquePtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderEmpty()
{
	return MakeUnique<TestBehaviorReader>();
}

TUniquePtr<FRigCurveContainer> FRigUnit_RigLogic::TestAccessor::CreateCurveContainerEmpty()
{
	return MakeUnique<FRigCurveContainer>();
}

TUniquePtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderOneCurve(FString ControlNameStr)
{
	TUniquePtr<TestBehaviorReader> BehaviorReader = MakeUnique<TestBehaviorReader>();
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

void FRigUnit_RigLogic::TestAccessor::Exec_MapInputCurve(TestBehaviorReader* TestReader, FRigCurveContainer* TestCurveContainer)
{
	Unit->Data.MapInputCurveIndices(TestReader, TestCurveContainer); //inside the method so we can access data, which is a private member
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

TUniquePtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderTwoJoints(FString Joint1NameStr, FString Joint2NameStr)
{
	TUniquePtr<TestBehaviorReader> TestReader = MakeUnique<TestBehaviorReader>();
	TestReader->addJoint(Joint1NameStr);
	TestReader->addJoint(Joint2NameStr);
	TestReader->LODCount = 1;
	return TestReader;
}

void FRigUnit_RigLogic::TestAccessor::Exec_MapJoints(TestBehaviorReader* TestReader, FRigBoneHierarchy* TestHierarchy)
{
	Unit->Data.MapJoints(TestReader, TestHierarchy);
}

/** ====== Map Morph Targets ===== **/

TUniquePtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderNoBlendshapes(FString MeshNameStr)
{
	TUniquePtr<TestBehaviorReader> BehaviorReader = MakeUnique<TestBehaviorReader>();
	BehaviorReader->addMeshName(*MeshNameStr);
	BehaviorReader->LODCount = 1; //there is one mesh, so LODs exist
	return BehaviorReader;
}

TUniquePtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderOneBlendShape(FString MeshNameStr, FString BlendShapeNameStr)
{
	TUniquePtr<TestBehaviorReader> BehaviorReader = MakeUnique<TestBehaviorReader>();
	BehaviorReader->addBlendShapeChannelName(*BlendShapeNameStr);
	BehaviorReader->addBlendShapeChannelName(*BlendShapeNameStr);
	BehaviorReader->addMeshName(*MeshNameStr);
	BehaviorReader->addBlendShapeMapping(0, 0);
	BehaviorReader->addBlendShapeMappingIndicesToLOD(0, 0); //mapping 0 to LOD0
	BehaviorReader->LODCount = 1;

	return BehaviorReader;
}

TUniquePtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderTwoBlendShapes(FString MeshNameStr, FString BlendShape1Str, FString BlendShape2Str)
{
	TUniquePtr<TestBehaviorReader> BehaviorReader = MakeUnique<TestBehaviorReader>();
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

void FRigUnit_RigLogic::TestAccessor::Exec_MapMorphTargets(TestBehaviorReader* TestReader, FRigCurveContainer* TestCurveContainer)
{
	//put into a separate method so we can access the private Data member
	Unit->Data.MapMorphTargets(TestReader, TestCurveContainer); 
}

/** ====== Map Mask Multipliers ===== **/

TUniquePtr<TestBehaviorReader> FRigUnit_RigLogic::TestAccessor::CreateBehaviorReaderOneAnimatedMap(FString AnimatedMapNameStr)
{
	TUniquePtr<TestBehaviorReader> BehaviorReader = MakeUnique<TestBehaviorReader>();
	BehaviorReader->animatedMaps.Add(AnimatedMapNameStr);
	BehaviorReader->addAnimatedMapIndicesToLOD(0, 0);
	BehaviorReader->LODCount = 1;
	return BehaviorReader;
}

void FRigUnit_RigLogic::TestAccessor::Exec_MapMaskMultipliers(TestBehaviorReader* TestReader, FRigCurveContainer* TestCurveContainer)
{
	Unit->Data.MapMaskMultipliers(TestReader, TestCurveContainer); //inside the method so we can access data, which is a private member
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

void FRigUnit_RigLogic::TestAccessor::Exec_UpdateJoints(FRigHierarchyContainer* TestHierarchyContainer, FRigUnit_RigLogic_JointUpdateParams& JointUpdateParams)
{
	Unit->Data.UpdateJoints(TestHierarchyContainer, JointUpdateParams);
}

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_RigLogic)
{
	FRigUnit_RigLogic::TestAccessor Test(&Unit);


	//=============== INPUT CURVES MAPPING ====================


	//=== MapInputCurve ValidReader ValidCurvesNameMismatch ===

	//Prepare
	TUniquePtr<TestBehaviorReader> TestReaderValid = Test.CreateBehaviorReaderOneCurve("CTRL_Expressions.Some_Control");
	TUniquePtr<FRigCurveContainer> TestCurveContainerNameMismatch = Test.CreateCurveContainerOneCurve("CTRL_Expressions_NOT_ThatControl");
	//Test
	Test.Exec_MapInputCurve(TestReaderValid.Get(), TestCurveContainerNameMismatch.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->InputCurveIndices.Num() == 1 &&
		Test.GetData()->InputCurveIndices[0] == INDEX_NONE,
		TEXT("MapInputCurve - ValidReader CurveContainerWithNameMismatch")
	);

	//=== MapInputCurve EmptyReader ValidCurve ===

	//Prepare
	TUniquePtr<TestBehaviorReader> TestReaderEmpty = Test.CreateBehaviorReaderEmpty();
	TUniquePtr<FRigCurveContainer> TestCurveContainerValid = Test.CreateCurveContainerOneCurve("CTRL_Expressions_Some_Control");
	//Test
	Test.Exec_MapInputCurve(TestReaderEmpty.Get(), TestCurveContainerValid.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->InputCurveIndices.Num() == 0,
		TEXT("MapInputCurve - EmptyReader ValidCurveContainer")
	);

	//=== MapInputCurve ValidReader EmptyCurveContainer ===

	//Prepare
	TUniquePtr<FRigCurveContainer> TestCurveContainerEmpty = Test.CreateCurveContainerEmpty();
	TestCurveContainerEmpty->Initialize();
	//Test
	Test.Exec_MapInputCurve(TestReaderValid.Get(), TestCurveContainerEmpty.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->InputCurveIndices.Num() == 1 &&
		Test.GetData()->InputCurveIndices[0] == INDEX_NONE,
		TEXT("MapInputCurve - ValidReader EmptyCurveContainer")
	);

	//=== MapInputCurve InvalidReader ValidCurveContainer ===
	//Prepare
	TUniquePtr<TestBehaviorReader> TestInvalidReader = Test.CreateBehaviorReaderOneCurve("InvalidControlNameNoDot");
	//Expected error
	AddExpectedError("RigUnit_R: Missing '.' in ");
	//Test
	Test.Exec_MapInputCurve(TestInvalidReader.Get(), TestCurveContainerValid.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->InputCurveIndices.Num() == 0,
		TEXT("MapInputCurve - InvalidReader ValidCurveContainer")
	);

	//=== MapInputCurve Valid Inputs ===

	//Prepare
	//  ---
	//Test
	Test.Exec_MapInputCurve(TestReaderValid.Get(), TestCurveContainerValid.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->InputCurveIndices.Num() == 1 &&
		Test.GetData()->InputCurveIndices[0] == 0,
		TEXT("MapInputCurve - Valid Inputs")
	);

	//===================== JOINTS MAPPING =====================


	//=== MapJoints EmptyInputs ===
	//Prepare
	TUniquePtr<FRigBoneHierarchy> TestHierarchyEmpty = Test.CreateBoneHierarchyEmpty();
	//Test
	Test.Exec_MapJoints(TestReaderEmpty.Get(), TestHierarchyEmpty.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->HierarchyBoneIndices.Num() == 0,
		TEXT("MapJoints - Empty Inputs")
	);

	//=== MapJoints EmptyReader TwoBones ===
	//Prepare
	TUniquePtr<FRigBoneHierarchy> TestHierarchyTwoBones = Test.CreateBoneHierarchyTwoBones("BoneA", "BoneB");
	//Test
	Test.Exec_MapJoints(TestReaderEmpty.Get(), TestHierarchyTwoBones.Get());
	AddErrorIfFalse(
		Test.GetData()->HierarchyBoneIndices.Num() == 0,
		TEXT("MapJoints - EmptyReader TwoBones")
	);

	//=== MapJoints TwoJoints NoBones ===
	//Prepare
	TUniquePtr<TestBehaviorReader> TestReaderTwoJoints = Test.CreateBehaviorReaderTwoJoints("BoneA", "BoneB");
	//Test
	Test.Exec_MapJoints(TestReaderTwoJoints.Get(), TestHierarchyEmpty.Get());
	AddErrorIfFalse(
		Test.GetData()->HierarchyBoneIndices.Num() == 2,
		TEXT("MapJoints - TwoJoints NoBones - expected 2 bone indices")
	);
	AddErrorIfFalse(
		Test.GetData()->HierarchyBoneIndices.Num() == 2 && //repeated condition to prevent crash
		Test.GetData()->HierarchyBoneIndices[0] == INDEX_NONE,
		TEXT("MapJoints - TwoJoints NoBones - Expected joint 0 index to be NONE")
	);
	AddErrorIfFalse(
		Test.GetData()->HierarchyBoneIndices.Num() == 2 && //repeated condition to prevent crash
		Test.GetData()->HierarchyBoneIndices[1] == INDEX_NONE,
		TEXT("MapJoints - TwoJoints NoBones - Expected joint 1 index to be NONE")
	);

	//=== MapJoints TwoJoints TwoBones ===
	//Prepare
	//  already done
	//Test
	Test.Exec_MapJoints(TestReaderTwoJoints.Get(), TestHierarchyTwoBones.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->HierarchyBoneIndices.Num() == 2,
		TEXT("MapJoints - TwoJoints TwoBones - Expected 2 bone indices")
	);
	AddErrorIfFalse(
		Test.GetData()->HierarchyBoneIndices.Num() == 2 && //prevent crash
		Test.GetData()->HierarchyBoneIndices[0] == 0,
		TEXT("MapJoints - TwoJoints TwoBones - Expected bone 0 index to be 0")
	);
	AddErrorIfFalse(
		Test.GetData()->HierarchyBoneIndices.Num() == 2 && //prevent crash
		Test.GetData()->HierarchyBoneIndices[1] == 1,
		TEXT("MapJoints - TwoJoints TwoBones - Expected bone index 1 index to be 1")
	);

	//===================== BLENDSHAPES MAPPING =====================


	//=== MapMorphTargets ValidReader MorphTargetWithNameMismatch ===

	//Prepare
	TUniquePtr<TestBehaviorReader> TestReaderBlendshapeValid = Test.CreateBehaviorReaderOneBlendShape("head", "blendshape");
	TUniquePtr<FRigCurveContainer> TestMorphTargetNameMismatch = Test.CreateCurveContainerOneMorphTarget("head_NOT_that_blendshape");
	//Test
	Test.Exec_MapMorphTargets(TestReaderBlendshapeValid.Get(), TestMorphTargetNameMismatch.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->BlendShapeIndices.Num() == 1 && //LOD 0
		Test.GetData()->MorphTargetCurveIndices.Num() == 1 && //LOD 0
		Test.GetData()->BlendShapeIndices[0].Values.Num() == 1 && //at least one blendshape exists
		Test.GetData()->BlendShapeIndices[0].Values[0] == 0 && //has index 0
		Test.GetData()->MorphTargetCurveIndices[0].Values.Num() == 1 && //but morph target corresponding to that blendshape
		Test.GetData()->MorphTargetCurveIndices[0].Values[0] == INDEX_NONE, //wasn't found
		TEXT("MapMorphTargets - ValidReader MorphTargetWithNameMismatch")
	);

	//=== MapMorphTargets EmptyReader ValidMorphTargetCurve ===

	//Prepare
	//Empty reader (no meshes, no blendshapes)
	TUniquePtr<FRigCurveContainer> TestMorphTargetCurveValid = Test.CreateCurveContainerOneMorphTarget("head__blendshape");
	//Test
	Test.Exec_MapMorphTargets(TestReaderEmpty.Get(), TestMorphTargetCurveValid.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->MorphTargetCurveIndices.Num() == 0 &&
		Test.GetData()->BlendShapeIndices.Num() == 0,
		TEXT("MapMorphTargets - EmptyReader ValidMorphTargetCurve")
	);


	//=== MapMorphTargets NoBlendShapes ValidMorphTargetCurve ===

	//Prepare
	TUniquePtr<TestBehaviorReader> TestReaderNoBlendshapes = Test.CreateBehaviorReaderNoBlendshapes("head"); //has a mesh, but no blend shapes
	//Test
	Test.Exec_MapMorphTargets(TestReaderNoBlendshapes.Get(), TestMorphTargetCurveValid.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->BlendShapeIndices.Num() == 1 && //LOD 0 exists
		Test.GetData()->BlendShapeIndices[0].Values.Num() == 0 && //but no blend shapes mapped
		Test.GetData()->MorphTargetCurveIndices[0].Values.Num() == 0, //or morph targets
		TEXT("MapMorphTargets - NoBlendShapes ValidMorphTargetCurve")
	);

	//=== MapMorphTargets ValidReader EmptyCurveContainer ===

	//Prepare
	//  ---
	//Test
	Test.Exec_MapMorphTargets(TestReaderBlendshapeValid.Get(), TestCurveContainerEmpty.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->BlendShapeIndices.Num() == 1 && //LOD 0
		Test.GetData()->BlendShapeIndices[0].Values.Num() == 1 && //one blend shape
		Test.GetData()->BlendShapeIndices[0].Values[0] == 0 && //of index 0
		Test.GetData()->MorphTargetCurveIndices[0].Values.Num() == 1 && //we put in a morph target index corresponding to it
		Test.GetData()->MorphTargetCurveIndices[0].Values[0] == INDEX_NONE, //but just to signal that it is not found
		TEXT("MapMorphTargets - ValidReader EmptyCurveContainer")
	);

	//=== MapMorphTargets InvalidReader ValidMorphTargetCurve ===

	//Prepare
	TUniquePtr<TestBehaviorReader> TestReaderBlendshapesInvalid = Test.CreateBehaviorReaderOneBlendShape("head", "");
	//Test
	Test.Exec_MapMorphTargets(TestReaderBlendshapesInvalid.Get(), TestMorphTargetCurveValid.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->BlendShapeIndices.Num() == 1 && //LOD 0
		Test.GetData()->BlendShapeIndices[0].Values.Num() == 1 && //one blend shape (empty named)
		Test.GetData()->BlendShapeIndices[0].Values[0] == 0 && //of index 0
		Test.GetData()->MorphTargetCurveIndices[0].Values.Num() == 1 && //we put in a morph target index corresponding to it
		Test.GetData()->MorphTargetCurveIndices[0].Values[0] == INDEX_NONE, //but just to signal that it is not found
		TEXT("MapMorphTargets - InvalidReader ValidMorphTargetCurve")
	);

	//=== MapMorphTargets ValidReader InvalidMorphTargetCurve ===

	//Prepare
	TUniquePtr<FRigCurveContainer> TestMorphTargetCurvesInvalid = Test.CreateCurveContainerOneMorphTarget("");
	//Test
	Test.Exec_MapMorphTargets(TestReaderBlendshapeValid.Get(), TestMorphTargetCurvesInvalid.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->BlendShapeIndices.Num() == 1 && //LOD 0
		Test.GetData()->BlendShapeIndices[0].Values.Num() == 1 && //one blend shape
		Test.GetData()->BlendShapeIndices[0].Values[0] == 0 && //of index 0
		Test.GetData()->MorphTargetCurveIndices[0].Values.Num() == 1 && //we put in a morph target index corresponding to it
		Test.GetData()->MorphTargetCurveIndices[0].Values[0] == INDEX_NONE, //but just to signal that it is not found
		TEXT("MapMorphTargets - ValidReader InvalidMorphTargetCurve")
	);

	//=== MapMorphTargets Valid Inputs ===

	//Prepare
	//  ---
	//Test
	Test.Exec_MapMorphTargets(TestReaderBlendshapeValid.Get(), TestMorphTargetCurveValid.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->BlendShapeIndices.Num() == 1 && //LOD 0
		Test.GetData()->MorphTargetCurveIndices.Num() == 1 && //LOD 0
		Test.GetData()->BlendShapeIndices[0].Values.Num() == 1 && //at least one blendshape exists
		Test.GetData()->BlendShapeIndices[0].Values[0] == 0 && //has index 0
		Test.GetData()->MorphTargetCurveIndices[0].Values.Num() == 1 && //morph target corresponding to that blendshape exists
		Test.GetData()->MorphTargetCurveIndices[0].Values[0] == 0, //and actually points to the right index
		TEXT("MapMorphTargets - ValidReader ValidTestMorphTarget")
	);

	//=== MapMorphTargets LOD0(AB) LOD1(A) ===

	//Prepare
	TUniquePtr<TestBehaviorReader> TestReaderBlendshapes_LOD0AB_LOD1A = Test.CreateBehaviorReaderTwoBlendShapes("head", "blendshapeA", "blendshapeB");
	TUniquePtr<FRigCurveContainer> TestMorphTargetTwoCurves = Test.CreateCurveContainerTwoMorphTargets("head__blendshapeA", "head__blendshapeB");
	//NOTE: indices in the first param here are not blendshape indices, but rather mappings from blendshapes to meshes
	//in this test, they will correspond to blendshape indices


	TestReaderBlendshapes_LOD0AB_LOD1A->addBlendShapeMappingIndicesToLOD(0, 0); //A -> LOD 0
	TestReaderBlendshapes_LOD0AB_LOD1A->addBlendShapeMappingIndicesToLOD(1, 0); //B -> -||-
	TestReaderBlendshapes_LOD0AB_LOD1A->addBlendShapeMappingIndicesToLOD(0, 1); //A -> LOD 1

	TestReaderBlendshapes_LOD0AB_LOD1A->LODCount = 2; //needs to be set explicitly if not default (=1)
	//Test
	Test.Exec_MapMorphTargets(TestReaderBlendshapes_LOD0AB_LOD1A.Get(), TestMorphTargetTwoCurves.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->BlendShapeIndices.Num() == 2 &&  //2 LODs
		Test.GetData()->MorphTargetCurveIndices.Num() == 2,
		TEXT("MapMorphTargets LOD0(AB) LOD1(A) - Expected 2 LODs for both blendshapes and morph targets")
	);

	AddErrorIfFalse(
		Test.GetData()->BlendShapeIndices.Num() == 2 &&         //condition repeated for crash prevention
		Test.GetData()->BlendShapeIndices[0].Values.Num() == 2 && //two blendshapes at LOD 0
		Test.GetData()->BlendShapeIndices[0].Values[0] == 0 &&  // A
		Test.GetData()->BlendShapeIndices[0].Values[1] == 1 &&  // B
		Test.GetData()->BlendShapeIndices[1].Values.Num() == 1 && //one blendshape at LOD 1
		Test.GetData()->BlendShapeIndices[1].Values[0] == 0,    // A
			TEXT("MapMorphTargets LOD0(AB) LOD1(A) - resulting blendshape indices not correct")
	);

	AddErrorIfFalse(
		Test.GetData()->MorphTargetCurveIndices.Num() == 2 &&         //condition repeated for crash prevention
		Test.GetData()->MorphTargetCurveIndices[0].Values.Num() == 2 && //two morph targets at LOD 0
		Test.GetData()->MorphTargetCurveIndices[0].Values[0] == 0 &&  // A
		Test.GetData()->MorphTargetCurveIndices[0].Values[1] == 1 &&  // B
		Test.GetData()->MorphTargetCurveIndices[1].Values.Num() == 1 && //one morph target at LOD 1
		Test.GetData()->MorphTargetCurveIndices[1].Values[0] == 0,    // A
		TEXT("MapMorphTargets LOD0(AB) LOD1(A) - resulting morph target indices not correct")
	);

	//=== MapMorphTargets LOD0(AB) LOD1(-) ===

	//Prepare
	TUniquePtr<TestBehaviorReader> TestReaderBlendshapes_LOD0AB_LOD1N = Test.CreateBehaviorReaderTwoBlendShapes("head", "blendshapeA", "blendshapeB");
	TestReaderBlendshapes_LOD0AB_LOD1N->addBlendShapeMappingIndicesToLOD(0, 0); //LOD 0
	TestReaderBlendshapes_LOD0AB_LOD1N->addBlendShapeMappingIndicesToLOD(1, 0); //LOD 0
	//LODCount = 1 by default

	//Test
	Test.Exec_MapMorphTargets(TestReaderBlendshapes_LOD0AB_LOD1N.Get(), TestMorphTargetTwoCurves.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->BlendShapeIndices.Num() == 1 &&  //1 LOD
		Test.GetData()->MorphTargetCurveIndices.Num() == 1, //1 LOD
		TEXT("MapMorphTargets LOD0(AB) LOD1(-) - Expected 1 LOD for both blendshapes and morph targets")
	);


	AddErrorIfFalse(
		Test.GetData()->BlendShapeIndices.Num() == 1 &&         //condition repeated for crash prevention
		Test.GetData()->BlendShapeIndices[0].Values.Num() == 2 && //two blendshapes at LOD 0
		Test.GetData()->BlendShapeIndices[0].Values[0] == 0 &&  // A
		Test.GetData()->BlendShapeIndices[0].Values[1] == 1,    // B
		TEXT("MapMorphTargets LOD0(AB) LOD1(-) - Resulting blendshapes not correct")
	);

	AddErrorIfFalse(
		Test.GetData()->MorphTargetCurveIndices.Num() == 1 &&         //condition repeated for crash prevention
		Test.GetData()->MorphTargetCurveIndices[0].Values.Num() == 2 && //two morph targets at LOD 0
		Test.GetData()->MorphTargetCurveIndices[0].Values[0] == 0 &&  // A
		Test.GetData()->MorphTargetCurveIndices[0].Values[1] == 1,    // B
		TEXT("MapMorphTargets LOD0(AB) LOD1(-) - Resulting morph targets not correct")
	);

	//=== MapMorphTargets LOD0(A) LOD1(B) ===
	//Prepare
	TUniquePtr<TestBehaviorReader> TestReaderBlendshapes_LOD0A_LOD1B = Test.CreateBehaviorReaderTwoBlendShapes("head", "blendshapeA", "blendshapeB");
	TestReaderBlendshapes_LOD0A_LOD1B->addBlendShapeMappingIndicesToLOD(0, 0);
	TestReaderBlendshapes_LOD0A_LOD1B->addBlendShapeMappingIndicesToLOD(1, 1);
	TestReaderBlendshapes_LOD0A_LOD1B->LODCount = 2;

	//Test
	Test.Exec_MapMorphTargets(TestReaderBlendshapes_LOD0A_LOD1B.Get(), TestMorphTargetTwoCurves.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->BlendShapeIndices.Num() == 2 &&  //2 LODs
		Test.GetData()->MorphTargetCurveIndices.Num() == 2, //2 LODs
		TEXT("MapMorphTargets LOD0(A) LOD1(B) - Expected 2 LODs for both blendshapes and morph targets")
	);

	AddErrorIfFalse(
		Test.GetData()->BlendShapeIndices.Num() == 2 &&         //condition repeated for crash prevention
		Test.GetData()->BlendShapeIndices[0].Values.Num() == 1 && //1 blendshape at LOD 0
		Test.GetData()->BlendShapeIndices[0].Values[0] == 0 &&  // A
		Test.GetData()->BlendShapeIndices[1].Values.Num() == 1 && //1 blendshape at LOD 1
		Test.GetData()->BlendShapeIndices[1].Values[0] == 1,    // B
		TEXT("MapMorphTargets LOD0(A) LOD1(B) - Resulting blendshape indices not correct")
	);

	AddErrorIfFalse(
		Test.GetData()->MorphTargetCurveIndices.Num() == 2 &&         //condition repeated for crash prevention
		Test.GetData()->MorphTargetCurveIndices[0].Values.Num() == 1 && //1 morph target at LOD 0
		Test.GetData()->MorphTargetCurveIndices[0].Values[0] == 0 &&  // A
		Test.GetData()->MorphTargetCurveIndices[1].Values.Num() == 1 && //1 morph target at LOD 1
		Test.GetData()->MorphTargetCurveIndices[1].Values[0] == 1,    // B
		TEXT("MapMorphTargets LOD0(A) LOD1(B) - Resulting morph target indices not correct")
	);

	//=============== MASK MULTIPLIERS MAPPING ====================


	//=== MapMaskMultipliers ValidReader ValidAnimatedMapNameMismatch ===

	//Prepare
	TUniquePtr<TestBehaviorReader> TestReaderAnimMapsValid = Test.CreateBehaviorReaderOneAnimatedMap("CTRL_AnimMap.Some_Multiplier");
	TUniquePtr<FRigCurveContainer> TestCurveContainerForAnimMapsNameMismatch = Test.CreateCurveContainerOneCurve("CTRL_AnimMap_NOT_ThatMultiploer");
	//Test
	Test.Exec_MapMaskMultipliers(TestReaderAnimMapsValid.Get(), TestCurveContainerForAnimMapsNameMismatch.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->CurveContainerIndicesForAnimMaps.Num() == 1 &&
		Test.GetData()->CurveContainerIndicesForAnimMaps[0].Values.Num() == 1 &&
		Test.GetData()->CurveContainerIndicesForAnimMaps[0].Values[0] == INDEX_NONE &&
		Test.GetData()->RigLogicIndicesForAnimMaps.Num() == 1 &&
		Test.GetData()->RigLogicIndicesForAnimMaps[0].Values.Num() == 1 &&
		Test.GetData()->RigLogicIndicesForAnimMaps[0].Values[0] == 0,
		TEXT("MapMaskMultipliers - ValidReader ValidAnimatedMapNameMismatch")
	);

	//=== MapMaskMultipliers EmptyReader ValidAnimatedMap ===

	//Prepare
	TUniquePtr<FRigCurveContainer> TestCurveContainerForAnimMapsValid = Test.CreateCurveContainerOneCurve("CTRL_AnimMap_Some_Multiplier");
	//Test
	Test.Exec_MapMaskMultipliers(TestReaderEmpty.Get(), TestCurveContainerForAnimMapsValid.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->CurveContainerIndicesForAnimMaps.Num() == 0 &&
		Test.GetData()->RigLogicIndicesForAnimMaps.Num() == 0,
		TEXT("MapMaskMultipliers - EmptyReader ValidAnimatedMap")
	);

	//=== MapMaskMultipliers ValidReader EmptyCurveContainer ===

	//Test
	Test.Exec_MapMaskMultipliers(TestReaderAnimMapsValid.Get(), TestCurveContainerEmpty.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->CurveContainerIndicesForAnimMaps.Num() == 1 &&
		Test.GetData()->CurveContainerIndicesForAnimMaps[0].Values.Num() == 1 &&
		Test.GetData()->CurveContainerIndicesForAnimMaps[0].Values[0] == INDEX_NONE &&
		Test.GetData()->RigLogicIndicesForAnimMaps.Num() == 1 &&
		Test.GetData()->RigLogicIndicesForAnimMaps[0].Values.Num() == 1 &&
		Test.GetData()->RigLogicIndicesForAnimMaps[0].Values[0] == 0,
		TEXT("MapMaskMultipliers - ValidReader EmptyCurveContainer")
	);

	//=== MapMaskMultipliers Valid Inputs ===

	//Prepare
	//  ---
	//Test
	Test.Exec_MapMaskMultipliers(TestReaderAnimMapsValid.Get(), TestCurveContainerForAnimMapsValid.Get());
	//Assert
	AddErrorIfFalse(
		Test.GetData()->CurveContainerIndicesForAnimMaps.Num() == 1 &&
		Test.GetData()->CurveContainerIndicesForAnimMaps[0].Values.Num() == 1 &&
		Test.GetData()->CurveContainerIndicesForAnimMaps[0].Values[0] == 0 &&
		Test.GetData()->RigLogicIndicesForAnimMaps.Num() == 1 &&
		Test.GetData()->RigLogicIndicesForAnimMaps[0].Values.Num() == 1 &&
		Test.GetData()->RigLogicIndicesForAnimMaps[0].Values[0] == 0,
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
	Test.Exec_UpdateJoints(HierarchyContainerPtr, TestJointUpdateParamsTwoJoints_LOD0);
	//Assert
	//Note that BoneB.GlobalTransform.Z should be zero since the scale.Z is zero. Also, translation Y becomes -Y 
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(0).GetTranslation().Equals(FVector(1.f, 0.f, 0.f)), TEXT("UpdateJoints LOD0 Bone 01 - unexpected transform"));
	AddErrorIfFalse(BoneHierarchy.GetGlobalTransform(1).GetTranslation().Equals(FVector(2.f, -2.f, 0.f)), TEXT("UpdateJoints LOD0 Bone 02 - unexpected transform"));


	//Prepare
	//-----
	//Create skeleton, skeletal mesh and skeletal mesh component
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage());
	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(GetTransientPackage());
	SkeletalMesh->Skeleton = Skeleton;
	USkeletalMeshComponent* SkelMeshComponent = NewObject<USkeletalMeshComponent>();
	SkelMeshComponent->SetSkeletalMesh(SkeletalMesh);

	const FString DNAAssetFileName(TEXT("rl_unit_behavior_test.dna"));
	const FString DNAFolder = FPackageName::LongPackageNameToFilename(TEXT("/CharacterParts/Face/DNA/Test"));
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
	//Assert
	AddErrorIfFalse(
		// Check rig logic initialized
		Test.GetData()->RigLogic != nullptr &&
		Test.GetData()->RigInstance != nullptr &&
		
		// Check joints
		Test.GetData()->HierarchyBoneIndices.Num() > 0 &&
		
		// Check input curves
		Test.GetData()->InputCurveIndices.Num() > 0 &&
		
		// Check morph targets
		Test.GetData()->MorphTargetCurveIndices.Num() > 0 &&
		Test.GetData()->BlendShapeIndices.Num() > 0 &&

		// Check mask multipliers
		Test.GetData()->RigLogicIndicesForAnimMaps.Num() > 0 &&
		Test.GetData()->CurveContainerIndicesForAnimMaps.Num() > 0,

		TEXT("InitAndExecute failed to initialize rig logic.")
	);

	return true;
}

#endif
