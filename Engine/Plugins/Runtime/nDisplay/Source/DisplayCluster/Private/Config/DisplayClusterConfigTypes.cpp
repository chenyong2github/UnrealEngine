// Copyright Epic Games, Inc. All Rights Reserved.

#include "Config/DisplayClusterConfigTypes.h"

#include "DisplayClusterLog.h"
#include "DisplayClusterStrings.h"
#include "Misc/DisplayClusterHelpers.h"

#include "DisplayClusterUtils/DisplayClusterTypesConverter.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigInfo
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigInfo::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s]"),
		*FDisplayClusterConfigBase::ToString(),
		DisplayClusterStrings::cfg::data::info::Version, *Version);
}

bool FDisplayClusterConfigInfo::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::info::Version), Version);

	return FDisplayClusterConfigBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigClusterNode
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigClusterNode::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s=%s, %s=%s, %s=%d, %s=%d, %s=%d, %s=%s]"),
		*FDisplayClusterConfigBase::ToString(),
		DisplayClusterStrings::cfg::data::Id,               *Id,
		DisplayClusterStrings::cfg::data::cluster::Window,  *WindowId,
		DisplayClusterStrings::cfg::data::cluster::Addr,    *Addr,
		DisplayClusterStrings::cfg::data::cluster::Master,  *DisplayClusterHelpers::str::BoolToStr(IsMaster),
		DisplayClusterStrings::cfg::data::cluster::PortCS,  Port_CS,
		DisplayClusterStrings::cfg::data::cluster::PortSS,  Port_SS,
		DisplayClusterStrings::cfg::data::cluster::PortCE,  Port_CE,
		DisplayClusterStrings::cfg::data::cluster::Sound,   *DisplayClusterHelpers::str::BoolToStr(SoundEnabled)
	);
}

bool FDisplayClusterConfigClusterNode::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::Id),                Id);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::cluster::Window),   WindowId);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::cluster::Addr),     Addr);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::cluster::Master),   IsMaster);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::cluster::PortCS),   Port_CS);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::cluster::PortSS),   Port_SS);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::cluster::PortCE),   Port_CE);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::cluster::Sound),    SoundEnabled);

	return FDisplayClusterConfigBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigWindow
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigWindow::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s=%s, %s=%s, %s=%d, %s=%d, %s=%d, %s=%d]"),
		*FDisplayClusterConfigBase::ToString(),
		DisplayClusterStrings::cfg::data::Id,                  *Id,
		DisplayClusterStrings::cfg::data::window::Viewports,   *DisplayClusterHelpers::str::ArrayToStr(ViewportIds),
		DisplayClusterStrings::cfg::data::window::Postprocess, *DisplayClusterHelpers::str::ArrayToStr(PostprocessIds),
		DisplayClusterStrings::cfg::data::window::Fullscreen,  *DisplayClusterHelpers::str::BoolToStr(IsFullscreen),
		DisplayClusterStrings::cfg::data::window::WinX,        WinX,
		DisplayClusterStrings::cfg::data::window::WinY,        WinY,
		DisplayClusterStrings::cfg::data::window::ResX,        ResX,
		DisplayClusterStrings::cfg::data::window::ResY,        ResY
	);
}

bool FDisplayClusterConfigWindow::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::Id),                  Id);
	DisplayClusterHelpers::str::ExtractArray(line, FString(DisplayClusterStrings::cfg::data::window::Viewports),   FString(DisplayClusterStrings::strArrayValSeparator), ViewportIds);
	DisplayClusterHelpers::str::ExtractArray(line, FString(DisplayClusterStrings::cfg::data::window::Postprocess), FString(DisplayClusterStrings::strArrayValSeparator), PostprocessIds);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::window::Fullscreen),  IsFullscreen);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::window::WinX),        WinX);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::window::WinY),        WinY);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::window::ResX),        ResX);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::window::ResY),        ResY);

	return FDisplayClusterConfigBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigViewport
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigViewport::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s=%s, %s=%d, %s=%d, %s=%d, %s=%d, %s=%s, %s=%d]"),
		*FDisplayClusterConfigBase::ToString(),
		DisplayClusterStrings::cfg::data::Id, *Id,
		DisplayClusterStrings::cfg::data::viewport::Projection,  *ProjectionId,
		DisplayClusterStrings::cfg::data::viewport::Camera,      *CameraId,
		DisplayClusterStrings::cfg::data::viewport::PosX,        Loc.X,
		DisplayClusterStrings::cfg::data::viewport::PosY,        Loc.Y,
		DisplayClusterStrings::cfg::data::viewport::Width,       Size.X,
		DisplayClusterStrings::cfg::data::viewport::Height,      Size.Y,
		DisplayClusterStrings::cfg::data::viewport::RTT,         *DisplayClusterHelpers::str::BoolToStr(IsRTT),
		DisplayClusterStrings::cfg::data::viewport::BufferRatio, BufferRatio
	);
}

