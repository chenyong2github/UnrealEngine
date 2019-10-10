// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Run.Interfaces
{
	/// <summary>
	/// Interface for classes which listen for error matches
	/// </summary>
	interface IErrorListener : IDisposable
	{
		/// <summary>
		/// Called when an error is matched
		/// </summary>
		/// <param name="Error">The matched error</param>
		void OnErrorMatch(ErrorMatch Error);
	}
}
