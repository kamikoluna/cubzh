// -------------------------------------------------------------
//  Cubzh Core
//  serialization_v6.c
//  Created by Adrien Duermael on July 25, 2019.
// -------------------------------------------------------------

#include "serialization_v6.h"

#include <stdlib.h>
#include <string.h>

#include "cclog.h"
#include "map_string_float3.h"
#include "serialization.h"
#include "transform.h"
#include "zlib.h"
#include "stream.h"

typedef enum P3sCompressionMethod {
    P3sCompressionMethod_NONE = 0,
    P3sCompressionMethod_ZIP = 1,
    P3sCompressionMethod_COUNT = 2,
} P3sCompressionMethod;

#define P3S_CHUNK_ID_NONE 0 // not used as a chunk ID
#define P3S_CHUNK_ID_PREVIEW 1
#define P3S_CHUNK_ID_PALETTE_LEGACY 2
#define P3S_CHUNK_ID_SHAPE 3
#define P3S_CHUNK_ID_SHAPE_SIZE 4 // size of the shape (boundaries)
#define P3S_CHUNK_ID_SHAPE_BLOCKS 5
#define P3S_CHUNK_ID_SHAPE_POINT 6
#define P3S_CHUNK_ID_SHAPE_BAKED_LIGHTING 7
#define P3S_CHUNK_ID_SHAPE_POINT_ROTATION 8
//#define P3S_CHUNK_ID_SELECTED_COLOR 8
//#define P3S_CHUNK_ID_SELECTED_BACKGROUND_COLOR 9
//#define P3S_CHUNK_ID_CAMERA 10
//#define P3S_CHUNK_ID_DIRECTIONAL_LIGHT 11
//#define P3S_CHUNK_ID_SOURCE_METADATA 12
//#define P3S_CHUNK_ID_SHAPE_NAME 13
//#define P3S_CHUNK_ID_GENERAL_RENDERING_OPTIONS 14
#define P3S_CHUNK_ID_PALETTE_ID 15
#define P3S_CHUNK_ID_PALETTE 16
#define P3S_CHUNK_ID_MAX 17 // not used as a chunk ID, but used to check if chunk ID is known or not

// size of the chunk header, without chunk ID (it's already read at this point)
#define CHUNK_V6_HEADER_NO_ID_SIZE (sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t))
#define CHUNK_V6_HEADER_NO_ID_SKIP_SIZE (sizeof(uint8_t) + sizeof(uint32_t))

// --------------------------------------------------
//
// Static functions prototypes
//
// --------------------------------------------------

bool chunk_v6_shape_create_and_write_uncompressed_buffer(const Shape *shape,
                                                         uint32_t *uncompressedSize,
                                                         void **uncompressedData,
                                                         SHAPE_COLOR_INDEX_INT_T *paletteMapping);

bool chunk_v6_shape_create_and_write_compressed_buffer(const Shape *shape,
                                                       uint32_t *uncompressedSize,
                                                       uint32_t *compressedSize,
                                                       void **compressedData,
                                                       SHAPE_COLOR_INDEX_INT_T *paletteMapping);

void _chunk_v6_palette_create_and_write_uncompressed_buffer(ColorPalette *palette,
                                                            uint32_t *uncompressedSize,
                                                            void **uncompressedData,
                                                            SHAPE_COLOR_INDEX_INT_T **paletteMapping);

bool _chunk_v6_palette_create_and_write_compressed_buffer(ColorPalette *palette,
                                                          uint32_t *uncompressedSize,
                                                          uint32_t *compressedSize,
                                                          void **compressedData,
                                                          SHAPE_COLOR_INDEX_INT_T **paletteMapping);

/// Writes full chunk (header + data) in the provided memory buffer.
/// It compresses the data if requested.
/// It does NOT free the data.
/// Return value: true on success, false otherwise.
static bool write_chunk_in_buffer(void *destBuffer,
                                  const uint8_t chunkID,
                                  const void *chunkData,
                                  const uint32_t chunkDataSize,
                                  const bool doCompress,
                                  uint32_t *externCursor);

// Writes chunk that's already compressed.
static bool write_compressed_chunk_in_buffer(void *destBuffer,
                                             const uint8_t chunkID,
                                             const void *chunkCompressedData,
                                             const uint32_t chunkCompressedDataSize,
                                             const uint32_t chunkUncompressedDataSize,
                                             uint32_t *externCursor);

///
/// Return value: true on success, false otherwise.
static bool write_preview_chunk_in_buffer(void *destBuffer,
                                          const void *previewBytes,
                                          const uint32_t previewBytesCount,
                                          uint32_t *externCursor);

bool v6_write_size_at(long position, uint32_t size, FILE *fd);
// Writes full chunk (header + data) to file, compress the data if required, function will free data
// when done
bool chunk_v6_write_file(uint8_t chunkID, uint32_t size, void *data, uint8_t doCompress, FILE *fd);
bool chunk_v6_write_palette(FILE *fd, const ColorPalette *palette, bool doCompress, SHAPE_COLOR_INDEX_INT_T **paletteMapping);
bool chunk_v6_write_shape(FILE *fd, Shape *shape, bool doCompress, SHAPE_COLOR_INDEX_INT_T *paletteMapping);
bool chunk_v6_write_preview_image(FILE *fd, const void *imageData, uint32_t imageDataSize);

/// READ
// all read functions return number of bytes read or 0 if the file can't be read
uint8_t chunk_v6_read_identifier(Stream *s);
uint32_t chunk_v6_read_size(Stream *s);
// Reads full chunk, uncompressing it if necessary,
// function allocates data that must be freed by caller
bool chunk_v6_read(void **chunkData,
                   uint32_t *chunkSize,
                   uint32_t *uncompressedSize,
                   Stream *s);

// TODO: unify headers, currently only chunks writing with the function chunk_v6_write_file use v6
// header ie. Shape & Palette skips a chunk with v5 header (only chunkSize as uint32_t)
uint32_t chunk_v6_with_v5_header_skip(Stream *s);

// skips a chunk with v6 header
uint32_t chunk_v6_skip(Stream *s);

uint32_t chunk_v6_read_palette(Stream *s, ColorAtlas *colorAtlas, ColorPalette **palette, bool isLegacy);

uint32_t chunk_v6_read_palette_id(Stream *s, uint8_t *paletteID);

// @param shrinkPalette used as reference to build a shrinked palette w/ only used colors
uint32_t chunk_v6_read_shape_process_blocks(void *cursor,
                                            Shape *shape,
                                            uint16_t w,
                                            uint16_t h,
                                            uint16_t d,
                                            uint8_t paletteID,
                                            ColorPalette *shrinkPalette);

// chunk_v6_read_shape allocates a new Shape if shape != NULL
uint32_t chunk_v6_read_shape(Stream *s,
                             Shape **shape,
                             const bool fixedSize,
                             const bool octree,
                             const bool lighting,
                             const bool isMutable,
                             ColorAtlas *colorAtlas,
                             ColorPalette **serializedPalette,
                             uint8_t paletteID,
                             bool sharedColors);

uint32_t chunk_v6_read_preview_image(Stream *s, void **imageData, uint32_t *size);

/// Utils
static uint32_t getChunkHeaderSize(const uint8_t chunkID);
static uint32_t compute_preview_chunk_size(const uint32_t previewBytesCount);
uint32_t compute_shape_chunk_size(uint32_t shapeBufferDataSize);

bool v6_write_size_at(long position, uint32_t size, FILE *fd) {

    long currentPosition = ftell(fd);

    fseek(fd, position, SEEK_SET);
    if (fwrite(&size, sizeof(uint32_t), 1, fd) != 1) {
        return false;
    }

    fseek(fd, currentPosition, SEEK_SET); // back to current position
    return true;
}

