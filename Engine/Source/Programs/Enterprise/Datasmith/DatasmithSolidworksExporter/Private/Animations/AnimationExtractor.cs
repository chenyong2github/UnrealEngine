// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Runtime.InteropServices;
using System.IO;
using System.Drawing;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swmotionstudy;
using SolidworksDatasmith.Geometry;

using SolidworksDatasmith.SwObjects;

namespace SolidworksDatasmith.Animations
{
    class AnimationExtractor
    {
        void ExtractAnimations(IModelDoc2 doc, Component2 root)
        {
            var ext = doc.Extension;
            var manager = ext.GetMotionStudyManager() as IMotionStudyManager;
            if (manager != null)
            {
                int num = manager.GetMotionStudyCount();
                if (num > 0)
                {
                    string[] names = manager.GetMotionStudyNames();
                    for (int i = 0; i < num; i++)
                    {
                        var study = manager.GetMotionStudy(names[i]);
                        if (study != null)
                            ExtractAnimation(study, names[i], root);
                    }
                }
            }
        }

        void ExtractAnimation(IMotionStudy study, string name, Component2 root)
        {
            bool success = study.Activate();

	        //if (result)
	        {
                var duration = study.GetDuration();
		        if (duration > 0.0)
		        {
                    SwAnimation anim = new SwAnimation();
                    anim.Name = name;
                    double initialTime = study.GetTime();

                    //swMotionStudyTypeAssembly   1 or 0x1 = Animation; D - cubed solver is used to do presentation animation only; no simulation is performed, so no results or plots are available; gravity, contact, springs, and forces cannot be used; mass and inertia values have no effect on the animation
                    //swMotionStudyTypeCosmosMotion   4 or 0x4 = Motion Analysis; ADAMS(MSC.Software) solver is used to return accurate results; you must load the SOLIDWORKS Motion add -in with a SOLIDWORKS premium license to use this option
                    //swMotionStudyTypeLegacyCosmosMotion 8 or 0x8 = Legacy COSMOSMotion; in SOLIDWORKS 2007 and earlier, motion analysis was provided through the COSMOSMotion add -in; this option is available if either the COSMOSMotion add -in is loaded or you open an older model that was created using that add-in; models with legacy COSMOSMotion data can be opened but not edited
                    //swMotionStudyTypeNewCosmosMotion    16 or 0x10
                    //swMotionStudyTypePhysicalSimulation
                    int supportedTypes;
                    study.GetSupportedStudyTypes(out supportedTypes);
                    var properties = study.GetProperties((int)swMotionStudyType_e.swMotionStudyTypeAssembly);

                    int fps = properties.GetFrameRate();
                    double step = 1.0 / (double)fps;
                    int st = 0;
                    uint allSteps = (uint)(duration / step);

                    var rootTm = root.GetTotalTransform(true);
			        
			        for (double t = 0.0; t<duration; t += step)
			        {
                        study.SetTime(t);
                        ExtractAnimationData(st++, t, root, anim, rootTm);
                    }
                    study.SetTime(duration);
                    ExtractAnimationData(st++, duration, root, anim, rootTm);

                    study.SetTime(initialTime);
                    study.Stop();

                    // make transform matrices local and create actual gltf channels
                    anim.BakeAnimationFromIntermediateChannels(false);
		        }
	        }
        }

        void ExtractAnimationData(int step, double t, Component2 component, SwAnimation anim, MathTransform rootTm)
        {
            if (component == null)
                return;
            if (!component.IsRoot())
            {
                var parent = component.GetParent();
                MathTransform tm = component.GetTotalTransform(true);
                if (tm == null)
                    tm = component.Transform2;
                if (tm != null)
                {
                    MathTransform global;
                    if (parent != null)
                    {
                        global = rootTm.IMultiply(tm);
                        tm = global;
                    }
                }

                if (tm != null)
                {
                    var channel = anim.getIntermediateChannel(component);
                    if (channel == null)
                    {
                        channel = anim.newIntermediateChannel(component);
                    }
                    var key = channel.newKeyframe(step, t);
                    key.GlobalTm = tm;
                }
            }

            if (component.IGetChildrenCount() > 0)
            {
                Component2[] children = component.GetChildren();
                foreach (var child in children)
                {
                    ExtractAnimationData(step, t, child, anim, rootTm);
                }
            }
        }


    }
}
