// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceDebugDraw.h"
#include "NiagaraTypes.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraGpuComputeDebug.h"
#include "NiagaraWorldManager.h"
#include "NiagaraSystemInstance.h"

#include "Async/Async.h"
#include "DrawDebugHelpers.h"

//////////////////////////////////////////////////////////////////////////

FName UNiagaraDataInterfaceDebugDraw::CompileTagKey = TEXT("CompilerTagKey");


struct FNDIDebugDrawInstanceData_GameThread
{
	FNDIDebugDrawInstanceData_GameThread() 
	{

	}

	
#if NIAGARA_COMPUTEDEBUG_ENABLED
	void AddLine(const FVector& Start, const FVector& End, const FLinearColor& Color)
	{
		//-OPT: Need to improve this
		FScopeLock RWLock(&LineBufferLock);

		auto& Line = LineBuffer.AddDefaulted_GetRef();
		Line.Start = Start;
		Line.End = End;
		Line.Color  = uint32(FMath::Clamp(Color.R, 0.0f, 1.0f) * 255.0f) << 24;
		Line.Color |= uint32(FMath::Clamp(Color.G, 0.0f, 1.0f) * 255.0f) << 16;
		Line.Color |= uint32(FMath::Clamp(Color.B, 0.0f, 1.0f) * 255.0f) <<  8;
		Line.Color |= uint32(FMath::Clamp(Color.A, 0.0f, 1.0f) * 255.0f) <<  0;
	}

	void AddSphere(const FVector& Location, float Radius, int32 Segments, const FLinearColor& Color)
	{
		const float uinc = 2.0f * PI / float(Segments);

		float ux = 0.0f;
		float SinX0 = FMath::Sin(ux);
		float CosX0 = FMath::Cos(ux);
		for (int x = 0; x < Segments; ++x)
		{
			ux += uinc;
			const float SinX1 = FMath::Sin(ux);
			const float CosX1 = FMath::Cos(ux);

			float uy = 0.0f;
			float SinY0 = FMath::Sin(uy);
			float CosY0 = FMath::Cos(uy);
			for (int y = 0; y < Segments; ++y)
			{
				uy += uinc;
				const float SinY1 = FMath::Sin(uy);
				const float CosY1 = FMath::Cos(uy);

				const FVector Point0 = Location + FVector(CosX0 * CosY0, SinY0, SinX0 * CosY0) * Radius;
				const FVector Point1 = Location + FVector(CosX1 * CosY0, SinY0, SinX1 * CosY0) * Radius;
				const FVector Point2 = Location + FVector(CosX0 * CosY1, SinY1, SinX0 * CosY1) * Radius;
				AddLine(Point0, Point1, Color);
				AddLine(Point0, Point2, Color);

				SinY0 = SinY1;
				CosY0 = CosY1;
			}

			SinX0 = SinX1;
			CosX0 = CosX1;
		}
	}

	void AddBox(const FVector& Location, const FQuat& Rotation, const FVector& Extents, const FLinearColor& Color)
	{
		const FVector Points[] =
		{
			Location + Rotation.RotateVector(FVector(Extents.X,  Extents.Y,  Extents.Z)),
			Location + Rotation.RotateVector(FVector(-Extents.X,  Extents.Y,  Extents.Z)),
			Location + Rotation.RotateVector(FVector(-Extents.X, -Extents.Y,  Extents.Z)),
			Location + Rotation.RotateVector(FVector(Extents.X, -Extents.Y,  Extents.Z)),
			Location + Rotation.RotateVector(FVector(Extents.X,  Extents.Y, -Extents.Z)),
			Location + Rotation.RotateVector(FVector(-Extents.X,  Extents.Y, -Extents.Z)),
			Location + Rotation.RotateVector(FVector(-Extents.X, -Extents.Y, -Extents.Z)),
			Location + Rotation.RotateVector(FVector(Extents.X, -Extents.Y, -Extents.Z)),
		};
		AddLine(Points[0], Points[1], Color);
		AddLine(Points[1], Points[2], Color);
		AddLine(Points[2], Points[3], Color);
		AddLine(Points[3], Points[0], Color);

		AddLine(Points[4], Points[5], Color);
		AddLine(Points[5], Points[6], Color);
		AddLine(Points[6], Points[7], Color);
		AddLine(Points[7], Points[4], Color);

		AddLine(Points[0], Points[4], Color);
		AddLine(Points[1], Points[5], Color);
		AddLine(Points[2], Points[6], Color);
		AddLine(Points[3], Points[7], Color);
	}

	bool bResolvedPersistentShapes = false;
	FCriticalSection LineBufferLock;
	TArray<FNiagaraSimulationDebugDrawData::FGpuLine> LineBuffer;

	struct FDebugPrim_PersistentShape
	{
		UNiagaraDataInterfaceDebugDraw::ShapeId ShapeId;
		const UNiagaraScript* Script = nullptr;
		bool bSimSpaceIsLocal = false;
		FName CenterName;
		FName CenterWorldSpaceName;
		FName OffsetName;
		FName OffsetWorldSpaceName;
		FName RadiusName;
		FName ColorName;
		FName SegmentName;
		FName ExtentsName;
		FName RotationAxisName;
		FName RotationNormalizedAngleName;
		FName RotationWorldSpaceName;
		FName HalfExtentsName;

		ENiagaraCoordinateSpace GetConcreteSource(bool bVectorWasSet, const TOptional<ENiagaraCoordinateSpace>& SourceSpace)
		{
			ENiagaraCoordinateSpace SourceSpaceConcrete = ENiagaraCoordinateSpace::Simulation;
			if (SourceSpace.IsSet())
				SourceSpaceConcrete = SourceSpace.GetValue();

			if (SourceSpaceConcrete == ENiagaraCoordinateSpace::Simulation && bSimSpaceIsLocal)
				SourceSpaceConcrete = ENiagaraCoordinateSpace::Local;
			else if (SourceSpaceConcrete == ENiagaraCoordinateSpace::Simulation)
				SourceSpaceConcrete = ENiagaraCoordinateSpace::World;

			// Override it all as local space if the source vector wasn't set...
			if (bVectorWasSet)
				return SourceSpaceConcrete;
			else
				return ENiagaraCoordinateSpace::Local;
		}

		void TransformVector(bool bVectorWasSet, FVector& Vector, const TOptional<ENiagaraCoordinateSpace>& SourceSpace, const FNiagaraSystemInstance* SystemInstance)
		{
			ENiagaraCoordinateSpace SourceSpaceConcrete = GetConcreteSource(bVectorWasSet, SourceSpace);

			// We are always going to world, so if wer'e already world, just do nothing.
			if (SourceSpaceConcrete == ENiagaraCoordinateSpace::World)
				return;

			ensure(SourceSpaceConcrete == ENiagaraCoordinateSpace::Local);
			Vector = SystemInstance->GetWorldTransform().TransformVector(Vector);
		}

		void TransformPosition(bool bPointWasSet, FVector& Point, const TOptional<ENiagaraCoordinateSpace>& SourceSpace, const FNiagaraSystemInstance* SystemInstance)
		{
			ENiagaraCoordinateSpace SourceSpaceConcrete = GetConcreteSource(bPointWasSet, SourceSpace);

			// We are always going to world, so if wer'e already world, just do nothing.
			if (SourceSpaceConcrete == ENiagaraCoordinateSpace::World)
				return;

			ensure(SourceSpaceConcrete == ENiagaraCoordinateSpace::Local);
			Point = SystemInstance->GetWorldTransform().TransformPosition(Point);
		}