bool chunk_v6_write_file(uint8_t chunkID, uint32_t size, void *data, uint8_t doCompress, FILE *fd) {
    uint32_t chunkSize = size;
    const uint32_t uncompressedSize = size;

    // compress data if required by this chunk
    if (doCompress != 0) {
        uLong compressedSize = compressBound(size);
        void *compressedData = malloc(compressedSize);
        if (compress(compressedData, &compressedSize, data, size) != Z_OK) {
            // failed to compress data, let's free the destination buffer
            free(compressedData);
            free(data);
            return false;
        }
        free(data);
        chunkSize = (uint32_t)compressedSize;
        data = compressedData;
    }

    // write header
    if (fwrite(&chunkID, sizeof(uint8_t), 1, fd) != 1) {
        free(data);
        return false;
    }
    if (fwrite(&chunkSize, sizeof(uint32_t), 1, fd) != 1) {
        free(data);
        return false;
    }
    if (fwrite(&doCompress, sizeof(uint8_t), 1, fd) != 1) {
        free(data);
        return false;
    }
    if (fwrite(&uncompressedSize, sizeof(uint32_t), 1, fd) != 1) {
        free(data);
        return false;
    }
    // write data
    if (fwrite(data, chunkSize, 1, fd) != 1) {
        free(data);
        return false;
    }

    free(data);
    return true;
}

bool chunk_v6_write_palette(FILE *fd, const ColorPalette *palette, bool doCompress, SHAPE_COLOR_INDEX_INT_T **paletteMapping) {
    uint32_t uncompressedSize = 0;
    void *uncompressedData = NULL;
    _chunk_v6_palette_create_and_write_uncompressed_buffer(palette, &uncompressedSize, &uncompressedData, paletteMapping);

    /// write file
    if (chunk_v6_write_file(P3S_CHUNK_ID_PALETTE,
                            uncompressedSize,
                            uncompressedData,
                            doCompress,
                            fd) == false) {
        cclog_error("failed to write palette chunk");
        return false;
    }

    return true;
}

bool chunk_v6_write_shape(FILE *fd, Shape *shape, bool doCompress,
                          SHAPE_COLOR_INDEX_INT_T *paletteMapping) {

    if (fd == NULL) {
        return false;
    }
    if (shape == NULL) {
        return false;
    }

    uint32_t uncompressedSize = 0;
    void *uncompressedData = NULL;

    if (chunk_v6_shape_create_and_write_uncompressed_buffer(shape,
                                                            &uncompressedSize,
                                                            &uncompressedData,
                                                            paletteMapping) == false) {
        cclog_error("chunk_v6_shape_create_and_write_uncompressed_buffer failed");
        return false;
    }

    /// write file
    // uncompressedData freed within chunk_v6_write_file
    if (chunk_v6_write_file(P3S_CHUNK_ID_SHAPE,
                            uncompressedSize,
                            uncompressedData,
                            doCompress,
                            fd) == false) {
        cclog_error("failed to write shape chunk");
        return false;
    }

    return true;
}

bool chunk_v6_write_preview_image(FILE *fd, const void *imageData, uint32_t imageDataSize) {
    const uint8_t chunkID = P3S_CHUNK_ID_PREVIEW;
    const uint32_t chunkSize = imageDataSize;

    // v5 chunk header
    if (fwrite(&chunkID, sizeof(uint8_t), 1, fd) != 1) {
        cclog_error("failed to write preview chunk ID");
        return false;
    }
    if (fwrite(&chunkSize, sizeof(uint32_t), 1, fd) != 1) {
        cclog_error("failed to write preview chunk size");
        return false;
    }

    // it is possible not to have a preview
    if (imageDataSize > 0) {
        // save preview bytes
        if (fwrite(imageData, imageDataSize, 1, fd) != 1) {
            cclog_error("failed to write preview bytes");
            return false;
        }
    }
    
    return true;
}

uint8_t chunk_v6_read_identifier(Stream *s) {
    uint8_t i;
    if (stream_read_uint8(s, &i) == false) {
        return P3S_CHUNK_ID_NONE;
    }

    if (i > P3S_CHUNK_ID_NONE && i < P3S_CHUNK_ID_MAX) {
        return i;
    }

    return P3S_CHUNK_ID_NONE;
}

uint32_t chunk_v6_read_size(Stream *s) {
    uint32_t i;
    if (stream_read_uint32(s, &i) == false) {
        cclog_error("failed to read v6 size");
        return 0;
    }
    // cclog_debug("> read v6 size: %d", i);
    return i;
}

bool chunk_v6_read(void **chunkData,
                   uint32_t *chunkSize,
                   uint32_t *uncompressedSize,
                   Stream *s) {
    
    uint32_t _chunkSize = 0;
    uint8_t _isCompressed = 0;
    uint32_t _uncompressedSize = 0;

    // read chunk header, chunk ID should be read already at this point
    if (stream_read_uint32(s, &_chunkSize) == false) {
        return false;
    }
    if (stream_read_uint8(s, &_isCompressed) == false) {
        return false;
    }
    if (stream_read_uint32(s, &_uncompressedSize) == false) {
        return false;
    }

    if (_chunkSize == 0 || _uncompressedSize == 0) {
        return false;
    }

    // read chunk data
    void *_chunkData = malloc(_chunkSize);
    if (stream_read(s, _chunkData, _chunkSize, 1) == false) {
        free(_chunkData);
        return false;
    }

    // uncompress if required by this chunk
    if (_isCompressed != 0) {
        uLong resultSize = _uncompressedSize;
        void *uncompressedData = malloc(_uncompressedSize);
        if (uncompress(uncompressedData, &resultSize, _chunkData, _chunkSize) != Z_OK) {
            free(uncompressedData);
            free(_chunkData);
            return false;
        }
        free(_chunkData);

        *chunkData = uncompressedData;
    } else {
        *chunkData = _chunkData;
    }
    *chunkSize = _chunkSize;
    *uncompressedSize = _uncompressedSize;
    return true;
}

// skips a chunk with v5 header (only chunkSize as uint32_t)
uint32_t chunk_v6_with_v5_header_skip(Stream *s) {
    uint32_t chunkSize = chunk_v6_read_size(s);
    uint32_t skippedBytes = 4;

    stream_skip(s, chunkSize);
    skippedBytes += chunkSize;
    return skippedBytes;
}

uint32_t chunk_v6_skip(Stream *s) {
    uint32_t chunkSize = chunk_v6_read_size(s);
    stream_skip(s, chunkSize + CHUNK_V6_HEADER_NO_ID_SKIP_SIZE);
    return CHUNK_V6_HEADER_NO_ID_SIZE + chunkSize;
}

///
bool serialization_v6_save_shape(Shape *shape,
                                 const void *imageData,
                                 uint32_t imageDataSize,
                                 FILE *fd) {

    // -------------------
    // HEADER
    // -------------------

    // write file format version
    uint32_t format = 6;
    if (fwrite(&format, sizeof(uint32_t), 1, fd) != 1) {
        cclog_error("failed to write file format");
        return false;
    }

    // write compression algo
    const P3sCompressionMethod compressionAlgo = P3sCompressionMethod_ZIP;
    if (fwrite(&compressionAlgo, sizeof(uint8_t), 1, fd) != 1) {
        cclog_error("failed to write compression algo");
        return false;
    }

    long positionBeforeTotalSize = ftell(fd);

    // write total size (will be updated at the end)
    uint32_t totalSize = 0;
    if (fwrite(&totalSize, sizeof(uint32_t), 1, fd) != 1) {
        cclog_error("failed to write total size");
        return false;
    }

    long positionBeforeChunks = ftell(fd);

    // -------------------
    // CHUNKS
    // -------------------

    SHAPE_COLOR_INDEX_INT_T *paletteMapping = NULL;
    chunk_v6_write_palette(fd, shape_get_palette(shape), true, &paletteMapping);

    chunk_v6_write_shape(fd, shape, true, paletteMapping);

    chunk_v6_write_preview_image(fd, imageData, imageDataSize);

    // -------------------
    // END OF FILE
    // -------------------

    // update total size
    totalSize = (uint32_t)(ftell(fd) - positionBeforeChunks);

    if (v6_write_size_at(positionBeforeTotalSize, (uint32_t)totalSize, fd) == false) {
        cclog_error("failed to write compressed file size");
        return false;
    }

    return true;
}

