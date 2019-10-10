// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Tools.DotNETCommon
{
	/// <summary>
	/// Base class for user-facing fatal errors. These errors are shown to the user prior to termination, without a callstack, and may dictate the program exit code.
	/// </summary>
	public class FatalErrorException : Exception
	{
		/// <summary>
		/// Exit code for the process
		/// </summary>
		public int ExitCode = 1;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Message">The error message to display.</param>
		public FatalErrorException(string Message)
			: base(Message)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InnerException">An inner exception to wrap</param>
		/// <param name="Message">The error message to display.</param>
		public FatalErrorException(Exception InnerException, string Message)
			: base(Message, InnerException)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Format">Formatting string for the error message</param>
		/// <param name="Arguments">Arguments for the formatting string</param>
		public FatalErrorException(string Format, params object[] Arguments)
			: base(String.Format(Format, Arguments))
		{
		}

		/// <summary>
		/// Constructor which wraps another exception
		/// </summary>
		/// <param name="InnerException">The inner exception being wrapped</param>
		/// <param name="Format">Format for the message string</param>
		/// <param name="Arguments">Format arguments</param>
		public FatalErrorException(Exception InnerException, string Format, params object[] Arguments)
			: base(String.Format(Format, Arguments), InnerException)
		{
		}

		/// <summary>
		/// Returns the string representing the exception. Our build exceptions do not show the callstack since they are used to report known error conditions.
		/// </summary>
		/// <returns>Message for the exception</returns>
		public override string ToString()
		{
			return Message;
		}
	}
}