		void TransformQuat(bool bRotationWasSet, FQuat& Quat, const TOptional<ENiagaraCoordinateSpace>& SourceSpace, const FNiagaraSystemInstance* SystemInstance)
		{
			ENiagaraCoordinateSpace SourceSpaceConcrete = GetConcreteSource(bRotationWasSet, SourceSpace);

			// We are always going to world, so if wer'e already world, just do nothing.
			if (SourceSpaceConcrete == ENiagaraCoordinateSpace::World)
				return;

			ensure(SourceSpaceConcrete == ENiagaraCoordinateSpace::Local);
			Quat = SystemInstance->GetWorldTransform().Rotator().Quaternion() * Quat;
		}

		void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, const FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
		{
			switch (ShapeId)
			{
			case UNiagaraDataInterfaceDebugDraw::Sphere:
				DrawSphere(InstanceData, SystemInstance, DeltaSeconds);
				break;
			case UNiagaraDataInterfaceDebugDraw::Box:
				DrawBox(InstanceData, SystemInstance, DeltaSeconds);
				break;

			};
				
		}



		void DrawSphere(FNDIDebugDrawInstanceData_GameThread* InstanceData, const FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
		{
			if (Script == nullptr)
				return;
			static FNiagaraTypeDefinition CoordTypeDef = FNiagaraTypeDefinition(FNiagaraTypeDefinition::GetCoordinateSpaceEnum());			;

			TOptional<FVector> Center = Script->GetCompilerTag< FVector>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), CenterName), SystemInstance->GetOverrideParameters());
			TOptional<ENiagaraCoordinateSpace> CenterWorldSpace = Script->GetCompilerTag<ENiagaraCoordinateSpace>(FNiagaraVariableBase(CoordTypeDef, CenterWorldSpaceName), SystemInstance->GetOverrideParameters());;
			TOptional<FVector> Offset = Script->GetCompilerTag< FVector>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), OffsetName), SystemInstance->GetOverrideParameters());
			TOptional<ENiagaraCoordinateSpace> OffsetWorldSpace = Script->GetCompilerTag<ENiagaraCoordinateSpace>(FNiagaraVariableBase(CoordTypeDef, OffsetWorldSpaceName), SystemInstance->GetOverrideParameters());;
			TOptional<float> Radius = Script->GetCompilerTag<float>(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), RadiusName), SystemInstance->GetOverrideParameters());
			TOptional<FLinearColor> Color = Script->GetCompilerTag< FLinearColor>(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), ColorName), SystemInstance->GetOverrideParameters());
			TOptional<int32> NumSegments = Script->GetCompilerTag<int32>(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), SegmentName), SystemInstance->GetOverrideParameters());

			FVector DrawCenter = Center.IsSet() ? Center.GetValue() : FVector::ZeroVector;
			FVector DrawOffset = Offset.IsSet() ? Offset.GetValue() : FVector::ZeroVector;
			float DrawRadius = Radius.IsSet() ? Radius.GetValue() : 1.0f;
			FLinearColor DrawColor = Color.IsSet() ? Color.GetValue() : FLinearColor::Green;
			int32 DrawNumSegments = NumSegments.IsSet() ? NumSegments.GetValue() : 6;

			TransformPosition(Center.IsSet(), DrawCenter, CenterWorldSpace, SystemInstance);
			TransformVector(Offset.IsSet(), DrawOffset, OffsetWorldSpace, SystemInstance);


			if (Radius.IsSet())
				InstanceData->AddSphere(DrawCenter + DrawOffset, DrawRadius, DrawNumSegments, DrawColor);
		}

		void DrawBox(FNDIDebugDrawInstanceData_GameThread* InstanceData, const FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
		{
			if (Script == nullptr)
				return;
			static FNiagaraTypeDefinition CoordTypeDef = FNiagaraTypeDefinition(FNiagaraTypeDefinition::GetCoordinateSpaceEnum());

			TOptional<FVector> Center = Script->GetCompilerTag< FVector>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), CenterName), SystemInstance->GetOverrideParameters());
			TOptional<FVector> Offset = Script->GetCompilerTag< FVector>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), OffsetName), SystemInstance->GetOverrideParameters());
			TOptional<ENiagaraCoordinateSpace> CenterWorldSpace = Script->GetCompilerTag<ENiagaraCoordinateSpace>(FNiagaraVariableBase(CoordTypeDef, CenterWorldSpaceName), SystemInstance->GetOverrideParameters());;
			TOptional<ENiagaraCoordinateSpace> OffsetWorldSpace = Script->GetCompilerTag<ENiagaraCoordinateSpace>(FNiagaraVariableBase(CoordTypeDef, OffsetWorldSpaceName), SystemInstance->GetOverrideParameters());;
			TOptional<ENiagaraCoordinateSpace> RotationWorldSpace = Script->GetCompilerTag<ENiagaraCoordinateSpace>(FNiagaraVariableBase(CoordTypeDef, RotationWorldSpaceName), SystemInstance->GetOverrideParameters());;
			TOptional<FVector> Extents = Script->GetCompilerTag<FVector>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), ExtentsName), SystemInstance->GetOverrideParameters());
			TOptional<FNiagaraBool> HalfExtents = Script->GetCompilerTag<FNiagaraBool>(FNiagaraVariableBase(FNiagaraTypeDefinition::GetBoolDef(), HalfExtentsName), SystemInstance->GetOverrideParameters());;
			TOptional<FVector> RotationAxis = Script->GetCompilerTag<FVector>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), RotationAxisName), SystemInstance->GetOverrideParameters());
			TOptional<float> RotationNormalizedAngle = Script->GetCompilerTag<float>(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), RotationNormalizedAngleName), SystemInstance->GetOverrideParameters());
			TOptional<FLinearColor> Color = Script->GetCompilerTag< FLinearColor>(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), ColorName), SystemInstance->GetOverrideParameters());


			FVector DrawCenter = Center.IsSet() ? Center.GetValue() : FVector::ZeroVector;
			FVector DrawOffset = Offset.IsSet() ? Offset.GetValue() : FVector::ZeroVector;
			FVector DrawExtents = Extents.IsSet() ? Extents.GetValue() : FVector::ZeroVector*10.0f;
			FVector DrawRotationAxis = RotationAxis.IsSet() ? RotationAxis.GetValue() : FVector(0.0f, 0.0f, 1.0f);
			float DrawRotationNormalizedAngle = RotationNormalizedAngle.IsSet() ? RotationNormalizedAngle.GetValue() : 0.0f;
			FLinearColor DrawColor = Color.IsSet() ? Color.GetValue() : FLinearColor::Green;
			FQuat DrawRotation = FQuat::Identity;

			TransformPosition(Center.IsSet(), DrawCenter, CenterWorldSpace, SystemInstance);
			TransformVector(Offset.IsSet(), DrawOffset, OffsetWorldSpace, SystemInstance);

			DrawRotation = FQuat(DrawRotationAxis, FMath::DegreesToRadians(DrawRotationNormalizedAngle * 360.0f));
			TransformQuat(RotationAxis.IsSet(), DrawRotation, RotationWorldSpace, SystemInstance);

						
			if ((HalfExtents.IsSet() && HalfExtents.GetValue().GetValue()) || !HalfExtents.IsSet())
			{
				DrawExtents /= 2.0f;
			}

			if (Extents.IsSet())
				InstanceData->AddBox(DrawCenter + DrawOffset, DrawRotation, DrawExtents,  DrawColor);
		}
	};


	

	TArray<TPair<FName, UNiagaraDataInterfaceDebugDraw::ShapeId>> PersistentShapeIds;
	TArray<FDebugPrim_PersistentShape> PersistentShapes;

	void AddNamedPersistentShape(const FName& InName, UNiagaraDataInterfaceDebugDraw::ShapeId InShapeId)
	{
		for (const TPair<FName, UNiagaraDataInterfaceDebugDraw::ShapeId>& ExistingShape : PersistentShapeIds)
		{
			if (ExistingShape.Key == InName && ExistingShape.Value == InShapeId)
				return;
		}


		PersistentShapeIds.Add(TPair<FName, UNiagaraDataInterfaceDebugDraw::ShapeId>(InName, InShapeId));
		bResolvedPersistentShapes = false;
	}


	void HandlePersistentShapes(FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
	{

		if (!bResolvedPersistentShapes && PersistentShapeIds.Num() != 0)
		{
			TArray<UNiagaraScript*> Scripts;
			TArray<bool> ScriptIsLocal;
			UNiagaraSystem* System = SystemInstance->GetSystem();
			if (System)
			{
				Scripts.Add(System->GetSystemSpawnScript());
				Scripts.Add(System->GetSystemUpdateScript());
				ScriptIsLocal.Add(false);
				ScriptIsLocal.Add(false);

				for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
				{
					UNiagaraEmitter* Emitter = Handle.GetInstance();
					if (Emitter)
					{
						bool bEmitterIsLocal = Emitter->bLocalSpace;
						if (Emitter->SimTarget == ENiagaraSimTarget::CPUSim)
						{
							int32 ScriptCap = Scripts.Num();
							Emitter->GetScripts(Scripts, true, true);

							for (int32 i = ScriptCap; i < Scripts.Num(); i++)
								ScriptIsLocal.Add(bEmitterIsLocal);
						}
						else
						{
							// It's a little weird to do this, but ultimately all the rapid iteration values are 
							// referenced by the compile tags from these scripts and we want to get the most up-to-date 
							// values here. If we reference the GPU script here, it will have stale values for some reason.
							Scripts.Add(Emitter->SpawnScriptProps.Script);
							Scripts.Add(Emitter->UpdateScriptProps.Script);
							ScriptIsLocal.Add(bEmitterIsLocal);
							ScriptIsLocal.Add(bEmitterIsLocal);
						}
					}
				}
			}

			ensure(ScriptIsLocal.Num() == Scripts.Num());

			PersistentShapes.Empty();
			for (const TPair<FName, UNiagaraDataInterfaceDebugDraw::ShapeId>& ExistingShape : PersistentShapeIds)
			{
				for (int32 i = 0; i< Scripts.Num(); i++)
				{
					UNiagaraScript* Script = Scripts[i];
					bool bIsLocal = ScriptIsLocal[i];
					if (Script)
					{
						switch (ExistingShape.Value)
						{
						case UNiagaraDataInterfaceDebugDraw::Sphere:
							{
								FName CenterName = *(ExistingShape.Key.ToString() + TEXT(".Center"));
								FName CenterWorldSpaceName = *(ExistingShape.Key.ToString() + TEXT(".CenterCoordinateSpace"));
								FName OffsetName = *(ExistingShape.Key.ToString() + TEXT(".OffsetFromCenter"));
								FName OffsetWorldSpaceName = *(ExistingShape.Key.ToString() + TEXT(".OffsetCoordinateSpace"));
								FName RadiusName = *(ExistingShape.Key.ToString() + TEXT(".Radius"));
								FName ColorName = *(ExistingShape.Key.ToString() + TEXT(".Color"));
								FName SegmentName = *(ExistingShape.Key.ToString() + TEXT(".Num Segments"));

								TOptional<FVector> Center = Script->GetCompilerTag< FVector>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), CenterName));
								TOptional<float> Radius = Script->GetCompilerTag<float>(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), RadiusName));
								TOptional<FLinearColor> Color = Script->GetCompilerTag< FLinearColor>(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), ColorName));
								TOptional<int32> NumSegments = Script->GetCompilerTag<int32>(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), SegmentName));

								if (Center.IsSet() || Radius.IsSet() || Color.IsSet() || NumSegments.IsSet())
								{
									int32 Index = PersistentShapes.Emplace();
									PersistentShapes[Index].ShapeId = ExistingShape.Value;
									PersistentShapes[Index].Script = Script;
									PersistentShapes[Index].bSimSpaceIsLocal = bIsLocal;
									PersistentShapes[Index].CenterName = CenterName;
									PersistentShapes[Index].CenterWorldSpaceName = CenterWorldSpaceName;
									PersistentShapes[Index].OffsetName = OffsetName;
									PersistentShapes[Index].OffsetWorldSpaceName = OffsetWorldSpaceName;
									PersistentShapes[Index].RadiusName = RadiusName;
									PersistentShapes[Index].ColorName = ColorName;
									PersistentShapes[Index].SegmentName = SegmentName;
								}
							}
							break;

						case UNiagaraDataInterfaceDebugDraw::Box:
						{
							FName CenterName = *(ExistingShape.Key.ToString() + TEXT(".Center"));
							FName CenterWorldSpaceName = *(ExistingShape.Key.ToString() + TEXT(".CenterCoordinateSpace"));
							FName ExtentsName = *(ExistingShape.Key.ToString() + TEXT(".Extents"));
							FName HalfExtentsName = *(ExistingShape.Key.ToString() + TEXT(".HalfExtents"));
							FName RotationAxisName = *(ExistingShape.Key.ToString() + TEXT(".RotationAxis"));

							FName RotationNormalizedAngleName = *(ExistingShape.Key.ToString() + TEXT(".RotationNormalizedAngle"));
							FName ColorName = *(ExistingShape.Key.ToString() + TEXT(".Color"));

							FName OffsetName = *(ExistingShape.Key.ToString() + TEXT(".Offset"));
							FName OffsetWorldSpaceName = *(ExistingShape.Key.ToString() + TEXT(".OffsetCoordinateSpace"));

							TOptional<FVector> Center = Script->GetCompilerTag< FVector>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), CenterName));
							TOptional<FVector> Extents = Script->GetCompilerTag<FVector>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), ExtentsName));
							TOptional<FLinearColor> Color = Script->GetCompilerTag< FLinearColor>(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), ColorName));
							TOptional<FVector> RotationAxis = Script->GetCompilerTag<FVector>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), RotationAxisName));
							TOptional<float> RotationNormalizedAngle = Script->GetCompilerTag<float>(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), RotationNormalizedAngleName));



							if (Center.IsSet() || Extents.IsSet() || Color.IsSet() || RotationAxis.IsSet() || RotationNormalizedAngle.IsSet())
							{
								int32 Index = PersistentShapes.Emplace();
								PersistentShapes[Index].ShapeId = ExistingShape.Value;
								PersistentShapes[Index].bSimSpaceIsLocal = bIsLocal;
								PersistentShapes[Index].Script = Script;
								PersistentShapes[Index].CenterName = CenterName;
								PersistentShapes[Index].CenterWorldSpaceName = CenterWorldSpaceName;
								PersistentShapes[Index].ExtentsName = ExtentsName;
								PersistentShapes[Index].HalfExtentsName = HalfExtentsName;
								PersistentShapes[Index].ColorName = ColorName;

								PersistentShapes[Index].RotationAxisName = RotationAxisName;
								PersistentShapes[Index].RotationNormalizedAngleName = RotationNormalizedAngleName;
								PersistentShapes[Index].OffsetName = OffsetName;
								PersistentShapes[Index].OffsetWorldSpaceName = OffsetWorldSpaceName;
							}
						}
						break;
						}
						
					}
				}
			}


			bResolvedPersistentShapes = true;
		}

		if (bResolvedPersistentShapes)
		{
			for (FDebugPrim_PersistentShape& Shape : PersistentShapes)
			{
				Shape.Draw(this, SystemInstance, DeltaSeconds);
			}
		}
	}