/// serialize a shape in a newly created memory buffer
/// Arguments:
/// - shape (mandatory)
/// - palette (optional)
/// - imageData (optional)
bool serialization_v6_save_shape_as_buffer(const Shape *shape,
                                           const void *previewData,
                                           const uint32_t previewDataSize,
                                           void **outBuffer,
                                           uint32_t *outBufferSize) {
    if (shape == NULL || outBuffer == NULL || outBufferSize == NULL) {
        return false;
    }

    const bool hasPreview = previewData != NULL && previewDataSize > 0;

    // --------------------------------------------------
    // Compute buffer size
    // --------------------------------------------------

    // Header
    uint32_t size = MAGIC_BYTES_SIZE;
    size += SERIALIZATION_FILE_FORMAT_VERION_SIZE;
    size += SERIALIZATION_COMPRESSION_ALGO_SIZE;
    size += SERIALIZATION_TOTAL_SIZE_SIZE;

    // Preview
    if (hasPreview) {
        const uint32_t chunkSize = compute_preview_chunk_size(previewDataSize);
        size += chunkSize;
    }

    // Palette
    uint32_t paletteUncompressedDataSize;
    uint32_t paletteCompressedDataSize;
    void *paletteCompressedData = NULL;
    SHAPE_COLOR_INDEX_INT_T *paletteMapping = NULL;
    if (_chunk_v6_palette_create_and_write_compressed_buffer(shape_get_palette(shape),
                                                             &paletteUncompressedDataSize,
                                                             &paletteCompressedDataSize,
                                                             &paletteCompressedData,
                                                             &paletteMapping) == false) {
        return false;
    }
    size += getChunkHeaderSize(P3S_CHUNK_ID_PALETTE) + paletteCompressedDataSize;

    // Shape
    // create shape buffer now to get its size
    uint32_t shapeUncompressedDataSize;
    uint32_t shapeCompressedDataSize;
    void *shapeCompressedData = NULL;
    if (chunk_v6_shape_create_and_write_compressed_buffer(shape,
                                                          &shapeUncompressedDataSize,
                                                          &shapeCompressedDataSize,
                                                          &shapeCompressedData,
                                                          paletteMapping) == false) {
        return false;
    }
    size += compute_shape_chunk_size(shapeCompressedDataSize);

    // allocate buffer
    uint8_t *buf = (uint8_t *)malloc(sizeof(char) * size);
    if (buf == NULL) {
        return false;
    }

    // writing cursor
    uint32_t cursor = 0;
    bool ok = false;

    // write magic bytes
    serialization_utils_writeCString(buf + cursor, MAGIC_BYTES, MAGIC_BYTES_SIZE, &cursor);

    // write file format version
    const uint32_t formatVersion = 6;
    serialization_utils_writeUint32(buf + cursor, formatVersion, &cursor);

    // write compression algo
    const P3sCompressionMethod compressionAlgo = P3sCompressionMethod_ZIP;
    serialization_utils_writeUint8(buf + cursor, compressionAlgo, &cursor);

    const uint32_t positionBeforeTotalSize = cursor;

    // write total size (will be updated at the end)
    uint32_t totalSize = size;
    serialization_utils_writeUint32(buf + cursor, totalSize, &cursor);

    uint32_t positionBeforeChunks = cursor;

    // write preview
    if (hasPreview) {
        ok = write_preview_chunk_in_buffer(buf + cursor, previewData, previewDataSize, &cursor);
        if (ok == false) {
            free(buf);
            free(shapeCompressedData);
            return false;
        }
    }

    // write palette
    {
        ok = write_compressed_chunk_in_buffer(buf + cursor,
                                              P3S_CHUNK_ID_PALETTE,
                                              paletteCompressedData,
                                              paletteCompressedDataSize,
                                              paletteUncompressedDataSize,
                                              &cursor);
        if (ok == false) {
            free(buf);
            free(shapeCompressedData);
            return false;
        }
    }

    // write shape
    {
        ok = write_compressed_chunk_in_buffer(buf + cursor,
                                              P3S_CHUNK_ID_SHAPE,
                                              shapeCompressedData,
                                              shapeCompressedDataSize,
                                              shapeUncompressedDataSize,
                                              &cursor);
        if (ok == false) {
            free(buf);
            free(shapeCompressedData);
            return false;
        }
    }

    // update total size
    totalSize = cursor - positionBeforeChunks;

    memcpy(&buf[positionBeforeTotalSize], &totalSize, sizeof(uint32_t));

    *outBuffer = buf;
    *outBufferSize = cursor;

    return true;
}

/// get preview data from save file path (caller must free *imageData)
bool serialization_v6_get_preview_data(Stream *s, void **imageData, uint32_t *size) {

    uint8_t i;
    if (stream_read_uint8(s, &i) == false) {
        cclog_error("failed to read compression algo");
        return false;
    }
    P3sCompressionMethod compressionAlgo = (P3sCompressionMethod)i;

    // File header may mention a compression algorithm but the preview
    // chunk is never compressed (as it is already compressed, being a PNG)
    if (compressionAlgo >= P3sCompressionMethod_COUNT) {
        cclog_error("compression algo not supported (v6)");
        return false;
    }

    uint32_t totalSize = 0;

    if (stream_read_uint32(s, &totalSize) == false) {
        cclog_error("failed to read total size");
        return false;
    }

    // READ ALL CHUNKS UNTIL PREVIEW IMAGE IS FOUND

    uint32_t totalSizeRead = 0;
    uint32_t sizeRead = 0;

    uint8_t chunkID;

    while (totalSizeRead < totalSize) {
        chunkID = chunk_v6_read_identifier(s);
        totalSizeRead += 1; // size of chunk id

        switch (chunkID) {
            case P3S_CHUNK_ID_NONE:
                cclog_error("wrong chunk id found");
                return false;
            case P3S_CHUNK_ID_PREVIEW:
                sizeRead = chunk_v6_read_preview_image(s, imageData, size);
                if (sizeRead == 0) {
                    cclog_error("error while reading overview image");
                    return false;
                }
                return true;
            case P3S_CHUNK_ID_SHAPE:
            case P3S_CHUNK_ID_PALETTE:
            case P3S_CHUNK_ID_PALETTE_LEGACY:
            case P3S_CHUNK_ID_PALETTE_ID:
                // v6 chunks we don't need to read
                totalSizeRead += chunk_v6_skip(s);
                break;
            default:
                // v5 chunks we don't need to read
                totalSizeRead += chunk_v6_with_v5_header_skip(s);
                break;
        }
    }
    return false;
}

