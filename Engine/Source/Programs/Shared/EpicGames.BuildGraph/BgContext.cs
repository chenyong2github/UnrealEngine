// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Linq;
using EpicGames.BuildGraph.Expressions;
using EpicGames.Core;

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
		public HashSet<FileReference> BuildProducts { get; } = new HashSet<FileReference>();

		/// <summary>
		/// Constructor
		/// </summary>
		public BgContext(BgExprContext context)
		{
			Context = context;
		}

		/// <summary>
		/// Resolve a boolean expression to a value
		/// </summary>
		/// <param name="expr">The boolean expression</param>
		/// <returns>Value of the expression</returns>
		public bool Get(BgBool expr) => expr.Compute(Context);

		/// <summary>
		/// Resolve an integer expression to a value
		/// </summary>
		/// <param name="expr">The integer expression</param>
		/// <returns>Value of the expression</returns>
		public int Get(BgInt expr) => expr.Compute(Context);

		/// <summary>
		/// Resolve a string expression to a value
		/// </summary>
		/// <param name="expr">The string expression</param>
		/// <returns>Value of the expression</returns>
		public string Get(BgString expr) => expr.Compute(Context);

		/// <summary>
		/// Resolves an enum expression to a value
		/// </summary>
		/// <typeparam name="TEnum">The enum type</typeparam>
		/// <param name="expr">Enum expression</param>
		/// <returns>The enum value</returns>
		public TEnum Get<TEnum>(BgEnum<TEnum> expr) where TEnum : struct => expr.Compute(Context);

		/// <summary>
		/// Resolve a list of enums to a value
		/// </summary>
		/// <typeparam name="TEnum">The enum type</typeparam>
		/// <param name="expr">Enum expression</param>
		/// <returns>The enum value</returns>
		public List<TEnum> Get<TEnum>(BgList<BgEnum<TEnum>> expr) where TEnum : struct => expr.GetEnumerable(Context).Select(x => Get(x)).ToList();

		/// <summary>
		/// Resolve a list of strings
		/// </summary>
		/// <param name="expr">List expression</param>
		/// <returns></returns>
		public List<string> Get(BgList<BgString> expr) => expr.Compute(Context);

		/// <summary>
		/// Resolve a file set
		/// </summary>
		/// <param name="fileSet">The token expression</param>
		/// <returns>Set of files for the token</returns>
		public FileSet Get(BgFileSet fileSet) => fileSet.ComputeValue(Context);

		/// <summary>
		/// Resolve a file set
		/// </summary>
		/// <param name="fileSets">The token expression</param>
		/// <returns>Set of files for the token</returns>
		public FileSet Get(BgList<BgFileSet> fileSets)
		{
			FileSet result = FileSet.Empty;
			foreach (BgFileSet fileSet in fileSets.GetEnumerable(Context))
			{
				result += Get(fileSet);
			}
			return result;
		}

		/// <summary>
		/// Resolve a file set
		/// </summary>
		/// <param name="fileSets">The token expression</param>
		/// <returns>Set of files for the token</returns>
		public FileSet Get(IEnumerable<BgFileSet> fileSets)
		{
			FileSet result = FileSet.Empty;
			foreach (BgFileSet fileSet in fileSets)
			{
				result += fileSet.ComputeValue(Context);
			}
			return result;
		}
	}
}