#endif //NIAGARA_COMPUTEDEBUG_ENABLED
};

#if NIAGARA_COMPUTEDEBUG_ENABLED
struct FNDIDebugDrawInstanceData_RenderThread
{
	FNiagaraGpuComputeDebug* GpuComputeDebug = nullptr;
};
#endif //NIAGARA_COMPUTEDEBUG_ENABLED

struct FNDIDebugDrawProxy : public FNiagaraDataInterfaceProxy
{
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override {}
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }

#if NIAGARA_COMPUTEDEBUG_ENABLED
	TMap<FNiagaraSystemInstanceID, FNDIDebugDrawInstanceData_RenderThread> SystemInstancesToProxyData_RT;
#endif
};

//////////////////////////////////////////////////////////////////////////

namespace NDIDebugDrawLocal
{
	static const FName DrawBoxName(TEXT("DrawBox"));
	static const FName DrawCircleName(TEXT("DrawCircle"));
	static const FName DrawCoordinateSystemName(TEXT("DrawCoordinateSystem"));
	static const FName DrawGrid2DName(TEXT("DrawGrid2D"));
	static const FName DrawGrid3DName(TEXT("DrawGrid3D"));
	static const FName DrawLineName(TEXT("DrawLine"));
	static const FName DrawSphereName(TEXT("DrawSphere"));
	static const FName DrawSpherePersistentName(TEXT("DrawSpherePersistent"));
	static const FName DrawBoxPersistentName(TEXT("DrawBoxPersistent"));

