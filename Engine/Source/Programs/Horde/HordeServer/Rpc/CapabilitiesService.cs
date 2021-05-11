// Copyright Epic Games, Inc. All Rights Reserved.

using Build.Bazel.Remote.Execution.V2;
using Build.Bazel.Semver;
using Grpc.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Rpc
{
	/// <inheritdoc cref="Capabilities"/>
	class CapabilitiesService : Capabilities.CapabilitiesBase
	{
		/// <inheritdoc/>
		public override Task<ServerCapabilities> GetCapabilities(GetCapabilitiesRequest Request, ServerCallContext Context)
		{
			ServerCapabilities Capabilities = new ServerCapabilities();

			Capabilities.DeprecatedApiVersion = new SemVer { Major = 2 };
			Capabilities.LowApiVersion = new SemVer { Major = 2 };
			Capabilities.HighApiVersion = new SemVer { Major = 2 };

			Capabilities.CacheCapabilities = new CacheCapabilities();
			Capabilities.CacheCapabilities.DigestFunction.Add(DigestFunction.Types.Value.Sha256);
			Capabilities.CacheCapabilities.DigestFunction.Add(DigestFunction.Types.Value.Epiciohash);

			Capabilities.ExecutionCapabilities = new ExecutionCapabilities();
			Capabilities.ExecutionCapabilities.DigestFunction = DigestFunction.Types.Value.Sha256;
			Capabilities.ExecutionCapabilities.DigestFunction = DigestFunction.Types.Value.Epiciohash;
			Capabilities.ExecutionCapabilities.ExecEnabled = true;

			return Task.FromResult(Capabilities);
		}
	}
}
