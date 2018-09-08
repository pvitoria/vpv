#include <errno.h>

extern "C" {
#include "iio.h"
}

#include "Image.hpp"
#include "editors.hpp"
#include "ImageProvider.hpp"

void IIOFileImageProvider::progress()
{
    int w, h, d;
    float* pixels = iio_read_image_float_vec(filename.c_str(), &w, &h, &d);
    if (!pixels) {
        onFinish(makeError("cannot load image '" + filename + "'"));
        return;
    }

    if (d > 4) {
        printf("warning: '%s' has %d channels, extracting the first four\n", filename.c_str(), d);
        for (size_t y = 0; y < (size_t)h; y++) {
            for (size_t x = 0; x < (size_t)w; x++) {
                for (size_t l = 0; l < 4; l++) {
                    pixels[(y*h+x)*4+l] = pixels[(y*h+x)*d+l];
                }
            }
        }
        d = 4;
    }
    std::shared_ptr<Image> image = std::make_shared<Image>(pixels, w, h, d);
    onFinish(image);
    mark(image);
}

#include <jpeglib.h>

static void onJPEGError(j_common_ptr cinfo)
{
    char buf[JMSG_LENGTH_MAX];
    JPEGFileImageProvider* provider = (JPEGFileImageProvider*) cinfo->client_data;
    (*cinfo->err->format_message)(cinfo, buf);
    provider->onJPEGError(buf);
}

JPEGFileImageProvider::~JPEGFileImageProvider()
{
    if (file) {
        fclose(file);
    }
    if (cinfo) {
        jpeg_abort((j_common_ptr) cinfo);
        delete cinfo;
    }
    if (jerr) {
        delete jerr;
    }
    if (pixels) {
        free(pixels);
    }
    if (scanline) {
        delete[] scanline;
    }
}

void JPEGFileImageProvider::onJPEGError(const std::string& error)
{
    onFinish(makeError(error));
    this->error = true;
}

float JPEGFileImageProvider::getProgressPercentage() const {
    if (cinfo) {
        return (float) cinfo->output_scanline / cinfo->output_height;
    } else {
        return 0.f;
    }
}

void JPEGFileImageProvider::progress()
{
    assert(!error);
    if (!cinfo) {
        file = fopen(filename.c_str(), "rb");
        if (!file) {
            onFinish(makeError(strerror(errno)));
            return;
        }
        cinfo = new struct jpeg_decompress_struct;
        cinfo->client_data = this;
        jerr = new jpeg_error_mgr;
        cinfo->err = jpeg_std_error(jerr);
        jerr->error_exit = ::onJPEGError;
        jpeg_create_decompress(cinfo);
        if (error) return;

        jpeg_stdio_src(cinfo, file);
        if (error) return;

        jpeg_read_header(cinfo, TRUE);
        if (error) return;

        jpeg_start_decompress(cinfo);
        if (error) return;

        pixels = (float*) malloc(sizeof(float)*cinfo->output_width*cinfo->output_height*cinfo->output_components);
        scanline = new unsigned char[cinfo->output_width*cinfo->output_components];
    } else if (cinfo->output_scanline < cinfo->output_height) {
        jpeg_read_scanlines(cinfo, &scanline, 1);
        if (error) return;
        size_t rowwidth = cinfo->output_width*cinfo->output_components;
        for (size_t j = 0; j < rowwidth; j++) {
            pixels[(size_t)(cinfo->output_scanline-1)*rowwidth + j] = scanline[j];
        }
    } else {
        jpeg_finish_decompress(cinfo);
        if (error) return;

        std::shared_ptr<Image> image = std::make_shared<Image>(pixels,
                               cinfo->output_width, cinfo->output_height, cinfo->output_components);
        onFinish(image);
        mark(image);
        pixels = nullptr;
    }
}

#include <png.h>

struct PNGPrivate {
    PNGFileImageProvider* provider;

    FILE* file;
    png_structp png_ptr;
    png_infop info_ptr;
    uint32_t width, height;
    int channels;
    int depth;
    uint32_t cur;
    float* pixels;
    png_bytep pngframe;

    uint32_t length;
    unsigned char* buffer;

    PNGPrivate(PNGFileImageProvider* provider)
        : provider(provider), file(nullptr), png_ptr(nullptr), info_ptr(nullptr),
          height(0), pixels(nullptr), pngframe(nullptr),  buffer(nullptr)
    {}

    ~PNGPrivate() {
        if (png_ptr) {
            png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        }
        if (pngframe) {
            free(pngframe);
        }
        if (pixels) {
            free(pixels);
        }
        if (buffer) {
            free(buffer);
        }
        if (file) {
            fclose(file);
        }
    }

