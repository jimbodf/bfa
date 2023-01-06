#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include <freetype/freetype.h>

enum {
	MEMORY_TYPE_CPU,
	MEMORY_TYPE_GPU,
};

enum {
	MAX_TEXTURE_DIMENSION = 16384,
};

enum {
	PROGRAM_FLAG_DEBUG = 0x1,
};

enum {
	GLYPH_FLAGS_IS_SPACE = 0x1,
};

enum {
	MAP_TYPE_UTF16,
	MAP_TYPE_ASCII,
	MAP_TYPE_UTF16_MAPPED,
	  };

struct bfa_glyph {
	uint16_t x, y;
	uint16_t w, h;
	uint16_t x_bearing, y_bearing;
	uint16_t advance;
	uint16_t flags;
};

struct bfa_header {
	uint32_t magic;
	uint32_t flags;
	uint16_t glyph_count;
	uint8_t map_type;
	uint8_t font_size;
	uint16_t atlas_width;
	uint16_t atlas_height;
	// Zeroes at the end of the generated
	// image get truncated.
	uint32_t size_of_stored_image_data;
};

struct vulkan_context {
	VkInstance instance;
	VkDevice device;
	VkPhysicalDevice physical_device;
	VkQueue queue;
	VkCommandPool command_pool;
	VkCommandBuffer command_buffer;
	uint32_t memory_type_indices[2];
	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;
	void *staging_buffer_pointer;
	VkFence fence;
};

static struct vulkan_context vk;
static int max_image_width = 8192;
static int max_image_height = 8192;
static char *tga_file_name;
static int requested_font_size = 16;
static int map_type = MAP_TYPE_UTF16_MAPPED;

static void init_vulkan_context(uint32_t staging_buffer_size) {
	// Instance
	{
		VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
		app.apiVersion = VK_API_VERSION_1_0;
		app.applicationVersion = VK_MAKE_VERSION(1,0,0);
		app.engineVersion = app.applicationVersion;
		app.pEngineName = "BFA";
		app.pApplicationName = "BFA";
		
		VkInstanceCreateInfo instance = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		instance.pApplicationInfo = &app;
#if defined(ENABLE_VALIDATION)
		instance.ppEnabledExtensionNames = (const char *const[]) {"VK_EXT_debug_utils"};
		instance.enabledExtensionCount = 1;
		instance.ppEnabledLayerNames = (const char *const[]) {"VK_LAYER_KHRONOS_validation"};
		instance.enabledLayerCount = 1;
		#endif
		vkCreateInstance(&instance, NULL, &vk.instance);
	}
	
	// Device
	{
		VkDeviceCreateInfo device = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		const float queue_priorities = {0.f};
		uint32_t one = 1;
		
		vkEnumeratePhysicalDevices(vk.instance, &one, &vk.physical_device);
		
		queue_info.queueFamilyIndex = 0;
		queue_info.queueCount = 1;
		queue_info.pQueuePriorities = &queue_priorities;
		
		device.queueCreateInfoCount = 1;
		device.pQueueCreateInfos = &queue_info;
		
		vkCreateDevice(vk.physical_device, &device, NULL, &vk.device);
		vkGetDeviceQueue(vk.device, 0, 0, &vk.queue);
	}
	
	// Memory type map
	{
		VkPhysicalDeviceMemoryProperties memory_properties;
		vkGetPhysicalDeviceMemoryProperties(vk.physical_device, &memory_properties);
		
		VkMemoryPropertyFlags desired_cpu_memory_flags = 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		VkMemoryPropertyFlags desired_gpu_memory_flags = 
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		
		for (int i = 0; i < memory_properties.memoryTypeCount; ++i) {
			if ((memory_properties.memoryTypes[i].propertyFlags & desired_cpu_memory_flags) == 
				desired_cpu_memory_flags) {
				vk.memory_type_indices[MEMORY_TYPE_CPU] = i;
				break;
			}
		}
		
		for (int i = 0; i < memory_properties.memoryTypeCount; ++i) {
			if ((memory_properties.memoryTypes[i].propertyFlags & desired_gpu_memory_flags) == 
				desired_gpu_memory_flags) {
				vk.memory_type_indices[MEMORY_TYPE_GPU] = i;
				break;
			}
		}
	}
	
	// Command buffer
	{
		VkCommandPoolCreateInfo command_pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		command_pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		vkCreateCommandPool(vk.device, &command_pool, NULL, &vk.command_pool);
		
		VkCommandBufferAllocateInfo command_buffer = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		command_buffer.commandPool = vk.command_pool;
		command_buffer.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		command_buffer.commandBufferCount = 1;
		vkAllocateCommandBuffers(vk.device, &command_buffer, &vk.command_buffer);
	}
	
	// Fence
	{
		VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		vkCreateFence(vk.device, &fence, NULL, &vk.fence);
	}
	
	// Staging buffer
	{
		VkBufferCreateInfo buffer = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		buffer.size = staging_buffer_size;
		buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		
		vkCreateBuffer(vk.device, &buffer, NULL, &vk.staging_buffer);
		
		VkMemoryRequirements memory_requirements;
		VkMemoryAllocateInfo memory_allocation = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		vkGetBufferMemoryRequirements(vk.device, vk.staging_buffer, &memory_requirements);
		memory_allocation.allocationSize = memory_requirements.size;
		memory_allocation.memoryTypeIndex = vk.memory_type_indices[MEMORY_TYPE_CPU];
		
		vkAllocateMemory(vk.device, &memory_allocation, NULL, &vk.staging_buffer_memory);
		vkBindBufferMemory(vk.device, vk.staging_buffer, vk.staging_buffer_memory, 0);
		vkMapMemory(vk.device, vk.staging_buffer_memory, 0, VK_WHOLE_SIZE, 0, &vk.staging_buffer_pointer);
	}
}

