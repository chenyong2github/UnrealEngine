// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tokenizer;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;

namespace EpicGames.UHT.Utils
{

	/// <summary>
	/// Type of message
	/// </summary>
	public enum UhtMessageType
	{
		/// <summary>
		/// The message is an error and goes to the log and console
		/// </summary>
		Error,

		/// <summary>
		/// The message is a warning and goes to the log and console
		/// </summary>
		Warning,

		/// <summary>
		/// The message is for information only and goes to the log and console
		/// </summary>
		Info,

		/// <summary>
		/// The message is for debugging and goes to the log only
		/// </summary>
		Trace,

		/// <summary>
		/// The message is an internal error and goes to the log and console
		/// </summary>
		Ice,
	}

	/// <summary>
	/// A message session is the destination object for all generated messages
	/// </summary>
	public interface IUhtMessageSession
	{

		/// <summary>
		/// Add the given message
		/// </summary>
		/// <param name="Message">The message to be added</param>
		void AddMessage(UhtMessage Message);
	}

	/// <summary>
	/// A message source represents the source file where the message occurred.
	/// </summary>
	public interface IUhtMessageSource
	{
		/// <summary>
		/// File path of the file being parsed
		/// </summary>
		string MessageFilePath { get; }

		/// <summary>
		/// The full file path of being parsed
		/// </summary>
		string MessageFullFilePath { get; }

		/// <summary>
		/// If true, the source is a source fragment from the testing harness
		/// </summary>
		bool bMessageIsFragment { get; }

		/// <summary>
		/// If this is a fragment, this is the container file path of the fragment
		/// </summary>
		string MessageFragmentFilePath { get; }

		/// <summary>
		/// If this is a fragment, this is the container full file path of the fragment
		/// </summary>
		string MessageFragmentFullFilePath { get; }

		/// <summary>
		/// If this is a fragment, this is the line number in the container file where the fragment is defined.
		/// </summary>
		int MessageFragmentLineNumber { get; }
	}

	/// <summary>
	/// A message site can automatically provide a line number where the site was defined
	/// in the source.  If no line number is provided when the message is created or if the
	/// site doesn't support this interface, the line number of '1' will be used.
	/// </summary>
	public interface IUhtMessageLineNumber
	{

		/// <summary>
		/// Line number where the type was defined.
		/// </summary>
		int MessageLineNumber { get; }
	}

	/// <summary>
	/// This interface provides a mechanism for things to provide more context for an error
	/// </summary>
	public interface IUhtMessageExtraContext
	{
		/// <summary>
		/// Enumeration of objects to add as extra context.
		/// </summary>
		IEnumerable<object?>? MessageExtraContext { get; }
	}

	/// <summary>
	/// A message site is any object that can generate a message.  In general, all 
	/// types are also message sites. This provides a convenient method to log messages
	/// where the type was defined.
	/// </summary>
	public interface IUhtMessageSite
	{

		/// <summary>
		/// Destination message session for the messages
		/// </summary>
		public IUhtMessageSession MessageSession { get; }
		
		/// <summary>
		/// Source file generating messages
		/// </summary>
		public IUhtMessageSource? MessageSource { get; }

		/// <summary>
		/// Optional line number where type was defined
		/// </summary>
		public IUhtMessageLineNumber? MessageLineNumber { get; }
	}

	/// <summary>
	/// Represents a UHT message
	/// </summary>
	public struct UhtMessage
	{
		/// <summary>
		/// The type of message
		/// </summary>
		public UhtMessageType MessageType;

		/// <summary>
		/// Optional message source for the message.  Either the MessageSource or FilePath must be set.
		/// </summary>
		public IUhtMessageSource? MessageSource;

		/// <summary>
		/// Optional file path for the message.  Either the MessageSource or FilePath must be set.
		/// </summary>
		public string? FilePath;

		/// <summary>
		/// Line number where error occurred.
		/// </summary>
		public int LineNumber;

		/// <summary>
		/// Text of the message
		/// </summary>
		public string Message;

		/// <summary>
		/// Make a new message with the given settings
		/// </summary>
		/// <param name="MessageType">Type of message</param>
		/// <param name="MessageSource">Source of the message</param>
		/// <param name="FilePath">File path of the message</param>
		/// <param name="LineNumber">Line number where message occurred</param>
		/// <param name="Message">Text of the message</param>
		/// <returns>Created message</returns>
		public static UhtMessage MakeMessage(UhtMessageType MessageType, IUhtMessageSource? MessageSource, string? FilePath, int LineNumber, string Message)
		{
			return new UhtMessage
			{
				MessageType = MessageType,
				MessageSource = MessageSource,
				FilePath = FilePath,
				LineNumber = LineNumber,
				Message = Message
			};
		}

