// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/ChaosVDTraceAnalyzer.h"

#include "Trace/ChaosVDTraceProvider.h"
#include "TraceServices/Model/AnalysisSession.h"

void FChaosVDTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	FInterfaceBuilder& Builder = Context.InterfaceBuilder;
	Builder.RouteEvent(RouteId_ChaosVDParticle, "ChaosVDLogger", "ChaosVDParticle");
	
	Builder.RouteEvent(RouteId_ChaosVDSolverFrameStart, "ChaosVDLogger", "ChaosVDSolverFrameStart");
	Builder.RouteEvent(RouteId_ChaosVDSolverFrameEnd, "ChaosVDLogger", "ChaosVDSolverFrameEnd");
	
	Builder.RouteEvent(RouteId_ChaosVDSolverStepStart, "ChaosVDLogger", "ChaosVDSolverStepStart");
	Builder.RouteEvent(RouteId_ChaosVDSolverStepEnd, "ChaosVDLogger", "ChaosVDSolverStepEnd");

	TraceServices::FAnalysisSessionEditScope _(Session);
	ChaosVDTraceProvider->CreateRecordingInstanceForSession(Session.GetName());
}

void FChaosVDTraceAnalyzer::OnAnalysisEnd()
{
	OnAnalysisComplete().Broadcast();
}

bool FChaosVDTraceAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	LLM_SCOPE_BYNAME(TEXT("Insights/FChaosVDTraceAnalyzer"));

	TraceServices::FAnalysisSessionEditScope _(Session);

	const FEventData& EventData = Context.EventData;
	
	switch (RouteId)
	{
	case RouteId_ChaosVDSolverFrameStart:
		{
			FChaosVDSolverFrameData NewFrameData;

			NewFrameData.SolverID = EventData.GetValue<int32>("SolverID");

			FWideStringView DebugNameView;
			EventData.GetString("DebugName", DebugNameView);
			NewFrameData.DebugName = DebugNameView;

			// Add an empty frame. It will be filled out by the solver trace events
			ChaosVDTraceProvider->AddFrame(NewFrameData.SolverID, MoveTemp(NewFrameData));
	
			break;
		}
	case RouteId_ChaosVDSolverFrameEnd:
		{
			break;
		}
	case RouteId_ChaosVDSolverStepStart:
		{
			const int32 SolverID = EventData.GetValue<int32>("SolverID");

			// This can be null if the recording started Mid-Frame. In this case we just discard the data for now
			if (FChaosVDSolverFrameData* FrameData  = ChaosVDTraceProvider->GetLastFrame(SolverID))
			{
				// Add an empty step. It will be filled out by the particle (and later on other objects/elements) events
				FrameData->SolverSteps.AddDefaulted();
			}
	
			break;
		}
	case RouteId_ChaosVDSolverStepEnd:
		{
			break;
		}
	case RouteId_ChaosVDParticle:
		{
			const int32 SolverID = EventData.GetValue<int32>("SolverID");

			FChaosVDParticleDebugData ParticleData;
			ReadParticleDataFromEvent(EventData, ParticleData);

			// This can be null if the recording started Mid-Frame. In this case we just discard the data for now
			if (FChaosVDSolverFrameData* FrameData = ChaosVDTraceProvider->GetLastFrame(SolverID))
			{
				if (FrameData->SolverSteps.Num() > 0)
				{
					FrameData->SolverSteps.Last().RecordedParticles.Add(MoveTemp(ParticleData));
				}
			}

			break;
		}
	default:
		break;
	}

	return true;
}

void FChaosVDTraceAnalyzer::ReadParticleDataFromEvent(const FEventData& InEventData, FChaosVDParticleDebugData& OutParticleData)
{
	OutParticleData.ParticleType = static_cast<EChaosVDParticleType>(InEventData.GetValue<uint8>("ParticleType"));
	OutParticleData.ParticleState = static_cast<EChaosVDParticleState>(InEventData.GetValue<int8>("ObjectState"));
	OutParticleData.ParticleIndex = InEventData.GetValue<int32>("ParticleID");

	FWideStringView DebugNameView;
	InEventData.GetString("DebugName", DebugNameView);

	OutParticleData.DebugName = DebugNameView;

	OutParticleData.Position.X = InEventData.GetValue<float>("PositionX");
	OutParticleData.Position.Y = InEventData.GetValue<float>("PositionY");
	OutParticleData.Position.Z = InEventData.GetValue<float>("PositionZ");

	OutParticleData.Rotation.X = InEventData.GetValue<float>("RotationX");
	OutParticleData.Rotation.Y = InEventData.GetValue<float>("RotationY");
	OutParticleData.Rotation.Z = InEventData.GetValue<float>("RotationZ");
	OutParticleData.Rotation.W = InEventData.GetValue<float>("RotationW");
			
	OutParticleData.Velocity.X = InEventData.GetValue<float>("VelocityX");
	OutParticleData.Velocity.Y = InEventData.GetValue<float>("VelocityY");
	OutParticleData.Velocity.Z = InEventData.GetValue<float>("VelocityZ");
			
	OutParticleData.AngularVelocity.X = InEventData.GetValue<float>("AngularVelocityX");
	OutParticleData.AngularVelocity.Y = InEventData.GetValue<float>("AngularVelocityY");
	OutParticleData.AngularVelocity.Z = InEventData.GetValue<float>("AngularVelocityZ");
}