	static int32 GNiagaraDebugDrawEnabled = 1;
	static FAutoConsoleVariableRef CVarNiagaraDebugDrawEnabled(
		TEXT("fx.Niagara.DebugDraw.Enabled"),
		GNiagaraDebugDrawEnabled,
		TEXT("Enable or disable the Debug Draw Data Interface, note does not fully disable the overhead."),
		ECVF_Default
	);

	struct FDebugPrim_Box
	{
		struct VMBindings
		{
			VMBindings(FVectorVMContext& Context)
				: LocationParam(Context)
				, RotationParam(Context)
				, ExtentsParam(Context)
				, ColorParam(Context)
			{
			}

			FNDIInputParam<FVector> LocationParam;
			FNDIInputParam<FQuat> RotationParam;
			FNDIInputParam<FVector> ExtentsParam;
			FNDIInputParam<FLinearColor> ColorParam;
		};

		struct PersistentVMBindings
		{
			PersistentVMBindings(FVectorVMContext& Context)
				: LocationParam(Context)
				, LocationWSParam(Context)
				, ExtentsParam(Context)
				, HalfExtentsParam(Context)
				, RotationAxisParam(Context)
				, RotationAngleParam(Context)
				, RotationWSParam(Context)
				, OffsetParam(Context)
				, OffsetWSParam(Context)
				, ColorParam(Context)
			{

			}

			FNDIInputParam<FVector> LocationParam;
			FNDIInputParam<FNiagaraBool> LocationWSParam;
			FNDIInputParam<FVector> ExtentsParam;
			FNDIInputParam<FNiagaraBool> HalfExtentsParam;
			FNDIInputParam<FVector> RotationAxisParam;
			FNDIInputParam<float> RotationAngleParam;
			FNDIInputParam<FNiagaraBool> RotationWSParam;
			FNDIInputParam<FVector> OffsetParam;
			FNDIInputParam<FNiagaraBool> OffsetWSParam;
			FNDIInputParam<FLinearColor> ColorParam;
		};

#if NIAGARA_COMPUTEDEBUG_ENABLED
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bExecute)
		{
			const FVector Location = Bindings.LocationParam.GetAndAdvance();
			const FQuat Rotation = Bindings.RotationParam.GetAndAdvance();
			const FVector Extents = Bindings.ExtentsParam.GetAndAdvance();
			const FLinearColor Color = Bindings.ColorParam.GetAndAdvance();

			if (bExecute)
			{
				InstanceData->AddBox(Location, Rotation, Extents, Color);
			}
		}
#endif
	};

	struct FDebugPrim_Circle
	{
		struct VMBindings
		{
			VMBindings(FVectorVMContext& Context)
				: LocationParam(Context)
				, XAxisParam(Context)
				, YAxisParam(Context)
				, ScaleParam(Context)
				, SegmentsParam(Context)
				, ColorParam(Context)
			{
			}

			FNDIInputParam<FVector> LocationParam;
			FNDIInputParam<FVector> XAxisParam;
			FNDIInputParam<FVector> YAxisParam;
			FNDIInputParam<float> ScaleParam;
			FNDIInputParam<int32> SegmentsParam;
			FNDIInputParam<FLinearColor> ColorParam;
		};

#if NIAGARA_COMPUTEDEBUG_ENABLED
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bExecute)
		{
			const FVector Location = Bindings.LocationParam.GetAndAdvance();
			const FVector XAxis = Bindings.XAxisParam.GetAndAdvance();
			const FVector YAxis = Bindings.YAxisParam.GetAndAdvance();
			const float Scale = Bindings.ScaleParam.GetAndAdvance();
			const int32 Segments = FMath::Clamp(Bindings.SegmentsParam.GetAndAdvance(), 4, 16);
			const FLinearColor Color = Bindings.ColorParam.GetAndAdvance();

			if (bExecute)
			{
				const FVector X = XAxis * Scale;
				const FVector Y = YAxis * Scale;

				const float d = 2.0f * PI / float(Segments);
				float u = 0.0f;

				FVector LastPoint = Location + (X * FMath::Cos(u)) + (Y * FMath::Sin(u));

				for ( int32 x=0; x < Segments; ++x )
				{
					u += d;
					const FVector CurrPoint = Location + (X * FMath::Cos(u)) + (Y * FMath::Sin(u));
					InstanceData->AddLine(LastPoint, CurrPoint, Color);
					LastPoint = CurrPoint;
				}
			}
		}
#endif
	};

	struct FDebugPrim_CoordinateSystem
	{
		struct VMBindings
		{
			VMBindings(FVectorVMContext& Context)
				: LocationParam(Context)
				, RotationParam(Context)
				, ScaleParam(Context)
			{
			}

			FNDIInputParam<FVector> LocationParam;
			FNDIInputParam<FQuat> RotationParam;
			FNDIInputParam<float> ScaleParam;
		};

#if NIAGARA_COMPUTEDEBUG_ENABLED
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bExecute)
		{
			const FVector Location = Bindings.LocationParam.GetAndAdvance();
			const FQuat Rotation = Bindings.RotationParam.GetAndAdvance();
			const float Scale = Bindings.ScaleParam.GetAndAdvance();

			if (bExecute)
			{
				const FVector XAxis = Rotation.RotateVector(FVector(Scale, 0.0f, 0.0f));
				const FVector YAxis = Rotation.RotateVector(FVector(0.0f, Scale, 0.0f));
				const FVector ZAxis = Rotation.RotateVector(FVector(0.0f, 0.0f, Scale));

				InstanceData->AddLine(Location, Location + XAxis, FLinearColor::Red);
				InstanceData->AddLine(Location, Location + YAxis, FLinearColor::Green);
				InstanceData->AddLine(Location, Location + ZAxis, FLinearColor::Blue);
			}
		}