    void info_callback()
    {
        width = png_get_image_width(png_ptr, info_ptr);
        height = png_get_image_height(png_ptr, info_ptr);
        channels = png_get_channels(png_ptr, info_ptr);
        depth = png_get_bit_depth(png_ptr, info_ptr);
        pixels = (float*) malloc(sizeof(float)*width*height*channels);
        pngframe = (png_bytep) malloc(sizeof(*pngframe) * width*height*channels*depth/8);

        png_start_read_image(png_ptr);
    }

    void row_callback(png_bytep new_row, png_uint_32 row_num, int pass)
    {
        if (new_row) {
            png_progressive_combine_row(png_ptr, pngframe+row_num*width*channels*depth/8, new_row);
        }
        cur = row_num;
    }

    void end_callback()
    {
    }

    std::shared_ptr<Image> getImage()
    {
        switch (depth) {
            case 1:
            case 8:
                for (size_t i = 0; i < width*height*channels; i++) {
                    pixels[i] = *(pngframe + i);
                }
                break;
            case 16:
                for (size_t i = 0; i < width*height*channels; i++) {
                    png_byte *b = (pngframe + i * 2);
                    std::swap(b[0], b[1]);
                    uint16_t sample = *(uint16_t *)b;
                    pixels[i] = sample;
                }
                break;
            default:
                return nullptr;
        }

        auto img = std::make_shared<Image>(pixels, width, height, channels);
        pixels = nullptr;
        return img;
    }
};

PNGFileImageProvider::~PNGFileImageProvider()
{
    if (p) delete p;
}

float PNGFileImageProvider::getProgressPercentage() const
{
    if (!p || p->height == 0)
        return 0.f;
    return (float) p->cur / p->height;
}

static void on_error(png_structp pp, const char* msg)
{
    void* userdata = png_get_progressive_ptr(pp);
    PNGPrivate* p = (PNGPrivate*) userdata;
    p->provider->onPNGError(msg);
}

static void info_callback(png_structp png_ptr, png_infop info)
{
    void* userdata = png_get_progressive_ptr(png_ptr);
    PNGPrivate* p = (PNGPrivate*) userdata;
    p->info_callback();
}

static void row_callback(png_structp png_ptr, png_bytep new_row,
                         png_uint_32 row_num, int pass)
{
    void* userdata = png_get_progressive_ptr(png_ptr);
    PNGPrivate* p = (PNGPrivate*) userdata;
    p->row_callback(new_row, row_num, pass);
}

static void end_callback(png_structp png_ptr, png_infop info)
{
    void* userdata = png_get_progressive_ptr(png_ptr);
    PNGPrivate* p = (PNGPrivate*) userdata;
    p->end_callback();
}


int PNGFileImageProvider::initialize_png_reader()
{
    p->png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp) p, on_error, NULL);
    if (!p->png_ptr)
        return 1;

    p->info_ptr = png_create_info_struct(p->png_ptr);
    if (!p->info_ptr) {
        png_destroy_read_struct(&p->png_ptr, (png_infopp)NULL,
                                (png_infopp)NULL);
        return 1;
    }

    if (setjmp(png_jmpbuf(p->png_ptr))) {
        return 2;
    }

    // see http://www.libpng.org/pub/png/libpng-1.2.5-manual.html#section-3.10
    png_set_progressive_read_fn(p->png_ptr, p, ::info_callback, ::row_callback, ::end_callback);
    return 0;
}

void PNGFileImageProvider::progress()
{
    if (!p) {
        p = new PNGPrivate(this);
        p->file = fopen(filename.c_str(), "rb");
        if (!p->file) {
            onFinish(makeError(strerror(errno)));
            return;
        }

        int ret = initialize_png_reader();
        if (ret != 0) {
            onFinish(makeError("cannot initialize png reader"));
            return;
        }

        p->length = 1<<12;
        p->buffer = (png_bytep) malloc(sizeof(*p->buffer) * p->length);
        p->cur = 0;
    } else if (!feof(p->file)) {
        int read = fread(p->buffer, 1, p->length, p->file);

        if (ferror(p->file)) {
            onFinish(makeError(strerror(errno)));
            return;
        }

        if (setjmp(png_jmpbuf(p->png_ptr))) {
            return;
        }

        png_process_data(p->png_ptr, p->info_ptr, p->buffer, read);
    } else {
        std::shared_ptr<Image> image = p->getImage();
        if (!image) {
            onFinish(makeError("error png invalid depth(?)"));
        } else {
            onFinish(image);
            mark(image);
        }
    }
}

void PNGFileImageProvider::onPNGError(const std::string& error)
{
    onFinish(makeError(error));
    longjmp(png_jmpbuf(p->png_ptr), 1);
}


