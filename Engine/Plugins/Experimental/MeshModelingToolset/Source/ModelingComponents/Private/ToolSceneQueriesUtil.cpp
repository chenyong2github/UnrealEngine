// Copyright Epic Games, Inc. All Rights Reserved.


#include "ToolSceneQueriesUtil.h"
#include "VectorUtil.h"
#include "Quaternion.h"


static double VISUAL_ANGLE_SNAP_THRESHOLD_DEG = 1.0;


double ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD()
{
	return VISUAL_ANGLE_SNAP_THRESHOLD_DEG;
}


bool ToolSceneQueriesUtil::PointSnapQuery(const UInteractiveTool* Tool, const FVector3d& Point1, const FVector3d& Point2, double VisualAngleThreshold)
{
	IToolsContextQueriesAPI* QueryAPI = Tool->GetToolManager()->GetContextQueriesAPI();
	FViewCameraState CameraState;
	QueryAPI->GetCurrentViewState(CameraState);
	return PointSnapQuery(CameraState, Point1, Point2, VisualAngleThreshold);
}

bool ToolSceneQueriesUtil::PointSnapQuery(const FViewCameraState& CameraState, const FVector3d& Point1, const FVector3d& Point2, double VisualAngleThreshold)
{
	double UseThreshold = (VisualAngleThreshold <= 0) ? GetDefaultVisualAngleSnapThreshD() : VisualAngleThreshold;
	double VisualAngle = VectorUtil::OpeningAngleD(Point1, Point2, (FVector3d)CameraState.Position);
	return FMathd::Abs(VisualAngle) < UseThreshold;
}



double ToolSceneQueriesUtil::CalculateViewVisualAngleD(const UInteractiveTool* Tool, const FVector3d& Point1, const FVector3d& Point2)
{
	IToolsContextQueriesAPI* QueryAPI = Tool->GetToolManager()->GetContextQueriesAPI();
	FViewCameraState CameraState;
	QueryAPI->GetCurrentViewState(CameraState);
	return CalculateViewVisualAngleD(CameraState, Point1, Point2);
}

double ToolSceneQueriesUtil::CalculateViewVisualAngleD(const FViewCameraState& CameraState, const FVector3d& Point1, const FVector3d& Point2)
{
	double VisualAngle = VectorUtil::OpeningAngleD(Point1, Point2, (FVector3d)CameraState.Position);
	return FMathd::Abs(VisualAngle);
}



double ToolSceneQueriesUtil::CalculateDimensionFromVisualAngleD(const UInteractiveTool* Tool, const FVector3d& Point, double TargetVisualAngleDeg)
{
	IToolsContextQueriesAPI* QueryAPI = Tool->GetToolManager()->GetContextQueriesAPI();
	FViewCameraState CameraState;
	QueryAPI->GetCurrentViewState(CameraState);
	return CalculateDimensionFromVisualAngleD(CameraState, Point, TargetVisualAngleDeg);
}
double ToolSceneQueriesUtil::CalculateDimensionFromVisualAngleD(const FViewCameraState& CameraState, const FVector3d& Point, double TargetVisualAngleDeg)
{
	FVector3d EyePos = (FVector3d)CameraState.Position;
	FVector3d PointVec = Point - EyePos;
	FVector3d RotPointPos = EyePos + FQuaterniond(CameraState.Up(), TargetVisualAngleDeg, true)*PointVec;
	double ActualAngleDeg = CalculateViewVisualAngleD(CameraState, Point, RotPointPos);
	return Point.Distance(RotPointPos) * (TargetVisualAngleDeg/ActualAngleDeg);
}



bool ToolSceneQueriesUtil::IsPointVisible(const FViewCameraState& CameraState, const FVector3d& Point)
{
	if (CameraState.bIsOrthographic == false)
	{
		FVector3d PointDir = (Point - CameraState.Position);
		//@todo should use view frustum here!
		if (PointDir.Dot(CameraState.Forward()) < 0.25)		// ballpark estimate
		{
			return false;
		}
	}
	else
	{
		// @todo probably not always true but it's not exactly clear how ortho camera is configured...
		return true;
	}
	return true;
}


bool ToolSceneQueriesUtil::FindSceneSnapPoint(const UInteractiveTool* Tool, const FVector3d& Point, FVector3d& SnapPointOut,
	bool bVertices, bool bEdges, double VisualAngleThreshold, 
	FSnapGeometry* SnapGeometry, FVector* DebugTriangleOut)
{
	double UseThreshold = (VisualAngleThreshold <= 0) ? GetDefaultVisualAngleSnapThreshD() : VisualAngleThreshold;
	IToolsContextQueriesAPI* QueryAPI = Tool->GetToolManager()->GetContextQueriesAPI();
	FSceneSnapQueryRequest Request;
	Request.RequestType = ESceneSnapQueryType::Position;
	Request.TargetTypes = ESceneSnapQueryTargetType::None;
	if (bVertices)
	{
		Request.TargetTypes |= ESceneSnapQueryTargetType::MeshVertex;
	}
	if (bEdges)
	{
		Request.TargetTypes |= ESceneSnapQueryTargetType::MeshEdge;
	}
	Request.Position = (FVector)Point;
	Request.VisualAngleThresholdDegrees = VISUAL_ANGLE_SNAP_THRESHOLD_DEG;
	TArray<FSceneSnapQueryResult> Results;
	if (QueryAPI->ExecuteSceneSnapQuery(Request, Results))
	{
		SnapPointOut = Results[0].Position;

		if (SnapGeometry != nullptr)
		{
			int iSnap = Results[0].TriSnapIndex;
			SnapGeometry->Points[0] = Results[0].TriVertices[iSnap];
			SnapGeometry->PointCount = 1;
			if (Results[0].TargetType == ESceneSnapQueryTargetType::MeshEdge)
			{
				SnapGeometry->Points[1] = Results[0].TriVertices[(iSnap+1)%3];
				SnapGeometry->PointCount = 2;
			}
		}

		if (DebugTriangleOut != nullptr)
		{
			DebugTriangleOut[0] = Results[0].TriVertices[0];
			DebugTriangleOut[1] = Results[0].TriVertices[1];
			DebugTriangleOut[2] = Results[0].TriVertices[2];
		}

		return true;
	}
	return false;
}


bool ToolSceneQueriesUtil::FindWorldGridSnapPoint(const UInteractiveTool* Tool, const FVector3d& Point, FVector3d& GridSnapPointOut)
{
	IToolsContextQueriesAPI* QueryAPI = Tool->GetToolManager()->GetContextQueriesAPI();
	FSceneSnapQueryRequest Request;
	Request.RequestType = ESceneSnapQueryType::Position;
	Request.TargetTypes = ESceneSnapQueryTargetType::Grid;
	Request.Position = (FVector)Point;
	TArray<FSceneSnapQueryResult> Results;
	if ( QueryAPI->ExecuteSceneSnapQuery(Request, Results) )
	{
		GridSnapPointOut = Results[0].Position;
		return true;
	};
	return false;
}