#endif
	};

	struct FDebugPrim_Grid2D
	{
		struct VMBindings
		{
			VMBindings(FVectorVMContext& Context)
				: CenterParam(Context)
				, RotationParam(Context)
				, ExtentsParam(Context)
				, NumCellsXParam(Context)
				, NumCellsYParam(Context)
				, ColorParam(Context)
			{
			}

			FNDIInputParam<FVector> CenterParam;
			FNDIInputParam<FQuat> RotationParam;
			FNDIInputParam<FVector2D> ExtentsParam;
			FNDIInputParam<int32> NumCellsXParam;
			FNDIInputParam<int32> NumCellsYParam;
			FNDIInputParam<FLinearColor> ColorParam;
		};

#if NIAGARA_COMPUTEDEBUG_ENABLED
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bExecute)
		{
			const FVector Center = Bindings.CenterParam.GetAndAdvance();
			const FQuat Rotation = Bindings.RotationParam.GetAndAdvance();
			const FVector2D Extents = Bindings.ExtentsParam.GetAndAdvance();
			const FIntPoint NumCells(Bindings.NumCellsXParam.GetAndAdvance(), Bindings.NumCellsYParam.GetAndAdvance());
			const FLinearColor Color = Bindings.ColorParam.GetAndAdvance();

			if (bExecute && NumCells.X > 0 && NumCells.Y > 0)
			{
				const FVector Corner = Center - Rotation.RotateVector(FVector(Extents.X, Extents.Y, 0.0f));
				const FVector XLength = Rotation.RotateVector(FVector(Extents.X * 2.0f, 0.0f, 0.0f));
				const FVector YLength = Rotation.RotateVector(FVector(0.0f, Extents.Y * 2.0f, 0.0f));
				const FVector XDelta = XLength / float(NumCells.X);
				const FVector YDelta = YLength / float(NumCells.Y);

				for (int X=0; X <= NumCells.X; ++X)
				{
					const FVector XOffset = XDelta * float(X);
					for (int Y=0; Y <= NumCells.Y; ++Y)
					{
						const FVector YOffset = YDelta * float(Y);
						InstanceData->AddLine(Corner + XOffset, Corner + XOffset + YLength, Color);
						InstanceData->AddLine(Corner + YOffset, Corner + YOffset + XLength, Color);
					}
				}
			}
		}
#endif
	};

	struct FDebugPrim_Grid3D
	{
		struct VMBindings
		{
			VMBindings(FVectorVMContext& Context)
				: CenterParam(Context)
				, RotationParam(Context)
				, ExtentsParam(Context)
				, NumCellsXParam(Context)
				, NumCellsYParam(Context)
				, NumCellsZParam(Context)
				, ColorParam(Context)
			{
			}

			FNDIInputParam<FVector> CenterParam;
			FNDIInputParam<FQuat> RotationParam;
			FNDIInputParam<FVector> ExtentsParam;
			FNDIInputParam<int32> NumCellsXParam;
			FNDIInputParam<int32> NumCellsYParam;
			FNDIInputParam<int32> NumCellsZParam;
			FNDIInputParam<FLinearColor> ColorParam;
		};

#if NIAGARA_COMPUTEDEBUG_ENABLED
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bExecute)
		{
			const FVector Center = Bindings.CenterParam.GetAndAdvance();
			const FQuat Rotation = Bindings.RotationParam.GetAndAdvance();
			const FVector Extents = Bindings.ExtentsParam.GetAndAdvance();
			const FIntVector NumCells(Bindings.NumCellsXParam.GetAndAdvance(), Bindings.NumCellsYParam.GetAndAdvance(), Bindings.NumCellsZParam.GetAndAdvance());
			const FLinearColor Color = Bindings.ColorParam.GetAndAdvance();

			if (bExecute && NumCells.X > 0 && NumCells.Y > 0 && NumCells.Z > 0)
			{
				const FVector Corner = Center - Rotation.RotateVector(Extents);
				const FVector XLength = Rotation.RotateVector(FVector(Extents.X * 2.0f, 0.0f, 0.0f));
				const FVector YLength = Rotation.RotateVector(FVector(0.0f, Extents.Y * 2.0f, 0.0f));
				const FVector ZLength = Rotation.RotateVector(FVector(0.0f, 0.0f, Extents.Z * 2.0f));
				const FVector XDelta = XLength / float(NumCells.X);
				const FVector YDelta = YLength / float(NumCells.Y);
				const FVector ZDelta = ZLength / float(NumCells.Z);

				for (int X = 0; X <= NumCells.X; ++X)
				{
					const FVector XOffset = XDelta * float(X);
					for (int Y = 0; Y <= NumCells.Y; ++Y)
					{
						const FVector YOffset = YDelta * float(Y);
						for (int Z = 0; Z <= NumCells.Z; ++Z)
						{
							const FVector ZOffset = ZDelta * float(Z);

							InstanceData->AddLine(Corner + ZOffset + XOffset, Corner + ZOffset + XOffset + YLength, Color);		// Z Slice: X -> Y
							InstanceData->AddLine(Corner + ZOffset + YOffset, Corner + ZOffset + YOffset + XLength, Color);		// Z Slice: Y -> X

							InstanceData->AddLine(Corner + XOffset + YOffset, Corner + XOffset + YOffset + ZLength, Color);		// X Slice: Y -> Z
							InstanceData->AddLine(Corner + XOffset + ZOffset, Corner + XOffset + ZOffset + YLength, Color);		// X Slice: Z -> Y
						}
					}
				}
			}
		}
#endif
	};

	struct FDebugPrim_Line
	{
		struct VMBindings
		{
			VMBindings(FVectorVMContext& Context)
				: LineStartParam(Context)
				, LineEndParam(Context)
				, ColorParam(Context)
			{
			}

			FNDIInputParam<FVector> LineStartParam;
			FNDIInputParam<FVector> LineEndParam;
			FNDIInputParam<FLinearColor> ColorParam;
		};

#if NIAGARA_COMPUTEDEBUG_ENABLED
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bExecute)
		{
			const FVector LineStart = Bindings.LineStartParam.GetAndAdvance();
			const FVector LineEnd = Bindings.LineEndParam.GetAndAdvance();
			const FLinearColor Color = Bindings.ColorParam.GetAndAdvance();

			if (bExecute)
			{
				InstanceData->AddLine(LineStart, LineEnd, Color);
			}
		}