		/// <summary>
		/// Format an object to be included in a message
		/// </summary>
		/// <param name="Context">Contextual object</param>
		/// <returns>Formatted context</returns>
		public static string FormatContext(object Context)
		{
			if (Context is IUhtMessageExtraContext ExtraContextInterface)
			{
				StringBuilder Builder = new StringBuilder();
				Append(Builder, ExtraContextInterface, false);
				return Builder.ToString();
			}
			else if (Context is UhtToken Token)
			{
				switch (Token.TokenType)
				{
					case UhtTokenType.EndOfFile:
						return "EOF";
					case UhtTokenType.EndOfDefault:
						return "'end of default value'";
					case UhtTokenType.EndOfType:
						return "'end of type'";
					case UhtTokenType.EndOfDeclaration:
						return "'end of declaration'";
					case UhtTokenType.StringConst:
						return "string constant";
					default:
						return $"'{Token.Value}'";
				}
			}
			else if (Context is char C)
			{
				return $"'{C}'";
			}
			else if (Context is string[] StringArray)
			{
				return UhtUtilities.MergeTypeNames(StringArray, "or", true);
			}
			else
			{
				return Context.ToString() ?? string.Empty;
			}
		}

		/// <summary>
		/// Append message context
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="MessageExtraContextInterface">Extra context to append</param>
		/// <param name="bAlwaysIncludeSeparator">If true, always include the separator</param>
		public static void Append(StringBuilder Builder, IUhtMessageExtraContext? MessageExtraContextInterface, bool bAlwaysIncludeSeparator)
		{
			if (MessageExtraContextInterface == null)
			{
				return;
			}

			IEnumerable<object?>? ExtraContextList = MessageExtraContextInterface.MessageExtraContext;
			if (ExtraContextList != null)
			{
				int StartingLength = Builder.Length;
				foreach (object? EC in ExtraContextList)
				{
					if (EC != null)
					{
						if (Builder.Length != StartingLength || bAlwaysIncludeSeparator)
						{
							Builder.Append(" in ");
						}
						Builder.Append(UhtMessage.FormatContext(EC));
					}
				}
			}
		}
	}

	/// <summary>
	/// A placeholder message site
	/// </summary>
	public class UhtEmptyMessageSite : IUhtMessageSite
	{
		/// <inheritdoc/>
		public IUhtMessageSession MessageSession => throw new NotImplementedException();

		/// <inheritdoc/>
		public IUhtMessageSource? MessageSource => throw new NotImplementedException();

		/// <inheritdoc/>
		public IUhtMessageLineNumber? MessageLineNumber => throw new NotImplementedException();
	}

	/// <summary>
	/// Creates a message site from a message session interface and a message source interface
	/// </summary>
	public class UhtSimpleMessageSite : IUhtMessageSite
	{
		private readonly IUhtMessageSession MessageSessionInternal;
		private IUhtMessageSource? MessageSourceInternal;

		#region IUHTMessageSite implementation
		/// <inheritdoc/>
		public IUhtMessageSession MessageSession => this.MessageSessionInternal;
		/// <inheritdoc/>
		public IUhtMessageSource? MessageSource { get => this.MessageSourceInternal; set => this.MessageSourceInternal = value; }
		/// <inheritdoc/>
		public IUhtMessageLineNumber? MessageLineNumber => null;
		#endregion

		/// <summary>
		/// Create a simple message site for the given session and source
		/// </summary>
		/// <param name="MessageSession">Associated message session</param>
		/// <param name="MessageSource">Source for the messages</param>
		public UhtSimpleMessageSite(IUhtMessageSession MessageSession, IUhtMessageSource? MessageSource = null)
		{
			this.MessageSessionInternal = MessageSession;
			this.MessageSourceInternal = MessageSource;
		}
	}

	/// <summary>
	/// Simple message site for the given file.
	/// </summary>
	public class UhtSimpleFileMessageSite : UhtSimpleMessageSite, IUhtMessageSource
	{
		#region IUHTMessageSource implementation
		/// <inheritdoc/>
		public string MessageFilePath { get => this.FilePath; }
		/// <inheritdoc/>
		public string MessageFullFilePath { get => this.FilePath; }
		/// <inheritdoc/>
		public bool bMessageIsFragment { get => false; }
		/// <inheritdoc/>
		public string MessageFragmentFilePath { get => ""; }
		/// <inheritdoc/>
		public string MessageFragmentFullFilePath { get => ""; }
		/// <inheritdoc/>
		public int MessageFragmentLineNumber { get => -1; }
		#endregion

