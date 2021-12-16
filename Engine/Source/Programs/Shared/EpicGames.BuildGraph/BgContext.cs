// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.BuildGraph.Expressions;
using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Context for executing a node method
	/// </summary>
	public abstract class BgContext
	{
		/// <summary>
		/// The stream executing the current build
		/// </summary>
		public abstract string Stream { get; }

		/// <summary>
		/// Changelist being built
		/// </summary>
		public abstract int Change { get; }

		/// <summary>
		/// The code changelist currently being built
		/// </summary>
		public abstract int CodeChange { get; }

		/// <summary>
		/// Version number for the engine
		/// </summary>
		public abstract (int Major, int Minor, int Patch) EngineVersion { get; }

		/// <summary>
		/// Whether this machine is a builder
		/// </summary>
		public abstract bool IsBuildMachine { get; }

		/// <summary>
		/// Context for evaluating expressions
		/// </summary>
		public BgExprContext Context { get; }

		/// <summary>
		/// All outputs for the node
		/// </summary>
		public HashSet<FileReference> BuildProducts = new HashSet<FileReference>();

		/// <summary>
		/// Constructor
		/// </summary>
		public BgContext(BgExprContext Context)
		{
			this.Context = Context;
		}

		/// <summary>
		/// Resolve a boolean expression to a value
		/// </summary>
		/// <param name="Bool">The boolean expression</param>
		/// <returns>Value of the expression</returns>
		public bool Get(BgBool Bool) => Bool.Compute(Context);

		/// <summary>
		/// Resolve an integer expression to a value
		/// </summary>
		/// <param name="Integer">The integer expression</param>
		/// <returns>Value of the expression</returns>
		public int Get(BgInt Integer) => Integer.Compute(Context);

		/// <summary>
		/// Resolve a string expression to a value
		/// </summary>
		/// <param name="String">The string expression</param>
		/// <returns>Value of the expression</returns>
		public string Get(BgString String) => String.Compute(Context);

		/// <summary>
		/// Resolves an enum expression to a value
		/// </summary>
		/// <typeparam name="TEnum">The enum type</typeparam>
		/// <param name="Enum">Enum expression</param>
		/// <returns>The enum value</returns>
		public TEnum Get<TEnum>(BgEnum<TEnum> Enum) where TEnum : struct => Enum.Compute(Context);

		/// <summary>
		/// Resolve a list of enums to a value
		/// </summary>
		/// <typeparam name="TEnum">The enum type</typeparam>
		/// <param name="List">Enum expression</param>
		/// <returns>The enum value</returns>
		public List<TEnum> Get<TEnum>(BgList<BgEnum<TEnum>> List) where TEnum : struct => List.GetEnumerable(Context).Select(x => Get(x)).ToList();

		/// <summary>
		/// Resolve a list of strings
		/// </summary>
		/// <param name="List">List expression</param>
		/// <returns></returns>
		public List<string> Get(BgList<BgString> List) => List.Compute(Context);

		/// <summary>
		/// Resolve a file set
		/// </summary>
		/// <param name="FileSet">The token expression</param>
		/// <returns>Set of files for the token</returns>
		public FileSet Get(BgFileSet FileSet) => FileSet.ComputeValue(Context);

		/// <summary>
		/// Resolve a file set
		/// </summary>
		/// <param name="FileSets">The token expression</param>
		/// <returns>Set of files for the token</returns>
		public FileSet Get(BgList<BgFileSet> FileSets)
		{
			FileSet Result = FileSet.Empty;
			foreach (BgFileSet FileSet in FileSets.GetEnumerable(Context))
			{
				Result += Get(FileSet);
			}
			return Result;
		}

		/// <summary>
		/// Resolve a file set
		/// </summary>
		/// <param name="FileSets">The token expression</param>
		/// <returns>Set of files for the token</returns>
		public FileSet Get(IEnumerable<BgFileSet> FileSets)
		{
			FileSet Result = FileSet.Empty;
			foreach (BgFileSet FileSet in FileSets)
			{
				Result += FileSet.ComputeValue(Context);
			}
			return Result;
		}
	}
}
