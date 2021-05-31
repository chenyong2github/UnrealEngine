// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigDefinition.h"
#include "IKRigSolver.h"
#include "Core/PBIKSolver.h"
#include "PBIK_Shared.h"

#include "IKRig_PBIKSolver.generated.h"

UCLASS()
class IKRIG_API UIKRig_FBIKEffector : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category = "Full Body IK Effector")
	FName GoalName;

	UPROPERTY(VisibleAnywhere, Category = "Full Body IK Effector")
	FName BoneName;
	
	UPROPERTY(EditAnywhere, Category = "Full Body IK Effector", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0.0", UIMax = "1.0"))
	float StrengthAlpha = 1.0f;

	UPROPERTY(Transient)
	int32 IndexInSolver = -1;

	void CopySettings(const UIKRig_FBIKEffector* Other)
	{
		StrengthAlpha = Other->StrengthAlpha;
	}
};

UCLASS()
class IKRIG_API UIKRig_PBIKBoneSettings : public UObject
{
	GENERATED_BODY()

public:
	
	UIKRig_PBIKBoneSettings()
		: Bone(NAME_None), 
		X(EPBIKLimitType::Free),
		Y(EPBIKLimitType::Free),
		Z(EPBIKLimitType::Free),
		PreferredAngles(FVector::ZeroVector){}

	UPROPERTY(VisibleAnywhere, Category = Bone, meta = (Constant, CustomWidget = "BoneName"))
	FName Bone;

	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (ClampMin = "0", ClampMax = "1", UIMin = "0.0", UIMax = "1.0"))
	float RotationStiffness = 0.0f;
	UPROPERTY(EditAnywhere, Category = Stiffness, meta = (ClampMin = "0", ClampMax = "1", UIMin = "0.0", UIMax = "1.0"))
	float PositionStiffness = 0.0f;

	UPROPERTY(EditAnywhere, Category = Limits)
	EPBIKLimitType X;
	UPROPERTY(EditAnywhere, Category = Limits, meta = (ClampMin = "-180", ClampMax = "180", UIMin = "-180.0", UIMax = "180.0"))
	float MinX = 0.0f;
	UPROPERTY(EditAnywhere, Category = Limits, meta = (ClampMin = "0", ClampMax = "180", UIMin = "0.0", UIMax = "180.0"))
	float MaxX = 0.0f;

	UPROPERTY(EditAnywhere, Category = Limits)
	EPBIKLimitType Y;
	UPROPERTY(EditAnywhere, Category = Limits, meta = (ClampMin = "-180", ClampMax = "180", UIMin = "-180.0", UIMax = "180.0"))
	float MinY = 0.0f;
	UPROPERTY(EditAnywhere, Category = Limits, meta = (ClampMin = "0", ClampMax = "180", UIMin = "0.0", UIMax = "180.0"))
	float MaxY = 0.0f;

	UPROPERTY(EditAnywhere, Category = Limits)
	EPBIKLimitType Z;
	UPROPERTY(EditAnywhere, Category = Limits, meta = (ClampMin = "-180", ClampMax = "180", UIMin = "-180.0", UIMax = "180.0"))
	float MinZ = 0.0f;
	UPROPERTY(EditAnywhere, Category = Limits, meta = (ClampMin = "0", ClampMax = "180", UIMin = "0.0", UIMax = "180.0"))
	float MaxZ = 0.0f;

	UPROPERTY(EditAnywhere, Category = PreferredAngles)
	bool bUsePreferredAngles = false;
	UPROPERTY(EditAnywhere, Category = PreferredAngles)
	FVector PreferredAngles;

	void CopyToCoreStruct(PBIK::FBoneSettings& Settings) const
	{
		Settings.RotationStiffness = RotationStiffness;
		Settings.PositionStiffness = PositionStiffness;
		Settings.X = static_cast<PBIK::ELimitType>(X);
		Settings.MinX = MinX;
		Settings.MaxX = MaxX;
		Settings.Y = static_cast<PBIK::ELimitType>(Y);
		Settings.MinY = MinY;
		Settings.MaxY = MaxY;
		Settings.Z = static_cast<PBIK::ELimitType>(Z);
		Settings.MinZ = MinZ;
		Settings.MaxZ = MaxZ;
		Settings.bUsePreferredAngles = bUsePreferredAngles;
		Settings.PreferredAngles.Pitch = PreferredAngles.Y;
		Settings.PreferredAngles.Yaw = PreferredAngles.Z;
		Settings.PreferredAngles.Roll = PreferredAngles.X;
	}

	void CopySettings(const UIKRig_PBIKBoneSettings* Other)
	{	
		RotationStiffness = Other->RotationStiffness;
		PositionStiffness = Other->PositionStiffness;
		X = Other->X;
		MinX = Other->MinX;
		MaxX = Other->MaxX;
		Y = Other->Y;
		MinY = Other->MinY;
		MaxY = Other->MaxY;
		Z = Other->Z;
		MinZ = Other->MinZ;
		MaxZ = Other->MaxZ;
		bUsePreferredAngles = Other->bUsePreferredAngles;
		PreferredAngles = Other->PreferredAngles;
	}
};

