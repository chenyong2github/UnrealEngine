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
	public class Action
	{
		/// <summary>
		/// Hash of a <see cref="Command"/> object encoded as a CbObject and stored in the CAS
		/// </summary>
		[CbField("c")]
		public IoHash CommandHash { get; set; }

		/// <summary>
		/// Hash of a <see cref="DirectoryTree"/> object encoded as a CbObject and stored in the CAS
		/// </summary>
		[CbField("s")]
		public IoHash SandboxHash { get; set; }

		/// <summary>
		/// Requirements for the agent to execute the work
		/// </summary>
		[CbField("r")]
		public Requirements? Requirements { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="CommandHash"></param>
		/// <param name="SandboxHash"></param>
		public Action(IoHash CommandHash, IoHash SandboxHash)
		{
			this.CommandHash = CommandHash;
			this.SandboxHash = SandboxHash;
		}
	}
}
