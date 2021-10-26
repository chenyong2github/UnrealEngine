// Copyright Epic Games, Inc. All Rights Reserved.

using Grpc.Core;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Exception class designed to allow logging structured log messages
	/// </summary>
	[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1032:Implement standard exception constructors", Justification = "<Pending>")]
	public class StructuredRpcException : RpcException
	{
		/// <summary>
		/// The format string with named parameters
		/// </summary>
		public string Format { get; }

		/// <summary>
		/// The argument list
		/// </summary>
		public object[] Args { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StatusCode">Status code to return</param>
		/// <param name="Format">The format string</param>
		/// <param name="Args">Arguments for the format string</param>
		public StructuredRpcException(StatusCode StatusCode, string Format, params object[] Args)
			: base(new Status(StatusCode, FormatMessage(Format, Args)))
		{
			this.Format = Format;
			this.Args = Args;
		}

		/// <summary>
		/// Replace named arguments in the format message with their values
		/// </summary>
		/// <param name="Format"></param>
		/// <param name="Args"></param>
		/// <returns></returns>
		static string FormatMessage(string Format, params object[] Args)
		{
			string NewFormat = ConvertToFormatString(Format);
			return String.Format(CultureInfo.CurrentCulture, NewFormat, Args);
		}

		/// <summary>
		/// Converts a named parameter format string to a String.Format style string
		/// </summary>
		/// <param name="Format"></param>
		/// <returns></returns>
		static string ConvertToFormatString(string Format)
		{
			int ArgIdx = 0;

			StringBuilder NewFormat = new StringBuilder();
			for (int Idx = 0; Idx < Format.Length; Idx++)
			{
				char Character = Format[Idx];
				NewFormat.Append(Character);

				if (Character == '{' && Idx + 1 < Format.Length)
				{
					char NextCharacter = Format[Idx + 1];
					if ((NextCharacter >= 'a' && NextCharacter <= 'z') || (NextCharacter >= 'A' && NextCharacter <= 'Z') || NextCharacter == '_')
					{
						for (int EndIdx = Idx + 2; EndIdx < Format.Length; EndIdx++)
						{
							if (Format[EndIdx] == ':' || Format[EndIdx] == '}')
							{
								NewFormat.Append(ArgIdx++);
								Idx = EndIdx - 1;
								break;
							}
						}
					}
				}
			}

			return NewFormat.ToString();
		}
	}
}