bool FDisplayClusterConfigViewport::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::Id),                    Id);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::viewport::Projection),  ProjectionId);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::viewport::Camera),      CameraId);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::viewport::PosX),        Loc.X);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::viewport::PosY),        Loc.Y);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::viewport::Width),       Size.X);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::viewport::Height),      Size.Y);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::viewport::RTT),         IsRTT);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::viewport::BufferRatio), BufferRatio);

	return FDisplayClusterConfigBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigPostprocess
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigPostprocess::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s]"),
		*FDisplayClusterConfigBase::ToString(),
		DisplayClusterStrings::cfg::data::Id, *Id,
		DisplayClusterStrings::cfg::data::postprocess::PostprocessId, *PostprocessId,
		*ConfigLine
	);
}

bool FDisplayClusterConfigPostprocess::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::Id), Id);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::postprocess::PostprocessId), PostprocessId);
	ConfigLine = line; //Save unparsed args for custom pp parsers

	return FDisplayClusterConfigBase::DeserializeFromString(line);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigSceneNode
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigSceneNode::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s=%s, %s=%s, %s=%s, %s=%d]"),
		*FDisplayClusterConfigBase::ToString(),
		DisplayClusterStrings::cfg::data::Id, *Id,
		DisplayClusterStrings::cfg::data::ParentId, *ParentId,
		DisplayClusterStrings::cfg::data::Loc, *Loc.ToString(),
		DisplayClusterStrings::cfg::data::Rot, *Rot.ToString(),
		DisplayClusterStrings::cfg::data::scene::TrackerId, *TrackerId,
		DisplayClusterStrings::cfg::data::scene::TrackerCh, TrackerCh);
}

bool FDisplayClusterConfigSceneNode::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::Id),               Id);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::ParentId),         ParentId);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::Loc),              Loc);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::Rot),              Rot);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::scene::TrackerId), TrackerId);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::scene::TrackerCh), TrackerCh);
	return FDisplayClusterConfigBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigScreen
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigScreen::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s]"),
		*FDisplayClusterConfigSceneNode::ToString(),
		DisplayClusterStrings::cfg::data::screen::Size, *Size.ToString());
}

bool FDisplayClusterConfigScreen::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::screen::Size), Size);
	return FDisplayClusterConfigSceneNode::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigCamera
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigCamera::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%f, %s=%s, %s=%d]"),
		*FDisplayClusterConfigSceneNode::ToString(),
		DisplayClusterStrings::cfg::data::camera::EyeDist,     EyeDist,
		DisplayClusterStrings::cfg::data::camera::EyeSwap,     *DisplayClusterHelpers::str::BoolToStr(EyeSwap),
		DisplayClusterStrings::cfg::data::camera::ForceOffset, ForceOffset);
}

bool FDisplayClusterConfigCamera::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::camera::EyeDist),     EyeDist);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::camera::EyeSwap),     EyeSwap);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::camera::ForceOffset), ForceOffset);

	return FDisplayClusterConfigSceneNode::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigInput
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigInput::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s={%s}]"),
		*FDisplayClusterConfigBase::ToString(),
		DisplayClusterStrings::cfg::data::Id, *Id,
		DisplayClusterStrings::cfg::data::input::Type, *Type,
		TEXT("params"), *Params);
}

bool FDisplayClusterConfigInput::DeserializeFromString(const FString& line)
{
	// Save full string to allow an input device to parse (polymorphic)
	Params = line;

	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::Id),           Id);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::input::Type),  Type);
	DisplayClusterHelpers::str::ExtractMap(line,   FString(DisplayClusterStrings::cfg::data::input::Remap), DisplayClusterStrings::strArrayValSeparator, TEXT(":"), ChMap);

	return FDisplayClusterConfigBase::DeserializeFromString(line);
}

FString FDisplayClusterConfigInputSetup::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%d, %s=%s, %s=%s]"),
		*FDisplayClusterConfigBase::ToString(),
		DisplayClusterStrings::cfg::data::Id, *Id,
		DisplayClusterStrings::cfg::data::inputsetup::Channel, Channel,
		DisplayClusterStrings::cfg::data::inputsetup::Key, *Key,
		DisplayClusterStrings::cfg::data::inputsetup::Bind, *BindName);
}

bool FDisplayClusterConfigInputSetup::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::Id), Id);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::inputsetup::Channel), Channel);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::inputsetup::Key),     Key);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::inputsetup::Bind),    BindName);

	return FDisplayClusterConfigBase::DeserializeFromString(line);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigGeneral
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigGeneral::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%d, %s=%d]"),
		*FDisplayClusterConfigBase::ToString(),
		DisplayClusterStrings::cfg::data::general::SwapSyncPolicy, SwapSyncPolicy,
		DisplayClusterStrings::cfg::data::general::UnrealInputSyncPolicy, NativeInputSyncPolicy);
}

