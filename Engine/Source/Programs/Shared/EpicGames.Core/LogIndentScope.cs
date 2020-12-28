// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Class to apply a log indent for the lifetime of an object 
	/// </summary>
	public class LogIndentScope : IDisposable
	{
		/// <summary>
		/// Whether the object has been disposed
		/// </summary>
		bool Disposed;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Indent">Indent to append to the existing indent</param>
		public LogIndentScope(string Indent)
		{
			LogIndent.Push(Indent);
		}

		/// <summary>
		/// Restore the log indent to normal
		/// </summary>
		public void Dispose()
		{
			if (!Disposed)
			{
				LogIndent.Pop();
				Disposed = true;
			}
		}
	}
}