static void create_bfa_file(FT_Face ft_face, FILE *output_file) {
	int total_glyph_count = 0;
	int width_accumulator = 0;
	int output_width = 0;
	int output_height = 0;
	int row_height = 0;
	FT_GlyphSlot glyph_slot = ft_face->glyph;
	int atlas_row_count = 1;
	const int char_code_max = map_type == MAP_TYPE_ASCII ? 256 : 16384;
	
	// ===========================================================================
	// Calculate the size of the output image
	// ===========================================================================
	for (int char_code = 0; char_code < char_code_max; ++char_code) {
		uint32_t glyph_index = FT_Get_Char_Index(ft_face, char_code);
		if (!glyph_index) continue;
		
		FT_Load_Glyph(ft_face, glyph_index, 0);
		FT_Render_Glyph(glyph_slot, FT_RENDER_MODE_NORMAL);
		
		width_accumulator += glyph_slot->bitmap.width;
		
		if (glyph_slot->bitmap.rows > row_height) {
			row_height = glyph_slot->bitmap.rows;
		}
		
		if (width_accumulator > max_image_width) {
			width_accumulator = 0;
			output_width = max_image_width;
			atlas_row_count++;
		}
		
		++total_glyph_count;
	}
	
	if (!output_width) output_width = width_accumulator;
	output_height = row_height * atlas_row_count;
	
	if (output_height > max_image_height) {
		printf("Image is too large!! Try increasing --max-width and --max-height\n");
		exit(1);
	}
	
	printf("No. Glyphs: %d\n", total_glyph_count);
	printf("Width: %d\n", output_width);
	printf("Height: %d\n", output_height);
	printf("Total texture size: %g MB\n", (double)(output_height * output_width) / (double)(1<<20));
	
	init_vulkan_context(output_width * output_height);
	
	VkImage output_image;
	VkDeviceMemory output_image_memory;
	
	// ===========================================================================
	// Prepare the output image
	// ===========================================================================
	{
		VkImageCreateInfo image = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = VK_FORMAT_R8_UINT;
		image.extent.width = output_width;
		image.extent.height = output_height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		
		vkCreateImage(vk.device, &image, NULL, &output_image);
		
		VkMemoryRequirements memory_requirements;
		VkMemoryAllocateInfo memory_allocation = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		vkGetImageMemoryRequirements(vk.device, output_image, &memory_requirements);
		memory_allocation.allocationSize = memory_requirements.size;
		memory_allocation.memoryTypeIndex = vk.memory_type_indices[MEMORY_TYPE_GPU];
		
		vkAllocateMemory(vk.device, &memory_allocation, NULL, &output_image_memory);
		vkBindImageMemory(vk.device, output_image, output_image_memory, 0);
	}
	
	// ===========================================================================
	// First issue commands to copy all rendered glyphs onto a target image
	// ===========================================================================
	
	VkImageMemoryBarrier image_barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
	image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.image = output_image;
	image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_barrier.subresourceRange.levelCount = 1;
	image_barrier.subresourceRange.layerCount = 1;
	
	VkBufferImageCopy buffer_copy = {0};
	buffer_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	buffer_copy.imageSubresource.layerCount = 1;
	buffer_copy.imageExtent.depth = 1;
	
	VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	vkBeginCommandBuffer(vk.command_buffer, &begin);
	
	image_barrier.srcAccessMask = 0;
	image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	
	vkCmdPipelineBarrier(vk.command_buffer,
							 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
							 VK_PIPELINE_STAGE_TRANSFER_BIT,
							 0,
							 0, NULL,
							 0, NULL,
							 1, &image_barrier
							 );
	
	static struct bfa_glyph output_frames[16384];
	static uint16_t unicode_to_glyph_map[16384];
	int current_atlas_row = 0;
	int glyphs_written = 0;
	
	for (int char_code = 0; char_code < char_code_max; ++char_code) {
		uint32_t glyph_index = FT_Get_Char_Index(ft_face, char_code);
		int width;
		int height;
		struct bfa_glyph *frame = 
			&output_frames[map_type == MAP_TYPE_UTF16_MAPPED ? glyphs_written : char_code];
		
		if (!glyph_index) continue;
		
		FT_Load_Glyph(ft_face, glyph_index, 0);
		FT_Render_Glyph(glyph_slot, FT_RENDER_MODE_NORMAL);
		
		width = glyph_slot->bitmap.width;
		height = glyph_slot->bitmap.rows;
		
		if ((buffer_copy.imageOffset.x + width) > output_width) {
			buffer_copy.imageOffset.x = 0;
			current_atlas_row++;
		}
		
		frame->x = buffer_copy.imageOffset.x;
		frame->y = current_atlas_row * row_height;
		frame->w = width;
		frame->h = height;
		frame->x_bearing = glyph_slot->metrics.horiBearingX >> 6;
		frame->y_bearing = glyph_slot->metrics.horiBearingY >> 6;
		frame->advance = glyph_slot->metrics.horiAdvance >> 6;
		
		unicode_to_glyph_map[glyphs_written] = char_code;
		glyphs_written++;
		
		// Some glyphs such as " " don't have an image so these 
		// shouldn't be rendered. But the frame needs to be kept for the advance metric.
		if (!width || !height) {
			frame->flags |= 0x1;
			continue;
		}
		
		memcpy(
				   (uint8_t*)vk.staging_buffer_pointer + buffer_copy.bufferOffset,
				   glyph_slot->bitmap.buffer,
				   width * height
				   );
		
		buffer_copy.imageOffset.y = current_atlas_row * row_height;
		buffer_copy.imageExtent.width = width;
		buffer_copy.imageExtent.height = height;
		
		vkCmdCopyBufferToImage(vk.command_buffer, vk.staging_buffer, output_image, 
							   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer_copy);
		
		buffer_copy.imageOffset.x += width;
		buffer_copy.bufferOffset += width * height;
	}
	
	image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	
	vkCmdPipelineBarrier(vk.command_buffer,
						 VK_PIPELINE_STAGE_TRANSFER_BIT,
						 VK_PIPELINE_STAGE_TRANSFER_BIT,
						 0,
						 0, NULL,
						 0, NULL,
						 1, &image_barrier
						 );
	
	vkEndCommandBuffer(vk.command_buffer);
	
	VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &vk.command_buffer;
	
	vkQueueSubmit(vk.queue, 1, &submit, vk.fence);
	vkWaitForFences(vk.device, 1, &vk.fence, VK_TRUE, UINT64_MAX);
	vkResetFences(vk.device, 1, &vk.fence);
	
	const uint32_t final_output_image_size = buffer_copy.bufferOffset;
	
	// ===========================================================================
	// Now copy the image back to CPU memory for writing to the output file
	// ===========================================================================
	vkBeginCommandBuffer(vk.command_buffer, &begin);
	
	buffer_copy.imageOffset.x = 0;
	buffer_copy.imageOffset.y = 0;
	buffer_copy.imageExtent.width = output_width;
	buffer_copy.imageExtent.height = output_height;
	buffer_copy.bufferOffset = 0;
	
	vkCmdCopyImageToBuffer(vk.command_buffer, output_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
							   vk.staging_buffer, 1, &buffer_copy);
	
	vkEndCommandBuffer(vk.command_buffer);
	
	vkQueueSubmit(vk.queue, 1, &submit, vk.fence);
	vkWaitForFences(vk.device, 1, &vk.fence, VK_TRUE, UINT64_MAX);
	vkResetFences(vk.device, 1, &vk.fence);
	
	// ===========================================================================
	// Finally, write the result to the output file
	// ===========================================================================
	struct bfa_header header;
	header.magic = *(uint32_t*)".bfa";
	header.flags = 0;
	header.glyph_count = total_glyph_count;
	header.map_type = map_type;
	header.font_size = requested_font_size;
	header.atlas_width = output_width;
	header.atlas_height = output_height;
	header.size_of_stored_image_data = final_output_image_size;
	
	fwrite(&header, sizeof(header), 1, output_file);
	switch (map_type) {
		case MAP_TYPE_UTF16:
		fwrite(output_frames, 16384 * sizeof(struct bfa_glyph), 1, output_file);
		break;
		
		case MAP_TYPE_ASCII:
		fwrite(output_frames, 256 * sizeof(struct bfa_glyph), 1, output_file);
		break;
		
		case MAP_TYPE_UTF16_MAPPED:
		fwrite(unicode_to_glyph_map, glyphs_written * 2, 1, output_file);
		fwrite(output_frames, glyphs_written * sizeof(struct bfa_glyph), 1, output_file);
		break;
	}
	fwrite(vk.staging_buffer_pointer, final_output_image_size, 1, output_file);
	
	// ===========================================================================
	// Optionally write the output image to a TGA file for debugging
	// ===========================================================================
	if (tga_file_name) {
		typedef struct {
			uint8_t id_field_length;
			uint8_t color_map_type;
			uint8_t image_type;
			uint8_t color_map_spec[5];
			uint16_t x_origin;
			uint16_t y_origin;
			uint16_t width;
			uint16_t height;
			uint8_t pixel_size;
			uint8_t image_descriptor;
			uint8_t image_data[];
		} TGA_Header;
		
		TGA_Header tga = {0};
		tga.image_type = 2;
		tga.width = output_width;
		tga.height = output_height;
		tga.pixel_size = 32;
		tga.image_descriptor = 0x28;
		
		FILE *test_tga = fopen(tga_file_name, "wb");
		fwrite(&tga, sizeof(tga), 1, test_tga);
		for (int y = 0; y < output_height; ++y) {
			for (int x = 0; x < output_width; ++x) {
				uint32_t pixel = ((uint8_t*)vk.staging_buffer_pointer)[(y * output_width) + x];
				pixel <<= 24; // Shift the value to alpha
				fwrite(&pixel, 4, 1, test_tga);
			}
		}
		fclose(test_tga);
	}
}

