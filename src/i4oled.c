/*
 * Copyright © 2012 Przemo Firszt
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of Red Hat
 * not be used in advertising or publicity pertaining to distribution
 * of the software without specific, written prior permission.  Red
 * Hat makes no representations about the suitability of this software
 * for any purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THE AUTHORS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *      Przemo Firszt (przemo@firszt.eu)
 */

#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <locale.h>
#include <png.h>
#include <pango/pangocairo.h>
#include <pango/pango.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wchar.h>

#define VER "0.3"
#define SIZE 30
#define MAX_LEN 11

struct params_s {
	char* device_filename;
	char* image_filename;
	char* output_filename;
	unsigned char* image;
	int scramble_image;
	wchar_t text[SIZE+1];
};

void i4oled_split_text(wchar_t *source, char* line1, char* line2)
{
	wchar_t buf[SIZE+1];
	wchar_t delimiters[SIZE+1] = L" -+_";
	wchar_t wcsline1[SIZE+1] = L"";
	wchar_t wcsline2[SIZE+1] = L"";
	wchar_t* token;
	wchar_t* state;
	int i;
	int token_len[SIZE >> 1]; /*Maximum number of tokens equals half of maximum number of characters*/
	size_t length, l;

	wcscpy(buf, source);
	token = wcstok(buf, delimiters, &state);

	if (wcslen(token) > MAX_LEN) {
		wcsncpy(wcsline1, source, MAX_LEN);
		wcsncpy(wcsline2, source + MAX_LEN, SIZE - MAX_LEN);
		goto out;
	}

	for (i = 0; i < 10; i++)
		token_len[i] = 0;

	i = 0;
	while (token) {
		token_len[i] = wcslen(token) + 1;
		i++;
		token = wcstok(NULL, delimiters, &state);
	}

	i = 0;
	length = token_len[i];
	while ((length + token_len[i + 1]) <= MAX_LEN) {
		i++;
		length = length + token_len[i];
	}

	wcsncpy(wcsline1, source, length - 1);
	wcsncpy(wcsline2, source + length, SIZE - length);
out:
	l = wcstombs(line1, wcsline1, MAX_LEN);
	if (l == -1) {
		wprintf(L"Invalid character sequance - please try a different text\n");
	}

	l = wcstombs(line2, wcsline2, wcslen(wcsline2) + 1);
	if (l == -1) {
		wprintf(L"Invalid character sequance - please try a different text\n");
	}
}

void i4oled_text_to_image(struct params_s* params, cairo_surface_t *surface)
{
	unsigned char* csurf;
	int i, x, y;
	unsigned char lo, hi;

	cairo_surface_flush(surface);
	csurf = cairo_image_surface_get_data(surface);
	i = 0;
        for (y = 0; y < 32; y++) {
                for (x = 0; x < (64 >> 1); x++) {
			hi = 0xf0 & csurf[256 * y + 8 * x + 1];
			lo = 0x0f & (csurf[256 * y + 8 * x + 5] >> 4);
			params->image[i] = hi | lo;
			i++;
                }
        }
}

int i4oled_render_text(struct params_s* params) 
{
	cairo_t *cr;
	cairo_status_t status;
	cairo_surface_t *surface;
	PangoFontDescription *desc;
	PangoLayout *layout;
	int width, height;
	double dx, dy;
	int x, y;
	char line1[SIZE+1] = "";
	char line2[SIZE+1] = "";
	char buf[SIZE+1];

	i4oled_split_text(params->text ,line1, line2);
	strcpy(buf, line1);
	strcat(buf, "\n");
	strcat(buf, line2);

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 32);
	cr = cairo_create(surface);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.99);
	cairo_paint(cr);

	layout = pango_cairo_create_layout(cr);
	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	pango_layout_set_text(layout, buf, -1);
	desc = pango_font_description_new();

	pango_font_description_set_family(desc, "Terminal");
	pango_font_description_set_absolute_size(desc, PANGO_SCALE * 11);
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);

	pango_layout_get_size(layout, &width, &height);
	width = width/PANGO_SCALE;
	cairo_new_path(cr);

	dx = trunc((64.0 - width)/2);

	if (!strcmp(line2, ""))
		dy = 10;
	else
		dy = 4;

	cairo_move_to(cr, dx, dy);
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
	pango_cairo_update_layout(cr, layout);

	pango_cairo_layout_path(cr, layout);
	cairo_fill(cr);

	if (params->device_filename)
		i4oled_text_to_image(params, surface);

	g_object_unref(layout);
	cairo_destroy(cr);

	if (params->output_filename) {
		status = cairo_surface_write_to_png(surface, params->output_filename);
		if (status != CAIRO_STATUS_SUCCESS) {
			printf("Could not save to png, \"%s \"\n", params->output_filename);
			return 1;
		}
	}

	cairo_surface_destroy(surface);
	return 0;
}

