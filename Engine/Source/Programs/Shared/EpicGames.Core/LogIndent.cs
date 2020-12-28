// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;

namespace EpicGames.Core
{
	/// <summary>
	/// Static class for tracking a stack of log indent prefixes
	/// </summary>
	public static class LogIndent
	{
		/// <summary>
		/// Tracks the current state
		/// </summary>
		class State
		{
			public State? PrevState { get; }
			public string Indent { get; }

			public State(State? PrevState, string Indent)
			{
				this.PrevState = PrevState;
				this.Indent = Indent;
			}
		}

		/// <summary>
		/// The current state value
		/// </summary>
		static AsyncLocal<State?> CurrentState = new AsyncLocal<State?>();

		/// <summary>
		/// Gets the current indent string
		/// </summary>
		public static string Current => CurrentState.Value?.Indent ?? String.Empty;

		/// <summary>
		/// Push a new indent onto the stack
		/// </summary>
		/// <param name="Indent">The indent to add</param>
		public static void Push(string Indent)
		{
			State? PrevState = CurrentState.Value;
			if (PrevState != null)
			{
				Indent = PrevState.Indent + Indent;
			}
			CurrentState.Value = new State(PrevState, Indent);
		}

		/// <summary>
		/// Pops an indent off the stack
		/// </summary>
		public static void Pop()
		{
			CurrentState.Value = CurrentState.Value!.PrevState;
		}
	}
}