#endif
	};

	struct FDebugPrim_Sphere
	{
		struct VMBindings
		{
			VMBindings(FVectorVMContext& Context)
				: LocationParam(Context)
				, RadiusParam(Context)
				, SegmentsParam(Context)
				, ColorParam(Context)
			{
			}

			FNDIInputParam<FVector> LocationParam;
			FNDIInputParam<float> RadiusParam;
			FNDIInputParam<int32> SegmentsParam;
			FNDIInputParam<FLinearColor> ColorParam;
		};

		struct PersistentVMBindings
		{
			PersistentVMBindings(FVectorVMContext& Context)
				: CenterParam(Context)
				, CenterWSParam(Context)
				, OffsetParam(Context)
				, OffsetWSParam(Context)
				, RadiusParam(Context)
				, SegmentsParam(Context)
				, ColorParam(Context)
			{
			}

			FNDIInputParam<FVector> CenterParam;
			FNDIInputParam<FNiagaraBool> CenterWSParam;
			FNDIInputParam<FVector> OffsetParam;
			FNDIInputParam<FNiagaraBool> OffsetWSParam;
			FNDIInputParam<float> RadiusParam;
			FNDIInputParam<int32> SegmentsParam;
			FNDIInputParam<FLinearColor> ColorParam;
		};

#if NIAGARA_COMPUTEDEBUG_ENABLED
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bExecute)
		{
			const FVector Location = Bindings.LocationParam.GetAndAdvance();
			const float Radius = Bindings.RadiusParam.GetAndAdvance();
			const int32 Segments = FMath::Clamp(Bindings.SegmentsParam.GetAndAdvance(), 4, 16);
			const FLinearColor Color = Bindings.ColorParam.GetAndAdvance();

			if (bExecute)
			{
				InstanceData->AddSphere(Location, Radius, Segments, Color);				
			}
		}
#endif
	};

	template<typename TPrimType>
	void DrawDebug(FVectorVMContext& Context)
	{
		VectorVM::FUserPtrHandler<FNDIDebugDrawInstanceData_GameThread> InstanceData(Context);
		FNDIInputParam<FNiagaraBool> ExecuteParam(Context);
		typename TPrimType::VMBindings Bindings(Context);

#if NIAGARA_COMPUTEDEBUG_ENABLED
		if ( !NDIDebugDrawLocal::GNiagaraDebugDrawEnabled )
		{
			return;
		}

		for ( int i=0; i < Context.NumInstances; ++i )
		{
			const bool bExecute = ExecuteParam.GetAndAdvance();
			TPrimType::Draw(InstanceData, Bindings, bExecute);
		}
#endif
	}

	template<typename TPrimType>
	void DrawDebugPersistent(FVectorVMContext& Context)
	{
		VectorVM::FUserPtrHandler<FNDIDebugDrawInstanceData_GameThread> InstanceData(Context);
		typename TPrimType::PersistentVMBindings Bindings(Context);

#if NIAGARA_COMPUTEDEBUG_ENABLED
		if (!NDIDebugDrawLocal::GNiagaraDebugDrawEnabled)
		{
			return;
		}

		// Do nothing here... will draw this later on..
#endif
	}
}

//////////////////////////////////////////////////////////////////////////

struct FNiagaraDataInterfaceParametersCS_DebugDraw : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_DebugDraw, NonVirtual);

public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		DrawArgsParams.Bind(ParameterMap, TEXT("NDIDebugDrawArgs"));
		DrawLineVertexParam.Bind(ParameterMap, TEXT("NDIDebugDrawLineVertex"));
		DrawLineMaxInstancesParam.Bind(ParameterMap, TEXT("NDIDebugDrawLineMaxInstances"));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
#if NIAGARA_COMPUTEDEBUG_ENABLED
		auto* DIProxy = static_cast<FNDIDebugDrawProxy*>(Context.DataInterface);
		auto* InstanceData = &DIProxy->SystemInstancesToProxyData_RT.FindChecked(Context.SystemInstanceID);

		FNiagaraSimulationDebugDrawData* DebugDraw = nullptr;
		if ( InstanceData->GpuComputeDebug )
		{
			DebugDraw = InstanceData->GpuComputeDebug->GetSimulationDebugDrawData(Context.SystemInstanceID, true);
		}

		const bool bIsValid =
			NDIDebugDrawLocal::GNiagaraDebugDrawEnabled &&
			(DebugDraw != nullptr) &&
			DrawArgsParams.IsUAVBound() &&
			DrawLineVertexParam.IsUAVBound();

		if ( bIsValid )
		{
			FRHITransitionInfo Transitions[] =
			{
				FRHITransitionInfo(DebugDraw->GpuLineBufferArgs.UAV, ERHIAccess::IndirectArgs, ERHIAccess::UAVCompute),
				FRHITransitionInfo(DebugDraw->GpuLineVertexBuffer.UAV, ERHIAccess::SRVMask, ERHIAccess::UAVCompute),
			};
			RHICmdList.Transition(Transitions);

			RHICmdList.SetUAVParameter(ComputeShaderRHI, DrawArgsParams.GetUAVIndex(), DebugDraw->GpuLineBufferArgs.UAV);
			RHICmdList.SetUAVParameter(ComputeShaderRHI, DrawLineVertexParam.GetUAVIndex(), DebugDraw->GpuLineVertexBuffer.UAV);
			SetShaderValue(RHICmdList, ComputeShaderRHI, DrawLineMaxInstancesParam, DebugDraw->GpuLineMaxInstances);
		}
		else
#endif //NIAGARA_COMPUTEDEBUG_ENABLED
		{
			SetShaderValue(RHICmdList, ComputeShaderRHI, DrawLineMaxInstancesParam, 0);
		}
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		DrawArgsParams.UnsetUAV(RHICmdList, ComputeShaderRHI);
		DrawLineVertexParam.UnsetUAV(RHICmdList, ComputeShaderRHI);

#if NIAGARA_COMPUTEDEBUG_ENABLED
		auto* DIProxy = static_cast<FNDIDebugDrawProxy*>(Context.DataInterface);
		auto* InstanceData = &DIProxy->SystemInstancesToProxyData_RT.FindChecked(Context.SystemInstanceID);

		FNiagaraSimulationDebugDrawData* DebugDraw = nullptr;
		if (InstanceData->GpuComputeDebug)
		{
			DebugDraw = InstanceData->GpuComputeDebug->GetSimulationDebugDrawData(Context.SystemInstanceID, true);
		}

		const bool bIsValid =
			NDIDebugDrawLocal::GNiagaraDebugDrawEnabled &&
			(DebugDraw != nullptr) &&
			DrawArgsParams.IsUAVBound() &&
			DrawLineVertexParam.IsUAVBound();

		if (bIsValid)
		{
			FRHITransitionInfo Transitions[] =
			{
				FRHITransitionInfo(DebugDraw->GpuLineBufferArgs.UAV, ERHIAccess::UAVCompute, ERHIAccess::IndirectArgs),
				FRHITransitionInfo(DebugDraw->GpuLineVertexBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
			};
			RHICmdList.Transition(Transitions);
		}
#endif //NIAGARA_COMPUTEDEBUG_ENABLED
	}

private:
	LAYOUT_FIELD(FRWShaderParameter,	DrawArgsParams);
	LAYOUT_FIELD(FRWShaderParameter,	DrawLineVertexParam);
	LAYOUT_FIELD(FShaderParameter,		DrawLineMaxInstancesParam);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_DebugDraw);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceDebugDraw, FNiagaraDataInterfaceParametersCS_DebugDraw);

//////////////////////////////////////////////////////////////////////////

UNiagaraDataInterfaceDebugDraw::UNiagaraDataInterfaceDebugDraw(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNDIDebugDrawProxy());
}

void UNiagaraDataInterfaceDebugDraw::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