static int i4oled_read_image(struct params_s* params)
{
	unsigned char header[8];
	unsigned char lo, hi;
	int x, y, i;
	int ret = 0;
	int width, height;
	png_byte color_type;
	png_byte bit_depth;
	png_structp png_ptr;
	png_infop info_ptr;
	png_bytep * row_pointers;

	FILE *fd = fopen(params->image_filename, "r");
	if (!fd) {
		ret = 1;
		printf("Failed to open params->filename: %s\n", params->image_filename);
		goto out;
	}

        fread(header, 1, 8, fd);
        if (png_sig_cmp(header, 0, 8)) {
		ret = 1;
                printf("File %s is not a PNG file", params->image_filename);
		goto out;
	}

        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

        if (!png_ptr) {
		ret = 1;
                printf("png_create_read_struct failed");
		goto out;
	}

        info_ptr = png_create_info_struct(png_ptr);
        if (!info_ptr) {
		ret = 1;
                printf("png_create_info_struct failed");
		goto out;
	}

        if (setjmp(png_jmpbuf(png_ptr))) {
		ret = 1;
                printf("Error during init_io");
		goto out;
	}

        png_init_io(png_ptr, fd);
        png_set_sig_bytes(png_ptr, 8);

        png_read_info(png_ptr, info_ptr);

        width = png_get_image_width(png_ptr, info_ptr);
        height = png_get_image_height(png_ptr, info_ptr);
        color_type = png_get_color_type(png_ptr, info_ptr);
        bit_depth = png_get_bit_depth(png_ptr, info_ptr);

	if (width != 64 || height !=32) {
		ret = 1;
                printf("Invalid params->image size: %dx%d, but expecting 64x32\n", width, height);
		goto out;
	}

	if (color_type != 6 || bit_depth !=8) {
		ret = 1;
                printf("Invalid color type or bit depth, please use RGBA 8-bit png\n"
			"Use 'file' command on the icon. Expected result:\n"
			"PNG params->image data, 64 x 32, 8-bit/color RGBA, non-interlaced\n");
		goto out;
	}

        png_read_update_info(png_ptr, info_ptr);

        if (setjmp(png_jmpbuf(png_ptr))) {
		ret = 1;
                printf("Error reading params->image");
		goto out;
	}

        row_pointers = (png_bytep*) malloc(sizeof(png_bytep) * height);
        for (y = 0; y < height; y++)
                row_pointers[y] = (png_byte*) malloc(png_get_rowbytes(png_ptr,info_ptr));

        png_read_image(png_ptr, row_pointers);
	png_set_strip_16(png_ptr);
	png_set_packing(png_ptr);

	i = 0;
        for (y = 0; y < height; y++) {
                png_byte* row = row_pointers[y];
                for (x = 0; x < (width >> 1) ; x++) {
                        png_byte* ptr = &(row[x * 8]);
			hi = 0xf0 & ptr[0];
			lo = 0x0f & (ptr[4] >> 4);
			params->image[i++] = hi | lo;
                }
        }

out:
	if (fd)
		fclose(fd);
	return ret;
}

static int i4oled_oled_write(struct params_s* params)
{
	int retval;
	int length = 1024;
	int fd = -1;
	int ret = 0;

	fd = open (params->device_filename , O_WRONLY);
	if (fd < 0) {
		ret = 1;
		printf("Failed to open filename: %s\n", params->device_filename);
		goto out;
	}

	retval = write (fd, params->image, length);
	if (retval != length) {
		ret = 1;
		printf("Writing to %s failed\n", params->device_filename);
		goto out;
	}
out:
	if (fd >= 0)
 		close (fd);
	return ret;
}

static void i4oled_scramble(struct params_s* params)
{
        unsigned char buf[1024];
        int x, y, i;
	unsigned char l1,l2,h1,h2;

		wprintf(L"SCRAMBLING\n");
        for (i = 0; i < 1024; i++)
                buf[i] = params->image[i];

        for (y = 0; y < 16; y++) {
                for (x = 0; x < 32; x++) {
			l1 = (0x0F & (buf[31 - x + 64 * y]));
			l2 = (0x0F & (buf[31 - x + 64 * y] >> 4));
			h1 = (0xF0 & (buf[63 - x + 64 * y] << 4));
			h2 = (0xF0 & (buf[63 - x + 64 * y]));

                        params->image[(2 * x) + (64 * y)] = h1 | l1;
                        params->image[(2 * x) + 1 + (64 * y)] = h2 | l2;
                }
        }
}

