// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using SolidWorks.Interop.sldworks;
using SolidworksDatasmith.Geometry;

namespace SolidworksDatasmith.SwObjects
{
	public class SwAnimation
	{
		public string Name { get; set; }

		public enum PathType
		{
			PATH_TRANSLATION,
			PATH_ROTATION,
			PATH_SCALE
		}

		private Dictionary<string, SwAnimationIntermediateChannel> node2IntermediateChannel = new Dictionary<string, SwAnimationIntermediateChannel>();
		public List<SwAnimationChannel> Channels = new List<SwAnimationChannel>();
		public List<SwAnimationIntermediateChannel> IntermediateChannels = new List<SwAnimationIntermediateChannel>();
		public int FPS {  get; set; } = 0;

		public SwAnimationIntermediateChannel NewIntermediateChannel(Component2 target)
		{
			SwAnimationIntermediateChannel channel = new SwAnimationIntermediateChannel();
			channel.Target = target;
			IntermediateChannels.Add(channel);
			node2IntermediateChannel.Add(target.Name2, channel);
			return channel;
		}

		public SwAnimationIntermediateChannel getIntermediateChannel(Component2 target)
		{
			SwAnimationIntermediateChannel channel = null;
			if (!node2IntermediateChannel.TryGetValue(target.Name2, out channel))
				return null;
			return channel;
		}

		public IMathTransform GetIntermediateTransform(Component2 target, int step, MathTransform rootTm)
		{
			MathTransform xform = null;

			var channel = getIntermediateChannel(target);

			if (channel != null && channel.keyframes.Count > 0)
			{
				MathTransform xprevform;
				foreach (var key in channel.keyframes)
				{
					xprevform = xform;
					xform = key.GlobalTm;
					if (key.Step == step)
						break;
					if (key.Step > step)
					{
						xform = xprevform;
						break;
					}
				}
			}

			if (xform == null)
			{
				xform = target.GetTotalTransform(true);
				if (xform == null)
					xform = target.Transform2;
				if (xform != null)
				{
					MathTransform temp = rootTm.IMultiply(xform);
					xform = temp;
				}
			}

			return xform;
		}

		public FDatasmithFacadeLevelSequence Export()
		{
			FDatasmithFacadeLevelSequence LevelSeq = new FDatasmithFacadeLevelSequence(Name);

			LevelSeq.SetFrameRate(FPS);

			foreach (var NodePair in node2IntermediateChannel)
			{
				SwAnimationIntermediateChannel Chan = NodePair.Value;
				Component2 Component = Chan.Target;
				FDatasmithFacadeTransformAnimation Anim = new FDatasmithFacadeTransformAnimation(Component.Name2);

				foreach (var Keyframe in Chan.keyframes)
				{
					Matrix4 LocalMatrix = Keyframe.LocalMatrix;

					// Get euler angles in degrees
					float X = Utility.Rad2Deg * (float)Math.Atan2(LocalMatrix[6], LocalMatrix[10]);
					float Y = Utility.Rad2Deg * (float)Math.Atan2(-LocalMatrix[2], Math.Sqrt(LocalMatrix[6] * LocalMatrix[6] + LocalMatrix[10] * LocalMatrix[10]));
					float Z = Utility.Rad2Deg * (float)Math.Atan2(LocalMatrix[1], LocalMatrix[0]);

					float Scale = LocalMatrix[15];

					Vec3 Translation = new Vec3(LocalMatrix[12], LocalMatrix[13], LocalMatrix[14]);

					Anim.AddFrame(EDatasmithFacadeAnimationTransformType.Rotation, Keyframe.Step, X, -Y, -Z);
					Anim.AddFrame(EDatasmithFacadeAnimationTransformType.Scale, Keyframe.Step, Scale, Scale, Scale);
					Anim.AddFrame(EDatasmithFacadeAnimationTransformType.Translation, Keyframe.Step, Translation.x, -Translation.y, Translation.z);
				}

				LevelSeq.AddAnimation(Anim);
			}

			return LevelSeq;
		}

		// inject keyframes to pad
		//
		public void PadIntermediateFrames()
		{
			List<SwAnimationIntermediateKeyframe> allFrames = new List<SwAnimationIntermediateKeyframe>();
			foreach (var channel in IntermediateChannels)
			{
				if (channel.keyframes.Count > 0)
					allFrames.Add(channel.keyframes[0]);
			}

			allFrames.Sort((item1, item2) => {
				int res = 0;
				if (item1.Step < item2.Step) res = -1;
				else if (item1.Step > item2.Step) res = 1;
				return res;
			} );

			for (int i = 0; i < allFrames.Count; i++)
			{
				var k = allFrames[i];
				if (k.Step > 1)
				{
					SwAnimationIntermediateKeyframe prec = new SwAnimationIntermediateKeyframe();
					prec.IsPadding = true;
					if (i > 0)
						prec.Step = allFrames[i - 1].Step;
					else
					{
						prec.Step = 0;
						var xform = k.Owner.Target.GetTotalTransform(true);
						if (xform == null)
							xform = k.Owner.Target.Transform2;
						prec.GlobalTm = xform;
						prec.Owner = k.Owner;
						k.Owner.keyframes.Insert(0, prec);
					}
				}
			}

			foreach (var channel in IntermediateChannels)
			{
				if (channel.keyframes.Count > 0)
					if (channel.keyframes[0].Step != 0)
					{
						SwAnimationIntermediateKeyframe prec = new SwAnimationIntermediateKeyframe();
						prec.IsPadding = true;
						prec.Step = 0;
						var xform = channel.Target.GetTotalTransform(true);
						if (xform == null)
							xform = channel.Target.Transform2;
						prec.GlobalTm = xform;
						prec.Owner = channel;
						channel.keyframes.Insert(0, prec);
					}
			}
		}

		public void BakeAnimationFromIntermediateChannels(bool cumulativeKeys = true)
		{

		}
	}
}
