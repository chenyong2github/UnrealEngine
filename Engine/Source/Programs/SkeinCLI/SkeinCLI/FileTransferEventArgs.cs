// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace SkeinCLI
{
	public class TransferStartedEventArgs : EventArgs
	{
		public TransferStartedEventArgs(string source, string destination, long size)
		{
			Source = source;
			Destination = destination;
			Size = size;
		}

		public string Source { get; }
		public string Destination { get; }
		public long Size { get; }
	}

	public class TransferCompletedEventArgs : EventArgs
	{
		public TransferCompletedEventArgs(string source, string destination, bool result)
		{
			Source = source;
			Destination = destination;
			Result = result;
		}

		public string Source { get; }
		public string Destination { get; }
		public bool Result { get; }
	}
}