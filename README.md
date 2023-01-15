# BFA (Baked Font Atlas)
A simple file format for storing pre-rendered fonts in a texture atlas and using unicode values to map
into the atlas. 

## Compiling the CLI
Requires Vulkan ([Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) on Windows) and [FreeType](https://freetype.org/index.html).
The CLI is in a single source file "cli.c".

#### Windows
1. Create a folder in the source directory named "external".
2. Copy freetype.lib and freetype headers into external. ft2build.h should be in "external/ft2build.h".
3. Run vcvars64.bat for whatever Visual Studio version you want to use.
4. Run build.bat. bfa.exe and the example program bfa_glyph.exe will be output to the source directory.

#### Linux
The source is C99 compliant so use whatever C99 compiler you want and link with Vulkan and FreeType.

## The Format
The BFA format consists of a header, a glyph map and a texture atlas image.
Look at example.c for reference.

Overview:
|Section|Size|
|-------|----|
|Header|24 B|
|Glyph Map|Variable|
|Image Data|Variable|

#### Header

|Type|Offset|Description|
|----|------|-----------|
|uint32|0|Magic. Always 0x6166622e (.bfa)|
|uint32|4|Flags.|
|uint16|8|The number of renderable glyphs (including spaces).|
|uint8|10|Map type. See below.|
|uint8|11|The font pixel size.|
|uint16|12|The width of the atlas image.|
|uint16|14|The height of the atlas image.|
|uint16|16|The largest width of any glyph.|
|uint16|18|The largest height of any glyph.|
|uint32|20|The size of the stored image data. This is not always the size of the full image because empty space at the end is truncated.|

As a an example C struct:
```
struct bfa_header {
	uint32_t magic;
	uint32_t flags;
	uint16_t glyph_count;
	uint8_t map_type;
	uint8_t font_size;
	uint16_t atlas_width;
	uint16_t atlas_height;
	uint16_t largest_glyph_width;
	uint16_t largest_glyph_height;
	uint32_t size_of_stored_image_data;
};
```

#### Glyph Map

Glyph maps contain an array of structures that point to glyphs in the atlas.
These are 16 byte structures as follows:

|Type|Offset|Description|
|----|------|-----------|
|uint16|0|The X offset of the glyph in the atlas.|
|uint16|2|The Y offset of the glyph in the atlas.|
|uint16|4|Width.|
|uint16|6|Height.|
|int16|8|X bearing in pixels (X offset for drawing the glyph).|
|int16|10|Y bearing in pixels (Y offset for drawing the glyph).|
|int16|12|The advance in pixels (how much to move across X to draw the next glyph).|
|uint16|14|Flags. If bit 0 is 1, the glyph doesn't have an image and is just a space.|

As an example C struct:
```
struct bfa_glyph {
	uint16_t x;
	uint16_t y;
	uint16_t w;
	uint16_t h;
	int16_t x_bearing;
	int16_t y_bearing;
	int16_t advance;
	uint16_t flags;
};
```

The glyph structures can be layed out in 3 different ways, described by the "Map type" value in the header.

##### Map Type 0
- Fixed size 256 KB block, an array of 16384 glyph structures. 
- Get glyph data by indexing into the glyph array using a unicode value.
- Covers all of the Basic Multilingual Plane in one array.
- Has the most empty space and needs to be stored compressed.

##### Map Type 1
- Fixed size 4 KB block, an array of 256 glyph structures. 
- Get glyph data by indexing into the glyph array using a unicode value.
- Covers all of extended ASCII in one array.

##### Map Type 2
- Two parallel variable size arrays. First an array of all the renderable glyphs unicode values, and then an array of their glyph structures. 
- Get glyph data by searching for the unicode value in the unicode array and using the index of that value.

#### Image Data
The image is stored 8-bit grayscale at the end of the file. For example, in Vulkan the image would be loaded at VK_FORMAT_R8_UNORM.
The CLI can optionally output to a .tga file to view the atlas image.

