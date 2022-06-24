// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq.Expressions;
using System.Threading.Tasks;
using EpicGames.BuildGraph.Expressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace EpicGames.BuildGraph.Expressions
{
	/// <summary>
	/// Describes an agent that can execute execute build steps
	/// </summary>
	public class BgAgent : BgExpr
	{
		/// <summary>
		/// Name of the agent
		/// </summary>
		public BgString Name { get; }

		/// <summary>
		/// List of agent types to select from, in order of preference. The first agent type supported by a stream will be used.
		/// </summary>
		public BgList<BgString> Types { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgAgent(BgString name, BgString type)
			: this(name, BgList<BgString>.Create(type))
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public BgAgent(BgString name, BgList<BgString> types)
			: base(BgExprFlags.ForceFragment)
		{
			Name = name;
			Types = types;
		}

		/// <inheritdoc/>
		public override void Write(BgBytecodeWriter writer)
		{
			writer.WriteOpcode(BgOpcode.Agent);
			writer.WriteExpr(Name);
			writer.WriteExpr(Types);
		}

		/// <inheritdoc/>
		public override BgString ToBgString() => "{Agent}";
	}
}
