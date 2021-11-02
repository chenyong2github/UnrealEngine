// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/Text/DisplayClusterConfigurationTextTypes.h"
#include "Formats/Text/DisplayClusterConfigurationTextStrings.h"

#include "Misc/DisplayClusterHelpers.h"

#include "DisplayClusterConfigurationLog.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// Info
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextInfo::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s]"),
		*FDisplayClusterConfigurationTextBase::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::info::Version, *Version);
}

bool FDisplayClusterConfigurationTextInfo::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::info::Version), Version);

	return FDisplayClusterConfigurationTextBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// ClusterNode
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextClusterNode::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s=%s, %s=%s, %s=%d, %s=%d, %s=%d, %s=%d, %s=%s]"),
		*FDisplayClusterConfigurationTextBase::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::Id,               *Id,
		DisplayClusterConfigurationTextStrings::cfg::data::cluster::Window,  *WindowId,
		DisplayClusterConfigurationTextStrings::cfg::data::cluster::Addr,    *Addr,
		DisplayClusterConfigurationTextStrings::cfg::data::cluster::Master,  *DisplayClusterHelpers::str::BoolToStr(IsMaster),
		DisplayClusterConfigurationTextStrings::cfg::data::cluster::PortCS,  Port_CS,
		DisplayClusterConfigurationTextStrings::cfg::data::cluster::PortSS,  Port_SS,
		DisplayClusterConfigurationTextStrings::cfg::data::cluster::PortCE,  Port_CE,
		DisplayClusterConfigurationTextStrings::cfg::data::cluster::PortCEB, Port_CEB,
		DisplayClusterConfigurationTextStrings::cfg::data::cluster::Sound,   *DisplayClusterHelpers::str::BoolToStr(SoundEnabled)
	);
}

bool FDisplayClusterConfigurationTextClusterNode::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::Id),                Id);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::cluster::Window),   WindowId);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::cluster::Addr),     Addr);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::cluster::Master),   IsMaster);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::cluster::PortCS),   Port_CS);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::cluster::PortSS),   Port_SS);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::cluster::PortCE),   Port_CE);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::cluster::PortCEB),  Port_CEB);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::cluster::Sound),    SoundEnabled);

	return FDisplayClusterConfigurationTextBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Window
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextWindow::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s=%s, %s=%s, %s=%d, %s=%d, %s=%d, %s=%d]"),
		*FDisplayClusterConfigurationTextBase::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::Id,                  *Id,
		DisplayClusterConfigurationTextStrings::cfg::data::window::Viewports,   *DisplayClusterHelpers::str::ArrayToStr(ViewportIds),
		DisplayClusterConfigurationTextStrings::cfg::data::window::Postprocess, *DisplayClusterHelpers::str::ArrayToStr(PostprocessIds),
		DisplayClusterConfigurationTextStrings::cfg::data::window::Fullscreen,  *DisplayClusterHelpers::str::BoolToStr(IsFullscreen),
		DisplayClusterConfigurationTextStrings::cfg::data::window::WinX,        WinX,
		DisplayClusterConfigurationTextStrings::cfg::data::window::WinY,        WinY,
		DisplayClusterConfigurationTextStrings::cfg::data::window::ResX,        ResX,
		DisplayClusterConfigurationTextStrings::cfg::data::window::ResY,        ResY
	);
}