		private string FilePath;

		/// <summary>
		/// Create a simple file site
		/// </summary>
		/// <param name="MessageSession">Associated message session</param>
		/// <param name="FilePath">File associated with the site</param>
		public UhtSimpleFileMessageSite(IUhtMessageSession MessageSession, string FilePath) : base(MessageSession, null)
		{
			this.FilePath = FilePath;
			this.MessageSource = this;
		}
	}

	/// <summary>
	/// Series of extensions for message sites.
	/// </summary>
	public static class UhtMessageSiteExtensions
	{
		/// <summary>
		/// Get the line number generating the error
		/// </summary>
		/// <param name="MessageSite">The message site generating the error.</param>
		/// <param name="LineNumber">An override line number</param>
		/// <returns>Either the overriding line number or the line number from the message site.</returns>
		public static int GetLineNumber(this IUhtMessageSite MessageSite, int LineNumber = -1)
		{
			if (LineNumber != -1)
			{
				return LineNumber;
			}
			else if (MessageSite.MessageLineNumber != null)
			{
				return MessageSite.MessageLineNumber.MessageLineNumber;
			}
			else
			{
				return 1;
			}
		}

		/// <summary>
		/// Log an error
		/// </summary>
		/// <param name="MessageSite">Message site associated with the message</param>
		/// <param name="LineNumber">Line number of the error</param>
		/// <param name="Message">Text of the error</param>
		/// <param name="ExtraContext">Addition context to be appended to the error message</param>
		public static void LogError(this IUhtMessageSite MessageSite, int LineNumber, string Message, object? ExtraContext = null)
		{
			LogMessage(UhtMessageType.Error, MessageSite, LineNumber, Message, ExtraContext);
		}

		/// <summary>
		/// Log an error
		/// </summary>
		/// <param name="MessageSite">Message site associated with the message</param>
		/// <param name="Message">Text of the error</param>
		/// <param name="ExtraContext">Addition context to be appended to the error message</param>
		public static void LogError(this IUhtMessageSite MessageSite, string Message, object? ExtraContext = null)
		{
			LogMessage(UhtMessageType.Error, MessageSite, -1, Message, ExtraContext);
		}

		/// <summary>
		/// Log a warning
		/// </summary>
		/// <param name="MessageSite">Message site associated with the message</param>
		/// <param name="LineNumber">Line number of the warning</param>
		/// <param name="Message">Text of the warning</param>
		/// <param name="ExtraContext">Addition context to be appended to the error message</param>
		public static void LogWarning(this IUhtMessageSite MessageSite, int LineNumber, string Message, object? ExtraContext = null)
		{
			LogMessage(UhtMessageType.Warning, MessageSite, LineNumber, Message, ExtraContext);
		}

		/// <summary>
		/// Log a warning
		/// </summary>
		/// <param name="MessageSite">Message site associated with the message</param>
		/// <param name="Message">Text of the warning</param>
		/// <param name="ExtraContext">Addition context to be appended to the error message</param>
		public static void LogWarning(this IUhtMessageSite MessageSite, string Message, object? ExtraContext = null)
		{
			LogMessage(UhtMessageType.Warning, MessageSite, -1, Message, ExtraContext);
		}

		/// <summary>
		/// Log information
		/// </summary>
		/// <param name="MessageSite">Message site associated with the message</param>
		/// <param name="LineNumber">Line number of the information</param>
		/// <param name="Message">Text of the information</param>
		/// <param name="ExtraContext">Addition context to be appended to the error message</param>
		public static void LogInfo(this IUhtMessageSite MessageSite, int LineNumber, string Message, object? ExtraContext = null)
		{
			LogMessage(UhtMessageType.Info, MessageSite, LineNumber, Message, ExtraContext);
		}

		/// <summary>
		/// Log a message directly to the log
		/// </summary>
		/// <param name="MessageSite">Message site associated with the message</param>
		/// <param name="Message">Text of the information</param>
		/// <param name="ExtraContext">Addition context to be appended to the error message</param>
		public static void LogTrace(this IUhtMessageSite MessageSite, string Message, object? ExtraContext = null)
		{
			LogMessage(UhtMessageType.Trace, MessageSite, -1, Message, ExtraContext);
		}

		/// <summary>
		/// Log a message directly to the log
		/// </summary>
		/// <param name="MessageSite">Message site associated with the message</param>
		/// <param name="LineNumber">Line number of the information</param>
		/// <param name="Message">Text of the information</param>
		/// <param name="ExtraContext">Addition context to be appended to the error message</param>
		public static void LogLog(this IUhtMessageSite MessageSite, int LineNumber, string Message, object? ExtraContext = null)
		{
			LogMessage(UhtMessageType.Trace, MessageSite, LineNumber, Message, ExtraContext);
		}

