// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tokenizer;
using System;
using System.Text;

namespace EpicGames.UHT.Utils
{
	/// <summary>
	/// Compiler exception that immediately stops the processing of the current header file.
	/// </summary>
	public class UhtException : Exception
	{
		/// <summary>
		/// The generated message
		/// </summary>
		public UhtMessage UhtMessage;

		/// <summary>
		/// Internal do nothing constructor
		/// </summary>
		protected UhtException()
		{
		}

		/// <summary>
		/// Exception with a simple message.  Context will be the current header file.
		/// </summary>
		/// <param name="Message">Text of the error</param>
		public UhtException(string Message)
		{
			this.UhtMessage = UhtMessage.MakeMessage(UhtMessageType.Error, null, null, 1, Message);
		}

		/// <summary>
		/// Exception with a simple message.  Context from the given message site.
		/// </summary>
		/// <param name="MessageSite">Site generating the exception</param>
		/// <param name="Message">Text of the error</param>
		/// <param name="ExtraContext">Addition context to be appended to the error message</param>
		public UhtException(IUhtMessageSite MessageSite, string Message, object? ExtraContext = null)
		{
			if (ExtraContext != null)
			{
				Message = $"{Message} while parsing {UhtMessage.FormatContext(ExtraContext)}";
			}
			this.UhtMessage = UhtMessage.MakeMessage(UhtMessageType.Error, MessageSite.MessageSource, null, MessageSite.GetLineNumber(), Message);
		}

		/// <summary>
		/// Make an exception to be thrown
		/// </summary>
		/// <param name="MessageSite">Message site to be associated with the exception</param>
		/// <param name="LineNumber">Line number of the error</param>
		/// <param name="Message">Text of the error</param>
		/// <param name="ExtraContext">Addition context to be appended to the error message</param>
		public UhtException(IUhtMessageSite MessageSite, int LineNumber, string Message, object? ExtraContext = null)
		{
			if (ExtraContext != null)
			{
				Message = $"{Message} while parsing {UhtMessage.FormatContext(ExtraContext)}";
			}
			this.UhtMessage = UhtMessage.MakeMessage(UhtMessageType.Error, MessageSite.MessageSource, null, MessageSite.GetLineNumber(LineNumber), Message);
		}
	}

	/// <summary>
	/// Exception where the current token isn't what was expected
	/// </summary>
	public class UhtTokenException : UhtException
	{

		/// <summary>
		/// Make a parsing error for when there is a mismatch between the expected token and what was parsed.
		/// </summary>
		/// <param name="MessageSite">Message site to be associated with the exception</param>
		/// <param name="Got">The parsed token.  Support for EOF also provided.</param>
		/// <param name="Expected">What was expected.</param>
		/// <param name="ExtraContext">Extra context to be appended to the error message</param>
		/// <returns>The exception object to throw</returns>
		public UhtTokenException(IUhtMessageSite MessageSite, UhtToken Got, object? Expected, object? ExtraContext = null)
		{
			string Message = Expected != null 
				? $"Found {UhtMessage.FormatContext(Got)} when expecting {UhtMessage.FormatContext(Expected)}{FormatExtraContext(MessageSite, ExtraContext)}"
				: $"Found {UhtMessage.FormatContext(Got)}{FormatExtraContext(MessageSite, ExtraContext)}";
			this.UhtMessage = UhtMessage.MakeMessage(UhtMessageType.Error, MessageSite.MessageSource, null, Got.InputLine, Message);
		}

		/// <summary>
		/// Format any extra context supplied by the caller or the message site
		/// </summary>
		/// <param name="MessageSite">Message site that may contain extra context</param>
		/// <param name="ExtraContext">Additional caller supplied context</param>
		/// <returns></returns>
		private string FormatExtraContext(IUhtMessageSite MessageSite, object? ExtraContext = null)
		{
			StringBuilder Builder = new StringBuilder(" while parsing ");
			int StartingLength = Builder.Length;
			if (ExtraContext != null)
			{
				Builder.Append(UhtMessage.FormatContext(ExtraContext));
			}
			UhtMessage.Append(Builder, UhtTlsMessageExtraContext.GetMessageExtraContext(), StartingLength != Builder.Length);

			return Builder.Length != StartingLength ? Builder.ToString() : string.Empty;
		}
	}

	/// <summary>
	/// Internal compiler error exception
	/// </summary>
	public class UhtIceException : UhtException
	{
		/// <summary>
		/// Exception with a simple message.  Context will be the current header file.
		/// </summary>
		/// <param name="Message">Text of the error</param>
		public UhtIceException(string Message)
		{
			this.UhtMessage = UhtMessage.MakeMessage(UhtMessageType.Ice, null, null, 1, Message);
		}
	}
}