bool FDisplayClusterConfigurationTextWindow::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::Id),                  Id);
	DisplayClusterHelpers::str::ExtractArray(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::window::Viewports),   FString(","), ViewportIds);
	DisplayClusterHelpers::str::ExtractArray(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::window::Postprocess), FString(","), PostprocessIds);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::window::Fullscreen),  IsFullscreen);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::window::WinX),        WinX);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::window::WinY),        WinY);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::window::ResX),        ResX);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::window::ResY),        ResY);

	return FDisplayClusterConfigurationTextBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Viewport
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextViewport::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s=%s, %s=%d, %s=%d, %s=%d, %s=%d, %s=%f, %s=%d, %s=%s]"),
		*FDisplayClusterConfigurationTextBase::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::Id, *Id,
		DisplayClusterConfigurationTextStrings::cfg::data::viewport::Projection,            *ProjectionId,
		DisplayClusterConfigurationTextStrings::cfg::data::viewport::Camera,                *CameraId,
		DisplayClusterConfigurationTextStrings::cfg::data::viewport::PosX,                  Loc.X,
		DisplayClusterConfigurationTextStrings::cfg::data::viewport::PosY,                  Loc.Y,
		DisplayClusterConfigurationTextStrings::cfg::data::viewport::Width,                 Size.X,
		DisplayClusterConfigurationTextStrings::cfg::data::viewport::Height,                Size.Y,
		DisplayClusterConfigurationTextStrings::cfg::data::viewport::BufferRatio,           BufferRatio,
		DisplayClusterConfigurationTextStrings::cfg::data::viewport::GPUIndex,              GPUIndex,
		DisplayClusterConfigurationTextStrings::cfg::data::viewport::IsShared,              *DisplayClusterHelpers::str::BoolToStr(IsShared));
}

bool FDisplayClusterConfigurationTextViewport::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::Id),                              Id);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::viewport::Projection),            ProjectionId);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::viewport::Camera),                CameraId);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::viewport::PosX),                  Loc.X);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::viewport::PosY),                  Loc.Y);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::viewport::Width),                 Size.X);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::viewport::Height),                Size.Y);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::viewport::BufferRatio),           BufferRatio);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::viewport::GPUIndex),              GPUIndex);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::viewport::IsShared),              IsShared);

	return FDisplayClusterConfigurationTextBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Projection
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextProjection::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s=%s]"),
		*FDisplayClusterConfigurationTextBase::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::Id, *Id,
		DisplayClusterConfigurationTextStrings::cfg::data::projection::Type, *Type,
		TEXT("params"), *Params);
}

bool FDisplayClusterConfigurationTextProjection::DeserializeFromString(const FString& line)
{
	// Save full string to allow to parse in a custom way (polymorphic)
	Params = line;

	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::Id), Id);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::projection::Type), Type);

	return FDisplayClusterConfigurationTextBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Postprocess
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextPostprocess::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s]"),
		*FDisplayClusterConfigurationTextBase::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::Id, *Id,
		DisplayClusterConfigurationTextStrings::cfg::data::postprocess::Type, *Type,
		*ConfigLine
	);
}

bool FDisplayClusterConfigurationTextPostprocess::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::Id), Id);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::postprocess::Type), Type);
	ConfigLine = line; //Save unparsed args for custom pp parsers

	return FDisplayClusterConfigurationTextBase::DeserializeFromString(line);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// SceneNode
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextSceneNode::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s=%s, %s=%s]"),
		*FDisplayClusterConfigurationTextBase::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::Id, *Id,
		DisplayClusterConfigurationTextStrings::cfg::data::ParentId, *ParentId,
		DisplayClusterConfigurationTextStrings::cfg::data::Loc, *Loc.ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::Rot, *Rot.ToString());
}

bool FDisplayClusterConfigurationTextSceneNode::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::Id),               Id);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::ParentId),         ParentId);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::Loc),              Loc);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::Rot),              Rot);
	return FDisplayClusterConfigurationTextBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Screen
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextScreen::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s]"),
		*FDisplayClusterConfigurationTextSceneNode::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::screen::Size, *Size.ToString());
}

bool FDisplayClusterConfigurationTextScreen::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::screen::Size), Size);
	return FDisplayClusterConfigurationTextSceneNode::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Camera
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextCamera::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%f, %s=%s, %s=%d]"),
		*FDisplayClusterConfigurationTextSceneNode::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::camera::EyeDist,     EyeDist,
		DisplayClusterConfigurationTextStrings::cfg::data::camera::EyeSwap,     *DisplayClusterHelpers::str::BoolToStr(EyeSwap),
		DisplayClusterConfigurationTextStrings::cfg::data::camera::ForceOffset, ForceOffset);
}