static void print_usage() {
	printf(
		   "Tool to bake a unicode font into a file.\n"
			   "Usage: bfa [options] <input> <font_size> <output>\n"
			   "Supported formats: TTF, OTF\n"
			   "Options:\n"
			   "    -w, --max-width <dimension>: Maximum width of the image (def. 8192).\n"
			   "                             It is recommended to tweak this value to achieve smaller sized textures by\n"
			   "                             avoiding empty space.\n"
			   "    -h, --max-height <dimension>: Maximum height of the image (def. 8192).\n"
			   "    -t, --output-tga <filename>: Quick and dirty output image to Targa file (overwrites if exists).\n"
			   "    -m, --map-type <type>: The map type. Valid values: ascii, utf16, mapped (default).\n"
		   );
}

int main(int argc, char **argv) {
	if (argc < 4) {
		print_usage();
		return 0;
	}
	
	// Parse arguments
	char *input_path;
	char *output_path;
	
	argv++;
	argc--;
	
	for (; argc > 3; ++argv, --argc) {
		int is_last_arg = argc == 4;
		
		if (!strcmp("--max-width", *argv) || !strcmp("-w", *argv)) {
			if (is_last_arg) {
				printf("Expected number after --max-width\n");
				print_usage();
				return 1;
			}
			max_image_width = atoi(argv[1]);
		}
		else if (!strcmp("--max-height", *argv) || !strcmp("-h", *argv)) {
			if (is_last_arg) {
				printf("Expected number after --max-height\n");
				print_usage();
				return 1;
			}
			max_image_height = atoi(argv[1]);
		}
		else if (!strcmp("--output-tga", *argv) || !strcmp("-t", *argv)) {
			if (is_last_arg) {
				printf("Expected path after --output-tga\n");
				print_usage();
				return 1;
			}
			tga_file_name = argv[1];
		}
		else if (!strcmp("--map-type", *argv) || !strcmp("-m", *argv)) {
			if (is_last_arg) {
				printf("Expected map type after --map-type\n");
				print_usage();
				return 1;
			}
			if (!strcmp(argv[1], "ascii")) {
				map_type = MAP_TYPE_ASCII;
			}
			else if (!strcmp(argv[1], "utf16")) {
				map_type = MAP_TYPE_UTF16;
			}
			else if (!strcmp(argv[1], "mapped")) {
				map_type = MAP_TYPE_UTF16_MAPPED;
			}
			else {
				printf("Invalid map type \"%s\"\n", argv[1]);
				return 1;
			}
		}
	}
	
	input_path = argv[0];
	requested_font_size = atoi(argv[1]);
	output_path = argv[2];
	
	if (requested_font_size <= 0) {
		printf("Invalid font size %s\n", argv[1]);
		printf("Font size must be > 0\n");
		return 1;
	}
	
	FILE *output_file;
	FT_Library freetype;
	FT_Face ft_face;
	
	output_file = fopen(output_path, "wb");
	if (!output_file) {
		perror("Failed to open output file");
		return 1;
	}
	
	FT_Init_FreeType(&freetype);
	
	if (FT_New_Face(freetype, input_path, 0, &ft_face)) {
		printf("FreeType failed to load font (is \"%s\" a valid file?)\n", input_path);
		return 1;
	}
	
	if (FT_Select_Charmap(ft_face, FT_ENCODING_UNICODE)) {
		printf("Font does not have unicode encoding!\n");
		return 1;
	}
	
	FT_Set_Pixel_Sizes(ft_face, requested_font_size, 0);
	
	create_bfa_file(ft_face, output_file);
	printf("Output written to %s\n", output_path);
	
	return 0;
}
	
	