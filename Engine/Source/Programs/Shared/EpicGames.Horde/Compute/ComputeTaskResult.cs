// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Describes an action to be executed in a particular workspace
	/// </summary>
	public class ComputeTaskResult
	{
		/// <summary>
		/// Exit code of the process
		/// </summary>
		[CbField("e")]
		public int ExitCode { get; set; }

		/// <summary>
		/// Hash of the stdout data
		/// </summary>
		[CbField("so")]
		public CbBinaryAttachment? StdOutHash { get; set; }

		/// <summary>
		/// Hash of the stderr data
		/// </summary>
		[CbField("se")]
		public CbBinaryAttachment? StdErrHash { get; set; }

		/// <summary>
		/// Hash of the output <see cref="DirectoryTree"/> stored in the CAS. Only items matching the output paths in the action will be included.
		/// </summary>
		[CbField("o")]
		public CbObjectAttachment? OutputHash { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		private ComputeTaskResult()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ExitCode">Exit code of the process</param>
		public ComputeTaskResult(int ExitCode)
		{
			this.ExitCode = ExitCode;
		}
	}
}
