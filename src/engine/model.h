/*
 * Model loading and structures for PS1 bare-metal
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ps1/gte.h"

/* Texture coordinates */
typedef struct {
	uint8_t u, v;
} UV;

/* Vertex color (RGB) */
typedef struct {
	uint8_t r, g, b;
} VertexColor;

/* Face structure */
typedef struct {
	int16_t v0, v1, v2, v3;     /* Vertex indices (v3 = -1 for triangles) */
	int16_t uv0, uv1, uv2, uv3; /* UV indices */
	int16_t n;                   /* Normal index (unused) */
} Face;

/* Model structure */
typedef struct {
	uint16_t numVertices;
	uint16_t numUVs;
	uint16_t numFaces;
	uint16_t reserved;

	const GTEVector16 *vertices;
	const VertexColor *colors;  /* Vertex colors (may be NULL) */
	const UV *uvs;
	const Face *faces;
} Model;

#ifdef __cplusplus
extern "C" {
#endif

/* Load a model from embedded binary data (no vertex colors) */
bool loadModel(Model *model, const uint8_t *data, size_t size);

/* Load a character model with vertex colors */
bool loadCharacterModel(Model *model, const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif
