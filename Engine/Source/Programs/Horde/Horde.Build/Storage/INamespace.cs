// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Storage
{
	using NamespaceId = StringId<INamespace>;

	/// <summary>
	/// Interface for a namespace
	/// </summary>
	public interface INamespace
	{
		/// <summary>
		/// Identifier for the namespace
		/// </summary>
		NamespaceId Id { get; }

		/// <summary>
		/// Access control list for the namespace
		/// </summary>
		Acl? Acl { get; }
	}
}
