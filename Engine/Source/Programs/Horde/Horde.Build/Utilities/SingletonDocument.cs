// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Reflection;
using System.Threading.Tasks;
using Horde.Build.Models;
using Horde.Build.Services;
using MongoDB.Bson;

namespace Horde.Build.Utilities
{
	/// <summary>
	/// Attribute specifying the unique id for a singleton document
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public sealed class SingletonDocumentAttribute : Attribute
	{
		/// <summary>
		/// Unique id for the singleton document
		/// </summary>
		public string Id { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="id">Unique id for the singleton document</param>
		public SingletonDocumentAttribute(string id)
		{
			Id = id;
		}
	}

	/// <summary>
	/// Interface for the getting and setting the singleton
	/// </summary>
	/// <typeparam name="T">Type of document</typeparam>
	public interface ISingletonDocument<T>
	{
		/// <summary>
		/// Gets the current document
		/// </summary>
		/// <returns>The current document</returns>
		Task<T> GetAsync();

		/// <summary>
		/// Attempts to update the document
		/// </summary>
		/// <param name="value">New state of the document</param>
		/// <returns>True if the document was updated, false otherwise</returns>
		Task<bool> TryUpdateAsync(T value);
	}

	/// <summary>
	/// Concrete implementation of <see cref="ISingletonDocument{T}"/>
	/// </summary>
	/// <typeparam name="T">The document type</typeparam>
	public class SingletonDocument<T> : ISingletonDocument<T> where T : SingletonBase, new()
	{
		/// <summary>
		/// The database service instance
		/// </summary>
		readonly DatabaseService _databaseService;

		/// <summary>
		/// Unique id for the singleton document
		/// </summary>
		readonly ObjectId _objectId;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="databaseService">The database service instance</param>
		public SingletonDocument(DatabaseService databaseService)
		{
			_databaseService = databaseService;

			SingletonDocumentAttribute? attribute = typeof(T).GetCustomAttribute<SingletonDocumentAttribute>();
			if (attribute == null)
			{
				throw new Exception($"Type {typeof(T).Name} is missing a {nameof(SingletonDocumentAttribute)} annotation");
			}

			_objectId = new ObjectId(attribute.Id);
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="databaseService">The database service instance</param>
		/// <param name="objectId">The singleton document object id</param>
		public SingletonDocument(DatabaseService databaseService, ObjectId objectId)
		{
			_databaseService = databaseService;
			_objectId = objectId;
		}

		/// <inheritdoc/>
		public async Task<T> GetAsync()
		{
			T value = await _databaseService.GetSingletonAsync<T>(_objectId);
			value.Id = _objectId;
			return value;
		}

		/// <inheritdoc/>
		public Task<bool> TryUpdateAsync(T value)
		{
			return _databaseService.TryUpdateSingletonAsync<T>(value);
		}
	}

	/// <summary>
	/// Extension methods for singletons
	/// </summary>
	public static class SingletonDocumentExtensions
	{
		/// <summary>
		/// Update a singleton
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="singleton"></param>
		/// <param name="updateAction"></param>
		/// <returns></returns>
		public static async Task<T> UpdateAsync<T>(this ISingletonDocument<T> singleton, Action<T> updateAction) where T : SingletonBase, new()
		{
			for (; ; )
			{
				T value = await singleton.GetAsync();
				updateAction(value);

				if (await singleton.TryUpdateAsync(value))
				{
					return value;
				}
			}
		}
	}
}