UCLASS(EditInlineNew, config = Engine, hidecategories = UObject)
class IKRIG_API UIKRigPBIKSolver : public UIKRigSolver
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category = "Solver Settings")
	FName RootBone;

	/** High iteration counts can help solve complex joint configurations with competing constraints, but will increase runtime cost. Default is 20. */
	UPROPERTY(EditAnywhere, Category = SolverSettings, meta = (ClampMin = "0", UIMin = "0.0", UIMax = "200.0"))
	int32 Iterations = 20;

	/** A global mass multiplier; higher values will make the joints more stiff, but require more iterations. Typical range is 0.0 to 10.0. */
	UPROPERTY(EditAnywhere, Category = SolverSettings, meta = (ClampMin = "0", UIMin = "0.0", UIMax = "10.0"))
	float MassMultiplier = 1.0f;

	/** Set this as low as possible while keeping the solve stable. Lower values improve convergence of effector targets. Default is 0.2. */
	UPROPERTY(EditAnywhere, Category = SolverSettings, meta = (ClampMin = "0", UIMin = "0.0", UIMax = "10.0"))
	float MinMassMultiplier = 0.2f;

	/** If true, joints will translate to reach the effectors; causing bones to lengthen if necessary. Good for cartoon effects. Default is false. */
	UPROPERTY(EditAnywhere, Category = SolverSettings)
	bool bAllowStretch = false;

	/** Lock the position and rotation of the solver root bone in-place (at animated position). Useful for partial-body solves. Default is false. */
	UPROPERTY(EditAnywhere, Category = SolverSettings)
	bool bPinRoot = false;

	/** When true, the solver is reset each tick to start from the current input pose. If false, incoming animated poses are ignored and the solver starts from the results of the previous solve. Default is true. */
	UPROPERTY(EditAnywhere, Category = SolverSettings)
	bool bStartSolveFromInputPose = true;
	
	UPROPERTY()
	TArray<UIKRig_FBIKEffector*> Effectors;

	UPROPERTY()
	TArray<UIKRig_PBIKBoneSettings*> BoneSettings;

	/** UIKRigSolver interface */
	// runtime
	virtual void Initialize(const FIKRigSkeleton& IKRigSkeleton) override;
	virtual void Solve(FIKRigSkeleton& IKRigSkeleton, const FIKRigGoalContainer& Goals) override;
	virtual void UpdateSolverSettings(UIKRigSolver* InSettings) override;
	// goals
	virtual void AddGoal(const UIKRigEffectorGoal& NewGoal) override;
	virtual void RemoveGoal(const FName& GoalName) override;
	virtual void RenameGoal(const FName& OldName, const FName& NewName) override;
	virtual void SetGoalBone(const FName& GoalName, const FName& NewBoneName) override;
	virtual bool IsGoalConnected(const FName& GoalName) const override;
	virtual void SetRootBone(const FName& RootBoneName) override;
	virtual UObject* GetEffectorWithGoal(const FName& GoalName) override;
	// bone settings
	virtual void AddBoneSetting(const FName& BoneName) override;
	virtual void RemoveBoneSetting(const FName& BoneName) override;
	virtual UObject* GetBoneSetting(const FName& BoneName) const override;
	virtual bool UsesBoneSettings() const override{ return true;};
	virtual void DrawBoneSettings(const FName& BoneName, const FIKRigSkeleton& IKRigSkeleton, FPrimitiveDrawInterface* PDI) const override;
	// root bone can be set on this solver
	virtual bool CanSetRootBone() const override { return true; };
	virtual bool IsBoneAffectedBySolver(const FName& BoneName, const FIKRigSkeleton& IKRigSkeleton) const override;
	/** END UIKRigSolver interface */

private:

	FPBIKSolver Solver;

	int32 GetIndexOfGoal(const FName& GoalName) const;

	//** UObject */
	virtual void PostLoad() override;
};


