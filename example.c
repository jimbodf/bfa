#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

struct bfa_glyph {
	uint16_t x, y;
	uint16_t w, h;
	int16_t x_bearing, y_bearing;
	int16_t advance;
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
	uint16_t largest_glyph_width;
	uint16_t largest_glyph_height;
	uint32_t size_of_stored_image_data;
};

static const char *map_type_names[] = {
	"utf16",
	"ascii",
	"mapped"
};

static void print_glyph(struct bfa_glyph *glyph) {
	printf(
		   "x: %u\n"
		   "y: %u\n"
		   "w: %u\n"
		   "h: %u\n"
		   "x bearing: %u\n"
		   "y bearing: %u\n"
		   "advance: %u\n"
		   "flags: 0x%x\n",
		   glyph->x, glyph->y, glyph->w, glyph->h,
		   glyph->x_bearing, glyph->y_bearing, glyph->advance,
		   glyph->flags
		   );
}

int main(int argc, char **argv) {
	if (argc < 3) {
		printf("Usage: bfa_glyph <file> <unicode_value>\n");
		return 1;
	}
	
	int input_unicode;
	 long file_buffer_size;
	uint8_t *file_buffer;
	
	input_unicode = atoi(argv[2]);
	if (!input_unicode) {
		printf("Invalid character code \"%s\"\n", argv[2]);
		return 1;
	}
	
	// Read the input file
	FILE *input_file = fopen(argv[1], "rb");
	if (!input_file) {
		perror("Failed to open input file");
		return 1;
	}
	
	fseek(input_file, 0, SEEK_END);
	file_buffer_size = ftell(input_file);
	file_buffer = malloc(file_buffer_size);
	rewind(input_file);
	
	fread(file_buffer, 1, file_buffer_size, input_file);
	fclose(input_file);
	
	// Get the glyph
	struct bfa_header *header = (struct bfa_header*)file_buffer;
	uint16_t *glyph_codes;
	struct bfa_glyph *glyphs;
	
	printf("File size: %u\n", file_buffer_size);
	printf(
			   "Magic: 0x%x\n"
			   "No. Glyphs: %u\n"
			   "Map type: %s\n"
			   "Font size: %u\n"
			   "Atlas width: %u\n"
			   "Atlas height: %u\n"
			   "Size of stored image data: %g MB\n\n",
			   header->magic,
			   header->glyph_count,
			   map_type_names[header->map_type],
			   header->font_size,
			   header->atlas_width,
			   header->atlas_height,
			   header->size_of_stored_image_data / (double)(1<<20)
			   );
	
	if (header->map_type == 2) {
		glyph_codes = (uint16_t*)&file_buffer[sizeof(struct bfa_header)];
		glyphs = (struct bfa_glyph*)&file_buffer[sizeof(struct bfa_header) + (header->glyph_count * sizeof(uint16_t))];
		
		for (int i = 0; i < header->glyph_count; ++i) {
			if (glyph_codes[i] == input_unicode) {
				struct bfa_glyph *glyph = &glyphs[i];
				print_glyph(glyph);
				goto END;
			}
		}
		
		printf("Glyph not found\n");
	}
	else {
		int max_unicode = header->map_type == 0 ? 0xffff : 0xff;
		
		if (input_unicode > max_unicode) {
			printf("Unicode value 0x%x out of range.\n", input_unicode);
			return 1;
		}
		
		glyph_codes = NULL;
		glyphs = (struct bfa_glyph*)&file_buffer[sizeof(struct bfa_header)];
		struct bfa_glyph *glyph = &glyphs[input_unicode];
		print_glyph(glyph);
	}
	
	END:
	free(file_buffer);
	
	return 0;
}
	
	