void UNiagaraDataInterfaceDebugDraw::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	OutFunctions.Reserve(OutFunctions.Num() + 7);

	FNiagaraFunctionSignature DefaultSignature;
	DefaultSignature.bMemberFunction = true;
	DefaultSignature.bRequiresContext = false;
	DefaultSignature.bSupportsGPU = true;
	DefaultSignature.bExperimental = true;
	DefaultSignature.bRequiresExecPin = true;

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawBoxName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Execute"))).SetValue(true);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Center")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Extents"))).SetValue(FVector(10.0f));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}

	{
		FNiagaraFunctionSignature & Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawCircleName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Execute"))).SetValue(true);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Center")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("XAxis"))).SetValue(FVector::XAxisVector);
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("YAxis"))).SetValue(FVector::YAxisVector);
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Radius"))).SetValue(10.0f);
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Segments"))).SetValue(6);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawCoordinateSystemName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Execute"))).SetValue(true);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Location")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Scale"))).SetValue(1.0f);
	}

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawGrid2DName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Execute"))).SetValue(true);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Center")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Extents"))).SetValue(FVector2D(10.0f));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsX"))).SetValue(1);
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY"))).SetValue(1);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawGrid3DName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Execute"))).SetValue(true);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Center")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Extents"))).SetValue(FVector(10.0f));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsX"))).SetValue(1);
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY"))).SetValue(1);
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsZ"))).SetValue(1);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}

	{
		FNiagaraFunctionSignature & Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawLineName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Execute"))).SetValue(true);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Start Location")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("End Location")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawSphereName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Execute"))).SetValue(true);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Center")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Radius"))).SetValue(10.0f);
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Segments"))).SetValue(6);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}

	FNiagaraTypeDefinition CoordTypeDef(FNiagaraTypeDefinition::GetCoordinateSpaceEnum());
	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawSpherePersistentName;
		Signature.FunctionSpecifiers.Add(TEXT("Identifier"));
		Signature.bIsCompileTagGenerator = true;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Center")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(CoordTypeDef, TEXT("CenterCoordinateSpace"))).SetValue(ENiagaraCoordinateSpace::Local);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("OffsetFromCenter")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(CoordTypeDef, TEXT("OffsetCoordinateSpace"))).SetValue(ENiagaraCoordinateSpace::Local);
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Radius"))).SetValue(10.0f);
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Segments"))).SetValue(36);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawBoxPersistentName;
		Signature.FunctionSpecifiers.Add(TEXT("Identifier"));
		Signature.bIsCompileTagGenerator = true;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Center")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(CoordTypeDef, TEXT("CenterCoordinateSpace"))).SetValue(ENiagaraCoordinateSpace::Local);
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Extents"))).SetValue(FVector(10.0f));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("HalfExtents"))).SetValue(true);
		Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("RotationAxis"))).SetValue(FVector(0.0f, 0.0f, 1.0f));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("RotationNormalizedAngle")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(CoordTypeDef, TEXT("RotationCoordinateSpace"))).SetValue(ENiagaraCoordinateSpace::Local);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Offset")));
		Signature.Inputs.Add_GetRef(FNiagaraVariable(CoordTypeDef, TEXT("OffsetCoordinateSpace"))).SetValue(ENiagaraCoordinateSpace::Local);
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}
}

void UNiagaraDataInterfaceDebugDraw::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	if (BindingInfo.Name == NDIDebugDrawLocal::DrawBoxName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(&NDIDebugDrawLocal::DrawDebug<NDIDebugDrawLocal::FDebugPrim_Box>);
	}
	else if (BindingInfo.Name == NDIDebugDrawLocal::DrawCircleName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(&NDIDebugDrawLocal::DrawDebug<NDIDebugDrawLocal::FDebugPrim_Circle>);
	}
	else if (BindingInfo.Name == NDIDebugDrawLocal::DrawCoordinateSystemName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(&NDIDebugDrawLocal::DrawDebug<NDIDebugDrawLocal::FDebugPrim_CoordinateSystem>);
	}
	else if (BindingInfo.Name == NDIDebugDrawLocal::DrawGrid2DName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(&NDIDebugDrawLocal::DrawDebug<NDIDebugDrawLocal::FDebugPrim_Grid2D>);
	}
	else if (BindingInfo.Name == NDIDebugDrawLocal::DrawGrid3DName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(&NDIDebugDrawLocal::DrawDebug<NDIDebugDrawLocal::FDebugPrim_Grid3D>);
	}
	else if (BindingInfo.Name == NDIDebugDrawLocal::DrawLineName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(&NDIDebugDrawLocal::DrawDebug<NDIDebugDrawLocal::FDebugPrim_Line>);
	}
	else if (BindingInfo.Name == NDIDebugDrawLocal::DrawSphereName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(&NDIDebugDrawLocal::DrawDebug<NDIDebugDrawLocal::FDebugPrim_Sphere>);
	}
	else if (BindingInfo.FunctionSpecifiers.Num() != 0)
	{
		// The HLSL translator adds this function specifier in so that we have a unqiue key during compilation.
		const FVMFunctionSpecifier* Specifier = BindingInfo.FunctionSpecifiers.FindByPredicate([&](const FVMFunctionSpecifier& Info) { return Info.Key == UNiagaraDataInterfaceDebugDraw::CompileTagKey; });

		if (Specifier && !Specifier->Value.IsNone())
		{
			FNDIDebugDrawInstanceData_GameThread* PerInstanceData = reinterpret_cast<FNDIDebugDrawInstanceData_GameThread*>(InstanceData);
			if (BindingInfo.Name == NDIDebugDrawLocal::DrawSpherePersistentName)
			{

#if NIAGARA_COMPUTEDEBUG_ENABLED
				PerInstanceData->AddNamedPersistentShape(Specifier->Value, UNiagaraDataInterfaceDebugDraw::Sphere);
#endif
				OutFunc = FVMExternalFunction::CreateStatic(&NDIDebugDrawLocal::DrawDebugPersistent<NDIDebugDrawLocal::FDebugPrim_Sphere>);
			}
			else if (BindingInfo.Name == NDIDebugDrawLocal::DrawBoxPersistentName)
			{
#if NIAGARA_COMPUTEDEBUG_ENABLED
				PerInstanceData->AddNamedPersistentShape(Specifier->Value, UNiagaraDataInterfaceDebugDraw::Box);
#endif
				OutFunc = FVMExternalFunction::CreateStatic(&NDIDebugDrawLocal::DrawDebugPersistent<NDIDebugDrawLocal::FDebugPrim_Box>);
			}
		}
	}
	
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceDebugDraw::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	if (!Super::AppendCompileHash(InVisitor))
		return false;

	FSHAHash Hash = GetShaderFileHash((TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceDebugDraw.ush")), EShaderPlatform::SP_PCD3D_SM5);
	InVisitor->UpdateString(TEXT("NiagaraDataInterfaceDebugDrawHLSLSource"), Hash.ToString());

	return true;
}

void UNiagaraDataInterfaceDebugDraw::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += TEXT("#include \"/Plugin/FX/Niagara/Private/NiagaraDataInterfaceDebugDraw.ush\"\n");
}

