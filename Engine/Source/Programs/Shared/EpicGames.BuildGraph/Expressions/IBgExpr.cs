// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using EpicGames.BuildGraph.Expressions;
using EpicGames.Core;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Context object used for evaluating expressions
	/// </summary>
	public class BgExprContext
	{
		/// <summary>
		/// Command line options for the graph
		/// </summary>
		public Dictionary<string, string> Options { get; set; } = new Dictionary<string, string>();

		/// <summary>
		/// Map from tag name to fileset
		/// </summary>
		public Dictionary<string, FileSet> TagNameToFileSet { get; set; } = new Dictionary<string, FileSet>();
	}

	/// <summary>
	/// Base class for computable expressions
	/// </summary>
	public interface IBgExpr
	{
		/// <summary>
		/// Compute the value of this expression
		/// </summary>
		/// <param name="context"></param>
		object Compute(BgExprContext context);

		/// <summary>
		/// Helper method to create an expression which formats this expression as a string
		/// </summary>
		BgString ToBgString();
	}

	/// <summary>
	/// Interface for a computable expression.
	/// </summary>
	public interface IBgExpr<TExpr> : IBgExpr
	{
		/// <summary>
		/// Chooses between the current value and another value based on a condition
		/// </summary>
		/// <param name="condition">The condition to evaluate</param>
		/// <param name="valueIfTrue">Value to return if the condition is true</param>
		TExpr IfThen(BgBool condition, TExpr valueIfTrue);
	}

	/// <summary>
	/// Sentinel expression node which can be assigned different values
	/// </summary>
	/// <typeparam name="TExpr">The expression type</typeparam>
	public interface IBgExprVariable<TExpr> where TExpr : IBgExpr<TExpr>
	{
		/// <summary>
		/// The current value of the variable
		/// </summary>
		TExpr Value { get; set; }
	}
}
