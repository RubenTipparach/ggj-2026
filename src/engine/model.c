/*
 * Model loading for PS1 bare-metal
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "model.h"

/*
 * Binary model format:
 *
 * Header (8 bytes):
 *   uint16_t num_vertices
 *   uint16_t num_uvs
 *   uint16_t num_faces
 *   uint16_t reserved
 *
 * Vertices (num_vertices * 6 bytes):
 *   int16_t x, y, z
 *
 * UVs (num_uvs * 2 bytes, padded to 4-byte alignment):
 *   uint8_t u, v
 *
 * Faces (num_faces * 18 bytes):
 *   int16_t v0, v1, v2, v3
 *   int16_t uv0, uv1, uv2, uv3
 *   int16_t normal_index
 */

bool loadModel(Model *model, const uint8_t *data, size_t size) {
	if (!model || !data || size < 8) {
		return false;
	}

	/* Read header */
	const uint16_t *header = (const uint16_t *)data;
	model->numVertices = header[0];
	model->numUVs = header[1];
	model->numFaces = header[2];
	model->reserved = header[3];

	/* Calculate offsets */
	size_t vertexOffset = 8;
	size_t vertexSize = model->numVertices * sizeof(GTEVector16);

	/* Align to 4 bytes */
	size_t uvOffset = vertexOffset + vertexSize;
	uvOffset = (uvOffset + 3) & ~3;

	size_t uvSize = model->numUVs * sizeof(UV);

	/* Align to 4 bytes */
	size_t faceOffset = uvOffset + uvSize;
	faceOffset = (faceOffset + 3) & ~3;

	/* Set pointers into the data */
	model->vertices = (const GTEVector16 *)(data + vertexOffset);
	model->colors = NULL;  /* No vertex colors in basic model format */
	model->uvs = (const UV *)(data + uvOffset);
	model->faces = (const Face *)(data + faceOffset);

	return true;
}

/*
 * Character model format (with vertex colors):
 *
 * Header (8 bytes):
 *   uint16_t num_vertices
 *   uint16_t num_uvs
 *   uint16_t num_faces
 *   uint16_t reserved
 *
 * Vertices (num_vertices * 8 bytes):
 *   int16_t x, y, z, padding
 *
 * Vertex colors (num_vertices * 3 bytes, padded to 4-byte alignment):
 *   uint8_t r, g, b
 *
 * UVs (num_uvs * 2 bytes, padded to 4-byte alignment):
 *   uint8_t u, v
 *
 * Faces (num_faces * 18 bytes):
 *   int16_t v0, v1, v2, v3
 *   int16_t uv0, uv1, uv2, uv3
 *   int16_t normal_index
 */

bool loadCharacterModel(Model *model, const uint8_t *data, size_t size) {
	if (!model || !data || size < 8) {
		return false;
	}

	/* Read header */
	const uint16_t *header = (const uint16_t *)data;
	model->numVertices = header[0];
	model->numUVs = header[1];
	model->numFaces = header[2];
	model->reserved = header[3];

	/* Calculate offsets */
	size_t vertexOffset = 8;
	size_t vertexSize = model->numVertices * sizeof(GTEVector16);

	/* Vertex colors come after vertices */
	size_t colorOffset = vertexOffset + vertexSize;
	size_t colorSize = model->numVertices * sizeof(VertexColor);

	/* Align to 4 bytes */
	size_t uvOffset = colorOffset + colorSize;
	uvOffset = (uvOffset + 3) & ~3;

	size_t uvSize = model->numUVs * sizeof(UV);

	/* Align to 4 bytes */
	size_t faceOffset = uvOffset + uvSize;
	faceOffset = (faceOffset + 3) & ~3;

	/* Set pointers into the data */
	model->vertices = (const GTEVector16 *)(data + vertexOffset);
	model->colors = (const VertexColor *)(data + colorOffset);
	model->uvs = (const UV *)(data + uvOffset);
	model->faces = (const Face *)(data + faceOffset);

	return true;
}