bool FDisplayClusterConfigurationTextCamera::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::camera::EyeDist),     EyeDist);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::camera::EyeSwap),     EyeSwap);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::camera::ForceOffset), ForceOffset);

	return FDisplayClusterConfigurationTextSceneNode::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// General
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextGeneral::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%d, %s=%d]"),
		*FDisplayClusterConfigurationTextBase::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::general::SwapSyncPolicy, SwapSyncPolicy,
		DisplayClusterConfigurationTextStrings::cfg::data::general::UnrealInputSyncPolicy, NativeInputSyncPolicy);
}

bool FDisplayClusterConfigurationTextGeneral::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::general::SwapSyncPolicy),        SwapSyncPolicy);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::general::UnrealInputSyncPolicy), NativeInputSyncPolicy);
	return FDisplayClusterConfigurationTextBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Nvidia
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextNvidia::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%d, %s=%d]"),
		*FDisplayClusterConfigurationTextBase::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::nvidia::SyncGroup,   SyncGroup,
		DisplayClusterConfigurationTextStrings::cfg::data::nvidia::SyncBarrier, SyncBarrier);
}

bool FDisplayClusterConfigurationTextNvidia::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::nvidia::SyncGroup),   SyncGroup);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::nvidia::SyncBarrier), SyncBarrier);
	return FDisplayClusterConfigurationTextBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Network
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextNetwork::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%d, %s=%d, %s=%d, %s=%d]"),
		*FDisplayClusterConfigurationTextBase::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::network::ClientConnectTriesAmount, ClientConnectTriesAmount,
		DisplayClusterConfigurationTextStrings::cfg::data::network::ClientConnectRetryDelay, ClientConnectRetryDelay,
		DisplayClusterConfigurationTextStrings::cfg::data::network::BarrierGameStartWaitTimeout, BarrierGameStartWaitTimeout,
		DisplayClusterConfigurationTextStrings::cfg::data::network::BarrierWaitTimeout, BarrierWaitTimeout);
}

bool FDisplayClusterConfigurationTextNetwork::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::network::ClientConnectTriesAmount),    ClientConnectTriesAmount);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::network::ClientConnectRetryDelay),     ClientConnectRetryDelay);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::network::BarrierGameStartWaitTimeout), BarrierGameStartWaitTimeout);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::network::BarrierWaitTimeout),          BarrierWaitTimeout);

	return FDisplayClusterConfigurationTextBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Debug
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextDebug::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s=%f]"),
		*FDisplayClusterConfigurationTextBase::ToString(),
		DisplayClusterConfigurationTextStrings::cfg::data::debug::DrawStats, *DisplayClusterHelpers::str::BoolToStr(DrawStats),
		DisplayClusterConfigurationTextStrings::cfg::data::debug::LagSim,    *DisplayClusterHelpers::str::BoolToStr(LagSimulateEnabled),
		DisplayClusterConfigurationTextStrings::cfg::data::debug::LagTime,   LagMaxTime);
}

bool FDisplayClusterConfigurationTextDebug::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::debug::DrawStats), DrawStats);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::debug::LagSim),    LagSimulateEnabled);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterConfigurationTextStrings::cfg::data::debug::LagTime),   LagMaxTime);
	return FDisplayClusterConfigurationTextBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Custom
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigurationTextCustom::ToString() const
{
	FString str = FDisplayClusterConfigurationTextBase::ToString() +  FString( + "[");
	int i = 0;

	for (auto it = Params.CreateConstIterator(); it; ++it)
	{
		str += FString::Printf(TEXT("\nCustom argument %d: %s=%s\n"), i++, *it->Key, *it->Value);
	}

	str += FString("]");

	return str;
}

bool FDisplayClusterConfigurationTextCustom::DeserializeFromString(const FString& line)
{
	// Non-typical way of specifying custom arguments (we don't know
	// the argument names) forces us to perform individual parsing approach.
	FString tmpLine = line;

	// Prepare string before parsing
	tmpLine.RemoveFromStart(DisplayClusterConfigurationTextStrings::cfg::data::custom::Header);
	tmpLine.TrimStartAndEndInline();

	DisplayClusterHelpers::str::StrToMap(line, Params);

	return FDisplayClusterConfigurationTextBase::DeserializeFromString(line);
}
