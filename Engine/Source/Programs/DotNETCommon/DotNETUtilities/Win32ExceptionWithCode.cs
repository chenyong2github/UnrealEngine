// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Tools.DotNETCommon
{
	/// <summary>
	/// Wrapper for Win32Exception which includes the error code in the exception message
	/// </summary>
	class Win32ExceptionWithCode : Win32Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Code">The Windows error code</param>
		public Win32ExceptionWithCode(int Code)
			: base(Code)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Message">Message to display</param>
		public Win32ExceptionWithCode(string Message)
			: base(Message)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Code">The Windows error code</param>
		/// <param name="Message">Message to display</param>
		public Win32ExceptionWithCode(int Code, string Message)
			: base(Code, Message)
		{
		}

		/// <summary>
		/// Returns the exception message. Overriden to include the error code in the message.
		/// </summary>
		public override string Message
		{
			get { return String.Format("{0} (code 0x{1:X8})", base.Message, base.NativeErrorCode); }
		}
	}
}
