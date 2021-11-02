// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using SolidWorks.Interop.sldworks;

namespace DatasmithSolidworks
{
	public class FAnimation
	{
		public class FKeyframe
		{
			public int Step;
			public double Time;
			public MathTransform GlobalTm;
			public MathTransform RelativeTm;
			public FChannel Owner;
			public bool IsPadding;
			public FMatrix4 LocalMatrix;

			public FKeyframe()
			{
				IsPadding = false;
				Owner = null;
				Step = -1;
				Time = -1.0;
			}
		};

		public class FChannel
		{
			private Dictionary<int, FKeyframe> Id2Key = new Dictionary<int, FKeyframe>();
			public List<FKeyframe> Keyframes = new List<FKeyframe>();
			public Component2 Target;
			public int Interpolation;

			public FKeyframe NewKeyframe(int InStep, MathTransform InLocalTransform, double InTime = -1.0)
			{
				FKeyframe Key = new FKeyframe();
				Key.Step = InStep;
				Key.Time = InTime;
				Key.Owner = this;
				Key.LocalMatrix = new FMatrix4(MathUtils.ConvertFromSolidworksTransform(InLocalTransform, 100f /*GeomScale*/));

				Keyframes.Add(Key);
				Id2Key.Add(Key.Step, Key);

				return Key;
			}

			public FKeyframe GetKeyframe(int InStep)
			{
				FKeyframe Res = null;
				{
					if (!Id2Key.TryGetValue(InStep, out Res))
						return null;
				}
				return Res;
			}
		}

		public string Name { get; set; }

		public enum PathType
		{
			PATH_TRANSLATION,
			PATH_ROTATION,
			PATH_SCALE
		}

		public Dictionary<string, FChannel> ComponentToChannelMap = new Dictionary<string, FChannel>();
		public List<FChannel> Channels = new List<FChannel>();
		public int FPS { get; set; } = 0;

		public FChannel NewChannel(Component2 InTarget)
		{
			FChannel Channel = new FChannel();
			Channel.Target = InTarget;
			Channels.Add(Channel);
			ComponentToChannelMap.Add(InTarget.Name2, Channel);
			return Channel;
		}

		public FChannel GetChannel(Component2 InTarget)
		{
			FChannel Channel = null;
			if (!ComponentToChannelMap.TryGetValue(InTarget.Name2, out Channel))
			{
				return null;
			}
			return Channel;
		}

		public IMathTransform GetIntermediateTransform(Component2 InTarget, int InStep, MathTransform InRootTm)
		{
			MathTransform XForm = null;

			FChannel Channel = GetChannel(InTarget);

			if (Channel != null && Channel.Keyframes.Count > 0)
			{
				MathTransform PrevXForm;

				foreach (FKeyframe Key in Channel.Keyframes)
				{
					PrevXForm = XForm;
					XForm = Key.GlobalTm;
					if (Key.Step == InStep)
					{
						break;
					}
					if (Key.Step > InStep)
					{
						XForm = PrevXForm;
						break;
					}
				}
			}

			if (XForm == null)
			{
				XForm = InTarget.GetTotalTransform(true);
				if (XForm == null)
				{
					XForm = InTarget.Transform2;
				}
				if (XForm != null)
				{
					MathTransform temp = InRootTm.IMultiply(XForm);
					XForm = temp;
				}
			}

			return XForm;
		}

#if false // level sequences are not implemented for 4.27
		public FDatasmithFacadeLevelSequence Export()
		{
			FDatasmithFacadeLevelSequence LevelSeq = new FDatasmithFacadeLevelSequence(Name);

			LevelSeq.SetFrameRate(FPS);

			foreach (var NodePair in ComponentToChannelMap)
			{
				FChannel Chan = NodePair.Value;
				Component2 Component = Chan.Target;
				FDatasmithFacadeTransformAnimation Anim = new FDatasmithFacadeTransformAnimation(Component.Name2);

				foreach (FKeyframe Keyframe in Chan.Keyframes)
				{
					FMatrix4 LocalMatrix = Keyframe.LocalMatrix;

					// Get euler angles in degrees
					float X = MathUtils.Rad2Deg * (float)Math.Atan2(LocalMatrix[6], LocalMatrix[10]);
					float Y = MathUtils.Rad2Deg * (float)Math.Atan2(-LocalMatrix[2], Math.Sqrt(LocalMatrix[6] * LocalMatrix[6] + LocalMatrix[10] * LocalMatrix[10]));
					float Z = MathUtils.Rad2Deg * (float)Math.Atan2(LocalMatrix[1], LocalMatrix[0]);

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
#endif
	}
}