bool UNiagaraDataInterfaceDebugDraw::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> ArgsSample =
	{
		{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
	};

	if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawBoxName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bExecute, float3 Location, float4 Rotation, float3 Extents, float4 Color) { NDIDebugDraw_DrawBox(bExecute, Location, Rotation, Extents, Color); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawCircleName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bExecute, float3 Location, float3 XAxis, float3 YAxis, float Scale, int Segments, float4 Color) { NDIDebugDraw_Circle(bExecute, Location, XAxis, YAxis, Scale, Segments, Color); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawCoordinateSystemName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bExecute, float3 Location, float4 Rotation, float Scale) { NDIDebugDraw_CoordinateSystem(bExecute, Location, Rotation, Scale); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawGrid2DName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bExecute, float3 Center, float4 Rotation, float2 Extents, int NumCellsX, int NumCellsY, float4 Color) { NDIDebugDraw_Grid2D(bExecute, Center, Rotation, Extents, int2(NumCellsX, NumCellsY), Color); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawGrid3DName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bExecute, float3 Center, float4 Rotation, float3 Extents, int NumCellsX, int NumCellsY, int NumCellsZ, float4 Color) { NDIDebugDraw_Grid3D(bExecute, Center, Rotation, Extents, int3(NumCellsX, NumCellsY, NumCellsZ), Color); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawLineName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bExecute, float3 LineStart, float3 LineEnd, float4 Color) { NDIDebugDraw_Line(bExecute, LineStart, LineEnd, Color); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawSphereName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bExecute, float3 LineStart, float Radius, int Segments, float4 Color) { NDIDebugDraw_Sphere(bExecute, LineStart, Radius, Segments, Color); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawSpherePersistentName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(float3 Center, int CenterCoordinateSpace, float3 OffsetFromCenter, int OffsetCoordinateSpace, float Radius, int NumSegments, float4 Color){ }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawBoxPersistentName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(float3 Center, int CenterCoordinateSpace, float3 Extents, bool HalfExtents, float3 RotationAxis, float RotationAngle, int RotationCoordinateSpace, float3 Offset, int OffsetCoordinateSpace, float4 Color) { /* Do nothing for now..*/}\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}

	return false;
}

bool UNiagaraDataInterfaceDebugDraw::GenerateCompilerTagPrefix(const FNiagaraFunctionSignature& InSignature, FString& OutPrefix) const 
{
	if (InSignature.bIsCompileTagGenerator && InSignature.FunctionSpecifiers.Num() == 1)
	{
		for (auto Specifier : InSignature.FunctionSpecifiers)
		{
			if (!Specifier.Value.IsNone())
			{
				OutPrefix = Specifier.Value.ToString();
				return true;
			}
		}
	}
	return false;
}

#endif

bool UNiagaraDataInterfaceDebugDraw::GPUContextInit(const FNiagaraScriptDataInterfaceCompileInfo& InInfo, void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) const
{

#if NIAGARA_COMPUTEDEBUG_ENABLED
	FNDIDebugDrawInstanceData_GameThread* InstanceData = reinterpret_cast<FNDIDebugDrawInstanceData_GameThread*>(PerInstanceData);
	for (const FNiagaraFunctionSignature& Sig : InInfo.RegisteredFunctions)
	{
		if (PerInstanceData && Sig.FunctionSpecifiers.Num() > 0)
		{
			// The HLSL translator adds this function specifier in so that we have a unqiue key during compilation.
			const FName* Specifier = Sig.FunctionSpecifiers.Find(UNiagaraDataInterfaceDebugDraw::CompileTagKey);

			if (Specifier && !Specifier->IsNone())
			{
				if (Sig.Name == NDIDebugDrawLocal::DrawSpherePersistentName)
					InstanceData->AddNamedPersistentShape(*Specifier, UNiagaraDataInterfaceDebugDraw::Sphere);
				else if (Sig.Name == NDIDebugDrawLocal::DrawBoxPersistentName)
					InstanceData->AddNamedPersistentShape(*Specifier, UNiagaraDataInterfaceDebugDraw::Box);
			}
		}
	}
#endif
	return true;
}


bool UNiagaraDataInterfaceDebugDraw::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
#if NIAGARA_COMPUTEDEBUG_ENABLED
	FNDIDebugDrawInstanceData_GameThread* InstanceData = reinterpret_cast<FNDIDebugDrawInstanceData_GameThread*>(PerInstanceData);
	InstanceData->LineBuffer.Reset();
#endif
	return false;
}

bool UNiagaraDataInterfaceDebugDraw::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
#if NIAGARA_COMPUTEDEBUG_ENABLED
	FNDIDebugDrawInstanceData_GameThread* InstanceData = reinterpret_cast<FNDIDebugDrawInstanceData_GameThread*>(PerInstanceData);

	if (InstanceData)
	{
		if (NDIDebugDrawLocal::GNiagaraDebugDrawEnabled)
		{
			InstanceData->HandlePersistentShapes(SystemInstance, DeltaSeconds);
		}
	}

	// Dispatch information to the RT proxy
	ENQUEUE_RENDER_COMMAND(NDIDebugDrawUpdate)(
		[RT_Proxy=GetProxyAs<FNDIDebugDrawProxy>(), RT_InstanceID=SystemInstance->GetId(), RT_TickCount=SystemInstance->GetTickCount(), RT_LineBuffer=MoveTemp(InstanceData->LineBuffer)](FRHICommandListImmediate& RHICmdList) mutable
		{
			FNDIDebugDrawInstanceData_RenderThread* RT_InstanceData = &RT_Proxy->SystemInstancesToProxyData_RT.FindChecked(RT_InstanceID);

			if ( RT_InstanceData->GpuComputeDebug )
			{
				if ( FNiagaraSimulationDebugDrawData* DebugDraw = RT_InstanceData->GpuComputeDebug->GetSimulationDebugDrawData(RT_InstanceID, false) )
				{
					if (DebugDraw->LastUpdateTickCount != RT_TickCount)
					{
						DebugDraw->LastUpdateTickCount = RT_TickCount;
						DebugDraw->bRequiresUpdate = true;
						DebugDraw->StaticLines = MoveTemp(RT_LineBuffer);
					}
					else
					{
						DebugDraw->StaticLines += MoveTemp(RT_LineBuffer);
					}
				}
			}
		}
	);
#endif
	return false;
}

int32 UNiagaraDataInterfaceDebugDraw::PerInstanceDataSize() const
{
	return sizeof(FNDIDebugDrawInstanceData_GameThread);
}

bool UNiagaraDataInterfaceDebugDraw::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	auto* InstanceData = new(PerInstanceData) FNDIDebugDrawInstanceData_GameThread();

#if NIAGARA_COMPUTEDEBUG_ENABLED
	ENQUEUE_RENDER_COMMAND(NDIDebugDrawInit)(
		[RT_Proxy=GetProxyAs<FNDIDebugDrawProxy>(), RT_InstanceID=SystemInstance->GetId(), RT_Batcher=SystemInstance->GetBatcher()](FRHICommandListImmediate& RHICmdList)
		{
			check(!RT_Proxy->SystemInstancesToProxyData_RT.Contains(RT_InstanceID));
			FNDIDebugDrawInstanceData_RenderThread* RT_InstanceData = &RT_Proxy->SystemInstancesToProxyData_RT.Add(RT_InstanceID);
			RT_InstanceData->GpuComputeDebug = RT_Batcher->GetGpuComputeDebug();
		}
	);
#endif
	return true;
}

void UNiagaraDataInterfaceDebugDraw::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
#if NIAGARA_COMPUTEDEBUG_ENABLED
	auto* InstanceData = reinterpret_cast<FNDIDebugDrawInstanceData_GameThread*>(PerInstanceData);
	InstanceData->~FNDIDebugDrawInstanceData_GameThread();

	ENQUEUE_RENDER_COMMAND(NDIDebugDrawInit)(
		[RT_Proxy=GetProxyAs<FNDIDebugDrawProxy>(), RT_InstanceID=SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
		{
			FNDIDebugDrawInstanceData_RenderThread OriginalData;
			if ( ensure(RT_Proxy->SystemInstancesToProxyData_RT.RemoveAndCopyValue(RT_InstanceID, OriginalData)) )
			{
				if ( OriginalData.GpuComputeDebug )
				{
					OriginalData.GpuComputeDebug->RemoveSimulationDebugDrawData(RT_InstanceID);
				}
			}
		}
	);
#endif
}