Shape *serialization_v6_load_shape(Stream *s,
                                   bool limitSize,
                                   bool octree,
                                   bool lighting,
                                   bool isMutable,
                                   ColorAtlas *colorAtlas,
                                   bool sharedColors) {
    uint8_t i;
    if (stream_read_uint8(s, &i) == false) {
        cclog_error("failed to read compression algo");
        return NULL;
    }
    P3sCompressionMethod compressionAlgo = (P3sCompressionMethod)i;

    if (compressionAlgo >= P3sCompressionMethod_COUNT) {
        cclog_error("compression algo not supported");
        return NULL;
    }

    uint32_t totalSize = 0;

    if (stream_read_uint32(s, &totalSize) == false) {
        cclog_error("failed to read total size");
        return NULL;
    }

    // READ ALL CHUNKS UNTIL DONE

    Shape *shape = NULL;

    uint32_t totalSizeRead = 0;
    uint32_t sizeRead = 0;

    uint8_t chunkID;

    bool error = false;

    // Shape octree may have been serialized w/ default or shape palette indices,
    // - if there is a serialized palette, we consider that the octree was serialized w/ shape
    // palette indices, nothing to do
    // - if not, the octree was serialized w/ default palette indices, we'll build a shape palette
    // from the used default colors
    ColorPalette *serializedPalette = NULL;
    bool paletteLocked = false; // shouldn't happen
    uint8_t paletteID = PALETTE_ID_IOS_ITEM_EDITOR_LEGACY; // by default, w/o palette ID or palette chunks

    while (totalSizeRead < totalSize && error == false) {
        chunkID = chunk_v6_read_identifier(s);
        totalSizeRead += 1; // size of chunk id

        switch (chunkID) {
            case P3S_CHUNK_ID_NONE: {
                cclog_error("wrong chunk id found");
                error = true;
                break;
            }
            case P3S_CHUNK_ID_PALETTE_LEGACY:
            case P3S_CHUNK_ID_PALETTE: {
                // a shape palette is created w/ each color as "unused", until the octree is built
                sizeRead = chunk_v6_read_palette(s, colorAtlas, &serializedPalette, chunkID == P3S_CHUNK_ID_PALETTE_LEGACY);
                paletteID = PALETTE_ID_CUSTOM;

                // ignore palette if octree was processed already w/ default palette
                // Note: shouldn't happen, palette chunk is written before shape chunk
                if (paletteLocked) {
                    color_palette_free(serializedPalette);
                    serializedPalette = NULL;
                }

                if (sizeRead == 0) {
                    cclog_error("error while reading palette");
                    error = true;
                    break;
                }

                totalSizeRead += sizeRead;
                break;
            }
            case P3S_CHUNK_ID_PALETTE_ID: {
                sizeRead = chunk_v6_read_palette_id(s, &paletteID);

                if (sizeRead == 0) {
                    cclog_error("error while reading palette ID");
                    error = true;
                    break;
                }

                totalSizeRead += sizeRead;
                break;
            }
            case P3S_CHUNK_ID_SHAPE: {
                paletteLocked = true;

                sizeRead = chunk_v6_read_shape(s,
                                               &shape,
                                               limitSize,
                                               octree,
                                               lighting,
                                               isMutable,
                                               colorAtlas,
                                               &serializedPalette,
                                               paletteID,
                                               sharedColors);

                if (sizeRead == 0) {
                    cclog_error("error while reading shape");
                    error = true;
                    break;
                }

                totalSizeRead += sizeRead;
                break;
            }
            default: {
                // v5 chunks we don't need to read
                totalSizeRead += chunk_v6_with_v5_header_skip(s);
                break;
            }
        }
    }
    
    if (serializedPalette != NULL) {
        color_palette_free(serializedPalette);
    }

    if (error) {
        if (shape != NULL) {
            cclog_error("error reading shape, but shape isn't NULL");
        }
    }

    return shape;
}

// ------------------------------
// CHUNK READERS
// ------------------------------

uint32_t chunk_v6_read_palette(Stream *s, ColorAtlas *colorAtlas, ColorPalette **palette, bool isLegacy) {

    if (palette == NULL) {
        cclog_error("can't read palette without pointer to store it");
        return 0;
    }

    if (*palette != NULL) {
        color_palette_free(*palette);
        *palette = NULL;
    }

    /// read file
    void *chunkData = NULL;
    uint32_t chunkSize = 0;
    uint32_t uncompressedSize = 0;
    if (chunk_v6_read(&chunkData, &chunkSize, &uncompressedSize, s) == false) {
        cclog_error("failed to read palette");
        return 0;
    }

    /// get palette data
    void *cursor = chunkData;

    uint16_t colorCount = 0;
    
    if (isLegacy) {
        // number of rows (unused)
        cursor = (void *)((uint8_t *)cursor + 1);
        // number of columns (unused)
        cursor = (void *)((uint8_t *)cursor + 1);
        // color count
        colorCount = *((uint16_t *)cursor);
        cursor = (void *)((uint16_t *)cursor + 1);
        // default color (unused)
        cursor = (void *)((uint8_t *)cursor + 1);
        // default background color (unused)
        cursor = (void *)((uint8_t *)cursor + 1);
    } else {
        // color count
        colorCount = *((uint8_t *)cursor);
        cursor = (void *)((uint8_t *)cursor + 1);
    }
    // pointer to colors
    RGBAColor *colors = (RGBAColor *)cursor;
    cursor = (void *)((RGBAColor *)cursor + colorCount);
    // pointer to emissive flags
    bool *emissive = (bool *)cursor;

    *palette = color_palette_new_from_data(colorAtlas, minimum(colorCount, UINT8_MAX),
                                           colors, emissive, true);

    free(chunkData);

    return CHUNK_V6_HEADER_NO_ID_SIZE + chunkSize;
}

uint32_t chunk_v6_read_palette_id(Stream *s, uint8_t *paletteID) {
    /// read file to get size, but this chunk is now unused
    void *chunkData = NULL;
    uint32_t chunkSize = 0;
    uint32_t uncompressedSize = 0;
    if (chunk_v6_read(&chunkData, &chunkSize, &uncompressedSize, s) == false) {
        return 0;
    }

    *paletteID = *((uint8_t *)chunkData);

    free(chunkData);

    return CHUNK_V6_HEADER_NO_ID_SIZE + chunkSize;
}

uint32_t chunk_v6_read_shape_process_blocks(void *cursor,
                                            Shape *shape,
                                            uint16_t w,
                                            uint16_t h,
                                            uint16_t d,
                                            uint8_t paletteID,
                                            ColorPalette *shrinkPalette) {

    uint32_t size = *((uint32_t *)cursor); // shape blocks chunk size
    cursor = (void *)((uint32_t *)cursor + 1);
    SHAPE_COLOR_INDEX_INT_T colorIndex;
    ColorPalette *palette = shape_get_palette(shape);
    for (uint32_t x = 0; x < w; x++) { // shape blocks
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t z = 0; z < d; z++) {
                colorIndex = *((SHAPE_COLOR_INDEX_INT_T*)cursor);
                cursor = (void*)((SHAPE_COLOR_INDEX_INT_T*)cursor + 1);

                if (colorIndex == SHAPE_COLOR_INDEX_AIR_BLOCK) { // no cube
                    continue;
                }

                bool success = true;
                // translate & shrink to a shape palette w/ only used colors if,
                // 1) octree was serialized w/ a palette ID using any of the default palettes
                if (paletteID == PALETTE_ID_IOS_ITEM_EDITOR_LEGACY) {
                    success = color_palette_check_and_add_default_color_pico8p(palette, colorIndex, &colorIndex);
                } else if (paletteID == PALETTE_ID_2021) {
                    success = color_palette_check_and_add_default_color_2021(palette, colorIndex, &colorIndex);
                }
                // 2) octree was serialized w/ a palette that exceeds max size
                else if (shrinkPalette != NULL) {
                    RGBAColor *color = color_palette_get_color(shrinkPalette, colorIndex);
                    if (color != NULL) {
                        success = color_palette_check_and_add_color(palette, *color, &colorIndex);
                    }
                }
                if (success == false) {
                    colorIndex = 0;
                }

                shape_add_block_with_color(shape, colorIndex, x, y, z,false,
                                           false, false, false);
            }
        }
    }
    color_palette_clear_lighting_dirty(palette);

    return size + sizeof(uint32_t);
}

