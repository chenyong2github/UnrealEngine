// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;

namespace HordeAgent.Parser.Interfaces
{
	/// <summary>
	/// Interface for context on the log being parsed
	/// </summary>
	interface ILogContext
	{
		/// <summary>
		/// The workspace root directory
		/// </summary>
		DirectoryReference? WorkspaceDir { get; }

		/// <summary>
		/// The root of the branch
		/// </summary>
		string? PerforceStream { get; }

		/// <summary>
		/// The current changelist being built
		/// </summary>
		int? PerforceChange { get; }

		/// <summary>
		/// Whether the logger has logged errors so far. This can be used to disable errors that would otherwise be perceived as noise.
		/// </summary>
		bool HasLoggedErrors { get; }
	}
}