static void i4oled_version(void)
{
	printf("%s\n", VER);
}

static void i4oled_usage(void)
{
	printf(
	"\n"
	"i4oled sets OLED icon on Intuos4 tablets. Also converts text to png image ready for use as Intuos4 OLED icon.\n"
	"Usage: i4oled [options] [device image]\n"
	"Options:\n"
	" -h, --help                 - usage\n"
	" -d, --device               - path to OLED sysfs entry\n"
	" -o, --output         	     - output png file\n"
	" -s, --scramble             - scramble image before sending. Useful for kernel without the 'scramble' patch\n"
	" -t, --text         	     - text string for convertsion into image\n"
	" -V, --version              - version info\n");

	printf(
	"Usage:\n"
	"i4oled --text \"Ctrl+Alt A\" --output ctrl_alt_A.png renders text to PNG image\n"
	"i4oled --image ctrl_alt_A --device  /sys/bus/usb/drivers/wacom/3-1.2:1.0/wacom_led/button0_rawimg\n"
	"Make sure OLED brightness is set, otherwise icons will be black\n"
	"echo 200 > /sys/bus/usb/drivers/wacom/3-1.2:1.0/wacom_led/buttons_luminance\n"
	"Expected image format is:\n"
	"PNG image file, has to be 64 x 32, 8-bit/color RGBA, non-interlaced \n");
}

int i4oled_acquire_text(struct params_s* params, char* char_text)
{
	int l, length;
	l = strlen(char_text);

	if (l > SIZE) {
		wprintf(L"Text too long: %d characters, but maximum accepted length is %d\n", l, SIZE);
		return 1;
	}

	length = mbstowcs(params->text, char_text, l + 1);

	if (length == -1) {
		wprintf(L"Invalid character sequance - please try a different text\n");
		return 1;
	}
	return 0;
}

int main (int argc, char **argv)
{
	int c, ret = 0;
	int optidx;
	struct params_s params;
	struct option options[] = {
		{"help", 0, NULL, 0},
		{"device", 0, NULL, 0},
		{"image", 0, NULL, 0},
		{"output", 0, NULL, 0},
		{"scramble", 0, NULL, 0},
		{"text", 0, NULL, 0},
		{"version", 0, NULL, 0},
		{NULL, 0, NULL, 0}
	};

	if (!setlocale(LC_CTYPE, "")) {
		fprintf(stderr, "Can't set the specified locale! "
			"Check LANG, LC_CTYPE, LC_ALL.\n");
		return 1;
	}

	params.device_filename= NULL;
	params.image_filename = NULL;
	params.output_filename = NULL;
	params.image = malloc(1024);
	params.text[0] = (wchar_t)0x0;
	params.scramble_image = 0;

	if (argc < 2) {
		i4oled_usage();
		ret = 1;
		goto out;
	}

	while ((c = getopt_long(argc, argv, "hd:i:o:st:V", options, &optidx)) != -1) {
		switch (c) {
		case 0:
			switch(optidx){
			case 0:
				i4oled_usage();
				ret = 0;
				goto out;
			case 1:
				params.device_filename = argv[optind];
				break;
			case 2:
				params.image_filename = argv[optind];
				break;
			case 3:
				params.output_filename = argv[optind];
				break;
			case 4:
				params.scramble_image = 1;
				break;
			case 5:
				if (i4oled_acquire_text(&params, argv[optind])) {
					ret = 1;
					goto out;
				}
				break;
			case 6:
				i4oled_version();
				ret = 0;
				goto out;
		}
		break;
		case 'd':
			params.device_filename = argv[optind-1];
			break;
		case 'i':
			params.image_filename = argv[optind-1];
			break;
		case 'o':
			params.output_filename = argv[optind-1];
			break;
		case 's':
			params.scramble_image = 1;
			break;
		case 't':
			if (i4oled_acquire_text(&params, argv[optind-1])) {
				ret = 1;
				goto out;	
			}
			break;
		case 'V':
			i4oled_version();
			ret = 0;
			goto out;
		case 'h':
		default:
			i4oled_usage();
			ret = 0;
			goto out;
		}
	}

	if (params.image_filename)
		if (i4oled_read_image(&params))
			goto out;

	if (wcscmp(params.text, L"")) {
		if (i4oled_render_text(&params)) {
			ret = 1;
			goto out;
		}
	}

	if (params.scramble_image)
		i4oled_scramble(&params);

	if (params.device_filename){
		ret = i4oled_oled_write(&params);
	}

out:
	free(params.image);
	return ret;
}

