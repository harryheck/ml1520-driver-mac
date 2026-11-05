// ml1520-print: prints PWG raster on a Samsung ML-1520 over USB.
//
// ippeveprinter runs this once per job. It needs no PPD, CUPS filter or CUPS backend.
// Page geometry, bands and PJL follow SpliX 2.0.2 (document.cpp, compress.cpp, qpdl.cpp,
// printer.cpp), and the 0x11 compressor is the unmodified SpliX code. tests/run.sh checks
// the output byte for byte against SpliX.
//
// Usage: ml1520-print [--output FILE] RASTER_FILE
//
// Based on SpliX, (C) 2006-2008 Aurelien Croc (AP2C) and contributors.
// SPDX-License-Identifier: GPL-2.0-only

#include "algo0x11.h"
#include "bandplane.h"

#include <libusb.h>

#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace {

const uint16_t kVendorId = 0x04e8;
const uint16_t kProductId = 0x3256;
const int kPrinterInterfaceClass = 7;

// How long a job waits for a printer that is switched off.
const int kDeviceWaitSeconds = 300;
// How long the printer may refuse data (paper jam, no paper) before the job is aborted.
const int kStallLimitSeconds = 1800;
const unsigned kUsbTimeoutMs = 5000;
const size_t kUsbChunkBytes = 16384;

// Values from ml1520.ppd
const unsigned long kBandHeight = 128;
const double kHardMarginXPoints = 10.75;
const double kHardMarginYPoints = 15;
const unsigned char kQpdlVersion = 1;
const unsigned char kCompression = 0x11;
const unsigned char kPaperSourceAuto = 1;
const unsigned char kSimplexDuplexByte = 1;

const int kCupsSpaceK = 3;
const size_t kRasterHeaderBytes = 1796;
const double kPaperMatchTolerancePoints = 4;

struct Paper {
    const char *name;
    unsigned char code;
    double width;
    double height;
};

// Matches media-supported in ml1520.conf. Sizes and codes come from ml1520.ppd and SpliX.
const Paper kPapers[] = {
    {"A4", 2, 595, 842},        {"A5", 16, 420, 595},       {"A6", 17, 297, 420},
    {"B5", 11, 516, 729},       {"B6", 18, 363, 516},       {"Letter", 0, 612, 792},
    {"Legal", 1, 612, 1008},    {"Executive", 3, 522, 756}, {"DL", 9, 312, 624},
    {"C5", 8, 459, 649},        {"C6", 23, 323, 459},       {"Env10", 6, 297, 684},
    {"Monarch", 7, 279, 540},
};

volatile sig_atomic_t gCancelled = 0;

void onCancel(int) { gCancelled = 1; }

__attribute__((noreturn, format(printf, 1, 2))) void die(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("ERROR: ", stderr);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
    exit(1);
}

class Output {
public:
    virtual ~Output() {}
    virtual void write(const void *data, size_t size) = 0;
    void write(const std::string &text) { write(text.data(), text.size()); }
};

class FileOutput : public Output {
public:
    explicit FileOutput(const char *path) : file_(fopen(path, "wb")) {
        if (!file_)
            die("Cannot open output file %s", path);
    }
    ~FileOutput() { fclose(file_); }
    void write(const void *data, size_t size) {
        if (fwrite(data, 1, size, file_) != size)
            die("Cannot write to output file");
    }

private:
    FILE *file_;
};

class UsbOutput : public Output {
public:
    UsbOutput() : context_(NULL), handle_(NULL), interface_(-1), endpoint_(0) {
        int result = libusb_init(&context_);
        if (result != 0)
            die("Cannot initialize USB: %s", libusb_error_name(result));

        time_t start = time(NULL);
        bool announced = false;
        while (!tryOpen()) {
            if (gCancelled)
                exit(0);
            if (time(NULL) - start >= kDeviceWaitSeconds)
                die("Printer not found. Is the ML-1520 switched on and connected over USB?");
            if (!announced) {
                fputs("INFO: Waiting for the printer\n", stderr);
                announced = true;
            }
            sleep(2);
        }
    }

    ~UsbOutput() {
        if (handle_) {
            libusb_release_interface(handle_, interface_);
            libusb_close(handle_);
        }
        if (context_)
            libusb_exit(context_);
    }

