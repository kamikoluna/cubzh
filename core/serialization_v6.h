// -------------------------------------------------------------
//  Cubzh Core
//  serialization_v6.h
//  Created by Adrien Duermael on July 25, 2019.
// -------------------------------------------------------------

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// C
#include <stdbool.h>
#include <stdio.h>

// Cubzh Core
#include "asset.h"
#include "colors.h"
#include "shape.h"

typedef struct _Transform Transform;
typedef struct _Stream Stream;

typedef struct _LocalTransform {
    float3 position; // 12 bytes
    float3 rotation; // 12 bytes
    float3 scale;    // 12 bytes
} LocalTransform;    // 36 bytes

#define SERIALIZATION_COMPRESSION_ALGO_SIZE sizeof(uint8_t)
#define SERIALIZATION_TOTAL_SIZE_SIZE sizeof(uint32_t)

DoublyLinkedList *serialization_load_assets_v6(Stream *s,
                                               ColorAtlas *colorAtlas,
                                               const AssetType filterMask,
                                               const LoadShapeSettings *const settings);

/// Saves shape in file w/ optional palette
bool serialization_v6_save_shape(Shape *shape,
                                 const void *imageData,
                                 uint32_t imageDataSize,
                                 FILE *fd);

/// Serialize a shape in a newly created memory buffer
bool serialization_v6_save_shape_as_buffer(const Shape *const shape,
                                           const ColorPalette *const artistPalette,
                                           const void *const previewData,
                                           const uint32_t previewDataSize,
                                           void **const outBuffer,
                                           uint32_t *const outBufferSize);

/// get preview data from save file path (caller must free *imageData)
bool serialization_v6_get_preview_data(Stream *s, void **imageData, uint32_t *size);

#ifdef __cplusplus
} // extern "C"
#endif