bool FDisplayClusterConfigGeneral::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::general::SwapSyncPolicy),        SwapSyncPolicy);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::general::UnrealInputSyncPolicy), NativeInputSyncPolicy);
	return FDisplayClusterConfigBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigRender
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigRender::ToString() const
{
	return FString::Printf(TEXT("%s + "),
		*FDisplayClusterConfigBase::ToString());
}

bool FDisplayClusterConfigRender::DeserializeFromString(const FString& line)
{
	return FDisplayClusterConfigBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigStereo
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigStereo::ToString() const
{
	return FString::Printf(TEXT("[%s]"),
		*FDisplayClusterConfigBase::ToString()
	);
}

bool FDisplayClusterConfigStereo::DeserializeFromString(const FString& line)
{
	return FDisplayClusterConfigBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigNetwork
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigNetwork::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%d, %s=%d, %s=%d, %s=%d]"),
		*FDisplayClusterConfigBase::ToString(),
		DisplayClusterStrings::cfg::data::network::ClientConnectTriesAmount, ClientConnectTriesAmount,
		DisplayClusterStrings::cfg::data::network::ClientConnectRetryDelay, ClientConnectRetryDelay,
		DisplayClusterStrings::cfg::data::network::BarrierGameStartWaitTimeout, BarrierGameStartWaitTimeout,
		DisplayClusterStrings::cfg::data::network::BarrierWaitTimeout, BarrierWaitTimeout);
}

bool FDisplayClusterConfigNetwork::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::network::ClientConnectTriesAmount),    ClientConnectTriesAmount);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::network::ClientConnectRetryDelay),     ClientConnectRetryDelay);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::network::BarrierGameStartWaitTimeout), BarrierGameStartWaitTimeout);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::network::BarrierWaitTimeout),          BarrierWaitTimeout);

	return FDisplayClusterConfigBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigDebug
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigDebug::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s=%f]"),
		*FDisplayClusterConfigBase::ToString(),
		DisplayClusterStrings::cfg::data::debug::DrawStats, *DisplayClusterHelpers::str::BoolToStr(DrawStats),
		DisplayClusterStrings::cfg::data::debug::LagSim,    *DisplayClusterHelpers::str::BoolToStr(LagSimulateEnabled),
		DisplayClusterStrings::cfg::data::debug::LagTime,   LagMaxTime);
}

bool FDisplayClusterConfigDebug::DeserializeFromString(const FString& line)
{
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::debug::DrawStats), DrawStats);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::debug::LagSim),    LagSimulateEnabled);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::debug::LagTime),   LagMaxTime);
	return FDisplayClusterConfigBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigCustom
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigCustom::ToString() const
{
	FString str = FDisplayClusterConfigBase::ToString() +  FString( + "[");
	int i = 0;

	for (auto it = Params.CreateConstIterator(); it; ++it)
	{
		str += FString::Printf(TEXT("\nCustom argument %d: %s=%s\n"), i++, *it->Key, *it->Value);
	}

	str += FString("]");

	return str;
}

bool FDisplayClusterConfigCustom::DeserializeFromString(const FString& line)
{
	// Non-typical way of specifying custom arguments (we don't know
	// the argument names) forces us to perform individual parsing approach.
	FString tmpLine = line;

	// Prepare string before parsing
	tmpLine.RemoveFromStart(DisplayClusterStrings::cfg::data::custom::Header);
	tmpLine.TrimStartAndEndInline();

	DisplayClusterHelpers::str::StrToMap(line, FString(DisplayClusterStrings::strPairSeparator), FString(DisplayClusterStrings::strKeyValSeparator), Params);

	return FDisplayClusterConfigBase::DeserializeFromString(line);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Projection
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigProjection::ToString() const
{
	return FString::Printf(TEXT("[%s + %s=%s, %s=%s, %s=%s]"),
		*FDisplayClusterConfigBase::ToString(),
		DisplayClusterStrings::cfg::data::Id,            *Id,
		DisplayClusterStrings::cfg::data::projection::Type, *Type,
		TEXT("params"), *Params);
}

bool FDisplayClusterConfigProjection::DeserializeFromString(const FString& line)
{
	// Save full string to allow to parse in a custom way (polymorphic)
	Params = line;

	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::Id),            Id);
	DisplayClusterHelpers::str::ExtractValue(line, FString(DisplayClusterStrings::cfg::data::projection::Type), Type);

	return FDisplayClusterConfigBase::DeserializeFromString(line);
}