#include <tiffio.h>

struct TIFFPrivate {
    TIFFFileImageProvider* provider;
    TIFF* tif;
    uint32_t w, h;
    uint16_t spp, bps, fmt;
    uint8_t* data;
    uint8_t* buf;
    bool broken;
    uint32_t curh;
    int sls;

    TIFFPrivate(TIFFFileImageProvider* provider)
        : provider(provider), tif(nullptr), h(0), data(nullptr), buf(nullptr), curh(0)
    {
    }

    ~TIFFPrivate()
    {
        if (tif) {
            TIFFClose(tif);
        }
    }
};

TIFFFileImageProvider::~TIFFFileImageProvider()
{
    if (p) {
        delete p;
    }
}

float TIFFFileImageProvider::getProgressPercentage() const
{
    if (p && p->h)
        return (float) p->curh / p->h;
    return 0.f;
}

void TIFFFileImageProvider::progress()
{
    if (!p) {
        p = new TIFFPrivate(this);
        p->tif = TIFFOpen(filename.c_str(), "rm");
        if (!p->tif) return onFinish(makeError("cannot read tiff " + filename));

        int r = 0;
        r += TIFFGetField(p->tif, TIFFTAG_IMAGEWIDTH, &p->w);
        r += TIFFGetField(p->tif, TIFFTAG_IMAGELENGTH, &p->h);

        if (r != 2) return onFinish(makeError("can not read tiff of unknown size"));

        r = TIFFGetField(p->tif, TIFFTAG_SAMPLESPERPIXEL, &p->spp);
        if (!r)
            p->spp=1;

        r = TIFFGetField(p->tif, TIFFTAG_BITSPERSAMPLE, &p->bps);
        if (!r)
            p->bps=1;

        r = TIFFGetField(p->tif, TIFFTAG_SAMPLEFORMAT, &p->fmt);
        if (!r)
            p->fmt = SAMPLEFORMAT_UINT;

        if (p->fmt == SAMPLEFORMAT_COMPLEXINT || p->fmt == SAMPLEFORMAT_COMPLEXIEEEFP) {
            p->spp *= 2;
            p->bps /= 2;
        }
        if (p->fmt == SAMPLEFORMAT_COMPLEXINT)
            p->fmt = SAMPLEFORMAT_INT;
        if (p->fmt == SAMPLEFORMAT_COMPLEXIEEEFP)
            p->fmt = SAMPLEFORMAT_IEEEFP;

        uint16_t planarity;
        r = TIFFGetField(p->tif, TIFFTAG_PLANARCONFIG, &planarity);
        if (r != 1) planarity = PLANARCONFIG_CONTIG;
        p->broken = planarity == PLANARCONFIG_SEPARATE;

        uint32_t scanline_size = (p->w * p->spp * p->bps)/8;
        int rbps = (p->bps/8) ? (p->bps/8) : 1;
        p->sls = TIFFScanlineSize(p->tif);
        if ((int)scanline_size != p->sls)
            fprintf(stderr, "scanline_size,sls = %d,%d\n", (int)scanline_size, p->sls);
        //assert((int)scanline_size == sls);
        if (!p->broken)
            assert((int)scanline_size == p->sls);
        else
            assert((int)scanline_size == p->spp*p->sls);
        assert((int)scanline_size >= p->sls);
        p->data = (uint8_t*) malloc(p->w * p->h * p->spp * rbps);
        p->buf = (uint8_t*) malloc(scanline_size);
        p->curh = 0;
        assert(!p->broken);
    } else if (p->curh < p->h) {
        int r = TIFFReadScanline(p->tif, p->buf, p->curh);
        if (r < 0) onFinish(makeError("error reading tiff row " + std::to_string(p->curh)));
        memcpy(p->data + p->curh * p->sls, p->buf, p->sls);
        p->curh++;
        // TODO: handle broken case
    } else {
        onFinish(std::make_shared<Image>((float*)p->data, p->w, p->h, p->spp));
    }
}

void EditedImageProvider::progress() {
    for (auto p : providers) {
        if (!p->isLoaded()) {
            p->progress();
            return;
        }
    }
    std::vector<std::shared_ptr<Image>> images;
    for (auto p : providers) {
        Result result = p->getResult();
        if (result.has_value()) {
            std::shared_ptr<Image> image = result.value();
            images.push_back(image);
            mark(image);
        } else {
            onFinish(result);
            return;
        }
    }
    std::shared_ptr<Image> image = edit_images(edittype, editprog, images);
    if (image) {
        onFinish(image);
        mark(image);
    } else {
        onFinish(makeError("cannot edit..."));
    }
}
