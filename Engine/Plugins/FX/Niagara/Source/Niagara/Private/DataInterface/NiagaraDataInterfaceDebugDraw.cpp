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

struct FNDIDebugDrawInstanceData_GameThread
{
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

	FCriticalSection LineBufferLock;
	TArray<FNiagaraSimulationDebugDrawData::FGpuLine> LineBuffer;
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

#if NIAGARA_COMPUTEDEBUG_ENABLED
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bDraw)
		{
			const FVector Location = Bindings.LocationParam.GetAndAdvance();
			const FQuat Rotation = Bindings.RotationParam.GetAndAdvance();
			const FVector Extents = Bindings.ExtentsParam.GetAndAdvance();
			const FLinearColor Color = Bindings.ColorParam.GetAndAdvance();

			if ( bDraw )
			{
				const FVector Points[] =
				{
					Location + Rotation.RotateVector(FVector( Extents.X,  Extents.Y,  Extents.Z)),
					Location + Rotation.RotateVector(FVector(-Extents.X,  Extents.Y,  Extents.Z)),
					Location + Rotation.RotateVector(FVector(-Extents.X, -Extents.Y,  Extents.Z)),
					Location + Rotation.RotateVector(FVector( Extents.X, -Extents.Y,  Extents.Z)),
					Location + Rotation.RotateVector(FVector( Extents.X,  Extents.Y, -Extents.Z)),
					Location + Rotation.RotateVector(FVector(-Extents.X,  Extents.Y, -Extents.Z)),
					Location + Rotation.RotateVector(FVector(-Extents.X, -Extents.Y, -Extents.Z)),
					Location + Rotation.RotateVector(FVector( Extents.X, -Extents.Y, -Extents.Z)),
				};
				InstanceData->AddLine(Points[0], Points[1], Color);
				InstanceData->AddLine(Points[1], Points[2], Color);
				InstanceData->AddLine(Points[2], Points[3], Color);
				InstanceData->AddLine(Points[3], Points[0], Color);

				InstanceData->AddLine(Points[4], Points[5], Color);
				InstanceData->AddLine(Points[5], Points[6], Color);
				InstanceData->AddLine(Points[6], Points[7], Color);
				InstanceData->AddLine(Points[7], Points[4], Color);

				InstanceData->AddLine(Points[0], Points[4], Color);
				InstanceData->AddLine(Points[1], Points[5], Color);
				InstanceData->AddLine(Points[2], Points[6], Color);
				InstanceData->AddLine(Points[3], Points[7], Color);
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
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bDraw)
		{
			const FVector Location = Bindings.LocationParam.GetAndAdvance();
			const FVector XAxis = Bindings.XAxisParam.GetAndAdvance();
			const FVector YAxis = Bindings.YAxisParam.GetAndAdvance();
			const float Scale = Bindings.ScaleParam.GetAndAdvance();
			const int32 Segments = FMath::Clamp(Bindings.SegmentsParam.GetAndAdvance(), 4, 16);
			const FLinearColor Color = Bindings.ColorParam.GetAndAdvance();

			if (bDraw)
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
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bDraw)
		{
			const FVector Location = Bindings.LocationParam.GetAndAdvance();
			const FQuat Rotation = Bindings.RotationParam.GetAndAdvance();
			const float Scale = Bindings.ScaleParam.GetAndAdvance();

			if (bDraw)
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
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bDraw)
		{
			const FVector Center = Bindings.CenterParam.GetAndAdvance();
			const FQuat Rotation = Bindings.RotationParam.GetAndAdvance();
			const FVector2D Extents = Bindings.ExtentsParam.GetAndAdvance();
			const FIntPoint NumCells(Bindings.NumCellsXParam.GetAndAdvance(), Bindings.NumCellsYParam.GetAndAdvance());
			const FLinearColor Color = Bindings.ColorParam.GetAndAdvance();

			if (bDraw && NumCells.X > 0 && NumCells.Y > 0)
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
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bDraw)
		{
			const FVector Center = Bindings.CenterParam.GetAndAdvance();
			const FQuat Rotation = Bindings.RotationParam.GetAndAdvance();
			const FVector Extents = Bindings.ExtentsParam.GetAndAdvance();
			const FIntVector NumCells(Bindings.NumCellsXParam.GetAndAdvance(), Bindings.NumCellsYParam.GetAndAdvance(), Bindings.NumCellsZParam.GetAndAdvance());
			const FLinearColor Color = Bindings.ColorParam.GetAndAdvance();

			if (bDraw && NumCells.X > 0 && NumCells.Y > 0 && NumCells.Z > 0)
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
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bDraw)
		{
			const FVector LineStart = Bindings.LineStartParam.GetAndAdvance();
			const FVector LineEnd = Bindings.LineEndParam.GetAndAdvance();
			const FLinearColor Color = Bindings.ColorParam.GetAndAdvance();

			if (bDraw)
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

#if NIAGARA_COMPUTEDEBUG_ENABLED
		static void Draw(FNDIDebugDrawInstanceData_GameThread* InstanceData, VMBindings& Bindings, bool bDraw)
		{
			const FVector Location = Bindings.LocationParam.GetAndAdvance();
			const float Radius = Bindings.RadiusParam.GetAndAdvance();
			const int32 Segments = FMath::Clamp(Bindings.SegmentsParam.GetAndAdvance(), 4, 16);
			const FLinearColor Color = Bindings.ColorParam.GetAndAdvance();

			if (bDraw)
			{
				const float uinc = 2.0f * PI / float(Segments);

				float ux = 0.0f;
				float SinX0 = FMath::Sin(ux);
				float CosX0 = FMath::Cos(ux);
				for (int x=0; x < Segments; ++x)
				{
					ux += uinc;
					const float SinX1 = FMath::Sin(ux);
					const float CosX1 = FMath::Cos(ux);

					float uy = 0.0f;
					float SinY0 = FMath::Sin(uy);
					float CosY0 = FMath::Cos(uy);
					for (int y=0; y < Segments; ++y)
					{
						uy += uinc;
						const float SinY1 = FMath::Sin(uy);
						const float CosY1 = FMath::Cos(uy);

						const FVector Point0 = Location + FVector(CosX0 * CosY0, SinY0, SinX0 * CosY0) * Radius;
						const FVector Point1 = Location + FVector(CosX1 * CosY0, SinY0, SinX1 * CosY0) * Radius;
						const FVector Point2 = Location + FVector(CosX0 * CosY1, SinY1, SinX0 * CosY1) * Radius;
						InstanceData->AddLine(Point0, Point1, Color);
						InstanceData->AddLine(Point0, Point2, Color);

						SinY0 = SinY1;
						CosY0 = CosY1;
					}

					SinX0 = SinX1;
					CosX0 = CosX1;
				}
			}
		}
#endif
	};

	template<typename TPrimType>
	void DrawDebug(FVectorVMContext& Context)
	{
		VectorVM::FUserPtrHandler<FNDIDebugDrawInstanceData_GameThread> InstanceData(Context);
		FNDIInputParam<FNiagaraBool> DoDrawParam(Context);
		typename TPrimType::VMBindings Bindings(Context);

#if NIAGARA_COMPUTEDEBUG_ENABLED
		if ( !NDIDebugDrawLocal::GNiagaraDebugDrawEnabled )
		{
			return;
		}

		for ( int i=0; i < Context.NumInstances; ++i )
		{
			const bool bDraw = DoDrawParam.GetAndAdvance();
			TPrimType::Draw(InstanceData, Bindings, bDraw);
		}
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
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
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
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("bDraw")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Center")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Extents")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}

	{
		FNiagaraFunctionSignature & Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawCircleName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("bDraw")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Center")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("XAxis")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("YAxis")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Radius")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Segments")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawCoordinateSystemName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("bDraw")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Location")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Scale")));
	}

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawGrid2DName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("bDraw")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Center")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Extents")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsX")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawGrid3DName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("bDraw")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Center")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Extents")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsX")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsZ")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}

	{
		FNiagaraFunctionSignature & Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawLineName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("bDraw")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Start Location")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("End Location")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
	}

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.Add_GetRef(DefaultSignature);
		Signature.Name = NDIDebugDrawLocal::DrawSphereName;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DebugDrawInterface")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("bDraw")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Center")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Radius")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Segments")));
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
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bDraw, float3 Location, float4 Rotation, float3 Extents, float4 Color) { NDIDebugDraw_DrawBox(bDraw, Location, Rotation, Extents, Color); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawCircleName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bDraw, float3 Location, float3 XAxis, float3 YAxis, float Scale, int Segments, float4 Color) { NDIDebugDraw_Circle(bDraw, Location, XAxis, YAxis, Scale, Segments, Color); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawCoordinateSystemName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bDraw, float3 Location, float4 Rotation, float Scale) { NDIDebugDraw_CoordinateSystem(bDraw, Location, Rotation, Scale); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawGrid2DName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bDraw, float3 Center, float4 Rotation, float2 Extents, int NumCellsX, int NumCellsY, float4 Color) { NDIDebugDraw_Grid2D(bDraw, Center, Rotation, Extents, int2(NumCellsX, NumCellsY), Color); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawGrid3DName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bDraw, float3 Center, float4 Rotation, float3 Extents, int NumCellsX, int NumCellsY, int NumCellsZ, float4 Color) { NDIDebugDraw_Grid3D(bDraw, Center, Rotation, Extents, int3(NumCellsX, NumCellsY, NumCellsZ), Color); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawLineName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bDraw, float3 LineStart, float3 LineEnd, float4 Color) { NDIDebugDraw_Line(bDraw, LineStart, LineEnd, Color); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIDebugDrawLocal::DrawSphereName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(bool bDraw, float3 LineStart, float Radius, int Segments, float4 Color) { NDIDebugDraw_Sphere(bDraw, LineStart, Radius, Segments, Color); }\n");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}

	return false;
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