uint32_t chunk_v6_read_shape(Stream *s,
                             Shape **shape,
                             const bool fixedSize,
                             const bool octree,
                             const bool lighting,
                             const bool isMutable,
                             ColorAtlas *colorAtlas,
                             ColorPalette **serializedPalette,
                             uint8_t paletteID,
                             bool sharedColors) {

    /// read file
    void *chunkData = NULL;
    uint32_t chunkSize = 0;
    uint32_t uncompressedSize = 0;
    if (chunk_v6_read(&chunkData, &chunkSize, &uncompressedSize, s) == false) {
        cclog_error("failed to read shape");
        color_palette_free(*serializedPalette);
        *serializedPalette = NULL;
        return 0;
    }

    // no need to read if shape return parameter is NULL
    if (shape == NULL) {
        cclog_error("shape pointer is null");
        free(chunkData);
        color_palette_free(*serializedPalette);
        *serializedPalette = NULL;
        return CHUNK_V6_HEADER_NO_ID_SIZE + chunkSize;
    }

    if (*shape != NULL) {
        shape_release(*shape);
        *shape = NULL;
    }

    /// get shape data
    void *cursor = chunkData;
    void *shapeBlocksCursor = NULL;

    uint32_t totalSizeRead = 0;
    uint32_t sizeRead = 0;
    uint32_t lightingDataSizeRead = 0;

    uint8_t chunkID;
    MapStringFloat3 *pois = map_string_float3_new();
    MapStringFloat3 *pois_rotation = map_string_float3_new();
    VERTEX_LIGHT_STRUCT_T *lightingData = NULL;

    bool shapeSizeRead = false;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t depth = 0;

    const bool shrinkPalette = *serializedPalette != NULL
        && color_palette_get_count(*serializedPalette) >= SHAPE_COLOR_INDEX_MAX_COUNT;
    
    while (totalSizeRead < uncompressedSize) {
        chunkID = *((uint8_t *)cursor);
        cursor = (void *)((uint8_t *)cursor + 1);
        totalSizeRead += 1; // size of chunk id
        switch (chunkID) {
            case P3S_CHUNK_ID_SHAPE_SIZE: {
                memcpy(&sizeRead, cursor, sizeof(uint32_t)); // shape size chunk size
                cursor = (void *)((uint32_t *)cursor + 1);
                memcpy(&width, cursor, sizeof(uint16_t)); // shape size X
                cursor = (void *)((uint16_t *)cursor + 1);
                memcpy(&height, cursor, sizeof(uint16_t)); // shape size Y
                cursor = (void *)((uint16_t *)cursor + 1);
                memcpy(&depth, cursor, sizeof(uint16_t)); // shape size Y
                cursor = (void *)((uint16_t *)cursor + 1);

                totalSizeRead += sizeRead + sizeof(uint32_t);
                shapeSizeRead = true;

                // size is known, now is a good time to create the shape
                if (octree) {
                    *shape = shape_make_with_octree(width,
                                                    height,
                                                    depth,
                                                    lighting,
                                                    isMutable,
                                                    fixedSize == false);
                } else if (fixedSize) {
                    *shape = shape_make_with_fixed_size(width,
                                                        height,
                                                        depth,
                                                        lighting,
                                                        isMutable);
                } else {
                    *shape = shape_make();
                }
                
                if (serializedPalette != NULL && paletteID == PALETTE_ID_CUSTOM && shrinkPalette == false) {
                    color_palette_set_shared(*serializedPalette, sharedColors);
                    shape_set_palette(*shape, *serializedPalette);
                    *serializedPalette = NULL;
                } else {
                    shape_set_palette(*shape, color_palette_new(colorAtlas, sharedColors));
                }

                // process blocks now if they were found before the size
                if (shapeBlocksCursor != NULL) {
                    chunk_v6_read_shape_process_blocks(shapeBlocksCursor,
                                                       *shape,
                                                       width,
                                                       height,
                                                       depth,
                                                       paletteID,
                                                       shrinkPalette ? *serializedPalette : NULL);
                }
                
                break;
            }
            case P3S_CHUNK_ID_SHAPE_BLOCKS: {
                // Size is required to read blocks, storing blocks position to process them later
                // /!\ shouldn't happen as shape size is serialized in order before shape blocks
                // TODO: v7: merge shape size + shape blocks chunks
                if (shapeSizeRead == false) {
                    shapeBlocksCursor = cursor;

                    // shape blocks chunk size
                    sizeRead = *((uint32_t *)cursor);
                    cursor = (void *)((uint32_t *)cursor + 1);

                    // advance cursor
                    cursor = (void *)((char *)cursor + sizeRead);
                    break;
                }

                sizeRead = chunk_v6_read_shape_process_blocks(cursor,
                                                              *shape,
                                                              width,
                                                              height,
                                                              depth,
                                                              paletteID,
                                                              shrinkPalette ? *serializedPalette : NULL);
                cursor = (void *)((char *)cursor + sizeRead);

                totalSizeRead += sizeRead;
                break;
            }
            case P3S_CHUNK_ID_SHAPE_POINT: {
                uint8_t nameLen = 0;
                char *nameStr = NULL;
                float3 *poi = float3_new(0, 0, 0);

                memcpy(&sizeRead, cursor, sizeof(uint32_t)); // shape POI chunk size
                cursor = (void *)((uint32_t *)cursor + 1);

                memcpy(&nameLen, cursor, sizeof(uint8_t)); // shape POI name length
                cursor = (void *)((uint8_t *)cursor + 1);

                nameStr = (char *)malloc(nameLen + 1); // +1 for null terminator
                if (nameStr == NULL) {
                    cclog_error("malloc failed");
                    // TODO: handle error
                }
                memcpy(nameStr, cursor, nameLen); // shape POI name
                nameStr[nameLen] = 0;             // add null terminator
                cursor = (void *)((char *)cursor + nameLen);

                memcpy(&(poi->x), cursor, sizeof(float)); // shape POI X
                cursor = (void *)((float *)cursor + 1);

                memcpy(&(poi->y), cursor, sizeof(float)); // shape POI Y
                cursor = (void *)((float *)cursor + 1);

                memcpy(&(poi->z), cursor, sizeof(float)); // shape POI Z
                cursor = (void *)((float *)cursor + 1);

                map_string_float3_set_key_value(pois, nameStr, poi);
                free(nameStr);

                totalSizeRead += sizeRead + sizeof(uint32_t);
                break;
            }
            case P3S_CHUNK_ID_SHAPE_POINT_ROTATION: {
                uint8_t nameLen = 0;
                char *nameStr = NULL;
                float3 *poi = float3_new(0, 0, 0);

                memcpy(&sizeRead, cursor, sizeof(uint32_t)); // shape POI chunk size
                cursor = (void *)((uint32_t *)cursor + 1);

                memcpy(&nameLen, cursor, sizeof(uint8_t)); // shape POI name length
                cursor = (void *)((uint8_t *)cursor + 1);

                nameStr = (char *)malloc(nameLen + 1); // +1 for null terminator
                memcpy(nameStr, cursor, nameLen);      // shape POI name
                nameStr[nameLen] = 0;                  // add null terminator
                cursor = (void *)((char *)cursor + nameLen);

                memcpy(&(poi->x), cursor, sizeof(float)); // shape POI X
                cursor = (void *)((float *)cursor + 1);

                memcpy(&(poi->y), cursor, sizeof(float)); // shape POI Y
                cursor = (void *)((float *)cursor + 1);

                memcpy(&(poi->z), cursor, sizeof(float)); // shape POI Z
                cursor = (void *)((float *)cursor + 1);

                map_string_float3_set_key_value(pois_rotation, nameStr, poi);
                free(nameStr);

                totalSizeRead += sizeRead + sizeof(uint32_t);
                break;
            }
#if GLOBAL_LIGHTING_BAKE_READ_ENABLED
            case P3S_CHUNK_ID_SHAPE_BAKED_LIGHTING: {
                memcpy(&lightingDataSizeRead,
                       cursor,
                       sizeof(uint32_t)); // shape baked lighting chunk size
                cursor = (void *)((uint32_t *)cursor + 1);

                uint32_t dataCount = lightingDataSizeRead / sizeof(VERTEX_LIGHT_STRUCT_T);
                if (dataCount == 0) {
                    cclog_error("baked light data count cannot be 0, skipping");
                    totalSizeRead += lightingDataSizeRead + sizeof(uint32_t);
                    break;
                }

                lightingData = (VERTEX_LIGHT_STRUCT_T *)malloc(lightingDataSizeRead);

                uint8_t v1, v2;
                for (int i = 0; i < (int)dataCount; i++) {
                    memcpy(&v1, cursor, sizeof(uint8_t)); // shape baked lighting v1
                    cursor = (void *)((uint8_t *)cursor + 1);

                    memcpy(&v2, cursor, sizeof(uint8_t)); // shape baked lighting v2
                    cursor = (void *)((uint8_t *)cursor + 1);

                    lightingData[i].red = v1 / 16;
                    lightingData[i].ambient = v1 - lightingData[i].red * 16;
                    lightingData[i].blue = v2 / 16;
                    lightingData[i].green = v2 - lightingData[i].blue * 16;
                }

                totalSizeRead += lightingDataSizeRead + sizeof(uint32_t);
            }
#endif
            default: // shape sub chunks we don't need to read
            {
                /*
                 #define P3S_CHUNK_ID_SELECTED_COLOR 8
                 #define P3S_CHUNK_ID_SELECTED_BACKGROUND_COLOR 9
                 #define P3S_CHUNK_ID_CAMERA 10
                 #define P3S_CHUNK_ID_DIRECTIONAL_LIGHT 11
                 #define P3S_CHUNK_ID_SOURCE_METADATA 12
                 #define P3S_CHUNK_ID_SHAPE_NAME 13
                 #define P3S_CHUNK_ID_GENERAL_RENDERING_OPTIONS 14
                 */
                // sub chunk header size + sub chunk data size
                if (uncompressedSize != totalSizeRead) {
                    sizeRead = CHUNK_V6_HEADER_NO_ID_SIZE + *((uint32_t *)cursor);
                    // advance cursor
                    cursor = (void *)((char *)cursor + sizeRead);

                    totalSizeRead += sizeRead;
                }
                break;
            }
        }
    }

    free(chunkData);
    
    if (*serializedPalette != NULL) {
        color_palette_free(*serializedPalette);
        *serializedPalette = NULL;
    }

    if (*shape == NULL) {
        cclog_error("error while reading shape : no shape were created");
        if (lightingData != NULL) {
            free(lightingData);
        }
        map_string_float3_free(pois);
        return 0;
    }

    float3 f3;

    // set shape POIs
    MapStringFloat3Iterator *it = map_string_float3_iterator_new(pois);
    while (map_string_float3_iterator_is_done(it) == false) {
        float3 *value = map_string_float3_iterator_current_value(it);
        float3_copy(&f3, value);
        shape_set_point_of_interest(*shape, map_string_float3_iterator_current_key(it), &f3);
        map_string_float3_iterator_next(it);
    }
    map_string_float3_iterator_free(it);
    map_string_float3_free(pois);

    // set shape points (rotation)
    it = map_string_float3_iterator_new(pois_rotation);
    while (map_string_float3_iterator_is_done(it) == false) {
        float3 *value = map_string_float3_iterator_current_value(it);
        float3_copy(&f3, value);
        shape_set_point_rotation(*shape, map_string_float3_iterator_current_key(it), &f3);
        map_string_float3_iterator_next(it);
    }
    map_string_float3_iterator_free(it);
    map_string_float3_free(pois_rotation);

    // set shape lighting data
    if (shape_uses_baked_lighting(*shape)) {
        if (lightingData == NULL) {
            cclog_warning("shape uses lighting but no baked lighting found");
        } else if (octree == false && fixedSize == false) {
            cclog_warning("shape uses lighting but does not have a fixed size");
            free(lightingData);
        } else if (lightingDataSizeRead != width * height * depth * sizeof(VERTEX_LIGHT_STRUCT_T)) {
            cclog_warning("shape uses lighting but does not match lighting data size");
            free(lightingData);
        } else {
            shape_set_lighting_data(*shape, lightingData);
        }
    } else if (lightingData != NULL) {
        cclog_warning("shape baked lighting data discarded");
        free(lightingData);
    }

    return CHUNK_V6_HEADER_NO_ID_SIZE + chunkSize;
}

