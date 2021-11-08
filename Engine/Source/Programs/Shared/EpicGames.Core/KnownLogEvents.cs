// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;

namespace EpicGames.Core
{
#pragma warning disable CA1707 // Identifiers should not contain underscores
	/// <summary>
	/// Well known log events
	/// </summary>
	public static class KnownLogEvents
	{
		/// <summary>
		/// Unset
		/// </summary>
		public static EventId None { get; } = new EventId(0);

		/// <summary>
		/// Generic error
		/// </summary>
		public static EventId Generic { get; } = new EventId(1);

		/// <summary>
		/// Generic exception
		/// </summary>
		public static EventId Exception { get; } = new EventId(2);

		/// <summary>
		/// Compiler error
		/// </summary>
		public static EventId Compiler { get; } = new EventId(100);

		/// <summary>
		/// Linker error
		/// </summary>
		public static EventId Linker { get; } = new EventId(200);

		/// <summary>
		/// Linker: Undefined symbol
		/// </summary>
		public static EventId Linker_UndefinedSymbol { get; } = new EventId(201);

		/// <summary>
		/// Linker: Multiply defined symbol
		/// </summary>
		public static EventId Linker_DuplicateSymbol { get; } = new EventId(202);

		/// <summary>
		/// Engine error
		/// </summary>
		public static EventId Engine { get; } = new EventId(300);

		/// <summary>
		/// Engine log channel
		/// </summary>
		public static EventId Engine_LogChannel { get; } = new EventId(301);

		/// <summary>
		/// Engine: Crash dump
		/// </summary>
		public static EventId Engine_Crash { get; } = new EventId(302);

		/// <summary>
		/// Engine: UE_ASSET_LOG output
		/// </summary>
		public static EventId Engine_AssetLog { get; } = new EventId(303);

		/// <summary>
		/// UAT error
		/// </summary>
		public static EventId AutomationTool { get; } = new EventId(400);

		/// <summary>
		/// UAT: Crash dump
		/// </summary>
		public static EventId AutomationTool_Crash { get; } = new EventId(401);

		/// <summary>
		/// UAT: Exit code indicating a crash
		/// </summary>
		public static EventId AutomationTool_CrashExitCode { get; } = new EventId(402);

		/// <summary>
		/// UAT: Source file with line number
		/// </summary>
		public static EventId AutomationTool_SourceFileLine { get; } = new EventId(403);

		/// <summary>
		/// UAT: Missing copyright notice
		/// </summary>
		public static EventId AutomationTool_MissingCopyright { get; } = new EventId(404);

		/// <summary>
		/// MSBuild: Generic error
		/// </summary>
		public static EventId MSBuild { get; } = new EventId(500);

		/// <summary>
		/// Microsoft: Generic Visual Studio error
		/// </summary>
		public static EventId Microsoft { get; } = new EventId(550);

		/// <summary>
		/// Error message from Gauntlet
		/// </summary>
		public static EventId Gauntlet { get; } = new EventId(600);

		/// <summary>
		/// Error message from Gauntlet engine tests
		/// </summary>
		public static EventId Gauntlet_UnitTest { get; } = new EventId(601);

		/// <summary>
		/// Error message from Gauntlet screenshot tests
		/// </summary>
		public static EventId Gauntlet_ScreenshotTest { get; } = new EventId(602);

		/// <summary>
		/// A systemic event, relating to the health of the farm
		/// </summary>
		public static EventId Systemic { get; } = new EventId(700);

		/// <summary>
		/// A systemic event from XGE
		/// </summary>
		public static EventId Systemic_Xge { get; } = new EventId(710);

		/// <summary>
		/// Builds will run in standalone mode
		/// </summary>
		public static EventId Systemic_Xge_Standalone { get; } = new EventId(711);

		/// <summary>
		/// BuildService.exe is not running
		/// </summary>
		public static EventId Systemic_Xge_ServiceNotRunning { get; } = new EventId(712);

		/// <summary>
		/// DDC is slow
		/// </summary>
		public static EventId Systemic_SlowDDC { get; } = new EventId(720);

		/// <summary>
		/// Internal Horde error
		/// </summary>
		public static EventId Systemic_Horde { get; } = new EventId(730);

		/// <summary>
		/// Artifact upload failed
		/// </summary>
		public static EventId Systemic_Horde_ArtifactUpload { get; } = new EventId(731);

		/// <summary>
		/// Harmless pdbutil error
		/// </summary>
		public static EventId Systemic_PdbUtil { get; } = new EventId(740);

		/// <summary>
		/// A systemic event from MSBuild
		/// </summary>
		public static EventId Systemic_MSBuild { get; } = new EventId(750);
	}
#pragma warning restore CA1707 // Identifiers should not contain underscores
}
