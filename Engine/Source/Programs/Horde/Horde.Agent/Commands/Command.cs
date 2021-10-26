// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace HordeAgent
{
	/// <summary>
	/// Base class for all commands that can be executed by HordeAgent
	/// </summary>
	abstract class Command
	{
		/// <summary>
		/// Configure this object with the given command line arguments
		/// </summary>
		/// <param name="Arguments">Command line arguments</param>
		/// <param name="Logger">Logging output device</param>
		public virtual void Configure(CommandLineArguments Arguments, ILogger Logger)
		{
			Arguments.ApplyTo(this, Logger);
		}

		/// <summary>
		/// Gets all command line parameters to show in help for this command
		/// </summary>
		/// <param name="Arguments">The command line arguments</param>
		/// <returns>List of name/description pairs</returns>
		public virtual List<KeyValuePair<string, string>> GetParameters(CommandLineArguments Arguments)
		{
			return CommandLineArguments.GetParameters(GetType());
		}

		/// <summary>
		/// Execute this command
		/// </summary>
		/// <param name="Logger">The logger to use for this command</param>
		/// <returns>Exit code</returns>
		public abstract Task<int> ExecuteAsync(ILogger Logger);
	}
}