//
uint32_t chunk_v6_read_preview_image(Stream *s, void **imageData, uint32_t *size) {
    uint32_t chunkSize = chunk_v6_read_size(s);
    if (chunkSize == 0) {
        cclog_error("can't read preview image chunk size (v6)");
        return 0;
    }

    // read preview data
    void *previewData = malloc(chunkSize);

    if (previewData == NULL) {
        // error
        cclog_error("failed to allocate preview data buffer");
        return 0;
    }
    if (stream_read(s, previewData, 1, chunkSize) == false) {
        // error
        cclog_error("failed to read preview data");
        free(previewData);
        return 0;
    }
    // success
    *size = chunkSize;
    *imageData = previewData;

    return chunkSize + 4;
}

// --------------------------------------------------
//
// MARK: - Static functions
//
// --------------------------------------------------

/// Arguments:
/// - chunkData : uncompressed data
///
/// If externCursor is not NULL, it is incremented by the number of bytes written.
bool write_chunk_in_buffer(void *destBuffer,
                           const uint8_t chunkID,
                           const void *chunkData,
                           const uint32_t chunkDataSize,
                           const bool doCompress,
                           uint32_t *externCursor) {

    const uint32_t uncompressedSize = chunkDataSize;

    const void *data = chunkData;
    uint32_t size = chunkDataSize;

    // compress chunk data if requested
    if (doCompress == true) {
        uLong compressedSize = compressBound(chunkDataSize);
        void *compressedData = malloc(compressedSize);
        if (compress(compressedData, &compressedSize, chunkData, chunkDataSize) != Z_OK) {
            return false;
        }
        data = compressedData;
        size = (uint32_t)compressedSize;
    }

    // write chunk in destination buffer
    uint8_t *cursor = destBuffer;

    // chunk header
    memcpy(cursor, &chunkID, sizeof(uint8_t));
    cursor += sizeof(uint8_t);

    memcpy(cursor, &size, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    memcpy(cursor, &doCompress, sizeof(uint8_t));
    cursor += sizeof(uint8_t);

    memcpy(cursor, &uncompressedSize, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    // chunk data
    memcpy(cursor, data, size);
    cursor += size;

    if (externCursor != NULL) {
        *externCursor += (uint32_t)(cursor - (uint8_t *)destBuffer);
    }

    return true;
}

static bool write_compressed_chunk_in_buffer(void *destBuffer,
                                             const uint8_t chunkID,
                                             const void *chunkCompressedData,
                                             const uint32_t chunkCompressedDataSize,
                                             const uint32_t chunkUncompressedDataSize,
                                             uint32_t *externCursor) {

    // write chunk in destination buffer
    uint8_t *cursor = destBuffer;

    // chunk header
    memcpy(cursor, &chunkID, sizeof(uint8_t));
    cursor += sizeof(uint8_t);

    memcpy(cursor, &chunkCompressedDataSize, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    bool compressed = true;
    memcpy(cursor, &compressed, sizeof(uint8_t));
    cursor += sizeof(uint8_t);

    memcpy(cursor, &chunkUncompressedDataSize, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    // chunk data
    memcpy(cursor, chunkCompressedData, chunkCompressedDataSize);
    cursor += chunkCompressedDataSize;

    if (externCursor != NULL) {
        *externCursor += (uint32_t)(cursor - (uint8_t *)destBuffer);
    }

    return true;
}

///
/// If externCursor is not NULL, it is incremented by the number of bytes written.
bool write_preview_chunk_in_buffer(void *destBuffer,
                                   const void *previewBytes,
                                   const uint32_t previewBytesCount,
                                   uint32_t *externCursor) {

    if (destBuffer == NULL) {
        return false;
    }
    if (previewBytes == NULL) {
        return false;
    }

    if (previewBytesCount == 0) {
        return false;
    }

    const uint8_t chunkID = P3S_CHUNK_ID_PREVIEW;
    const void *data = previewBytes;
    const uint32_t size = previewBytesCount;

    // write chunk in destination buffer
    uint8_t *cursor = destBuffer;

    // chunk header
    memcpy(cursor, &chunkID, sizeof(uint8_t));
    cursor += sizeof(uint8_t);

    memcpy(cursor, &size, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    // chunk data
    memcpy(cursor, data, size);
    cursor += size;

    if (externCursor != NULL) {
        *externCursor += (uint32_t)(cursor - (uint8_t *)destBuffer);
    }

    return true;
}

bool chunk_v6_shape_create_and_write_uncompressed_buffer(const Shape *shape,
                                                         uint32_t *uncompressedSize,
                                                         void **uncompressedData,
                                                         SHAPE_COLOR_INDEX_INT_T *paletteMapping) {

    if (uncompressedSize == NULL) {
        return false;
    }
    if (uncompressedData == NULL) {
        return false;
    }

    if (*uncompressedData != NULL) {
        free(*uncompressedData);
    }

    Block *block = NULL;
    MapStringFloat3Iterator *it = NULL;
    int3 shapeSize;

    // we only have to write blocks that are in the bounding box
    // using boundingBox->min to offset blocks at 0,0,0 when writing
    // blocks, POIs, and lightingData
    const Box *boundingBox = shape_get_model_aabb(shape);
    box_get_size_int(boundingBox, &shapeSize);

    int3 start; // inclusive
    int3 end;   // non-inclusive
    int3_set(&start,
             (int32_t)(boundingBox->min.x),
             (int32_t)(boundingBox->min.y),
             (int32_t)(boundingBox->min.z));
    int3_set(&end, start.x + shapeSize.x, start.y + shapeSize.y, start.z + shapeSize.z);

    uint32_t blockCount = shapeSize.x * shapeSize.y * shapeSize.z;

#if GLOBAL_LIGHTING_BAKE_WRITE_ENABLED
    bool hasLighting = shape_uses_baked_lighting(shape);
#else
    bool hasLighting = false;
#endif

    // prepare buffer with shape chunk uncompressed data

    // shape sub-chunks size
    uint32_t subheaderSize = sizeof(uint8_t) + sizeof(uint32_t);
    uint32_t shapeSizeSize = 3 * sizeof(uint16_t);
    uint32_t shapeBlocksSize = blockCount * sizeof(uint8_t);
    uint32_t shapeLightingSize = blockCount * sizeof(VERTEX_LIGHT_STRUCT_T);

    // Point positions sub-chunks collective size /!\ the name length can vary
    // TODO store this in a list to avoid recomputing it later
    uint32_t shapePointPositionsSize = 0, shapePointPositionsCount = 0;
    it = shape_get_poi_iterator(shape);
    while (map_string_float3_iterator_is_done(it) == false) {
        const char *key = map_string_float3_iterator_current_key(it);
        const float3 *f3 = map_string_float3_iterator_current_value(it);

        if (key == NULL || f3 == NULL) {
            continue;
        }

        // name length w/ 255 chars max, name is truncated if longer than this
        uint32_t nameLen = (uint32_t)strlen(key);
        nameLen = nameLen > 255 ? 255 : (uint8_t)nameLen;

        shapePointPositionsSize += sizeof(uint8_t) + nameLen + 3 * sizeof(float);
        shapePointPositionsCount++;

        map_string_float3_iterator_next(it);
    }
    map_string_float3_iterator_free(it);

    // Point rotations sub-chunks collective size /!\ the name length can vary
    // TODO store this in a list to avoid recomputing it later
    uint32_t shapePointRotationsSize = 0, shapePointRotationsCount = 0;
    it = shape_get_point_rotation_iterator(shape);
    while (map_string_float3_iterator_is_done(it) == false) {
        const char *key = map_string_float3_iterator_current_key(it);
        const float3 *f3 = map_string_float3_iterator_current_value(it);

        if (key == NULL || f3 == NULL) {
            continue;
        }

        // name length w/ 255 chars max, name is truncated if longer than this
        uint32_t nameLen = (uint32_t)strlen(key);
        nameLen = nameLen > 255 ? 255 : (uint8_t)nameLen;

        shapePointRotationsSize += sizeof(uint8_t) + nameLen + 3 * sizeof(float);
        shapePointRotationsCount++;

        map_string_float3_iterator_next(it);
    }
    map_string_float3_iterator_free(it);

    // allocate for uncompressed data
    *uncompressedSize = subheaderSize + shapeSizeSize + subheaderSize + shapeBlocksSize +
                        shapePointPositionsCount * subheaderSize + shapePointPositionsSize +
                        shapePointRotationsCount * subheaderSize + shapePointRotationsSize +
                        (hasLighting ? subheaderSize + shapeLightingSize : 0);

    *uncompressedData = malloc(*uncompressedSize);
    if (*uncompressedData == NULL) {
        return false;
    }

    void *cursor = *uncompressedData;

    // shape size sub-chunk
    const uint8_t chunk_id_shape_size = P3S_CHUNK_ID_SHAPE_SIZE;
    memcpy(cursor, &chunk_id_shape_size, sizeof(uint8_t)); // shape size chunk ID
    cursor = (void *)((uint8_t *)cursor + 1);

    memcpy(cursor, &shapeSizeSize, sizeof(uint32_t)); // shape size chunk size
    cursor = (void *)((uint32_t *)cursor + 1);

    memcpy(cursor, &(shapeSize.x), sizeof(uint16_t)); // shape size X
    cursor = (void *)((uint16_t *)cursor + 1);

    memcpy(cursor, &(shapeSize.y), sizeof(uint16_t)); // shape size Y
    cursor = (void *)((uint16_t *)cursor + 1);

    memcpy(cursor, &(shapeSize.z), sizeof(uint16_t)); // shape size Z
    cursor = (void *)((uint16_t *)cursor + 1);

    // shape blocks sub-chunk
    *((uint8_t *)cursor) = P3S_CHUNK_ID_SHAPE_BLOCKS; // shape blocks chunk ID
    cursor = (void *)((uint8_t *)cursor + 1);
    *((uint32_t *)cursor) = shapeBlocksSize; // shape blocks chunk size
    cursor = (void *)((uint32_t *)cursor + 1);
    for (int32_t x = start.x; x < end.x; x++) { // shape blocks
        for (int32_t y = start.y; y < end.y; y++) {
            for (int32_t z = start.z; z < end.z; z++) {
                block = shape_get_block(shape, x, y, z, false);
                if (block_is_solid(block)) {
                    *((uint8_t *)cursor) = paletteMapping != NULL ?
                                           paletteMapping[block_get_color_index(block)] :
                                           block_get_color_index(block);
                } else {
                    *((uint8_t *)cursor) = SHAPE_COLOR_INDEX_AIR_BLOCK;
                   
                }
                cursor = (void *)((uint8_t *)cursor + 1);
            }
        }
    }

    // shape POI sub-chunks (one per POI)
    {
        it = shape_get_poi_iterator(shape);

        while (map_string_float3_iterator_is_done(it) == false) {
            const char *key = map_string_float3_iterator_current_key(it);
            const float3 *f3 = map_string_float3_iterator_current_value(it);

            if (key == NULL || f3 == NULL) {
                cclog_error("Point position can't be written");
                continue;
            }

            // name length w/ 255 chars max, name is truncated if longer than this
            uint32_t nameLen = (uint32_t)strlen(key);
            nameLen = nameLen > 255 ? 255 : (uint8_t)nameLen;
            const uint32_t chunkSize = sizeof(uint8_t) + nameLen + 3 * sizeof(float);

            // shape POI sub-chunk
            *((uint8_t *)cursor) = P3S_CHUNK_ID_SHAPE_POINT; // shape POI chunk ID
            cursor = (void *)((uint8_t *)cursor + 1);
            *((uint32_t *)cursor) = chunkSize; // shape POI chunk size
            cursor = (void *)((uint32_t *)cursor + 1);
            *((uint8_t *)cursor) = nameLen; // shape POI name length
            cursor = (void *)((uint8_t *)cursor + 1);
            memcpy(cursor, key, nameLen); // shape POI name
            cursor = (void *)((char *)cursor + nameLen);
            *((float *)cursor) = f3->x - (float)(start.x); // shape POI X (empty space removed)
            cursor = (void *)((float *)cursor + 1);
            *((float *)cursor) = f3->y - (float)(start.y); // shape POI Y (empty space removed)
            cursor = (void *)((float *)cursor + 1);
            *((float *)cursor) = f3->z - (float)(start.z); // shape POI Z (empty space removed)
            cursor = (void *)((float *)cursor + 1);

            map_string_float3_iterator_next(it);
        }
        map_string_float3_iterator_free(it);
    }

    // shape points (rotation) sub-chunks (one per point)
    {
        it = shape_get_point_rotation_iterator(shape);

        while (map_string_float3_iterator_is_done(it) == false) {
            const char *key = map_string_float3_iterator_current_key(it);
            const float3 *f3 = map_string_float3_iterator_current_value(it);

            if (key == NULL || f3 == NULL) {
                cclog_error("Point rotation can't be written");
                continue;
            }

            // name length w/ 255 chars max, name is truncated if longer than this
            uint32_t nameLen = (uint32_t)strlen(key);
            nameLen = nameLen > 255 ? 255 : (uint8_t)nameLen;
            const uint32_t chunkSize = sizeof(uint8_t) + nameLen + 3 * sizeof(float);

            // shape POI sub-chunk
            *((uint8_t *)cursor) = P3S_CHUNK_ID_SHAPE_POINT_ROTATION; // shape POI chunk ID
            cursor = (void *)((uint8_t *)cursor + 1);
            *((uint32_t *)cursor) = chunkSize; // shape POI chunk size
            cursor = (void *)((uint32_t *)cursor + 1);
            *((uint8_t *)cursor) = nameLen; // shape POI name length
            cursor = (void *)((uint8_t *)cursor + 1);
            memcpy(cursor, key, nameLen); // shape POI name
            cursor = (void *)((char *)cursor + nameLen);
            *((float *)cursor) = (float)(f3->x); // shape POI X
            cursor = (void *)((float *)cursor + 1);
            *((float *)cursor) = (float)(f3->y); // shape POI Y
            cursor = (void *)((float *)cursor + 1);
            *((float *)cursor) = (float)(f3->z); // shape POI Z
            cursor = (void *)((float *)cursor + 1);

            map_string_float3_iterator_next(it);
        }
        map_string_float3_iterator_free(it);
    }

    // shape baked lighting sub-chunk
    if (hasLighting) {
        // shape baked lighting chunk ID
        const uint8_t chunkIdShapeBakedLighting = P3S_CHUNK_ID_SHAPE_BAKED_LIGHTING;
        memcpy(cursor, &chunkIdShapeBakedLighting, sizeof(uint8_t));
        cursor = (void *)((uint8_t *)cursor + 1);

        // shape baked lighting chunk size
        memcpy(cursor, &shapeLightingSize, sizeof(uint32_t));
        cursor = (void *)((uint32_t *)cursor + 1);

        // write offsetted backed lighting
        // ! \\ light is stored in a flat array, loop nesting is important
        for (int32_t x = start.x; x < end.x; x++) { // shape blocks
            for (int32_t y = start.y; y < end.y; y++) {
                for (int32_t z = start.z; z < end.z; z++) {
                    *((VERTEX_LIGHT_STRUCT_T *)cursor) = shape_get_light_without_checking(shape,
                                                                                          x,
                                                                                          y,
                                                                                          z);
                    cursor = (void *)((VERTEX_LIGHT_STRUCT_T *)cursor + 1);
                }
            }
        }
    }

    return true;
}

bool chunk_v6_shape_create_and_write_compressed_buffer(const Shape *shape,
                                                       uint32_t *uncompressedSize,
                                                       uint32_t *compressedSize,
                                                       void **compressedData,
                                                       SHAPE_COLOR_INDEX_INT_T *paletteMapping) {

    if (uncompressedSize == NULL) {
        return false;
    }
    if (compressedSize == NULL) {
        return false;
    }
    if (compressedData == NULL) {
        return false;
    }

    if (*compressedData != NULL) {
        free(*compressedData);
        *compressedData = NULL;
    }

    *uncompressedSize = 0;
    *compressedSize = 0;

    // first, get uncompressed data

    void *uncompressedData = NULL;

    if (chunk_v6_shape_create_and_write_uncompressed_buffer(shape,
                                                            uncompressedSize,
                                                            &uncompressedData,
                                                            paletteMapping) == false) {
        cclog_error("chunk_v6_shape_create_and_write_uncompressed_buffer failed");
        return false;
    }

    // compress it

    // compressBound is a zlib function making sure the buffer for compression will be large enough
    // _compressedSize here is not final, it will be known after compression.
    uLong _compressedSize = compressBound(*uncompressedSize);
    void *_compressedData = malloc(_compressedSize);

    if (compress(_compressedData, &_compressedSize, uncompressedData, *uncompressedSize) != Z_OK) {
        return false;
    }

    free(uncompressedData);

    // now we have the final compressed size and data, we can pass it to our outputs.
    *compressedSize = (uint32_t)_compressedSize;
    *compressedData = malloc(*compressedSize);
    memcpy(*compressedData, _compressedData, *compressedSize);

    free(_compressedData);

    return true;
}

void _chunk_v6_palette_create_and_write_uncompressed_buffer(ColorPalette *palette,
                                                            uint32_t *uncompressedSize,
                                                            void **uncompressedData,
                                                            SHAPE_COLOR_INDEX_INT_T **paletteMapping) {

    // apply internal mapping to re-order palette, get serialization mapping
    bool *emissive;
    RGBAColor *colors = color_palette_get_colors_as_array(palette, &emissive, paletteMapping);
    uint8_t colorCount = color_palette_get_ordered_count(palette);

    // prepare palette chunk uncompressed data
    *uncompressedSize = sizeof(uint8_t) + sizeof(RGBAColor) * colorCount
                                + sizeof(bool) * colorCount;
    *uncompressedData = malloc(*uncompressedSize);
    void *cursor = *uncompressedData;

    // number of colors
    *((uint8_t *)cursor) = colorCount;
    cursor = (void *)((uint8_t *)cursor + 1);
    // colors
    memcpy(cursor, colors, sizeof(RGBAColor) * colorCount);
    cursor = (void *)((RGBAColor *)cursor + colorCount);
    // emissive flags
    memcpy(cursor, emissive, sizeof(bool) * colorCount);

    free(colors);
    free(emissive);
}

bool _chunk_v6_palette_create_and_write_compressed_buffer(ColorPalette *palette,
                                                          uint32_t *uncompressedSize,
                                                          uint32_t *compressedSize,
                                                          void **compressedData,
                                                          SHAPE_COLOR_INDEX_INT_T **paletteMapping) {
    void *uncompressedData = NULL;
    _chunk_v6_palette_create_and_write_uncompressed_buffer(palette, uncompressedSize, &uncompressedData, paletteMapping);

    // zlib: compressBound estimates size so we can allocate
    uLong _compressedSize = compressBound(*uncompressedSize);
    void *_compressedData = malloc(_compressedSize);

    if (compress(_compressedData, &_compressedSize, uncompressedData, *uncompressedSize) != Z_OK) {
        return false;
    }
    free(uncompressedData);

    // now we have the final compressed size and data
    *compressedSize = (uint32_t)_compressedSize;
    *compressedData = malloc(*compressedSize);
    memcpy(*compressedData, _compressedData, *compressedSize);

    free(_compressedData);

    return true;
}

//
// MARK: - Utils -
//

uint32_t getChunkHeaderSize(const uint8_t chunkID) {
    uint32_t result = 0;
    switch (chunkID) {
        case P3S_CHUNK_ID_PREVIEW: {
            // v5 chunk header
            // chunkID | chunkSize
            result = sizeof(uint8_t) + sizeof(uint32_t);
            break;
        }
        case P3S_CHUNK_ID_PALETTE:
        case P3S_CHUNK_ID_PALETTE_LEGACY:
        case P3S_CHUNK_ID_PALETTE_ID:
        case P3S_CHUNK_ID_SHAPE: {
            // v6 chunk header
            // chunkID | chunkSize | isCompressed | chunkUncompressedSize
            result = sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);
            break;
        }
        default: {
            // this should not happen
            vx_assert(false);
            return 0;
        }
    }
    return result;
}

uint32_t compute_preview_chunk_size(const uint32_t previewBytesCount) {
    return getChunkHeaderSize(P3S_CHUNK_ID_PREVIEW) + previewBytesCount;
}

uint32_t compute_shape_chunk_size(uint32_t shapeBufferDataSize) {
    return getChunkHeaderSize(P3S_CHUNK_ID_SHAPE) + shapeBufferDataSize;
}