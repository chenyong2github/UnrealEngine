// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Runtime.InteropServices;
using System.IO;
using System.Drawing;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Globalization;
using SolidWorks.Interop.sldworks;
using SolidworksDatasmith.Geometry;
using System.Security.Cryptography;

namespace SolidworksDatasmith.SwObjects
{
	[ComVisible(false)]
	public class SwMaterial
	{
		private IRenderMaterial _source = null;
		public IRenderMaterial Source { get { return _source; } }

		private IAppearanceSetting _appearance = null;
		public IAppearanceSetting Appearance { get { return _appearance; } }
		
		private static List<Tuple<string, string>> Special_finishes = new List<Tuple<string, string>>();
		private static List<Tuple<string, string>> Special_diffuses = new List<Tuple<string, string>>();

		public int ID = -1;

		public enum MaterialType
		{
			TYPE_UNKNOWN,
			TYPE_FABRIC,
			TYPE_GLASS,
			TYPE_LIGHTS,
			TYPE_METAL,
			TYPE_MISC,
			TYPE_ORGANIC,
			TYPE_PAINTED,
			TYPE_PLASTIC,
			TYPE_RUBBER,
			TYPE_STONE,
			TYPE_METALLICPAINT,
			TYPE_LIGHTWEIGHT
		}

		public enum MappingType
		{
			TYPE_UNSPECIFIED = -1,
			TYPE_SURFACE = 0,
			TYPE_PROJECTION = 1,
			TYPE_SPHERICAL = 2,
			TYPE_CYLINDRICAL = 3,
			TYPE_AUTOMATIC = 4
		}

