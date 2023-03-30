// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute.Buffers;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Type of a compute message
	/// </summary>
	public enum ComputeMessageType
	{
		/// <summary>
		/// No message was received (end of stream)
		/// </summary>
		None = 0x00,

		/// <summary>
		/// Indicates that the remote is ready to receive messages on the current channel
		/// </summary>
		Ready = 0x01,

		/// <summary>
		/// Sent in place of a regular response if an error occurs on the remote
		/// </summary>
		Exception = 0x02,

		/// <summary>
		/// Fork a new request channel
		/// </summary>
		Fork = 0x03,

		/// <summary>
		/// Untyped user data
		/// </summary>
		UserData = 0x04,

		#region Process Management

		/// <summary>
		/// Extract files on the remote machine (Initiator -> Remote)
		/// </summary>
		WriteFiles = 0x10,

		/// <summary>
		/// Notification that files have been extracted (Remote -> Initiator)
		/// </summary>
		WriteFilesResponse = 0x11,

		/// <summary>
		/// Deletes files on the remote machine (Initiator -> Remote)
		/// </summary>
		DeleteFiles = 0x12,

		/// <summary>
		/// Execute a process in a sandbox (Initiator -> Remote)
		/// </summary>
		Execute = 0x16,

		/// <summary>
		/// Returns output from the child process to the caller (Remote -> Initiator)
		/// </summary>
		ExecuteOutput = 0x17,

		/// <summary>
		/// Returns the process exit code (Remote -> Initiator)
		/// </summary>
		ExecuteResult = 0x18,

		#endregion

		#region Storage

		/// <summary>
		/// Reads a blob from storage
		/// </summary>
		ReadBlob = 0x20,

		/// <summary>
		/// Response to a <see cref="ReadBlob"/> request.
		/// </summary>
		ReadBlobResponse = 0x21,

		#endregion

		#region Test Requests

		/// <summary>
		/// Xor a block of data with a value
		/// </summary>
		XorRequest = 0xf0,

		/// <summary>
		/// Result from an <see cref="XorRequest"/> request.
		/// </summary>
		XorResponse = 0xf1,

		#endregion
	}

	/// <summary>
	/// Information about a received compute message
	/// </summary>
	public interface IComputeMessage : IMemoryReader, IDisposable
	{
		/// <summary>
		/// Type of the message
		/// </summary>
		public ComputeMessageType Type { get; }

		/// <summary>
		/// Data that was read
		/// </summary>
		public ReadOnlyMemory<byte> Data { get; }
	}

	/// <summary>
	/// Message for reporting an error
	/// </summary>
	public record struct ExceptionMessage(string Message, string Description);

	/// <summary>
	/// Message to request forking the message loop
	/// </summary>
	public record struct ForkMessage(int ChannelId);

	/// <summary>
	/// Extract files from a bundle to a path in the remote sandbox
	/// </summary>
	/// <param name="Name">Path to extract the files to</param>
	/// <param name="Locator">Locator for the tree to extract</param>
	public record struct UploadFilesMessage(string Name, NodeLocator Locator);

	/// <summary>
	/// Deletes files or directories in the remote
	/// </summary>
	/// <param name="Filter">Filter for files to delete</param>
	public record struct DeleteFilesMessage(IReadOnlyList<string> Filter);

	/// <summary>
	/// Message to execute a new child process
	/// </summary>
	/// <param name="Executable">Executable path</param>
	/// <param name="Arguments">Arguments for the executable</param>
	/// <param name="WorkingDir">Working directory to execute in</param>
	/// <param name="EnvVars">Environment variables for the child process. Null values unset variables.</param>
	public record struct ExecuteProcessMessage(string Executable, IReadOnlyList<string> Arguments, string? WorkingDir, IReadOnlyDictionary<string, string?> EnvVars);

	/// <summary>
	/// Response from executing a child process
	/// </summary>
	/// <param name="ExitCode">Exit code for the process</param>
	public record struct ExecuteProcessResponseMessage(int ExitCode);

	/// <summary>
	/// Creates a blob read request
	/// </summary>
	public record struct ReadBlobMessage(BlobLocator Locator, int Offset, int Length);

	/// <summary>
	/// Message for running an XOR command
	/// </summary>
	/// <param name="Data">Data to xor</param>
	/// <param name="Value">Value to XOR with</param>
	public record struct XorRequestMessage(ReadOnlyMemory<byte> Data, byte Value);

	/// <summary>
	/// Wraps various requests across compute channels
	/// </summary>
	public static class ComputeMessageExtensions
	{
		/// <summary>
		/// Sends a message to the remote indicating that we're ready to receive messages on the given channel
		/// </summary>
		public static async ValueTask SendReadyAsync(this IComputeMessageChannel channel, CancellationToken cancellationToken)
		{
			using IComputeMessageBuilder message = await channel.CreateMessageAsync(ComputeMessageType.Ready, cancellationToken);
			message.Send();
		}

		/// <summary>
		/// Sends an exception response to the remote
		/// </summary>
		public static ValueTask SendExceptionAsync(this IComputeMessageChannel channel, Exception ex, CancellationToken cancellationToken) => SendExceptionAsync(channel, ex.Message, ex.ToString(), cancellationToken);

		/// <summary>
		/// Sends an exception response to the remote
		/// </summary>
		public static async ValueTask SendExceptionAsync(this IComputeMessageChannel channel, string description, string trace, CancellationToken cancellationToken)
		{
			using IComputeMessageBuilder message = await channel.CreateMessageAsync(ComputeMessageType.Exception, cancellationToken);
			message.WriteString(description);
			message.WriteString(trace);
			message.Send();
		}

		/// <summary>
		/// Parses a message as an <see cref="ExceptionMessage"/>
		/// </summary>
		public static ExceptionMessage ParseExceptionMessage(this IComputeMessage message)
		{
			string msg = message.ReadString();
			string description = message.ReadString();
			return new ExceptionMessage(msg, description);
		}
/*
		/// <inheritdoc/>
		public static void Fork(this IComputeMessageChannel channel, int newChannelId)
		{
			using (IComputeMessageBuilder builder = channel.CreateMessage(ComputeMessageType.Fork))
			{
				builder.WriteInt32(newChannelId);
				builder.Send();
			}
		}
*/
		/// <summary>
		/// Parses a fork message from the given compute message
		/// </summary>
		public static ForkMessage ParseForkMessage(this IComputeMessage message)
		{
			int channelId = message.ReadInt32();
			return new ForkMessage(channelId);
		}

		/// <summary>
		/// Sends untyped user data to the remote
		/// </summary>
		public static async ValueTask SendUserDataAsync(this IComputeMessageChannel channel, ReadOnlyMemory<byte> memory, CancellationToken cancellationToken)
		{
			using IComputeMessageBuilder message = await channel.CreateMessageAsync(ComputeMessageType.UserData, memory.Length, cancellationToken);
			message.WriteFixedLengthBytes(memory.Span);
			message.Send();
		}

		#region Process

		static async Task<IComputeMessage> RunStorageServer(this IComputeMessageChannel channel, IStorageClient storage, CancellationToken cancellationToken)
		{
			for (; ; )
			{
				IComputeMessage message = await channel.ReceiveAsync(cancellationToken);
				if (message.Type != ComputeMessageType.ReadBlob)
				{
					return message;
				}

				try
				{
					ReadBlobMessage readBlob = message.ParseReadBlobRequest();
					await SendBlobDataAsync(channel, readBlob, storage, cancellationToken);
				}
				finally
				{
					message.Dispose();
				}
			}
		}

		/// <summary>
		/// Creates a sandbox on the remote machine
		/// </summary>
		/// <param name="channel">Current channel</param>
		/// <param name="path">Root directory to extract files within the sandbox</param>
		/// <param name="locator">Location of a <see cref="DirectoryNode"/> describing contents of the sandbox</param>
		/// <param name="storage">Storage for the sandbox data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task UploadFilesAsync(this IComputeMessageChannel channel, string path, NodeLocator locator, IStorageClient storage, CancellationToken cancellationToken = default)
		{
			using (IComputeMessageBuilder request = await channel.CreateMessageAsync(ComputeMessageType.WriteFiles, cancellationToken))
			{
				request.WriteString(path);
				request.WriteNodeLocator(locator);
				request.Send();
			}

			using IComputeMessage response = await RunStorageServer(channel, storage, cancellationToken);
			if (response.Type != ComputeMessageType.WriteFilesResponse)
			{
				throw new NotSupportedException();
			}
		}

		/// <summary>
		/// Parses a message as a <see cref="UploadFilesMessage"/>
		/// </summary>
		public static UploadFilesMessage ParseUploadFilesMessage(this IComputeMessage message)
		{
			string name = message.ReadString();
			NodeLocator locator = message.ReadNodeLocator();
			return new UploadFilesMessage(name, locator);
		}

		/// <summary>
		/// Destroys a sandbox on the remote machine
		/// </summary>
		/// <param name="channel">Current channel</param>
		/// <param name="paths">Paths of files or directories to delete</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async ValueTask DeleteFilesAsync(this IComputeMessageChannel channel, IReadOnlyList<string> paths, CancellationToken cancellationToken)
		{
			IComputeMessageBuilder request = await channel.CreateMessageAsync(ComputeMessageType.DeleteFiles, cancellationToken);
			request.WriteList(paths, MemoryWriterExtensions.WriteString);
			request.Send();
		}

		/// <summary>
		/// Parses a message as a <see cref="DeleteFilesMessage"/>
		/// </summary>
		public static DeleteFilesMessage ParseDeleteFilesMessage(this IComputeMessage message)
		{
			List<string> files = message.ReadList(MemoryReaderExtensions.ReadString);
			return new DeleteFilesMessage(files);
		}

		/// <summary>
		/// Executes a remote process
		/// </summary>
		/// <param name="channel">Current channel</param>
		/// <param name="executable">Executable to run, relative to the sandbox root</param>
		/// <param name="arguments">Arguments for the child process</param>
		/// <param name="workingDir">Working directory for the process</param>
		/// <param name="envVars">Environment variables for the child process</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task<IComputeProcess> ExecuteAsync(this IComputeMessageChannel channel, string executable, IReadOnlyList<string> arguments, string? workingDir, IReadOnlyDictionary<string, string?>? envVars, CancellationToken cancellationToken = default)
		{
			using (IComputeMessageBuilder request = await channel.CreateMessageAsync(ComputeMessageType.Execute, cancellationToken))
			{
				request.WriteString(executable);
				request.WriteList(arguments, MemoryWriterExtensions.WriteString);
				request.WriteOptionalString(workingDir);
				request.WriteDictionary(envVars ?? new Dictionary<string, string?>(), MemoryWriterExtensions.WriteString, MemoryWriterExtensions.WriteOptionalString);
				request.Send();
			}
			return new ComputeProcess(channel);
		}

		/// <summary>
		/// Parses a message as a <see cref="ExecuteProcessMessage"/>
		/// </summary>
		public static ExecuteProcessMessage ParseExecuteProcessMessage(this IComputeMessage message)
		{
			string executable = message.ReadString();
			List<string> arguments = message.ReadList(MemoryReaderExtensions.ReadString);
			string? workingDir = message.ReadOptionalString();
			Dictionary<string, string?> envVars = message.ReadDictionary(MemoryReaderExtensions.ReadString, MemoryReaderExtensions.ReadOptionalString);
			return new ExecuteProcessMessage(executable, arguments, workingDir, envVars);
		}

		/// <summary>
		/// Sends output from a child process
		/// </summary>
		public static async ValueTask SendExecuteOutputAsync(this IComputeMessageChannel channel, ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
		{
			using IComputeMessageBuilder message = await channel.CreateMessageAsync(ComputeMessageType.ExecuteOutput, cancellationToken);
			message.WriteFixedLengthBytes(data.Span);
			message.Send();
		}

		/// <summary>
		/// Sends a response from executing a child process
		/// </summary>
		/// <param name="channel"></param>
		/// <param name="exitCode">Exit code from the process</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async ValueTask SendExecuteResultAsync(this IComputeMessageChannel channel, int exitCode, CancellationToken cancellationToken = default)
		{
			using IComputeMessageBuilder builder = await channel.CreateMessageAsync(ComputeMessageType.ExecuteResult, cancellationToken);
			builder.WriteInt32(exitCode);
			builder.Send();
		}

		/// <summary>
		/// Parses a message as a <see cref="ExecuteProcessMessage"/>
		/// </summary>
		public static ExecuteProcessResponseMessage ParseExecuteProcessResponse(this IComputeMessage message)
		{
			int exitCode = message.ReadInt32();
			return new ExecuteProcessResponseMessage(exitCode);
		}

		#endregion

		#region Storage

		/// <summary>
		/// 
		/// </summary>
		/// <param name="message"></param>
		/// <returns></returns>
		public static ReadBlobMessage ParseReadBlobRequest(this IComputeMessage message)
		{
			BlobLocator locator = message.ReadBlobLocator();
			int offset = (int)message.ReadUnsignedVarInt();
			int length = (int)message.ReadUnsignedVarInt();
			return new ReadBlobMessage(locator, offset, length);
		}

		/// <summary>
		/// Wraps a compute message containing blob data
		/// </summary>
		sealed class BlobDataStream : ReadOnlyMemoryStream
		{
			readonly IComputeMessage _message;

			public BlobDataStream(IComputeMessage message)
				: base(message.Data)
			{
				_message = message;
			}

			protected override void Dispose(bool disposing)
			{
				base.Dispose(disposing);

				if (disposing)
				{
					_message.Dispose();
				}
			}
		}

		/// <summary>
		/// Reads a blob from the remote
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="locator">Locator for the blob</param>
		/// <param name="offset">Offset within the blob</param>
		/// <param name="length">Length of data to return</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Stream containing the blob data</returns>
		public static async Task<Stream> ReadBlobAsync(this IComputeMessageChannel channel, BlobLocator locator, int offset, int length, CancellationToken cancellationToken = default)
		{
			using (IComputeMessageBuilder request = await channel.CreateMessageAsync(ComputeMessageType.ReadBlob, cancellationToken))
			{
				request.WriteBlobLocator(locator);
				request.WriteUnsignedVarInt(offset);
				request.WriteUnsignedVarInt(length);
				request.Send();
			}

			IComputeMessage response = await channel.ReceiveAsync(cancellationToken);
			if (response.Type != ComputeMessageType.ReadBlobResponse)
			{
				response.Dispose();
				throw new ComputeInvalidMessageException(response);
			}

			return new BlobDataStream(response);
		}

		/// <summary>
		/// Writes blob data to a compute channel
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="message">The read request</param>
		/// <param name="storage">Storage client to retrieve the blob from</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static Task SendBlobDataAsync(this IComputeMessageChannel channel, ReadBlobMessage message, IStorageClient storage, CancellationToken cancellationToken = default)
		{
			return SendBlobDataAsync(channel, message.Locator, message.Offset, message.Length, storage, cancellationToken);
		}

		/// <summary>
		/// Writes blob data to a compute channel
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="locator">Locator for the blob to send</param>
		/// <param name="offset">Starting offset of the data</param>
		/// <param name="length">Length of the data</param>
		/// <param name="storage">Storage client to retrieve the blob from</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task SendBlobDataAsync(this IComputeMessageChannel channel, BlobLocator locator, int offset, int length, IStorageClient storage, CancellationToken cancellationToken = default)
		{
			byte[] data;
			if (offset == 0 && length == 0)
			{
				using (Stream stream = await storage.ReadBlobAsync(locator, cancellationToken))
				{
					using MemoryStream target = new MemoryStream();
					await stream.CopyToAsync(target, cancellationToken);
					data = target.ToArray();
				}
			}
			else
			{
				using (Stream stream = await storage.ReadBlobRangeAsync(locator, offset, length, cancellationToken))
				{
					using MemoryStream target = new MemoryStream();
					await stream.CopyToAsync(target, cancellationToken);
					data = target.ToArray();
				}
			}

			using (IComputeMessageBuilder response = await channel.CreateMessageAsync(ComputeMessageType.ReadBlobResponse, data.Length + 128, cancellationToken))
			{
				response.WriteFixedLengthBytes(data);
				response.Send();
			}
		}

		#endregion

		#region Test Messages

		/// <summary>
		/// Send a message to request that a byte string be xor'ed with a particular value
		/// </summary>
		public static async ValueTask SendXorRequestAsync(this IComputeMessageChannel channel, ReadOnlyMemory<byte> data, byte value, CancellationToken cancellationToken = default)
		{
			using IComputeMessageBuilder message = await channel.CreateMessageAsync(ComputeMessageType.XorRequest, cancellationToken);
			message.WriteFixedLengthBytes(data.Span);
			message.WriteUInt8(value);
			message.Send();
		}

		/// <summary>
		/// Parse a message as an XOR request
		/// </summary>
		public static XorRequestMessage AsXorRequest(this IComputeMessage message)
		{
			ReadOnlyMemory<byte> data = message.Data;
			return new XorRequestMessage(data[0..^1], data.Span[^1]);
		}

		#endregion
	}
}