		/// <summary>
		/// Log a information
		/// </summary>
		/// <param name="MessageSite">Message site associated with the message</param>
		/// <param name="Message">Text of the information</param>
		/// <param name="ExtraContext">Addition context to be appended to the error message</param>
		public static void LogInfo(this IUhtMessageSite MessageSite, string Message, object? ExtraContext = null)
		{
			LogMessage(UhtMessageType.Info, MessageSite, -1, Message, ExtraContext);
		}

		/// <summary>
		/// Log a message
		/// </summary>
		/// <param name="MessageType">Type of the message being generated</param>
		/// <param name="MessageSite">Message site associated with the message</param>
		/// <param name="LineNumber">Line number of the information</param>
		/// <param name="Message">Text of the information</param>
		/// <param name="ExtraContext">Addition context to be appended to the error message</param>
		private static void LogMessage(UhtMessageType MessageType, IUhtMessageSite MessageSite, int LineNumber, string Message, object? ExtraContext)
		{
			if (ExtraContext != null)
			{
				Message = $"{Message} in {UhtMessage.FormatContext(ExtraContext)}";
			}
			MessageSite.MessageSession.AddMessage(UhtMessage.MakeMessage(MessageType, MessageSite.MessageSource, null, MessageSite.GetLineNumber(LineNumber), Message));
		}
	}

	/// <summary>
	/// Thread based message context.  Used to improve performance by avoiding allocations.
	/// </summary>
	public class UhtTlsMessageExtraContext : IUhtMessageExtraContext
	{
		private Stack<object?>? ExtraContexts;
		private static ThreadLocal<UhtTlsMessageExtraContext> Tls = new ThreadLocal<UhtTlsMessageExtraContext>(() => new UhtTlsMessageExtraContext());

		#region IUHTMessageExtraContext implementation
		IEnumerable<object?>? IUhtMessageExtraContext.MessageExtraContext => this.ExtraContexts;
		#endregion

		/// <summary>
		/// Add an extra context
		/// </summary>
		/// <param name="ExceptionContext"></param>
		public void PushExtraContext(object? ExceptionContext)
		{
			if (this.ExtraContexts == null)
			{
				this.ExtraContexts = new Stack<object?>(8);
			}
			this.ExtraContexts.Push(ExceptionContext);
		}

		/// <summary>
		/// Pop the top most extra context
		/// </summary>
		public void PopExtraContext()
		{
			if (this.ExtraContexts != null)
			{
				this.ExtraContexts.Pop();
			}
		}

		/// <summary>
		/// Get the extra context associated with this thread
		/// </summary>
		/// <returns>Extra context</returns>
		public static UhtTlsMessageExtraContext? GetTls() { return UhtTlsMessageExtraContext.Tls.Value; }

		/// <summary>
		/// Get the extra context interface
		/// </summary>
		/// <returns>Extra context interface</returns>
		public static IUhtMessageExtraContext? GetMessageExtraContext() { return UhtTlsMessageExtraContext.Tls.Value; }
	}

	/// <summary>
	/// A "using" object to automate the push/pop of extra context to the thread's current extra context
	/// </summary>
	public struct UhtMessageContext : IDisposable
	{
		private readonly UhtTlsMessageExtraContext? Stack;

		/// <summary>
		/// Construct a new entry
		/// </summary>
		/// <param name="MessageSite">Destination message site</param>
		/// <param name="ExtraContext">Extra context to be added</param>
		public UhtMessageContext(IUhtMessageSite MessageSite, object? ExtraContext)
		{
			this.Stack = UhtTlsMessageExtraContext.GetTls();
			if (this.Stack != null)
			{
				this.Stack.PushExtraContext(ExtraContext);
			}
		}

		/// <summary>
		/// Replace the extra context.  This replaces the top level context and thus 
		/// can have unexpected results if done when a more deeper context has been added
		/// </summary>
		/// <param name="ExtraContext">New extra context</param>
		public void Reset(object? ExtraContext)
		{
			if (this.Stack != null)
			{
				this.Stack.PopExtraContext();
				this.Stack.PushExtraContext(ExtraContext);
			}
		}

		/// <summary>
		/// Dispose the object and auto-remove the added context
		/// </summary>
		public void Dispose()
		{
			if (this.Stack != null)
			{
				this.Stack.PopExtraContext();
			}
		}
	}
}