		private static Dictionary<string, MaterialType> MaterialTypes = null;
		private static void InitializeMaterialTypes()
		{
			if (MaterialTypes == null)
			{
				MaterialTypes = new Dictionary<string, MaterialType>();

				MaterialTypes.Add("carpetcolor1", MaterialType.TYPE_FABRIC);
				MaterialTypes.Add("carpetcolor2", MaterialType.TYPE_FABRIC);
				MaterialTypes.Add("carpetcolor3", MaterialType.TYPE_FABRIC);
				MaterialTypes.Add("carpetcolor4", MaterialType.TYPE_FABRIC);
				MaterialTypes.Add("carpetcolor5", MaterialType.TYPE_FABRIC);
				MaterialTypes.Add("clothburgundycotton", MaterialType.TYPE_FABRIC);
				MaterialTypes.Add("clothburlap", MaterialType.TYPE_FABRIC);
				MaterialTypes.Add("clothcanvas", MaterialType.TYPE_FABRIC);
				MaterialTypes.Add("clothbeigecotton", MaterialType.TYPE_FABRIC);
				MaterialTypes.Add("clothbluecotton", MaterialType.TYPE_FABRIC);
				MaterialTypes.Add("cotton white 2d", MaterialType.TYPE_FABRIC);
				MaterialTypes.Add("clothgreycotton", MaterialType.TYPE_FABRIC);
				MaterialTypes.Add("blueglass", MaterialType.TYPE_GLASS);
				MaterialTypes.Add("brownglass", MaterialType.TYPE_GLASS);
				MaterialTypes.Add("clearglass", MaterialType.TYPE_GLASS);
				MaterialTypes.Add("clearglasspv", MaterialType.TYPE_GLASS);
				MaterialTypes.Add("greenglass", MaterialType.TYPE_GLASS);
				MaterialTypes.Add("mirror", MaterialType.TYPE_GLASS);
				MaterialTypes.Add("reflectiveblueglass", MaterialType.TYPE_GLASS);
				MaterialTypes.Add("reflectiveclearglass", MaterialType.TYPE_GLASS);
				MaterialTypes.Add("reflectivegreenglass", MaterialType.TYPE_GLASS);
				MaterialTypes.Add("frostedglass", MaterialType.TYPE_GLASS);
				MaterialTypes.Add("glassfibre", MaterialType.TYPE_GLASS);
				MaterialTypes.Add("sandblastedglass", MaterialType.TYPE_GLASS);
				MaterialTypes.Add("area_light", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("blue_backlit_lcd", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("green_backlit_lcd", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("amber_led", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("blue_led", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("green_led", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("red_led", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("white_led", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("yellow_led", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("blue_neon_tube", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("green_neon_tube", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("red_neon_tube", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("white_neon_tube", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("yellow_neon_tube", MaterialType.TYPE_LIGHTS);
				MaterialTypes.Add("aluminumtreadplate", MaterialType.TYPE_METAL);
				MaterialTypes.Add("blueanodizedaluminum", MaterialType.TYPE_METAL);
				MaterialTypes.Add("brushedaluminum", MaterialType.TYPE_METAL);
				MaterialTypes.Add("burnishedaluminum", MaterialType.TYPE_METAL);
				MaterialTypes.Add("castaluminum", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattealuminum", MaterialType.TYPE_METAL);
				MaterialTypes.Add("polishedaluminum", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastedaluminum", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishaluminum", MaterialType.TYPE_METAL);
				MaterialTypes.Add("brushedbrass", MaterialType.TYPE_METAL);
				MaterialTypes.Add("burnishedbrass", MaterialType.TYPE_METAL);
				MaterialTypes.Add("castbrass", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattebrass", MaterialType.TYPE_METAL);
				MaterialTypes.Add("polishedbrass", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastedbrass", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishbrass", MaterialType.TYPE_METAL);
				MaterialTypes.Add("brushedbronze", MaterialType.TYPE_METAL);
				MaterialTypes.Add("burnishedbronze", MaterialType.TYPE_METAL);
				MaterialTypes.Add("castbronze", MaterialType.TYPE_METAL);
				MaterialTypes.Add("manganesebronze", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattebronze", MaterialType.TYPE_METAL);
				MaterialTypes.Add("polishedbronze", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastedbronze", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishbronze", MaterialType.TYPE_METAL);
				MaterialTypes.Add("brushedchromium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("burnishedchrome", MaterialType.TYPE_METAL);
				MaterialTypes.Add("chromiumplatecast", MaterialType.TYPE_METAL);
				MaterialTypes.Add("chromiumplate", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattechrome", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastedchrome", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishchrome", MaterialType.TYPE_METAL);
				MaterialTypes.Add("brushedcopper", MaterialType.TYPE_METAL);
				MaterialTypes.Add("burnishedcopper", MaterialType.TYPE_METAL);
				MaterialTypes.Add("castcopper", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattecopper", MaterialType.TYPE_METAL);
				MaterialTypes.Add("polishedcopper", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastedcopper", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishcopper", MaterialType.TYPE_METAL);
				MaterialTypes.Add("wroughtcopper", MaterialType.TYPE_METAL);
				MaterialTypes.Add("brushedgalvanized", MaterialType.TYPE_METAL);
				MaterialTypes.Add("plaingalvanized", MaterialType.TYPE_METAL);
				MaterialTypes.Add("shinygalvanized", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattegold", MaterialType.TYPE_METAL);
				MaterialTypes.Add("polishedgold", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishgold", MaterialType.TYPE_METAL);
				MaterialTypes.Add("castiron", MaterialType.TYPE_METAL);
				MaterialTypes.Add("matteiron", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastediron", MaterialType.TYPE_METAL);
				MaterialTypes.Add("wroughtiron", MaterialType.TYPE_METAL);
				MaterialTypes.Add("burnishedlead", MaterialType.TYPE_METAL);
				MaterialTypes.Add("castlead", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattelead", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastedlead", MaterialType.TYPE_METAL);
				MaterialTypes.Add("brushedmagnesium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("burnishedmagnesium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("castmagnesium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattemagnesium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastedmagnesium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satin finish magnesium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("brushednickel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("burnishednickel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("castnickel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattenickel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("polishednickel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastednickel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishnickel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("matteplatinum", MaterialType.TYPE_METAL);
				MaterialTypes.Add("polishedplatinum", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishplatinum", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattesilver", MaterialType.TYPE_METAL);
				MaterialTypes.Add("polishedsilver", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishsilver", MaterialType.TYPE_METAL);
				MaterialTypes.Add("brushedsteel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("burnishedsteel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("polishedcarbonsteel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("castcarbonsteel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("caststainlesssteel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("chainlinksteel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("machinedsteel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattesteel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("polishedsteel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastedsteel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishstainlesssteel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("stainlesssteelknurled", MaterialType.TYPE_METAL);
				MaterialTypes.Add("stainlesssteeltreadplate", MaterialType.TYPE_METAL);
				MaterialTypes.Add("wroughtstainlesssteel", MaterialType.TYPE_METAL);
				MaterialTypes.Add("brushedtitanium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("burnishedtitanium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("casttitanium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattetitanium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastedtitanium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishtitanium", MaterialType.TYPE_METAL);
				MaterialTypes.Add("burnishedtungsten", MaterialType.TYPE_METAL);
				MaterialTypes.Add("casttungsten", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattetungsten", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastedtungsten", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishtungsten", MaterialType.TYPE_METAL);
				MaterialTypes.Add("brushedzinc", MaterialType.TYPE_METAL);
				MaterialTypes.Add("burnishedzinc", MaterialType.TYPE_METAL);
				MaterialTypes.Add("castzinc", MaterialType.TYPE_METAL);
				MaterialTypes.Add("mattezinc", MaterialType.TYPE_METAL);
				MaterialTypes.Add("polishedzinc", MaterialType.TYPE_METAL);
				MaterialTypes.Add("sandblastedzinc", MaterialType.TYPE_METAL);
				MaterialTypes.Add("satinfinishzinc", MaterialType.TYPE_METAL);
				MaterialTypes.Add("screwthread", MaterialType.TYPE_MISC);
				MaterialTypes.Add("cartoonsketch", MaterialType.TYPE_MISC);
				MaterialTypes.Add("blue_slate_floor", MaterialType.TYPE_MISC);
				MaterialTypes.Add("checker_floor_bright", MaterialType.TYPE_MISC);
				MaterialTypes.Add("scene_factory_floor", MaterialType.TYPE_MISC);
				MaterialTypes.Add("floorgrime", MaterialType.TYPE_MISC);
				MaterialTypes.Add("floorwhite", MaterialType.TYPE_MISC);
				MaterialTypes.Add("imperfect-floor", MaterialType.TYPE_MISC);
				MaterialTypes.Add("wallgrime", MaterialType.TYPE_MISC);
				MaterialTypes.Add("wallwhite", MaterialType.TYPE_MISC);
				MaterialTypes.Add("transparentfloor", MaterialType.TYPE_MISC);
				MaterialTypes.Add("grass", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("leather", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("sand", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("skin", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("sponge", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("clearsky", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("heavyclouds", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("lightclouds", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("waterheavyripple", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("waterslightripple", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("waterstill", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedash", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishash", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedash", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedbeech", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishbeech", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedbeech", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedbirch", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishbirch", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedbirch", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedcherry", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishcherry", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedcherry", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("hardwoodfloor3", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("hardwoodfloor2", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("hardwoodfloor", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("laminatefloor", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("laminatefloor2", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("laminatefloor3", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedmahogany", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishmahogany", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedmahogany", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedmaple", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishmaple", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedmaple", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedoak", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishoak", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedoak", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("orientedstrandboard", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedpine", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishpine", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedpine", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedrosewood", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishrosewood", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedrosewood", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedsatinwood", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedsatinwood", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishsatinwood ", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedspruce", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedspruce", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishspruce", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedteak", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishteak", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedteak", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("polishedwalnut", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("satinfinishwalnut", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("unfinishedwalnut", MaterialType.TYPE_ORGANIC);
				MaterialTypes.Add("blackcarpaint", MaterialType.TYPE_PAINTED);
				MaterialTypes.Add("candyappleredcarpainthq", MaterialType.TYPE_METALLICPAINT);
				MaterialTypes.Add("glossbluecarpainthq", MaterialType.TYPE_PAINTED);
				MaterialTypes.Add("glossredcarpainthq", MaterialType.TYPE_PAINTED);
				MaterialTypes.Add("metalliccoolgreycarpainthq", MaterialType.TYPE_METALLICPAINT);
				MaterialTypes.Add("metallicgoldcarpainthq", MaterialType.TYPE_METALLICPAINT);
				MaterialTypes.Add("metallicwarmgreycarpainthq", MaterialType.TYPE_METALLICPAINT);
				MaterialTypes.Add("sienacarpainthq", MaterialType.TYPE_PAINTED);
				MaterialTypes.Add("steelgreycarpainthq", MaterialType.TYPE_PAINTED);
				MaterialTypes.Add("whitecarpaint", MaterialType.TYPE_PAINTED);
				MaterialTypes.Add("aluminumpowdercoat", MaterialType.TYPE_PAINTED);
				MaterialTypes.Add("darkpowdercoat", MaterialType.TYPE_PAINTED);
				MaterialTypes.Add("blackspraypaint", MaterialType.TYPE_PAINTED);
				MaterialTypes.Add("redspraypaint", MaterialType.TYPE_PAINTED);
				MaterialTypes.Add("frostedplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("polycarbonateplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("polypropyleneplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("translucentplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("carbonfiberaramidfabric", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("carbonfiberdesignfabric", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("carbonfiberdyneemaplain", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("carbonfiberepoxy", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("carbonfiberinlayunidirectional", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("largesparkerosionplasticblue", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("sparkerosionplasticblue", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("beigehighglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("blackhighglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("bluehighglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("creamhighglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("darkgreyhighglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("greenhighglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("lightgreyhighglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("redhighglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("whitehighglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("yellowhighglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("beigelowglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("blacklowglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("bluelowglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("creamlowglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("darkgreylowglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("greenlowglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("lightgreylowglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("redlowglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("whitelowglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("yellowlowglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("beigemediumglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("blackmediumglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("bluemediumglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("creammediumglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("darkgreymediumglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("greenmediumglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("lightgreymediumglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("redmediumglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("whitemediumglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("yellowmediumglossplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("circularmeshplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("diamondmeshplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("bluedimpledplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("blueknurledplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("bluetreadplateplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("beigesatinfinishplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("blacksatinfinishplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("bluesatinfinishplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("creamsatinfinishplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("darkgreysatinfinishplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("greensatinfinishplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("lightgreysatinfinishplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("redsatinfinishplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("whitesatinfinishplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("yellowsatinfinishplastic", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11000", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11010", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11020", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11030", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11040", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11050", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11060", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11070", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11080", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11090", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11100", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11110", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11120", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11130", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11140", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11150", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11155", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11200", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11205", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11215", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11230", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11235", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11240", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11245", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("plasticmt11250", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("PlasticPolystyrene", MaterialType.TYPE_PLASTIC);
				MaterialTypes.Add("glossyrubber", MaterialType.TYPE_RUBBER);
				MaterialTypes.Add("matterubber", MaterialType.TYPE_RUBBER);
				MaterialTypes.Add("perforatedrubber", MaterialType.TYPE_RUBBER);
				MaterialTypes.Add("texturedrubber", MaterialType.TYPE_RUBBER);
				MaterialTypes.Add("tiretreadrubber", MaterialType.TYPE_RUBBER);
				MaterialTypes.Add("redceramictile", MaterialType.TYPE_STONE);
				MaterialTypes.Add("cobblestone", MaterialType.TYPE_STONE);
				MaterialTypes.Add("cobblestone1", MaterialType.TYPE_STONE);
				MaterialTypes.Add("cobblestone2", MaterialType.TYPE_STONE);
				MaterialTypes.Add("cobblestone3", MaterialType.TYPE_STONE);
				MaterialTypes.Add("Scene_Cobblestone", MaterialType.TYPE_STONE);
				MaterialTypes.Add("floortile1", MaterialType.TYPE_STONE);
				MaterialTypes.Add("floortile2", MaterialType.TYPE_STONE);
				MaterialTypes.Add("floortile3", MaterialType.TYPE_STONE);
				MaterialTypes.Add("floortile4", MaterialType.TYPE_STONE);
				MaterialTypes.Add("floortile5", MaterialType.TYPE_STONE);
				MaterialTypes.Add("floortile6", MaterialType.TYPE_STONE);
				MaterialTypes.Add("floortile7", MaterialType.TYPE_STONE);
				MaterialTypes.Add("granite", MaterialType.TYPE_STONE);
				MaterialTypes.Add("limestone", MaterialType.TYPE_STONE);
				MaterialTypes.Add("bluemarble", MaterialType.TYPE_STONE);
				MaterialTypes.Add("darkmarble", MaterialType.TYPE_STONE);
				MaterialTypes.Add("beigemarble", MaterialType.TYPE_STONE);
				MaterialTypes.Add("greenmarble", MaterialType.TYPE_STONE);
				MaterialTypes.Add("pinkmarble", MaterialType.TYPE_STONE);
				MaterialTypes.Add("redsandstone", MaterialType.TYPE_STONE);
				MaterialTypes.Add("sandstone", MaterialType.TYPE_STONE);
				MaterialTypes.Add("tansandstone", MaterialType.TYPE_STONE);
				MaterialTypes.Add("slate", MaterialType.TYPE_STONE);
				MaterialTypes.Add("fakebrick", MaterialType.TYPE_STONE);
				MaterialTypes.Add("firebrick", MaterialType.TYPE_STONE);
				MaterialTypes.Add("flemishbrick", MaterialType.TYPE_STONE);
				MaterialTypes.Add("oldenglishbrick2", MaterialType.TYPE_STONE);
				MaterialTypes.Add("oldenglishbrick", MaterialType.TYPE_STONE);
				MaterialTypes.Add("weatheredbrick", MaterialType.TYPE_STONE);
				MaterialTypes.Add("pavingasphalt", MaterialType.TYPE_STONE);
				MaterialTypes.Add("pavingdarkconcrete", MaterialType.TYPE_STONE);
				MaterialTypes.Add("pavinglightconcrete", MaterialType.TYPE_STONE);
				MaterialTypes.Add("pavingstone", MaterialType.TYPE_STONE);
				MaterialTypes.Add("pavingredconcrete", MaterialType.TYPE_STONE);
				MaterialTypes.Add("pavingwetconcrete", MaterialType.TYPE_STONE);
				MaterialTypes.Add("bonechina", MaterialType.TYPE_STONE);
				MaterialTypes.Add("ceramic", MaterialType.TYPE_STONE);
				MaterialTypes.Add("earthenware", MaterialType.TYPE_STONE);
				MaterialTypes.Add("porcelain", MaterialType.TYPE_STONE);
				MaterialTypes.Add("stoneware", MaterialType.TYPE_STONE);
			}
		}
		
		public static MaterialType GetMaterialType(string shaderName)
		{
			if (shaderName == null)
				return MaterialType.TYPE_LIGHTWEIGHT;
			InitializeMaterialTypes();
			MaterialType type;
			shaderName = shaderName.ToLower();
			if (!MaterialTypes.TryGetValue(shaderName, out type))
			{
				if (shaderName.IndexOf("plastic") >= 0) // legacy plastics
					type = MaterialType.TYPE_PLASTIC;
				else
					type = MaterialType.TYPE_UNKNOWN;
			}
			return type;
		}

		public string Name;
		public string ShaderName;
		public string FileName;
		public string BumpTextureFileName;
		public string Texture;

		// material data
		public long Type = 0;
		public long BumpMap = 0;
		public double BumpAmplitude = 0.0;
		public double Emission = 0.0;
		public double Glossy = 0.0;
		public double IOR = 0.0;
		public long ColorForm = 0;
		public Color PrimaryColor = Color.FromArgb(0xff, 0x7f, 0x7f, 0x7f);
		public Color SecondaryColor = Color.FromArgb(0xff, 0, 0, 0);
		public Color TertiaryColor = Color.FromArgb(0xff, 0, 0, 0);
		public double Reflectivity = 0.0;
		public double RotationAngle = 0.0;
		public double SpecularSpread = 0.0;
		public double Specular = 0.0;
		public double Roughness = 1.0;
		public double MetallicRoughness = 1.0;
		public long SpecularColor = 0;
		public double Transparency = 0.0;
		public double Translucency = 0.0;
		public double Width = 0.0;
		public double Height = 0.0;
		public double XPos = 0.0;
		public double YPos = 0.0;
		public double Rotation = 0.0;
		public Vec3 CenterPoint;
		public Vec3 UDirection;
		public Vec3 VDirection;
		public double Direction1RotationAngle = 0.0;
		public double Direction2RotationAngle = 0.0;
		public bool MirrorVertical = false;
		public bool MirrorHorizontal = false;
		public MappingType UVMappingType = MappingType.TYPE_UNSPECIFIED;
		public long ProjectionReference = 0;
		public double Diffuse = 1.0;
		public bool BlurryReflections = false;

		private static bool IS_BLACK(Color color)
		{
			int c = color.ToArgb();
			int cma = c & 0x00ffffff;
			return cma == 0;
		}

		private static bool IS_BLACK(int color)
		{
			int cma = color & 0x00ffffff;
			return cma == 0;
		}

		public SwMaterial()
		{
		}

		public SwMaterial(RenderMaterial m, IModelDocExtension ext)
		{
			if (m != null)
			{
				_source = m;

				Type = _source.IlluminationShaderType;

				FileName = _source.FileName;

				ID = GetMaterialID(m);

				Name = Path.GetFileNameWithoutExtension(FileName) + "<" + ID + ">";

				BumpMap = _source.BumpMap;
				BumpAmplitude = _source.BumpAmplitude;
				BumpTextureFileName = _source.BumpTextureFilename;

				Emission = _source.Emission;

				// fix emission inferred from ambient factor
				if (Emission > 0.0)
				{
					if ((Name.IndexOf("LED") == -1) &&
						(Name.IndexOf("Light") == -1) &&
						(Name.IndexOf("LCD") == -1) &&
						(Name.IndexOf("Neon") == -1))
					Emission = 0.0;
				}

				ColorForm = _source.ColorForm;
				// swRenderMaterialColorFormsColor_Undefined
				// swRenderMaterialColorFormsImage
				// swRenderMaterialColorFormsOne_Color
				// swRenderMaterialColorFormsTwo_Colors
				// swRenderMaterialColorFormsThree_Colors

				Glossy = _source.Glossy;
				Roughness = _source.Roughness;
				MetallicRoughness = _source.MetallicRoughness;
				Reflectivity = _source.Reflectivity;
				Specular = _source.Specular;
				SpecularColor = _source.SpecularColor;
				RotationAngle = _source.RotationAngle;

				PrimaryColor = Materials.Utility.ConvertColor(_source.PrimaryColor);
				SecondaryColor = Materials.Utility.ConvertColor(_source.SecondaryColor);
				TertiaryColor = Materials.Utility.ConvertColor(_source.TertiaryColor);
				
				double cx, cy, cz;
				_source.GetCenterPoint2(out cx, out cy, out cz);
				CenterPoint = new Vec3((float)cx, (float)cy, (float)cz);

				double uDirx, uDiry, uDirz;
				_source.GetUDirection2(out uDirx, out uDiry, out uDirz);
				UDirection = new Vec3((float)uDirx, (float)uDiry, (float)uDirz);

				double vDirx, vDiry, vDirz;
				_source.GetVDirection2(out vDirx, out vDiry, out vDirz);
				VDirection = new Vec3((float)vDirx, (float)vDiry, (float)vDirz);

				Direction1RotationAngle = _source.Direction1RotationAngle;
				Direction2RotationAngle = _source.Direction2RotationAngle;

				Transparency = _source.Transparency;
				Translucency = _source.Translucency;
				IOR = _source.IndexOfRefraction;

				Texture = _source.TextureFilename;

				Width = _source.Width;
				Height = _source.Height;
				XPos = _source.XPosition;
				YPos = _source.YPosition;
				Rotation = _source.RotationAngle;
				MirrorVertical = _source.HeightMirror;
				MirrorHorizontal = _source.WidthMirror;
				if (_source.MappingType == 0)
					UVMappingType = MappingType.TYPE_SURFACE;
				else if (_source.MappingType == 1)
					UVMappingType = MappingType.TYPE_PROJECTION;
				else if (_source.MappingType == 2)
					UVMappingType = MappingType.TYPE_SPHERICAL;
				else if (_source.MappingType == 3)
					UVMappingType = MappingType.TYPE_CYLINDRICAL;
				else if (_source.MappingType == 4)
					UVMappingType = MappingType.TYPE_AUTOMATIC;

				ProjectionReference = _source.ProjectionReference;

				bool isBlack = IS_BLACK(PrimaryColor);
				
				if (!string.IsNullOrEmpty(FileName))
				{
					if (!File.Exists(FileName)) // if the file is moved from its original location, the saved path might still refer to the old one
					{
						string path = Path.GetDirectoryName(ext.Document.GetPathName());
						string fname = Path.GetFileName(FileName);
						FileName = Path.Combine(path, fname);
					}

					if (File.Exists(FileName))
					{
						string fdata = File.ReadAllText(FileName);
						if (!string.IsNullOrEmpty(fdata))
						{
							var sLen = fdata.Length;
							ShaderName = FindP2MProperty(fdata, "\"sw_shader\"");

							if (isBlack)
							{
								var col1 = FindP2MProperty(fdata, "\"col1\"");
								if (!string.IsNullOrEmpty(col1))
								{
									var coords = col1.Split(',');
									if (coords.Length == 3)
									{
										byte r = (byte)(float.Parse(coords[0]) * 255f);
										byte g = (byte)(float.Parse(coords[0]) * 255f);
										byte b = (byte)(float.Parse(coords[0]) * 255f);
										if (r != 0 || g != 0 || b != 0)
											PrimaryColor = Color.FromArgb(0xff, r, g, b);
									}
								}
							}

							// could be in a parameter
							var path = FindP2MPropertyString(fdata, "\"bumpTexture\"");
							if (!string.IsNullOrEmpty(path))
							{
								BumpTextureFileName = Path.Combine(new string[] { SwAddin.Instance.SwApp.GetExecutablePath(), "data", path.Replace('/', '\\') });
							}
							else
							{
								var pos = fdata.IndexOf("color texture \"bump_file_texture\" ");
								if (pos != -1)
								{
									var pos1 = fdata.IndexOf('"', pos);
									if (pos1 != -1)
									{
										pos1 = fdata.IndexOf('"', pos1 + 1);
										if (pos1 != -1)
										{
											pos1 = fdata.IndexOf('"', pos1 + 1);
											if (pos1 != -1)
											{
												var pos2 = fdata.IndexOf('"', pos1 + 1);
												if (pos2 != -1)
												{
													BumpTextureFileName = Path.Combine(new string[] { SwAddin.Instance.SwApp.GetExecutablePath(), "data", fdata.Substring(pos1 + 1, pos2 - pos1 - 1).Replace('/', '\\') });
												}
											}
										}
									}
								}
							}
						}
					}
				}

				//
				if (!string.IsNullOrEmpty(Texture))
				{
					if (Special_diffuses.Count == 0)
					{
						Special_diffuses.Add(new Tuple<string, string>("ash", "organic\\wood\\ash\\polished ash.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("beech", "organic\\wood\\beech\\polished beech.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("birch", "organic\\wood\\birch\\polished birch.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("cherry", "organic\\wood\\cherry\\polished cherry.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("maple", "organic\\wood\\maple\\polished maple.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("oak", "organic\\wood\\oak\\polished oak.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("pine", "organic\\wood\\pine\\polished pine.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("rosewood", "organic\\wood\\rosewood\\polished rosewood.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("satinwood", "organic\\wood\\satinwood\\satinwood.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("spruce", "organic\\wood\\spruce\\spruce.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("teak", "organic\\wood\\teak\\polished teak.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("blue marble", "stone\\architectural\\marble\\marble blue.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("blue rough shiny marble", "stone\\architectural\\marble\\marble blue.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("blue shiny marble", "stone\\architectural\\marble\\marble blue.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("blue vein marble", "stone\\architectural\\marble\\marble blue vein.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("green marble", "stone\\architectural\\marble\\marble green.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("green vein marble", "stone\\architectural\\marble\\marble green vein.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("pink marble", "stone\\architectural\\marble\\pink marble.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("pink vein marble", "stone\\architectural\\marble\\marble pink vein.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("sandstone", "stone\\architectural\\sandstone\\sandstone.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("slate", "stone\\architectural\\slate\\slate.jpg"));
						Special_diffuses.Add(new Tuple<string, string>("slate", "stone\\brick\\fire brick.jpg"));
					}
					foreach (var sc in Special_diffuses)
					{
						CompareInfo sampleCInfo = CultureInfo.InvariantCulture.CompareInfo;
						if (sampleCInfo.IndexOf(Name, sc.Item1, CompareOptions.IgnoreCase) >= 0)
						{
							Texture = Path.Combine(new string[] { SwAddin.Instance.SwApp.GetExecutablePath(), "data", "Images", "textures", sc.Item2 });
							break;
						}
					}
				}
				
				if (!string.IsNullOrEmpty(Texture))
					PrimaryColor = Color.FromArgb(0xff, 0xff, 0xff, 0xff);

				if (!string.IsNullOrEmpty(BumpTextureFileName))
				{
					if (Special_finishes.Count == 0)
					{
						Special_finishes.Add(new Tuple<string, string>("burnished", "burnished_n.dds"));
						Special_finishes.Add(new Tuple<string, string>("burlap", "burlap_n.dds"));
						Special_finishes.Add(new Tuple<string, string>("brushed", "_brushed.dds"));
						Special_finishes.Add(new Tuple<string, string>("scratch", "_scratch_n.dds"));
						Special_finishes.Add(new Tuple<string, string>("knurled", "knurled.dds"));
						Special_finishes.Add(new Tuple<string, string>("spruce", "spruce_n.dds"));
						Special_finishes.Add(new Tuple<string, string>("frosted", "plasticmt11040_normalmap.dds"));
						Special_finishes.Add(new Tuple<string, string>("sandblasted", "sandblasted.dds"));
						Special_finishes.Add(new Tuple<string, string>("satin", "satinwood_n.dds"));
						Special_finishes.Add(new Tuple<string, string>("powdercoat", "powdercoat_n.dds"));
						Special_finishes.Add(new Tuple<string, string>("polypropylene", "..\\..\\textures\\plastic\\bumpy\\plasticpolypropylene_bump.jpg"));
					}
					foreach (var sc in Special_finishes)
					{
						CompareInfo sampleCInfo = CultureInfo.InvariantCulture.CompareInfo;
						if (sampleCInfo.IndexOf(Name, sc.Item1, CompareOptions.IgnoreCase) >= 0)
						{
							BumpTextureFileName = Path.Combine(new string[] { SwAddin.Instance.SwApp.GetExecutablePath(), "data", "Images", "shaders", "surfacefinish", sc.Item2 });
							break;
						}
					}
				}

				// Try to get the legacy appearance to fish up hidden properties such as BlurryReflections and Specular Spread
				//
				if (m.GetEntitiesCount() > 0)
				{
					object entity = m.GetEntities()[0];
					DisplayStateSetting settings = null;
					if (entity is ModelDoc2)
					{
						var doc = entity as ModelDoc2;
						ext = doc.Extension;
						Component2 root = doc.ConfigurationManager.ActiveConfiguration.GetRootComponent3(false);
						if (root != null)
						{
							settings = ext.GetDisplayStateSetting(1); // 1 = swThisDisplayState
							settings.Entities = new Component2[1] { root };
						}
					}
					else
					{
						settings = ext.GetDisplayStateSetting(1); // 1 = swThisDisplayState
						if (entity is Face2)
							settings.Entities = new Face2[1] { entity as Face2 };
						else if (entity is Feature)
							settings.Entities = new Feature[1] { entity as Feature };
						else if (entity is Body2)
							settings.Entities = new Body2[1] { entity as Body2 };
						else if (entity is PartDoc)
							settings.Entities = new PartDoc[1] { entity as PartDoc };
						else if (entity is Component2)
							settings.Entities = new Component2[1] { entity as Component2 };
					}

					if (settings != null && settings.Entities != null)
					{
						object[] appearances = null;
						try
						{
							appearances = ext.DisplayStateSpecMaterialPropertyValues[settings] as object[];
						}
						catch
						{
						}
						if (appearances != null && appearances.Length > 0)
						{
							SetAppearance(appearances[0] as IAppearanceSetting);
						}
					}
				}
			}
		}

		public void SetAppearance(IAppearanceSetting a)
		{
			if (a != null)
			{
				_appearance = a;

				double a_diffuse, a_emission, a_reflectivity, a_specular, a_specularSpread, a_transparency;
				int l_specularColor, l_primaryColor;

				BlurryReflections = _appearance.BlurryReflection;

				l_primaryColor = _appearance.Color;
				a_diffuse = _appearance.Diffuse;
				a_emission = _appearance.Luminous;
				a_reflectivity = _appearance.Reflection;
				a_specular = _appearance.Specular;
				l_specularColor = _appearance.SpecularColor;
				a_specularSpread = _appearance.SpecularSpread;
				a_transparency = _appearance.Transparent;

				Diffuse = a_diffuse;
				Emission = a_emission;
				Reflectivity = a_reflectivity;
				Specular = a_specular;
				SpecularSpread = a_specularSpread;
				Transparency = a_transparency;
				if (IS_BLACK(PrimaryColor) && !IS_BLACK(l_primaryColor) && string.IsNullOrEmpty(Texture))
					PrimaryColor = Materials.Utility.ConvertColor(l_primaryColor);
				SpecularColor = l_specularColor;
			}
		}

		string FindP2MProperty(string file, string propertyName)
		{
			string res = "";
			var pos = file.IndexOf(propertyName);
			if (pos >= 0)
			{
				var sLen = file.Length;
				pos += propertyName.Length;
				while (pos != sLen && (file[pos] == ' ' || file[pos] == '\t' || file[pos] == '\r' || file[pos] == '\n'))
					pos++;
				while (pos != sLen && file[pos] != ' ' && file[pos] != '\t' && file[pos] != '\r' && file[pos] != '\n')
					res += file[pos++];
			}
			return res;
		}

		string FindP2MPropertyString(string file, string propertyName)
		{
			string res = "";
			var pos = file.IndexOf(propertyName);
			if (pos >= 0)
			{
				var sLen = file.Length;
				pos += propertyName.Length;
				while (pos != sLen && file[pos] != '\"')
					pos++;
				if (pos != sLen)
					pos++;
				while (pos != sLen && file[pos] != '\"')
					res += file[pos++];
			}
			return res;
		}

		public static int GetMaterialID(RenderMaterial Material)
		{
			string MaterialStringID = Material.FileName;

			// Simple color materials need to be distinguished in another way than the file path (which is always the same)
			string MaterialFileName = Path.GetFileNameWithoutExtension(Material.FileName);
			if (MaterialFileName == "color")
			{
				MaterialStringID = Material.PrimaryColor.ToString();
			}

			MD5 MD5Hasher = MD5.Create();
			byte[] Hashed = MD5Hasher.ComputeHash(Encoding.UTF8.GetBytes(MaterialStringID));
			int ID = Math.Abs(BitConverter.ToInt32(Hashed, 0));
			return ID;
		}

		public override string ToString()
		{
			return Name;
		}

		public static string ComputeAssemblySideTexturePath(string originaltexturepath)
		{
			return Path.GetDirectoryName(SwSingleton.CurrentScene.Doc.GetPathName()) + "\\" + Path.GetFileName(originaltexturepath);
		}

		public UVPlane GetUVPlane(Vec3 uDir, Vec3 vDir)
		{
			UVPlane uvPlane = new UVPlane();

			if (!Utility.IsSame(RotationAngle, 0.0))
			{
				Vec3 vecZ = Vec3.Cross(uDir, vDir);
				Vec3 centerPoint = CenterPoint;
				Matrix4 rotateMatrix = Matrix4.FromRotationAxisAngle(vecZ, (float)RotationAngle);
				uvPlane.UDirection = (rotateMatrix.TransformVector(uDir)).Normalized();
				uvPlane.VDirection = (rotateMatrix.TransformVector(vDir)).Normalized();
			}
			else
			{
				uvPlane.UDirection = uDir;
				uvPlane.VDirection = vDir;
			}
			uvPlane.Offset = new Vec3(
				XPos * uDir.x + YPos * vDir.x - CenterPoint.x,
				XPos * uDir.y + YPos * vDir.y - CenterPoint.y,
				XPos * uDir.z + YPos * vDir.z - CenterPoint.z);
			uvPlane.Normal = new Vec3() - Vec3.Cross(uvPlane.UDirection, uvPlane.VDirection);

			return uvPlane;
		}

		public List<UVPlane> ComputeUVPlanes()
		{
			var planes = new List<UVPlane>();
			switch (UVMappingType)
			{
				case SwMaterial.MappingType.TYPE_SPHERICAL:
				case SwMaterial.MappingType.TYPE_PROJECTION:
					{
						planes.Add(GetUVPlane(UDirection, VDirection));
					}
					break;
				case SwMaterial.MappingType.TYPE_AUTOMATIC:
				case SwMaterial.MappingType.TYPE_CYLINDRICAL:
					{
						Vec3 cross = new Vec3(0f, 0f, 0f) - Vec3.Cross(UDirection, VDirection);
						planes.Add(GetUVPlane(UDirection, VDirection));
						planes.Add(GetUVPlane(UDirection, cross));
						planes.Add(GetUVPlane(VDirection, cross));
						planes.Add(GetUVPlane(new Vec3() - UDirection, cross));
						planes.Add(GetUVPlane(new Vec3() - VDirection, cross));
						planes.Add(GetUVPlane(UDirection, new Vec3() - VDirection));
					}
					break;
			}

			return planes;
		}

		public UVPlane GetTexturePlane(List<UVPlane> planes, Vec3 vertexNormal)
		{
			int planeIdx = -1;
			switch (UVMappingType)
			{
				case SwMaterial.MappingType.TYPE_SPHERICAL:
				case SwMaterial.MappingType.TYPE_PROJECTION:
					planeIdx = 0;
					break;
				case SwMaterial.MappingType.TYPE_AUTOMATIC:
				case SwMaterial.MappingType.TYPE_CYLINDRICAL:
					double max = double.MinValue;
					for (var i = 0; i < planes.Count; i++)
					{
						Vec3 boxPlaneVector = planes[i].Normal;
						double dot = Vec3.Dot(vertexNormal, boxPlaneVector);
						if (dot > max)
						{
							max = dot;
							planeIdx = i;
						}
					}
					break;
			}
			return (planeIdx >= 0) ? planes[planeIdx] : null;
		}

		public Vec3 RotateVectorByXY(Vec3 vec, Vec3 modelCenter)
		{
			Matrix4 matrix = null;
			Vec3 centerPoint = modelCenter;

			matrix = Matrix4.RotateVectorByAxis(centerPoint, matrix, Vec3.XAxis, Direction1RotationAngle);

			matrix = Matrix4.RotateVectorByAxis(centerPoint, matrix, Vec3.YAxis, Direction2RotationAngle);

			if (matrix != null)
			{
				vec = (matrix.TransformPoint(vec)).Normalized();
			}

			return vec;
		}

		public Vec2 ComputeNormalAtan2(Vec3 vertex, Vec3 modelCenter)
		{
			Vec3 normal = vertex - modelCenter;
			Vec3 uvCross = Vec3.Cross(new Vec3() - UDirection, VDirection);
			Vec3 uvNormal = new Vec3(-Vec3.Dot(normal, UDirection), Vec3.Dot(normal, VDirection), Vec3.Dot(normal, uvCross));
			return new Vec2(((float)Math.Atan2(uvNormal.z, uvNormal.x) / (Math.PI * 2)), uvNormal.y);
		}

		public Vec2 ComputeVertexUV(UVPlane plane, Vec3 vertex)
		{
			float flipU = MirrorHorizontal ? -1f : 1f;
			float flipV = MirrorVertical ? -1f : 1f;
			float u = (float)((Vec3.Dot((vertex + plane.Offset), plane.UDirection) * flipU) / Width);
			float v = (float)((Vec3.Dot((vertex + plane.Offset), plane.VDirection) * flipV) / Height);
			return new Vec2(u, v);
		}

		public static bool AreTheSame(SwMaterial mat1, SwMaterial mat2, bool nameMustAlsoBeSame)
		{
			if (nameMustAlsoBeSame)
				if (mat1.Name != mat2.Name) return false;
			if (mat1.ShaderName != mat2.ShaderName) return false;
			if (mat1.FileName != mat2.FileName) return false;
			if (mat1.BumpTextureFileName != mat2.BumpTextureFileName) return false;
			if (mat1.Texture != mat2.Texture) return false;
			if (mat1.Type != mat2.Type) return false;
			if (mat1.BumpMap != mat2.BumpMap) return false;
			if (!Utility.IsSame(mat1.BumpAmplitude, mat2.BumpAmplitude)) return false;
			if (!Utility.IsSame(mat1.Emission, mat2.Emission)) return false;
			if (!Utility.IsSame(mat1.Glossy, mat2.Glossy)) return false;
			if (!Utility.IsSame(mat1.IOR, mat2.IOR)) return false;
			if (mat1.ColorForm != mat2.ColorForm) return false;
			if (!Utility.IsSame(mat1.PrimaryColor, mat2.PrimaryColor)) return false;
			if (!Utility.IsSame(mat1.SecondaryColor, mat2.SecondaryColor)) return false;
			if (!Utility.IsSame(mat1.TertiaryColor, mat2.TertiaryColor)) return false;
			if (!Utility.IsSame(mat1.Reflectivity, mat2.Reflectivity)) return false;
			if (!Utility.IsSame(mat1.RotationAngle, mat2.RotationAngle)) return false;
			if (!Utility.IsSame(mat1.SpecularSpread, mat2.SpecularSpread)) return false;
			if (!Utility.IsSame(mat1.Specular, mat2.Specular)) return false;
			if (!Utility.IsSame(mat1.Roughness, mat2.Roughness)) return false;
			if (!Utility.IsSame(mat1.MetallicRoughness, mat2.MetallicRoughness)) return false;
			if (mat1.SpecularColor != mat2.SpecularColor) return false;
			if (!Utility.IsSame(mat1.Transparency, mat2.Transparency)) return false;
			if (!Utility.IsSame(mat1.Translucency, mat2.Translucency)) return false;

			// Only take into account UV mapping differences if there are actual textures assigned
			if (mat1.Texture.Length > 0 || mat1.BumpTextureFileName.Length > 0)
			{
				if (!Utility.IsSame(mat1.Width, mat2.Width)) return false;
				if (!Utility.IsSame(mat1.Height, mat2.Height)) return false;
				if (!Utility.IsSame(mat1.XPos, mat2.XPos)) return false;
				if (!Utility.IsSame(mat1.YPos, mat2.YPos)) return false;
				if (!Utility.IsSame(mat1.Rotation, mat2.Rotation)) return false;
				if (!Utility.IsSame(mat1.CenterPoint, mat2.CenterPoint)) return false;
				if (!Utility.IsSame(mat1.UDirection, mat2.UDirection)) return false;
				if (!Utility.IsSame(mat1.VDirection, mat2.VDirection)) return false;
				if (!Utility.IsSame(mat1.Direction1RotationAngle, mat2.Direction1RotationAngle)) return false;
				if (!Utility.IsSame(mat1.Direction2RotationAngle, mat2.Direction2RotationAngle)) return false;
				if (mat1.MirrorVertical != mat2.MirrorVertical) return false;
				if (mat1.MirrorHorizontal != mat2.MirrorHorizontal) return false;
				if (mat1.UVMappingType != mat2.UVMappingType) return false;
			}

			if (mat1.ProjectionReference != mat2.ProjectionReference) return false;
			if (!Utility.IsSame(mat1.Diffuse, mat2.Diffuse)) return false;
			if (mat1.BlurryReflections != mat2.BlurryReflections) return false;
			return true;
		}
	}
}