    void write(const void *data, size_t size) {
        unsigned char *bytes = (unsigned char *)data;
        size_t done = 0;
        time_t lastProgress = time(NULL);
        bool warned = false;

        while (done < size) {
            size_t chunk = size - done < kUsbChunkBytes ? size - done : kUsbChunkBytes;
            int sent = 0;
            int result = libusb_bulk_transfer(handle_, endpoint_, bytes + done, (int)chunk, &sent,
                                              kUsbTimeoutMs);
            done += (size_t)sent;
            if (sent > 0)
                lastProgress = time(NULL);

            if (result == 0)
                continue;
            if (result == LIBUSB_ERROR_PIPE) {
                libusb_clear_halt(handle_, endpoint_);
                continue;
            }
            if (result != LIBUSB_ERROR_TIMEOUT)
                die("USB transfer failed: %s", libusb_error_name(result));

            long stalled = (long)(time(NULL) - lastProgress);
            if (stalled >= kStallLimitSeconds)
                die("The printer stopped accepting data. Check for a paper jam or empty tray.");
            if (stalled >= 30 && !warned) {
                fputs("INFO: The printer is not accepting data right now\n", stderr);
                warned = true;
            }
        }
    }

private:
    bool tryOpen() {
        libusb_device_handle *handle =
            libusb_open_device_with_vid_pid(context_, kVendorId, kProductId);
        if (!handle)
            return false;

        int interfaceNumber = -1;
        unsigned char endpoint = 0;
        libusb_config_descriptor *config = NULL;
        if (libusb_get_active_config_descriptor(libusb_get_device(handle), &config) == 0) {
            for (int i = 0; i < config->bNumInterfaces && interfaceNumber < 0; i++) {
                const libusb_interface_descriptor &alt = config->interface[i].altsetting[0];
                if (alt.bInterfaceClass != kPrinterInterfaceClass)
                    continue;
                for (int e = 0; e < alt.bNumEndpoints; e++) {
                    const libusb_endpoint_descriptor &ep = alt.endpoint[e];
                    bool isBulk = (ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
                                  LIBUSB_TRANSFER_TYPE_BULK;
                    bool isOut = (ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
                                 LIBUSB_ENDPOINT_OUT;
                    if (isBulk && isOut) {
                        interfaceNumber = alt.bInterfaceNumber;
                        endpoint = ep.bEndpointAddress;
                        break;
                    }
                }
            }
            libusb_free_config_descriptor(config);
        }

        if (interfaceNumber < 0 || libusb_claim_interface(handle, interfaceNumber) != 0) {
            libusb_close(handle);
            return false;
        }
        handle_ = handle;
        interface_ = interfaceNumber;
        endpoint_ = endpoint;
        return true;
    }

    libusb_context *context_;
    libusb_device_handle *handle_;
    int interface_;
    unsigned char endpoint_;
};

struct PageHeader {
    unsigned long xResolution;
    unsigned long yResolution;
    unsigned long marginX;
    unsigned long marginY;
    unsigned long pageWidth;
    unsigned long pageHeight;
    unsigned long width;
    unsigned long height;
    unsigned long bitsPerPixel;
    unsigned long bytesPerLine;
    unsigned long colorSpace;
};

// Reads CUPS raster v2 (compressed, also used by PWG raster) and v3 (uncompressed).
class RasterReader {
public:
    explicit RasterReader(const char *path)
        : file_(fopen(path, "rb")), bigEndian_(false), compressed_(false), repeatLeft_(0) {
        if (!file_)
            die("Cannot read print data %s", path);
        char sync[4];
        if (fread(sync, 1, 4, file_) != 4)
            die("Print data is empty");
        if (!memcmp(sync, "RaS2", 4)) {
            bigEndian_ = true;
            compressed_ = true;
        } else if (!memcmp(sync, "2SaR", 4)) {
            bigEndian_ = false;
            compressed_ = true;
        } else if (!memcmp(sync, "RaS3", 4)) {
            bigEndian_ = true;
            compressed_ = false;
        } else if (!memcmp(sync, "3SaR", 4)) {
            bigEndian_ = false;
            compressed_ = false;
        } else {
            die("Unknown data format, expected PWG raster");
        }
    }

    ~RasterReader() { fclose(file_); }

    bool nextPage(PageHeader &page) {
        unsigned char h[kRasterHeaderBytes];
        size_t got = fread(h, 1, kRasterHeaderBytes, file_);
        if (got == 0)
            return false;
        if (got != kRasterHeaderBytes)
            die("Page header is incomplete");

        page.xResolution = field(h, 276);
        page.yResolution = field(h, 280);
        page.marginX = field(h, 312);
        page.marginY = field(h, 316);
        page.pageWidth = field(h, 352);
        page.pageHeight = field(h, 356);
        page.width = field(h, 372);
        page.height = field(h, 376);
        page.bitsPerPixel = field(h, 388);
        page.bytesPerLine = field(h, 392);
        page.colorSpace = field(h, 400);

        if (!page.xResolution || !page.yResolution || !page.width || !page.height ||
            page.bytesPerLine != (page.width * page.bitsPerPixel + 7) / 8)
            die("Page header is invalid");
        if (page.bitsPerPixel != 1 || page.colorSpace != kCupsSpaceK)
            die("Unsupported raster format: %lu bit, color space %lu", page.bitsPerPixel,
                page.colorSpace);

        repeatLeft_ = 0;
        line_.assign(page.bytesPerLine, 0);
        return true;
    }

    void readLine(std::vector<unsigned char> &line) {
        if (!compressed_) {
            readExact(line_.data(), line_.size());
        } else if (repeatLeft_ > 0) {
            repeatLeft_--;
        } else {
            repeatLeft_ = readByte();
            decodeLine();
        }
        line = line_;
    }

private:
    unsigned long field(const unsigned char *header, size_t offset) const {
        const unsigned char *p = header + offset;
        if (bigEndian_)
            return ((unsigned long)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
        return ((unsigned long)p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
    }

    unsigned char readByte() {
        int value = fgetc(file_);
        if (value == EOF)
            die("Print data ends unexpectedly");
        return (unsigned char)value;
    }

    void readExact(unsigned char *target, size_t size) {
        if (fread(target, 1, size, file_) != size)
            die("Print data ends unexpectedly");
    }

    // Run-length encoding from PWG 5102.4. At 1 bit per pixel the unit is one byte.
    void decodeLine() {
        size_t position = 0;
        size_t size = line_.size();
        while (position < size) {
            unsigned char code = readByte();
            if (code == 128) {
                memset(line_.data() + position, 0, size - position);
                position = size;
            } else if (code & 128) {
                size_t count = 257 - (size_t)code;
                if (count > size - position)
                    die("Print data is corrupt");
                readExact(line_.data() + position, count);
                position += count;
            } else {
                size_t count = (size_t)code + 1;
                if (count > size - position)
                    die("Print data is corrupt");
                memset(line_.data() + position, readByte(), count);
                position += count;
            }
        }
    }

    FILE *file_;
    bool bigEndian_;
    bool compressed_;
    unsigned repeatLeft_;
    std::vector<unsigned char> line_;
};

const Paper &findPaper(const PageHeader &page) {
    double width = page.pageWidth ? page.pageWidth : page.width * 72.0 / page.xResolution;
    double height = page.pageHeight ? page.pageHeight : page.height * 72.0 / page.yResolution;
    for (const Paper &paper : kPapers) {
        if (fabs(paper.width - width) <= kPaperMatchTolerancePoints &&
            fabs(paper.height - height) <= kPaperMatchTolerancePoints)
            return paper;
    }
    fprintf(stderr, "INFO: Unknown paper size %.0f x %.0f pt, using A4\n", width, height);
    return kPapers[0];
}

std::string pjlSafe(const char *value, const char *fallback) {
    std::string text;
    for (const char *p = value ? value : fallback; *p && text.size() < 64; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 0x20 && c < 0x7f && c != '"')
            text += (char)c;
    }
    return text;
}

std::string pjlHeader(const std::string &user, const std::string &job) {
    time_t now = time(NULL);
    char date[16];
    strftime(date, sizeof date, "%Y%m%d", localtime(&now));

    std::string pjl = "\x1b%-12345X";
    pjl += std::string("@PJL DEFAULT SERVICEDATE=") + date + "\n";
    pjl += "@PJL SET USERNAME=\"" + user + "\"\n";
    pjl += "@PJL SET JOBNAME=\"" + job + "\"\n";
    pjl += "@PJL DEFAULT POWERSAVE=ON\n";
    pjl += "@PJL DEFAULT POWERSAVETIME=5\n";
    pjl += "@PJL SET JAMRECOVERY=OFF\n";
    pjl += "@PJL SET DUPLEX=OFF\n";
    pjl += "@PJL SET PAPERTYPE=OFF\n";
    pjl += "@PJL SET ALTITUDE=LOW\n";
    pjl += "@PJL SET DENSITY=3\n";
    pjl += "@PJL SET RET=NORMAL\n";
    pjl += "@PJL ENTER LANGUAGE = QPDL\n";
    return pjl;
}

// Loads a page into a paper-sized bitmap, like SpliX Document::loadPage.
std::vector<unsigned char> loadBitmap(RasterReader &raster, const PageHeader &header,
                                      unsigned long pageWidth, unsigned long pageHeight) {
    unsigned long pageWidthInB = (pageWidth + 7) / 8;
    unsigned long lineSize = header.bytesPerLine;
    unsigned long documentHeight = header.height;
    unsigned long marginWidthInB =
        ((unsigned long)ceil(header.marginX * (double)header.xResolution / 72.0) + 7) / 8;
    unsigned long marginHeight =
        (unsigned long)ceil(header.marginY * (double)header.yResolution / 72.0);
    unsigned long clippingX = 0;
    unsigned long clippingY = 0;
    unsigned long bytesToCopy = lineSize;
    unsigned long index;

    if (lineSize > pageWidthInB - 2 * marginWidthInB) {
        clippingX = (lineSize - (pageWidthInB - 2 * marginWidthInB)) / 2;
        bytesToCopy = pageWidthInB - 2 * marginWidthInB;
    } else {
        marginWidthInB = (pageWidthInB - lineSize) / 2;
    }
    if (documentHeight > pageHeight - 2 * marginHeight) {
        clippingY = (documentHeight - (pageHeight - 2 * marginHeight)) / 2;
        index = pageWidthInB * marginHeight;
    } else {
        index = pageWidthInB * ((pageHeight - documentHeight) / 2);
    }
    documentHeight -= clippingY;
    unsigned long linesLeft = pageHeight - 2 * marginHeight;

    std::vector<unsigned char> bitmap(pageWidthInB * pageHeight, 0);
    std::vector<unsigned char> line;
    for (unsigned long i = 0; i < clippingY; i++)
        raster.readLine(line);
    while (linesLeft && documentHeight) {
        raster.readLine(line);
        memcpy(bitmap.data() + index + marginWidthInB, line.data() + clippingX, bytesToCopy);
        index += pageWidthInB;
        linesLeft--;
        documentHeight--;
    }
    while (documentHeight--)
        raster.readLine(line);
    return bitmap;
}

void writeBand(Output &output, unsigned long bandNumber, unsigned long bandWidth,
               unsigned long bandHeight, const BandPlane &plane) {
    const unsigned char signature[4] = {0xEF, 0xCD, 0xAB, 0x09};
    unsigned long dataSize = plane.dataSize() + sizeof signature + 4;
    unsigned char header[11] = {
        0x0C,
        (unsigned char)bandNumber,
        (unsigned char)(bandWidth >> 8),
        (unsigned char)bandWidth,
        (unsigned char)(bandHeight >> 8),
        (unsigned char)bandHeight,
        kCompression,
        (unsigned char)(dataSize >> 24),
        (unsigned char)(dataSize >> 16),
        (unsigned char)(dataSize >> 8),
        (unsigned char)dataSize,
    };
    unsigned long checksum = plane.checksum();
    for (unsigned char byte : signature)
        checksum += byte;
    unsigned char trailer[4] = {
        (unsigned char)(checksum >> 24),
        (unsigned char)(checksum >> 16),
        (unsigned char)(checksum >> 8),
        (unsigned char)checksum,
    };
    output.write(header, sizeof header);
    output.write(signature, sizeof signature);
    output.write(plane.data(), plane.dataSize());
    output.write(trailer, sizeof trailer);
}

// Splits the bitmap into bands like SpliX _compressBandedPage and sends the page.
void renderPage(Output &output, const std::vector<unsigned char> &bitmap, const PageHeader &header,
                const Paper &paper, unsigned long pageWidth, unsigned long pageHeight) {
    unsigned long hardMarginX =
        ((unsigned long)ceil(kHardMarginXPoints * header.xResolution / 72.0) + 7) & ~7ul;
    unsigned long hardMarginY =
        (unsigned long)ceil(kHardMarginYPoints * header.yResolution / 72.0);
    unsigned long hardMarginXInB = hardMarginX / 8;
    unsigned long lineWidthInB = (pageWidth + 7) / 8;
    unsigned long bandHeight = kBandHeight;
    if (header.xResolution == 300 && header.yResolution == 300)
        bandHeight /= 2;
    unsigned long bandWidth = lineWidthInB * 8;
    unsigned long bandSize = lineWidthInB * bandHeight;
    unsigned long linesLeft = pageHeight - hardMarginY;
    unsigned long index = hardMarginY * lineWidthInB;
    unsigned long bandNumber = 0;

    const unsigned char pageHeader[0x11] = {
        0,
        (unsigned char)(header.yResolution / 100),
        0,
        1,
        paper.code,
        (unsigned char)(pageWidth >> 8),
        (unsigned char)pageWidth,
        (unsigned char)(linesLeft >> 8),
        (unsigned char)linesLeft,
        kPaperSourceAuto,
        0,
        kSimplexDuplexByte,
        0,
        0,
        kQpdlVersion,
        0,
        (unsigned char)(header.xResolution / 100),
    };
    output.write(pageHeader, sizeof pageHeader);

    // Algo0x11 ignores the request parameter, it only exists in the SpliX interface.
    static char unusedRequest;
    const Request &request = *reinterpret_cast<const Request *>(&unusedRequest);

    std::vector<unsigned char> band(bandSize);
    while (linesLeft && !gCancelled) {
        bool lastBand = linesLeft < bandHeight;
        unsigned long height = lastBand ? linesLeft : bandHeight;
        if (lastBand)
            memset(band.data(), 0, bandSize);

        for (unsigned long y = 0; y < height; y++) {
            for (unsigned long x = 0; x < lineWidthInB - hardMarginXInB; x++)
                band[x * bandHeight + y] = bitmap[index + x + hardMarginXInB + y * lineWidthInB];
            for (unsigned long x = lineWidthInB - hardMarginXInB; x < lineWidthInB; x++)
                band[x * bandHeight + y] = 0;
        }

        bool empty = true;
        for (unsigned char byte : band) {
            if (byte) {
                empty = false;
                break;
            }
        }
        if (!empty) {
            for (unsigned char &byte : band)
                byte = (unsigned char)~byte;
            Algo0x11 algorithm;
            BandPlane *plane = algorithm.compress(request, band.data(), bandWidth, bandHeight);
            if (!plane)
                die("Cannot compress band %lu", bandNumber);
            writeBand(output, bandNumber, bandWidth, bandHeight, *plane);
            delete plane;
        }

        bandNumber++;
        index += lineWidthInB * bandHeight;
        linesLeft = lastBand ? 0 : linesLeft - bandHeight;
    }

    const unsigned char pageFooter[3] = {1, 0, 1};
    output.write(pageFooter, sizeof pageFooter);
}

void printJob(Output &output, RasterReader &raster) {
    output.write(pjlHeader(pjlSafe(getenv("IPP_JOB_ORIGINATING_USER_NAME"), "unknown"),
                           pjlSafe(getenv("IPP_JOB_NAME"), "print")));

    int printed = 0;
    PageHeader header;
    while (!gCancelled && raster.nextPage(header)) {
        const Paper &paper = findPaper(header);
        unsigned long pageWidth =
            ((unsigned long)ceil(paper.width * header.xResolution / 72.0) + 7) & ~7ul;
        unsigned long pageHeight = (unsigned long)ceil(paper.height * header.yResolution / 72.0);
        std::vector<unsigned char> bitmap =
            loadBitmap(raster, header, pageWidth, pageHeight);
        renderPage(output, bitmap, header, paper, pageWidth, pageHeight);
        printed++;
        fprintf(stderr, "ATTR: job-impressions-completed=%d\n", printed);
    }

    output.write("\x09\x1b%-12345X", 10);
}

} // namespace

int main(int argc, char **argv) {
    const char *outputPath = NULL;
    const char *rasterPath = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--output") && i + 1 < argc)
            outputPath = argv[++i];
        else
            rasterPath = argv[i];
    }
    if (!rasterPath) {
        fprintf(stderr, "Usage: %s [--output FILE] RASTER_FILE\n", argv[0]);
        return 2;
    }

    signal(SIGTERM, onCancel);
    signal(SIGINT, onCancel);

    RasterReader raster(rasterPath);
    Output *output;
    if (outputPath)
        output = new FileOutput(outputPath);
    else
        output = new UsbOutput();
    printJob(*output, raster);
    delete output;
    return 0;
}
